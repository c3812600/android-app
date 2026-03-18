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
#define RELAY1 32
#define RELAY2 33
#define RELAY3 25
#define RELAY4 26
#define RELAY5 27
#define RELAY6 14
#define RELAY7 12  // 注意：GPIO12 是启动引脚(Strapping pin)，开机时请勿外接上拉电阻
#define RELAY8 13

#define BUTTON_BOOT 0 // 开发板上的 BOOT 按钮
unsigned long buttonPressTime = 0;
bool buttonPressed = false;

// 将引脚放入数组，方便循环遍历控制
const int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};

// 继电器触发电平配置
// 注意：市面上大部分5V光耦继电器模块是“低电平触发”的。
// 如果你的继电器是低电平触发，请把 RELAY_ON 改为 LOW，RELAY_OFF 改为 HIGH
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// 开启 WebSocket 服务器，端口定为 81
WebSocketsServer webSocket = WebSocketsServer(81);

// 根据传入的1字节 Hex 值，更新8个继电器的状态
void setRelays(uint8_t hexValue) {
  for (int i = 0; i < 8; i++) {
    // 逐位读取状态 (第0位对应RELAY1，第7位对应RELAY8)
    bool isOn = (hexValue >> i) & 0x01;
    digitalWrite(relayPins[i], isOn ? RELAY_ON : RELAY_OFF);
  }
}

// WebSocket 事件处理回调
// 增加一个全局变量，用于记住当前8个继电器的总状态
uint8_t currentRelayState = 0x00; 

// 保存状态到 NVS (仅当状态改变时)
void saveState() {
  preferences.begin("state", false);
  uint8_t savedState = preferences.getUChar("relays", 0x00);
  if (savedState != currentRelayState) {
    preferences.putUChar("relays", currentRelayState);
  }
  preferences.end();
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
      
      // 连接后立即发送当前状态
      char reply[60];
      sprintf(reply, "已连接! 当前状态: 0x%02X", currentRelayState);
      webSocket.sendTXT(num, reply);
      break;
    }
    
    // 接收到文本消息
    case WStype_TEXT: {
      // 使用 strtoul 避免 String 内存碎片
      // 这里仍然为了方便处理 hex 字符串格式 (如 "FF", "0101")，我们简单处理一下
      // 如果 payload 是 C 字符串，可以直接用 strtoul
      
      // 为了安全性，确保 payload 是 null-terminated
      // WebSocketsServer 库通常保证 payload 是 buffer 指针，但不一定是 string
      // 我们这里还是转为 String 方便处理，但要注意长度限制
      String msg = "";
      for(size_t i=0; i<length; i++) msg += (char)payload[i];
      msg.trim(); 

      Serial.printf("[%u] 收到文本: %s\n", num, msg.c_str());

      bool stateChanged = false;

      // ==========================================
      // 【模式1】全量控制 (2位字符)，如 "FF", "00", "0F"
      // ==========================================
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
      // ==========================================
      // 【模式2】单路控制 (4位字符)，如 "0101", "0800"
      // ==========================================
      else if (msg.length() == 4) {
        // 提取前2位作为继电器编号(1-8)
        int relayIndex = strtol(msg.substring(0, 2).c_str(), NULL, 16);
        // 提取后2位作为开关动作(0或1)
        int action = strtol(msg.substring(2, 4).c_str(), NULL, 16);

        if (relayIndex >= 1 && relayIndex <= 8) {
          int bitPosition = relayIndex - 1; // 第1路对应第0位，以此类推
          
          uint8_t oldState = currentRelayState;
          if (action == 1) {
            currentRelayState |= (1 << bitPosition);
          } else if (action == 0) {
            currentRelayState &= ~(1 << bitPosition);
          }
          
          if (oldState != currentRelayState) stateChanged = true;
          
          // 执行动作
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
      
      // 如果状态改变，保存到 NVS (断电记忆)
      if (stateChanged) {
        saveState();
      }
      break;
    }
    
    // 二进制消息处理
    case WStype_BIN: {
      if(length > 0){
        currentRelayState = payload[0];
        setRelays(currentRelayState);
        saveState(); // 保存状态
        webSocket.sendBIN(num, &currentRelayState, 1);
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
  
  // 清除状态记忆
  preferences.begin("state", false);
  preferences.clear();
  preferences.end();
  
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
    // 升级开始时关闭所有继电器以防意外
    // setRelays(0x00); 
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
  
  // 初始化 BOOT 按钮
  pinMode(BUTTON_BOOT, INPUT_PULLUP);

  // 读取配置
  preferences.begin("config", true);
  ssid = preferences.getString("ssid", "ModelLight");
  password = preferences.getString("password", "admin888");
  String ip_str = preferences.getString("ip", "192.168.18.252");
  preferences.end();
  
  // 读取上次继电器状态
  preferences.begin("state", true);
  currentRelayState = preferences.getUChar("relays", 0x00);
  preferences.end();
  
  // 解析IP
  local_IP.fromString(ip_str);
  gateway = local_IP; // 网关与IP保持一致

  // 初始化所有继电器引脚为输出模式
  for(int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
  }
  
  // 恢复上次状态
  setRelays(currentRelayState);
  Serial.printf("恢复继电器状态: 0x%02X\n", currentRelayState);

  // 配置 AP 模式的 IP 地址
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // 启动 AP 模式
  Serial.print("正在启动热点: ");
  Serial.println(ssid);
  // 启动热点
  bool result = WiFi.softAP(ssid.c_str(), password.c_str());
  
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
  server.begin();
  Serial.println("Web 服务器已启动，端口号：80");

  // 启动 WebSocket 服务器
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket 服务器已启动，端口号：81");
}

void loop() {
  // 处理 OTA 请求
  ArduinoOTA.handle();
  // 保持 WebSocket 运行和侦听
  webSocket.loop();
  // 处理 Web 请求
  server.handleClient();
  
  // 稍微延迟，释放CPU给系统后台任务 (WiFi栈等)
  // 避免触发看门狗复位
  delay(2);
  
  // ================== 处理 BOOT 按钮长按恢复出厂设置 ==================
  if (digitalRead(BUTTON_BOOT) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressTime = millis();
    } else {
      // 持续按下，检查是否超过 5秒
      if (millis() - buttonPressTime > 5000) {
        Serial.println("BOOT 按钮长按超过5秒，执行恢复出厂设置...");
        
        // 闪烁所有继电器提示用户
        for(int i=0; i<3; i++) {
          setRelays(0xFF); delay(200);
          setRelays(0x00); delay(200);
        }
        
        // 调用重置函数 (不发网页，直接清除并重启)
        performFactoryReset();
      }
    }
  } else {
    buttonPressed = false;
  }
}