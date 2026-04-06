#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

// ================== 引脚定义 ==================
// 根据提供的 LED_Relay.ino 定义
#define PIN_LED    2
#define PIN_RELAY1 16 
#define PIN_RELAY2 14
#define PIN_RELAY3 12
#define PIN_RELAY4 13
#define PIN_RELAY5 15 
#define PIN_RELAY6 0
#define PIN_RELAY7 4
#define PIN_RELAY8 5

// 继电器触发电平配置
// 根据原 LED_Relay.ino 逻辑，HIGH 为开启
#define RELAY_ON  HIGH
#define RELAY_OFF LOW
#define LED_ON    LOW
#define LED_OFF   HIGH

// 将引脚放入数组，方便循环遍历控制
const int relayPins[8] = {PIN_RELAY1, PIN_RELAY2, PIN_RELAY3, PIN_RELAY4, PIN_RELAY5, PIN_RELAY6, PIN_RELAY7, PIN_RELAY8};

// ================== 持久化存储结构 ==================
struct Config {
  char ssid[33];
  char password[33];
  char ip[16];
  uint8_t relayState;
  uint8_t magic; // 0x55 标记为已初始化
};

Config config;

// ================== Web服务器 ==================
ESP8266WebServer server(80);

// ================== WebSocket ==================
WebSocketsServer webSocket = WebSocketsServer(81);
uint8_t currentRelayState = 0x00;

// AP 固定IP配置
IPAddress local_IP;
IPAddress gateway;
IPAddress subnet(255, 255, 255, 0);

// ================== 函数声明 ==================
void loadConfig();
void saveConfig();
void saveRelayState();
void setRelays(uint8_t hexValue);
void handleRoot();
void handleSave();
void handleReset();
void performFactoryReset();

// ================== 辅助函数 ==================

// 加载配置
void loadConfig() {
  EEPROM.begin(512);
  EEPROM.get(0, config);
  
  if (config.magic != 0x55) {
    // 初始化默认值
    Serial.println("初始化默认配置...");
    strcpy(config.ssid, "ModelLight");
    strcpy(config.password, "admin888");
    strcpy(config.ip, "192.168.18.252");
    config.relayState = 0x00;
    config.magic = 0x55;
    saveConfig();
  } else {
    Serial.println("配置加载成功");
  }
  
  currentRelayState = config.relayState;
}

// 保存所有配置
void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

// 仅保存继电器状态
void saveRelayState() {
  if (config.relayState != currentRelayState) {
    config.relayState = currentRelayState;
    EEPROM.put(0, config);
    EEPROM.commit();
  }
}

// 设置继电器
void setRelays(uint8_t hexValue) {
  for (int i = 0; i < 8; i++) {
    bool isOn = (hexValue >> i) & 0x01;
    digitalWrite(relayPins[i], isOn ? RELAY_ON : RELAY_OFF);
  }
  // LED 状态指示：如果有任意继电器开启，点亮LED，否则熄灭
  digitalWrite(PIN_LED, (hexValue > 0) ? LED_ON : LED_OFF);
}

// ================== WebSocket 事件处理 ==================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] 客户端已断开!\n", num);
      break;
      
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] 已连接，IP: %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      
      char reply[60];
      sprintf(reply, "已连接! 当前状态: 0x%02X", currentRelayState);
      webSocket.sendTXT(num, reply);
      break;
    }
    
    case WStype_TEXT: {
      String msg = "";
      for(size_t i=0; i<length; i++) msg += (char)payload[i];
      msg.trim(); 

      Serial.printf("[%u] 收到文本: %s\n", num, msg.c_str());

      bool stateChanged = false;

      // 【模式1】全量控制 (2位字符)，如 "FF", "00"
      if (msg.length() == 2) {
        uint8_t newState = (uint8_t)strtol(msg.c_str(), NULL, 16);
        if (newState != currentRelayState) {
          currentRelayState = newState;
          stateChanged = true;
        }
        setRelays(currentRelayState);
        
        char reply[50];
        sprintf(reply, "全量覆盖成功，当前总状态: 0x%02X", currentRelayState);
        webSocket.sendTXT(num, reply);
      } 
      // 【模式2】单路控制 (4位字符)，如 "0101"
      else if (msg.length() == 4) {
        int relayIndex = strtol(msg.substring(0, 2).c_str(), NULL, 16);
        int action = strtol(msg.substring(2, 4).c_str(), NULL, 16);

        if (relayIndex >= 1 && relayIndex <= 8) {
          int bitPosition = relayIndex - 1;
          uint8_t oldState = currentRelayState;
          if (action == 1) {
            currentRelayState |= (1 << bitPosition);
          } else if (action == 0) {
            currentRelayState &= ~(1 << bitPosition);
          }
          
          if (oldState != currentRelayState) stateChanged = true;
          setRelays(currentRelayState);
          
          char reply[60];
          sprintf(reply, "独立控制: 继电器%d已%s，当前总状态: 0x%02X", 
                  relayIndex, (action == 1 ? "开启" : "关闭"), currentRelayState);
          webSocket.sendTXT(num, reply);
        } else {
          webSocket.sendTXT(num, "错误: 继电器编号必须是 01 到 08");
        }
      } 
      else {
        webSocket.sendTXT(num, "格式错误! 全开/关发2位(如FF)，单路控制发4位(如0101)");
      }
      
      if (stateChanged) {
        saveRelayState();
      }
      break;
    }
    
    case WStype_BIN: {
      if(length > 0){
        currentRelayState = payload[0];
        setRelays(currentRelayState);
        saveRelayState();
        webSocket.sendBIN(num, &currentRelayState, 1);
      }
      break;
    }
  }
}

