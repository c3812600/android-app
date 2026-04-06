#include <WiFi.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// ================== 持久化存储 ==================
Preferences preferences;

// ================== Web服务器 ==================
WebServer server(80);

// ================== AP 热点配置 ==================
String ssid = "ModelLight";       // 热点名称
String password = "admin888"; // 热点密码 (至少8位)

// ================== AP 固定IP配置 ==================
// 手机连接热点后，通过此IP连接WebSocket
IPAddress local_IP(192, 168, 18, 252); // ESP32 本身的IP
IPAddress gateway(192, 168, 18, 252);  // 网关 (AP模式下就是自己)
IPAddress subnet(255, 255, 255, 0);    // 子网掩码

// ================== 引脚定义 ==================
#define HC595_SI_PIN 14
#define HC595_SCK_PIN 13
#define HC595_RCK_PIN 12
#define HC595_G_PIN 5

#define BUTTON_BOOT 0 // 开发板上的 BOOT 按钮
unsigned long buttonPressTime = 0;
bool buttonPressed = false;

// 开启 WebSocket 服务器，端口定为 81
WebSocketsServer webSocket = WebSocketsServer(81);

void HC595Init() {
  pinMode(HC595_SI_PIN, OUTPUT);
  pinMode(HC595_SCK_PIN, OUTPUT);
  pinMode(HC595_RCK_PIN, OUTPUT);
  pinMode(HC595_G_PIN, OUTPUT);
  digitalWrite(HC595_G_PIN, HIGH);
  digitalWrite(HC595_SI_PIN, LOW);
  digitalWrite(HC595_SCK_PIN, LOW);
  digitalWrite(HC595_RCK_PIN, LOW);
}

void HC595SendData(uint16_t outData) {
  for (uint8_t i = 0; i < 16; i++) {
    digitalWrite(HC595_SCK_PIN, LOW);
    if ((outData & 0x8000) == 0x8000) {
      digitalWrite(HC595_SI_PIN, HIGH);
    } else {
      digitalWrite(HC595_SI_PIN, LOW);
    }
    outData <<= 1;
    digitalWrite(HC595_SCK_PIN, HIGH);
  }
  digitalWrite(HC595_RCK_PIN, LOW);
  digitalWrite(HC595_RCK_PIN, HIGH);
  digitalWrite(HC595_G_PIN, LOW);
}

uint16_t currentRelayState = 0x0000;
unsigned long relayPulseEndTimes[16] = {0};
const unsigned long pulseDurationMs = 500;

void applyRelayState() {
  HC595SendData(currentRelayState);
}

void setRelayState(uint8_t relayIndex, bool enabled) {
  if (relayIndex < 1 || relayIndex > 16) {
    return;
  }

  uint16_t mask = (uint16_t)1 << (relayIndex - 1);
  if (enabled) {
    currentRelayState |= mask;
  } else {
    currentRelayState &= ~mask;
  }
  applyRelayState();
}

void triggerRelayPulse(uint8_t relayIndex) {
  if (relayIndex < 1 || relayIndex > 16) {
    return;
  }

  relayPulseEndTimes[relayIndex - 1] = millis() + pulseDurationMs;
  setRelayState(relayIndex, true);
}

void triggerRelayMaskPulse(uint16_t mask) {
  unsigned long endTime = millis() + pulseDurationMs;

  for (uint8_t i = 0; i < 16; i++) {
    if ((mask & ((uint16_t)1 << i)) != 0) {
      relayPulseEndTimes[i] = endTime;
      currentRelayState |= (uint16_t)1 << i;
    }
  }

  applyRelayState();
}

void updateRelayPulses() {
  unsigned long now = millis();
  bool changed = false;

  for (uint8_t i = 0; i < 16; i++) {
    if (relayPulseEndTimes[i] != 0 && (long)(now - relayPulseEndTimes[i]) >= 0) {
      relayPulseEndTimes[i] = 0;
      uint16_t mask = (uint16_t)1 << i;
      if ((currentRelayState & mask) != 0) {
        currentRelayState &= ~mask;
        changed = true;
      }
    }
  }

  if (changed) {
    applyRelayState();
  }
}

