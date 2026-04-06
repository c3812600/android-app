#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include <Preferences.h>

const char *prefsNamespace = "esp32";
const char *subTopic = "v1/devices/me/rpc/request/+";
const char *pubTopicBase = "v1/devices/me/rpc/response";
const char *defaultMqttServer = "10.0.0.1";
const char *defaultApPassword = "12345678";
const char *defaultWorkMode = "AP";
const char *defaultApIp = "192.168.4.1";
const uint16_t defaultMqttPort = 1883;
const unsigned long mqttReconnectIntervalMs = 5000;
const unsigned long wifiReconnectIntervalMs = 10000;
const unsigned long statusLedFastBlinkMs = 200;
const unsigned long statusLedSlowBlinkMs = 1000;
const unsigned long bootResetWifiMs = 3000;
const unsigned long bootFactoryResetMs = 10000;
const uint8_t statusLedPin = 5;
const uint8_t bootButtonPin = 0;
const uint8_t relayPins[] = { 12, 13, 14, 15 };
const size_t relayCount = sizeof(relayPins) / sizeof(relayPins[0]);

Preferences prefs;
WiFiClient espClient;
PubSubClient client(espClient);
WebServer webServer(80);

String clientId;
String apSsid;
String workMode;
String mqttServer;
String mqttUser;
String mqttPass;
String apPassword;
String apIpAddress;
uint16_t mqttPort = defaultMqttPort;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWifiReconnectAttempt = 0;
unsigned long bootButtonPressedAt = 0;
unsigned long lastStatusLedToggleAt = 0;
unsigned long restartScheduledAt = 0;
bool bootResetHandled = false;
bool statusLedOutput = false;
bool configPortalActive = false;

void usartCmd(void *pvParameters);
int ConnectWifi();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void loadMqttConfig();
bool connectMqtt();
void setupRelayPins();
bool isValidRelayIndex(int relayIndex);
void setRelayState(int relayIndex, bool state);
bool getRelayState(int relayIndex);
String buildAllRelayStatesJson();
String buildSingleRelayStateJson(int relayIndex);
String buildResponseTopic(const char *topic);
void publishMqttResponse(const String &responseTopic, const String &payload);
bool handleSetValue(JSONVar params, String &responsePayload);
String handleGetValue(JSONVar params);
void setupStatusLedAndButton();
void setStatusLed(bool on);
void updateStatusLed();
void handleBootButton();
void clearWifiSettings();
void restoreFactorySettings();
bool hasSavedWifiCredentials();
void attemptWifiReconnect();
void startConfigPortal();
void stopConfigPortal();
void handleConfigPage();
void handleSaveConfig();
void handleRelayApi();
void handleRelayApiPost();
void handleStatusApi();
void scheduleRestart();
void handlePendingRestart();
void connectWifiOrStartPortal();
void saveWifiConfig(const String &ssid, const String &password);
void saveMqttConfig(const String &server, uint16_t port, const String &user, const String &pass);
void loadPortalConfig();
void savePortalConfig(const String &mode, const String &ssid, const String &password, const String &ipAddress);
String htmlEscape(String value);
bool parseSwitchState(const String &value, bool &state);
void setAllRelays(bool state);
bool applyRelayChannelsString(const String &channels, bool state);
bool applyRelayJson(JSONVar request, String &responsePayload, String &errorPayload);
bool parseIpAddressString(const String &value, IPAddress &ipAddress);

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("boot start");
  setupStatusLedAndButton();
  setupRelayPins();
  clientId = "ESP32-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);
  apSsid = "ESP32-Relay-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  prefs.begin(prefsNamespace);
  loadPortalConfig();
  loadMqttConfig();
  client.setCallback(mqttCallback);
  client.setKeepAlive(60);
  client.setBufferSize(512);
  xTaskCreatePinnedToCore(usartCmd, "usartCmd", 4096, NULL, 2, NULL, tskNO_AFFINITY);
  connectWifiOrStartPortal();
}

void loop() {
  handleBootButton();
  handlePendingRestart();
  updateStatusLed();

  if (configPortalActive) {
    webServer.handleClient();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (!configPortalActive) {
      startConfigPortal();
    }
    attemptWifiReconnect();
    return;
  }

  if (configPortalActive) {
    stopConfigPortal();
  }

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt >= mqttReconnectIntervalMs) {
      lastMqttReconnectAttempt = now;
      connectMqtt();
    }
    return;
  }

  client.loop();
}