// ================== Web 页面处理 ==================
const char index_html[] PROGMEM = R"raw(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>网络配置</title>
  <style>
    body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;margin:0;padding:20px;background-color:#f5f5f5;color:#333;}
    .container{max_width:600px;margin:0 auto;background-color:white;padding:30px;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}
    h1{margin-top:0;color:#2c3e50;font-size:24px;}
    .form-group{margin-bottom:20px;}
    label{display:block;margin-bottom:8px;font-weight:600;color:#555;}
    input{width:100%;padding:12px;border:1px solid #ddd;border-radius:6px;font-size:16px;box-sizing:border-box;transition:border-color 0.3s;}
    input:focus{border-color:#4CAF50;outline:none;}
    .btn-group{display:flex;gap:15px;margin-top:30px;}
    button{flex:1;padding:12px;border:none;border-radius:6px;font-size:16px;font-weight:600;cursor:pointer;transition:opacity 0.2s;}
    button:hover{opacity:0.9;}
    button[type='submit']{background-color:#4CAF50;color:white;}
    button.reset{background-color:#ff5252;color:white;}
    .hint{font-size:12px;color:#888;margin-top:5px;}
  </style>
</head>
<body>
  <div class='container'>
    <h1>网络配置 (ESP8266)</h1>
    <form action='/save' method='POST'>
      <div class="form-group">
        <label>WiFi 名称 (SSID)</label>
        <input type='text' name='ssid' value='%SSID%' required>
        <div class="hint">当前设备热点名称</div>
      </div>
      
      <div class="form-group">
        <label>WiFi 密码</label>
        <input type='text' name='password' value='%PASSWORD%' required minlength="8">
        <div class="hint">连接密码 (至少8位)</div>
      </div>
      
      <div class="form-group">
        <label>IP 地址</label>
        <input type='text' name='ip' value='%IP%' required pattern="^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$">
        <div class="hint">默认: 192.168.18.252</div>
      </div>
      
      <div class="btn-group">
        <button type='submit'>保存并重启</button>
        <button type='button' class='reset' onclick="if(confirm('确定恢复出厂设置吗？')) location.href='/reset'">重置默认</button>
      </div>
    </form>
    <div style="margin-top:20px;text-align:center;color:#999;font-size:12px;">
      支持 OTA 无线升级
    </div>
  </div>
</body>
</html>
)raw";

void handleRoot() {
  String html = String(index_html);
  html.replace("%SSID%", String(config.ssid));
  html.replace("%PASSWORD%", String(config.password));
  html.replace("%IP%", String(config.ip));
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("ip")) {
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");
    String new_ip_str = server.arg("ip");
    
    if (new_password.length() < 8) {
      server.send(400, "text/plain", "Password must be at least 8 chars");
      return;
    }
    
    // 更新配置
    strncpy(config.ssid, new_ssid.c_str(), sizeof(config.ssid) - 1);
    strncpy(config.password, new_password.c_str(), sizeof(config.password) - 1);
    strncpy(config.ip, new_ip_str.c_str(), sizeof(config.ip) - 1);
    saveConfig();
    
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='text-align:center;padding-top:50px;font-family:sans-serif;'><h1>配置已保存!</h1><p>设备正在重启...</p><script>setTimeout(function(){location.href='/';}, 5000);</script></body></html>";
    server.send(200, "text/html", html);
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void performFactoryReset() {
  // 清除EEPROM标志位
  config.magic = 0x00;
  EEPROM.put(0, config);
  EEPROM.commit();
  delay(500);
  ESP.restart();
}

void handleReset() {
  String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='text-align:center;padding-top:50px;font-family:sans-serif;'><h1>已重置为默认配置!</h1><p>设备正在重启...</p><script>setTimeout(function(){location.href='/';}, 5000);</script></body></html>";
  server.send(200, "text/html", html);
  delay(500);
  performFactoryReset();
}

// ================== OTA 设置 ==================
void setupOTA() {
  ArduinoOTA.setHostname("ModelLight-ESP8266");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
}

// ================== Setup & Loop ==================
void setup() {
  Serial.begin(115200);
  
  // 初始化引脚
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LED_OFF);
  
  for(int i = 0; i < 8; i++) {
    // 优化：先写入关闭电平，再设置为输出，尽量减少上电抖动
    // 注意：GPIO0 (Relay6) 上电必须为高电平才能启动，这是芯片特性，软件无法消除其瞬间吸合
    digitalWrite(relayPins[i], RELAY_OFF);
    pinMode(relayPins[i], OUTPUT);
  }
  
  // 加载配置
  loadConfig();
  
  // 恢复上次状态
  setRelays(currentRelayState);
  Serial.printf("恢复继电器状态: 0x%02X\n", currentRelayState);

  // 解析IP
  local_IP.fromString(config.ip);
  gateway = local_IP; // 网关与IP保持一致

  // 配置 AP 模式的 IP 地址
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // 启动 AP 模式
  Serial.print("正在启动热点: ");
  Serial.println(config.ssid);
  
  bool result = WiFi.softAP(config.ssid, config.password);
  
  if (result) {
    Serial.println("热点启动成功!");
    Serial.print("AP IP地址: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("热点启动失败!");
  }

  // 启动 OTA
  setupOTA();

  // 启动 Web 服务器
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleReset);
  
  // 处理 404 请求，将其重定向到主页 (防止浏览器缓存导致的路径错误或强制门户探测)
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  
  server.begin();
  Serial.println("Web 服务器已启动，端口号：80");

  // 启动 WebSocket 服务器
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket 服务器已启动，端口号：81");
}

void loop() {
  ArduinoOTA.handle();
  webSocket.loop();
  server.handleClient();
  
  // ESP8266 不需要显式 delay 喂狗，只要不阻塞 loop 即可
  // 但加一点 delay 也没坏处
  // delay(1);
}