bool handleRelayBinaryCommand(uint8_t num, const uint8_t *payload, size_t length) {
  if (length == 1 && payload[0] >= 1 && payload[0] <= 16) {
    triggerRelayPulse(payload[0]);
    uint8_t reply = payload[0];
    webSocket.sendBIN(num, &reply, 1);
    return true;
  }

  if (length == 2) {
    uint8_t relayIndex = payload[0];
    uint8_t action = payload[1];

    if (relayIndex >= 1 && relayIndex <= 16 && action == 1) {
      triggerRelayPulse(relayIndex);
      uint8_t reply[2] = {relayIndex, action};
      webSocket.sendBIN(num, reply, 2);
      return true;
    }

    if (relayIndex >= 1 && relayIndex <= 16 && action == 0) {
      setRelayState(relayIndex, false);
      relayPulseEndTimes[relayIndex - 1] = 0;
      uint8_t reply[2] = {relayIndex, action};
      webSocket.sendBIN(num, reply, 2);
      return true;
    }
  }

  if (length >= 2) {
    uint16_t relayMask = ((uint16_t)payload[0] << 8) | payload[1];
    if (relayMask != 0) {
      triggerRelayMaskPulse(relayMask);
      uint8_t reply[2] = {payload[0], payload[1]};
      webSocket.sendBIN(num, reply, 2);
      return true;
    }
  }

  return false;
}

// WebSocket 事件处理回调
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    
    case WStype_DISCONNECTED:
      Serial.printf("[%u] 客户端已断开!\n", num);
      break;
      
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] 已连接，IP: %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      
      webSocket.sendTXT(num, "已连接! 支持 HEX 字节指令，单路:01 01~10 01，多路:00 01~FF FF");
      break;
    }
    
    case WStype_TEXT: {
      String msg = "";
      for(size_t i=0; i<length; i++) msg += (char)payload[i];
      msg.trim();

      Serial.printf("[%u] 收到文本: %s\n", num, msg.c_str());

      if (msg.length() == 4) {
        int relayIndex = strtol(msg.substring(0, 2).c_str(), NULL, 16);
        int action = strtol(msg.substring(2, 4).c_str(), NULL, 16);

        if (relayIndex >= 1 && relayIndex <= 16 && action == 1) {
          triggerRelayPulse((uint8_t)relayIndex);
          webSocket.sendTXT(num, "闪断执行: 继电器" + String(relayIndex) + " 吸合 0.5 秒后断开");
        } else if (relayIndex >= 1 && relayIndex <= 16 && action == 0) {
          setRelayState((uint8_t)relayIndex, false);
          relayPulseEndTimes[relayIndex - 1] = 0;
          webSocket.sendTXT(num, "继电器" + String(relayIndex) + " 已立即断开");
        } else {
          char *endPtr = nullptr;
          uint16_t relayMask = (uint16_t)strtoul(msg.c_str(), &endPtr, 16);
          if (endPtr != nullptr && *endPtr == '\0' && relayMask != 0) {
            triggerRelayMaskPulse(relayMask);
            char maskText[8];
            sprintf(maskText, "%04X", relayMask);
            webSocket.sendTXT(num, "闪断执行: 掩码 0x" + String(maskText) + " 已吸合 0.5 秒后断开");
          } else {
            webSocket.sendTXT(num, "错误: 单路格式 0101~1001，多路格式 0001~FFFF");
          }
        }
      } else {
        webSocket.sendTXT(num, "格式错误: 单路发 0101~1001，多路发 0001~FFFF");
      }
      break;
    }
    
    case WStype_BIN: {
      Serial.printf("[%u] 收到 HEX 数据:", num);
      for (size_t i = 0; i < length; i++) {
        Serial.printf(" %02X", payload[i]);
      }
      Serial.println();

      if (!handleRelayBinaryCommand(num, payload, length)) {
        const char *errorMsg = "错误: HEX 单路指令发 2 字节[路号][动作]，多路闪断发 2 字节掩码";
        webSocket.sendTXT(num, errorMsg);
      }
      break;
    }
  }
}