void setupRelayPins() {
  for (size_t i = 0; i < relayCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }
}

void setupStatusLedAndButton() {
  pinMode(statusLedPin, OUTPUT);
  pinMode(bootButtonPin, INPUT_PULLUP);
  setStatusLed(false);
}

void setStatusLed(bool on) {
  statusLedOutput = on;
  digitalWrite(statusLedPin, on ? LOW : HIGH);
}

void updateStatusLed() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    if (!statusLedOutput) {
      setStatusLed(true);
    }
    return;
  }

  unsigned long interval = WiFi.status() == WL_CONNECTED ? statusLedSlowBlinkMs : statusLedFastBlinkMs;
  if (now - lastStatusLedToggleAt < interval) {
    return;
  }

  lastStatusLedToggleAt = now;
  setStatusLed(!statusLedOutput);
}

void handleBootButton() {
  bool pressed = digitalRead(bootButtonPin) == LOW;
  unsigned long now = millis();

  if (!pressed) {
    if (bootButtonPressedAt != 0 && !bootResetHandled) {
      unsigned long pressedDuration = now - bootButtonPressedAt;
      if (pressedDuration >= bootResetWifiMs) {
        bootResetHandled = true;
        Serial.println("BOOT reset wifi");
        clearWifiSettings();
        delay(200);
        ESP.restart();
      }
    }
    bootButtonPressedAt = 0;
    bootResetHandled = false;
    return;
  }

  if (bootButtonPressedAt == 0) {
    bootButtonPressedAt = now;
    bootResetHandled = false;
    return;
  }

  if (bootResetHandled) {
    return;
  }

  unsigned long pressedDuration = now - bootButtonPressedAt;
  if (pressedDuration >= bootFactoryResetMs) {
    bootResetHandled = true;
    Serial.println("BOOT factory reset");
    restoreFactorySettings();
    delay(200);
    ESP.restart();
    return;
  }
}

void clearWifiSettings() {
  prefs.remove("ssid");
  prefs.remove("password");
  prefs.putString("work_mode", defaultWorkMode);
}

void restoreFactorySettings() {
  prefs.clear();
}

bool hasSavedWifiCredentials() {
  return prefs.getString("ssid", "").length() > 0;
}

void loadPortalConfig() {
  workMode = prefs.getString("work_mode", defaultWorkMode);
  if (workMode != "STA") {
    workMode = defaultWorkMode;
  }

  String defaultApSsid = "ESP32-Relay-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  apSsid = prefs.getString("ap_ssid", defaultApSsid);
  if (apSsid.length() == 0) {
    apSsid = defaultApSsid;
  }

  apPassword = prefs.getString("ap_password", defaultApPassword);
  if (apPassword.length() < 8) {
    apPassword = defaultApPassword;
  }

  apIpAddress = prefs.getString("ap_ip", defaultApIp);
  if (apIpAddress.length() == 0) {
    apIpAddress = defaultApIp;
  }
}

void loadMqttConfig() {
  mqttServer = prefs.getString("mqtt_server", defaultMqttServer);
  mqttPort = prefs.getUShort("mqtt_port", defaultMqttPort);
  mqttUser = prefs.getString("mqtt_user", "");
  mqttPass = prefs.getString("mqtt_pass", "");

  if (mqttServer.length() == 0) {
    mqttServer = defaultMqttServer;
  }
  if (mqttPort == 0) {
    mqttPort = defaultMqttPort;
  }

  client.setServer(mqttServer.c_str(), mqttPort);
  Serial.printf("mqtt clientId:%s\r\n", clientId.c_str());
  Serial.printf("mqtt server:%s, port:%u\r\n", mqttServer.c_str(), mqttPort);
}

void connectWifiOrStartPortal() {
  loadPortalConfig();
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");

  if (workMode != "STA") {
    Serial.println("work mode AP");
    startConfigPortal();
    return;
  }

  Serial.printf("wifi ssid:%s\r\n", ssid.c_str());

  if (ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    if (ConnectWifi() == 0) {
      return;
    }
  }

  Serial.println("WIFI CONNECT failed");
  startConfigPortal();
}

int ConnectWifi() {
  for (int i = 0; i < 20; i++) {
    Serial.print('.');
    delay(200);
    if (WiFi.status() != WL_CONNECTED) {
      continue;
    }

    Serial.println(WiFi.localIP());

    String savedSsid = prefs.getString("ssid", "");
    String savedPassword = prefs.getString("password", "");
    if (WiFi.SSID() != savedSsid || WiFi.psk() != savedPassword) {
      prefs.putString("ssid", WiFi.SSID());
      prefs.putString("password", WiFi.psk());
      Serial.println("wifi config save success");
    }
    lastWifiReconnectAttempt = 0;
    return 0;
  }
  return -1;
}

void attemptWifiReconnect() {
  if (workMode != "STA") {
    return;
  }

  if (!hasSavedWifiCredentials()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiReconnectAttempt < wifiReconnectIntervalMs) {
    return;
  }

  lastWifiReconnectAttempt = now;
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  if (ssid.length() == 0) {
    return;
  }

  Serial.println("wifi reconnect...");
  WiFi.disconnect();
  WiFi.mode(configPortalActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
}

void startConfigPortal() {
  if (configPortalActive) {
    return;
  }

  loadPortalConfig();
  WiFi.mode(WIFI_AP_STA);
  IPAddress localIp;
  if (!parseIpAddressString(apIpAddress, localIp)) {
    localIp = IPAddress(192, 168, 4, 1);
    apIpAddress = defaultApIp;
  }
  IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(localIp, localIp, subnet)) {
    localIp = IPAddress(192, 168, 4, 1);
    apIpAddress = defaultApIp;
    WiFi.softAPConfig(localIp, localIp, subnet);
  }
  WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  webServer.on("/", HTTP_GET, handleConfigPage);
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/relay", HTTP_GET, handleRelayApi);
  webServer.on("/api/relay", HTTP_POST, handleRelayApiPost);
  webServer.on("/status", HTTP_GET, handleStatusApi);
  webServer.onNotFound(handleConfigPage);
  webServer.begin();
  configPortalActive = true;
  Serial.println("AP config portal started");
  Serial.print("AP SSID: ");
  Serial.println(apSsid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopConfigPortal() {
  if (!configPortalActive) {
    return;
  }

  webServer.stop();
  WiFi.softAPdisconnect(true);
  configPortalActive = false;
  Serial.println("AP config portal stopped");
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("\"", "&quot;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  return value;
}

void handleConfigPage() {
  loadPortalConfig();
  String currentSsid = htmlEscape(prefs.getString("ssid", ""));
  String currentPassword = htmlEscape(prefs.getString("password", ""));
  String currentMqttServer = htmlEscape(mqttServer);
  String currentMqttUser = htmlEscape(mqttUser);
  String currentMqttPass = htmlEscape(mqttPass);
  String currentWorkMode = htmlEscape(workMode);
  String currentApSsid = htmlEscape(apSsid);
  String currentApPassword = htmlEscape(apPassword);
  String currentApIp = htmlEscape(apIpAddress);

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 配网</title><style>body{font-family:Arial;margin:20px;background:#f5f5f5;}form,.card{max-width:420px;margin:auto;background:#fff;padding:20px;border-radius:12px;}input,select{width:100%;padding:12px;margin:8px 0 16px 0;box-sizing:border-box;}button{width:100%;padding:12px;background:#1677ff;color:#fff;border:none;border-radius:8px;margin-top:8px;}h2,p{max-width:420px;margin:12px auto;}.status{max-width:420px;margin:12px auto;color:#333;}.muted{color:#666;font-size:14px;}.section{display:none;}.section.active{display:block;}</style></head><body>";
  html += "<h2>ESP32 模式配置</h2>";
  html += "<p>当前热点 " + currentApSsid + " ，密码 " + currentApPassword + " ，设备地址 " + currentApIp + " 。</p>";
  html += "<form method='post' action='/save'>";
  html += "<label>工作模式</label><select name='work_mode'>";
  html += "<option value='AP'";
  if (currentWorkMode == "AP") {
    html += " selected";
  }
  html += ">AP 模式</option>";
  html += "<option value='STA'";
  if (currentWorkMode == "STA") {
    html += " selected";
  }
  html += ">STA 模式</option></select>";
  html += "<div id='apSection' class='section'>";
  html += "<label>AP 名称</label><input name='ap_ssid' value='" + currentApSsid + "'>";
  html += "<label>AP 密码</label><input name='ap_password' type='password' value='" + currentApPassword + "'>";
  html += "<label>AP 设备地址</label><input name='ap_ip' value='" + currentApIp + "'>";
  html += "<p class='muted'>AP 模式下默认启动热点，重置后也会回到 AP 模式。设备地址建议保持同网段格式，如 192.168.4.1。</p>";
  html += "</div>";
  html += "<div id='staSection' class='section'>";
  html += "<label>WiFi 名称</label><input name='ssid' value='" + currentSsid + "'>";
  html += "<label>WiFi 密码</label><input name='password' type='password' value='" + currentPassword + "'>";
  html += "<label>MQTT 服务器</label><input name='mqtt_server' value='" + currentMqttServer + "'>";
  html += "<label>MQTT 端口</label><input name='mqtt_port' type='number' value='" + String(mqttPort) + "'>";
  html += "<label>MQTT 用户名</label><input name='mqtt_user' value='" + currentMqttUser + "'>";
  html += "<label>MQTT 密码</label><input name='mqtt_pass' type='password' value='" + currentMqttPass + "'>";
  html += "<p class='muted'>STA 模式下设备会连接你填写的路由器，并继续使用 MQTT 服务器参数。</p>";
  html += "</div>";
  html += "<button type='submit'>保存并重启</button></form>";
  html += "<div class='card'><h3>接口说明</h3><p>网页仅用于模式与参数配置。本地继电器控制请使用说明文档中的 GET 或 POST 接口，由你的 APP 或页面调用。</p></div>";
  html += "<script>const modeSelect=document.querySelector('select[name=\"work_mode\"]');const apSection=document.getElementById('apSection');const staSection=document.getElementById('staSection');function updateModeSections(){const isSta=modeSelect.value==='STA';apSection.className='section'+(isSta?'':' active');staSection.className='section'+(isSta?' active':'');}modeSelect.addEventListener('change',updateModeSections);updateModeSections();</script>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void saveWifiConfig(const String &ssid, const String &password) {
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
}

void savePortalConfig(const String &mode, const String &ssid, const String &password, const String &ipAddress) {
  prefs.putString("work_mode", mode == "STA" ? "STA" : defaultWorkMode);
  prefs.putString("ap_ssid", ssid.length() > 0 ? ssid : apSsid);
  prefs.putString("ap_password", password.length() >= 8 ? password : String(defaultApPassword));
  prefs.putString("ap_ip", ipAddress.length() > 0 ? ipAddress : String(defaultApIp));
}

void saveMqttConfig(const String &server, uint16_t port, const String &user, const String &pass) {
  prefs.putString("mqtt_server", server.length() > 0 ? server : String(defaultMqttServer));
  prefs.putUShort("mqtt_port", port == 0 ? defaultMqttPort : port);
  prefs.putString("mqtt_user", user);
  prefs.putString("mqtt_pass", pass);
}

void handleSaveConfig() {
  String selectedMode = webServer.arg("work_mode");
  if (selectedMode != "STA") {
    selectedMode = "AP";
  }

  String portalSsid = webServer.arg("ap_ssid");
  String portalPassword = webServer.arg("ap_password");
  String portalIp = webServer.arg("ap_ip");
  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  String server = webServer.arg("mqtt_server");
  String user = webServer.arg("mqtt_user");
  String pass = webServer.arg("mqtt_pass");
  uint16_t port = (uint16_t)webServer.arg("mqtt_port").toInt();

  if (portalSsid.length() == 0) {
    portalSsid = apSsid;
  }
  if (portalPassword.length() > 0 && portalPassword.length() < 8) {
    webServer.send(400, "text/html", "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body><h3>AP 密码至少 8 位</h3><a href='/'>返回</a></body></html>");
    return;
  }
  IPAddress localIp;
  if (portalIp.length() == 0) {
    portalIp = apIpAddress;
  }
  if (!parseIpAddressString(portalIp, localIp)) {
    webServer.send(400, "text/html", "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body><h3>AP 设备地址格式错误</h3><a href='/'>返回</a></body></html>");
    return;
  }

  if (selectedMode == "STA" && ssid.length() == 0) {
    webServer.send(400, "text/html", "<html><body><h3>WiFi 名称不能为空</h3><a href='/'>返回</a></body></html>");
    return;
  }

  if (server.length() == 0) {
    server = defaultMqttServer;
  }
  if (port == 0) {
    port = defaultMqttPort;
  }

  savePortalConfig(selectedMode, portalSsid, portalPassword, portalIp);
  if (selectedMode == "STA") {
    saveWifiConfig(ssid, password);
  } else {
    clearWifiSettings();
  }
  saveMqttConfig(server, port, user, pass);
  loadPortalConfig();
  loadMqttConfig();

  Serial.println("config saved from web");
  webServer.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body><h3>配置已保存，设备即将重启。</h3></body></html>");
  scheduleRestart();
}

bool parseSwitchState(const String &value, bool &state) {
  String normalized = value;
  normalized.toLowerCase();
  if (normalized == "1" || normalized == "true" || normalized == "on") {
    state = true;
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "off") {
    state = false;
    return true;
  }
  return false;
}

void setAllRelays(bool state) {
  for (size_t i = 0; i < relayCount; i++) {
    setRelayState((int)i, state);
  }
}

bool applyRelayChannelsString(const String &channels, bool state) {
  bool updated = false;
  int start = 0;
  while (start < channels.length()) {
    int commaIndex = channels.indexOf(',', start);
    String token = commaIndex == -1 ? channels.substring(start) : channels.substring(start, commaIndex);
    token.trim();
    int relayIndex = token.toInt() - 1;
    if (isValidRelayIndex(relayIndex)) {
      setRelayState(relayIndex, state);
      updated = true;
    }
    if (commaIndex == -1) {
      break;
    }
    start = commaIndex + 1;
  }
  return updated;
}

bool applyRelayJson(JSONVar request, String &responsePayload, String &errorPayload) {
  if (JSON.typeof(request) != "object") {
    errorPayload = "{\"error\":\"invalid_json\"}";
    return false;
  }

  if (JSON.typeof(request["state"]) != "boolean") {
    errorPayload = "{\"error\":\"missing_state\"}";
    return false;
  }

  bool state = (bool)request["state"];

  if (JSON.typeof(request["all"]) == "boolean" && (bool)request["all"]) {
    setAllRelays(state);
    responsePayload = buildAllRelayStatesJson();
    return true;
  }

  if (JSON.typeof(request["relay"]) == "number") {
    int relayIndex = (int)request["relay"] - 1;
    if (!isValidRelayIndex(relayIndex)) {
      errorPayload = "{\"error\":\"invalid_relay\"}";
      return false;
    }
    setRelayState(relayIndex, state);
    responsePayload = buildSingleRelayStateJson(relayIndex);
    return true;
  }

  if (JSON.typeof(request["channels"]) == "array") {
    bool updated = false;
    int channelCount = request["channels"].length();
    for (int i = 0; i < channelCount; i++) {
      if (JSON.typeof(request["channels"][i]) != "number") {
        continue;
      }
      int relayIndex = (int)request["channels"][i] - 1;
      if (!isValidRelayIndex(relayIndex)) {
        continue;
      }
      setRelayState(relayIndex, state);
      updated = true;
    }

    if (!updated) {
      errorPayload = "{\"error\":\"invalid_channels\"}";
      return false;
    }

    responsePayload = buildAllRelayStatesJson();
    return true;
  }

  errorPayload = "{\"error\":\"missing_target\"}";
  return false;
}

bool parseIpAddressString(const String &value, IPAddress &ipAddress) {
  int parts[4] = { 0, 0, 0, 0 };
  int partIndex = 0;
  int start = 0;

  while (start <= value.length() && partIndex < 4) {
    int dotIndex = value.indexOf('.', start);
    String token = dotIndex == -1 ? value.substring(start) : value.substring(start, dotIndex);
    token.trim();
    if (token.length() == 0) {
      return false;
    }
    for (int i = 0; i < token.length(); i++) {
      if (!isDigit(token[i])) {
        return false;
      }
    }
    int number = token.toInt();
    if (number < 0 || number > 255) {
      return false;
    }
    parts[partIndex++] = number;
    if (dotIndex == -1) {
      break;
    }
    start = dotIndex + 1;
  }

  if (partIndex != 4) {
    return false;
  }
  if (parts[0] == 0) {
    return false;
  }

  ipAddress = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

void handleRelayApi() {
  bool state;
  if (!parseSwitchState(webServer.arg("state"), state)) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_state\"}");
    return;
  }

  if (webServer.hasArg("ch")) {
    String channel = webServer.arg("ch");
    if (channel == "all") {
      setAllRelays(state);
      webServer.send(200, "application/json", buildAllRelayStatesJson());
      return;
    }

    int relayIndex = channel.toInt() - 1;
    if (!isValidRelayIndex(relayIndex)) {
      webServer.send(400, "application/json", "{\"error\":\"invalid_relay\"}");
      return;
    }

    setRelayState(relayIndex, state);
    webServer.send(200, "application/json", buildSingleRelayStateJson(relayIndex));
    return;
  }

  if (webServer.hasArg("channels")) {
    String channels = webServer.arg("channels");
    if (!applyRelayChannelsString(channels, state)) {
      webServer.send(400, "application/json", "{\"error\":\"invalid_channels\"}");
      return;
    }

    webServer.send(200, "application/json", buildAllRelayStatesJson());
    return;
  }

  webServer.send(400, "application/json", "{\"error\":\"missing_target\"}");
}

void handleRelayApiPost() {
  String body = webServer.arg("plain");
  JSONVar request = JSON.parse(body);
  String responsePayload;
  String errorPayload;

  if (!applyRelayJson(request, responsePayload, errorPayload)) {
    webServer.send(400, "application/json", errorPayload);
    return;
  }

  webServer.send(200, "application/json", responsePayload);
}

void handleStatusApi() {
  webServer.send(200, "application/json", buildAllRelayStatesJson());
}

void scheduleRestart() {
  restartScheduledAt = millis() + 1500;
}

void handlePendingRestart() {
  if (restartScheduledAt == 0) {
    return;
  }
  if (millis() < restartScheduledAt) {
    return;
  }
  ESP.restart();
}

bool connectMqtt() {
  if (mqttServer.length() == 0) {
    Serial.println("mqtt config missing");
    return false;
  }

  bool connected;
  if (mqttUser.length() > 0 || mqttPass.length() > 0) {
    connected = client.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str());
  } else {
    connected = client.connect(clientId.c_str());
  }

  if (!connected) {
    Serial.printf("mqtt connect failed, state=%d\r\n", client.state());
    return false;
  }

  if (!client.subscribe(subTopic)) {
    Serial.println("mqtt subscribe failed");
    client.disconnect();
    return false;
  }

  Serial.println("mqtt connect ok");
  return true;
}

bool isValidRelayIndex(int relayIndex) {
  return relayIndex >= 0 && relayIndex < (int)relayCount;
}

void setRelayState(int relayIndex, bool state) {
  if (!isValidRelayIndex(relayIndex)) {
    return;
  }
  digitalWrite(relayPins[relayIndex], state ? HIGH : LOW);
}

bool getRelayState(int relayIndex) {
  if (!isValidRelayIndex(relayIndex)) {
    return false;
  }
  return digitalRead(relayPins[relayIndex]) == HIGH;
}

String buildAllRelayStatesJson() {
  String payload = "{";
  for (size_t i = 0; i < relayCount; i++) {
    if (i > 0) {
      payload += ",";
    }
    payload += "\"relay";
    payload += String(i + 1);
    payload += "\":";
    payload += (getRelayState((int)i) ? "true" : "false");
  }
  payload += "}";
  return payload;
}

String buildSingleRelayStateJson(int relayIndex) {
  if (!isValidRelayIndex(relayIndex)) {
    return "{\"error\":\"invalid_relay\"}";
  }
  String payload = "{\"relay\":";
  payload += String(relayIndex + 1);
  payload += ",\"state\":";
  payload += (getRelayState(relayIndex) ? "true" : "false");
  payload += "}";
  return payload;
}

void usartCmd(void *pvParameters) {
  char text[100];
  while (true) {
    delay(1);
    if (Serial.available() <= 1) {
      continue;
    }
    if (Serial.read() != 'A') {
      continue;
    }
    if (Serial.read() != 'T') {
      continue;
    }
    delay(2);

    int len = 0;
    while (Serial.available() > 0 && len < (int)(sizeof(text) - 1)) {
      text[len++] = Serial.read();
    }
    text[len] = 0;

    if (strcmp(text, "") == 0) {
      Serial.println("OK");
    } else if (strcmp(text, "+ResetWifi") == 0) {
      Preferences prefsUsart;
      prefsUsart.begin(prefsNamespace);
      prefsUsart.remove("ssid");
      prefsUsart.remove("password");
      prefsUsart.end();
      Serial.println("ResetWifi OK");
      ESP.restart();
    } else if (strcmp(text, "+FactoryReset") == 0) {
      Preferences prefsUsart;
      prefsUsart.begin(prefsNamespace);
      prefsUsart.clear();
      prefsUsart.end();
      Serial.println("FactoryReset OK");
      ESP.restart();
    } else if (strcmp(text, "+Restart") == 0) {
      Serial.println("Restart OK");
      ESP.restart();
    } else {
      Serial.println("CMD ERROR");
    }
  }
}

String buildResponseTopic(const char *topic) {
  const char *ret = strrchr(topic, '/');
  if (ret == nullptr) {
    return "";
  }
  return String(pubTopicBase) + String(ret);
}

void publishMqttResponse(const String &responseTopic, const String &payload) {
  if (responseTopic.length() == 0 || !client.connected()) {
    return;
  }

  client.publish(responseTopic.c_str(), payload.c_str());
  Serial.print("return:");
  Serial.println(payload);
}

bool handleSetValue(JSONVar params, String &responsePayload) {
  if (JSON.typeof(params) == "boolean") {
    setRelayState(0, (bool)params);
    responsePayload = buildSingleRelayStateJson(0);
    return true;
  }

  if (JSON.typeof(params) != "object") {
    return false;
  }

  if (JSON.typeof(params["relay"]) == "number" && JSON.typeof(params["state"]) == "boolean") {
    int relayIndex = (int)params["relay"] - 1;
    if (!isValidRelayIndex(relayIndex)) {
      responsePayload = "{\"error\":\"invalid_relay\"}";
      return true;
    }
    setRelayState(relayIndex, (bool)params["state"]);
    responsePayload = buildSingleRelayStateJson(relayIndex);
    return true;
  }

  bool updated = false;
  for (size_t i = 0; i < relayCount; i++) {
    String key = "relay" + String(i + 1);
    if (JSON.typeof(params[key]) == "boolean") {
      setRelayState((int)i, (bool)params[key]);
      updated = true;
    }
  }

  if (!updated) {
    return false;
  }

  responsePayload = buildAllRelayStatesJson();
  return true;
}

String handleGetValue(JSONVar params) {
  if (JSON.typeof(params) == "object" && JSON.typeof(params["relay"]) == "number") {
    int relayIndex = (int)params["relay"] - 1;
    return buildSingleRelayStateJson(relayIndex);
  }
  return buildAllRelayStatesJson();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String responseTopic = buildResponseTopic(topic);
  if (responseTopic.length() == 0) {
    return;
  }

  Serial.print("subTopic:");
  Serial.println(responseTopic);

  String payloadText;
  payloadText.reserve(length);

  Serial.print("receive:");
  for (unsigned int i = 0; i < length; i++) {
    char currentChar = (char)payload[i];
    Serial.print(currentChar);
    payloadText += currentChar;
  }
  Serial.println();

  JSONVar myObject = JSON.parse(payloadText);
  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Parsing input failed!");
    publishMqttResponse(responseTopic, "{\"error\":\"invalid_json\"}");
    return;
  }

  const char *method = (const char *)myObject["method"];
  if (method == null) {
    publishMqttResponse(responseTopic, "{\"error\":\"missing_method\"}");
    return;
  }

  if (strcmp(method, "setValue") == 0) {
    String responsePayload;
    if (!handleSetValue(myObject["params"], responsePayload)) {
      publishMqttResponse(responseTopic, "{\"error\":\"invalid_params\"}");
      return;
    }
    publishMqttResponse(responseTopic, responsePayload);
    return;
  }

  if (strcmp(method, "getValue") == 0) {
    publishMqttResponse(responseTopic, handleGetValue(myObject["params"]));
    return;
  }

  publishMqttResponse(responseTopic, "{\"error\":\"unknown_method\"}");
}