// 处理根目录请求 (使用 PROGMEM 优化内存)
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
    <h1>网络配置</h1>
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
  html.replace("%SSID%", ssid);
  html.replace("%PASSWORD%", password);
  html.replace("%IP%", local_IP.toString());
  server.send(200, "text/html", html);
}

// 处理保存请求
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("ip")) {
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");
    String new_ip_str = server.arg("ip");
    
    // 基本验证
    if (new_password.length() < 8) {
      server.send(400, "text/plain", "Password must be at least 8 chars");
      return;
    }
    
    // 保存到 NVS
    preferences.begin("config", false);
    preferences.putString("ssid", new_ssid);
    preferences.putString("password", new_password);
    preferences.putString("ip", new_ip_str);
    preferences.end();
    
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='text-align:center;padding-top:50px;font-family:sans-serif;'><h1>配置已保存!</h1><p>设备正在重启...</p><script>setTimeout(function(){location.href='/';}, 5000);</script></body></html>";
    server.send(200, "text/html", html);
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// 执行恢复出厂设置的核心逻辑
void performFactoryReset() {
  preferences.begin("config", false);
  preferences.clear(); // 清除所有保存的配置
  preferences.end();

  currentRelayState = 0x0000;
  for (uint8_t i = 0; i < 16; i++) {
    relayPulseEndTimes[i] = 0;
  }
  applyRelayState();

  delay(500);
  ESP.restart();
}

// 处理重置请求 (Web端)
void handleReset() {
  String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='text-align:center;padding-top:50px;font-family:sans-serif;'><h1>已重置为默认配置!</h1><p>设备正在重启...</p><script>setTimeout(function(){location.href='/';}, 5000);</script></body></html>";
  server.send(200, "text/html", html);
  
  // 稍等片刻让网页发送完毕
  delay(500);
  
  performFactoryReset();
}

// 启动 OTA 功能
void setupOTA() {
  ArduinoOTA.setHostname("ModelLight-ESP32");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    Serial.println("Start updating " + type);
    currentRelayState = 0x0000;
    for (uint8_t i = 0; i < 16; i++) {
      relayPulseEndTimes[i] = 0;
    }
    applyRelayState();
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

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_BOOT, INPUT_PULLUP);

  preferences.begin("config", true);
  ssid = preferences.getString("ssid", "ModelLight");
  password = preferences.getString("password", "admin888");
  String ip_str = preferences.getString("ip", "192.168.18.252");
  preferences.end();

  local_IP.fromString(ip_str);
  gateway = local_IP; // 网关与IP保持一致

  HC595Init();
  applyRelayState();
  Serial.println("16路继电器闪断模式已初始化");

  WiFi.softAPConfig(local_IP, gateway, subnet);

  Serial.print("正在启动热点: ");
  Serial.println(ssid);
  bool result = WiFi.softAP(ssid.c_str(), password.c_str());
  
  if (result) {
    Serial.println("热点启动成功!");
    Serial.print("AP IP地址: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("热点启动失败!");
  }

  setupOTA();

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleReset);
  server.begin();
  Serial.println("Web 服务器已启动，端口号：80");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket 服务器已启动，端口号：81");
}

void loop() {
  ArduinoOTA.handle();
  webSocket.loop();
  server.handleClient();
  updateRelayPulses();
  delay(2);

  if (digitalRead(BUTTON_BOOT) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressTime = millis();
    } else {
      if (millis() - buttonPressTime > 5000) {
        Serial.println("BOOT 按钮长按超过5秒，执行恢复出厂设置...");

        for(int i=0; i<3; i++) {
          triggerRelayMaskPulse(0xFFFF);
          delay(200);
          currentRelayState = 0x0000;
          for (uint8_t j = 0; j < 16; j++) {
            relayPulseEndTimes[j] = 0;
          }
          applyRelayState();
          delay(200);
        }

        performFactoryReset();
      }
    }
  } else {
    buttonPressed = false;
  }
}
