#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_system.h>

#include <cstdarg>

#include "app_config.h"
#include "dashboard_html.h"

namespace {
constexpr size_t MAX_LOG_LINES = 100;
constexpr uint8_t BOOT_BUTTON_PIN = 0;

WiFiClient networkClient;
PubSubClient mqtt(networkClient);
WebServer web(80);
DNSServer dns;
Preferences preferences;

String deviceId;
String setupApSsid;
String hostName;
String statusTopic;
String telemetryTopic;
String commandTopic;

String mqttHost;
String mqttUser;
String mqttPassword;
uint16_t mqttPort = MQTT_PORT;

String logLines[MAX_LOG_LINES];
size_t logWriteIndex = 0;
size_t logLineCount = 0;

uint32_t lastMqttAttemptMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastStatusMs = 0;
uint32_t restartAtMs = 0;
bool forgetWifiOnRestart = false;
bool mdnsStarted = false;

void addLog(const String &message) {
  const String line = "[" + String(millis() / 1000) + "s] " + message;
  Serial.println(line);
  Serial0.println(line);
  logLines[logWriteIndex] = line;
  logWriteIndex = (logWriteIndex + 1) % MAX_LOG_LINES;
  if (logLineCount < MAX_LOG_LINES) {
    ++logLineCount;
  }
}

void addLogf(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  addLog(buffer);
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) {
          escaped += c;
        }
        break;
    }
  }
  return escaped;
}

String resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "上电启动";
    case ESP_RST_EXT: return "外部复位";
    case ESP_RST_SW: return "软件重启";
    case ESP_RST_PANIC: return "程序异常";
    case ESP_RST_INT_WDT: return "中断看门狗";
    case ESP_RST_TASK_WDT: return "任务看门狗";
    case ESP_RST_WDT: return "其他看门狗";
    case ESP_RST_DEEPSLEEP: return "深度睡眠唤醒";
    case ESP_RST_BROWNOUT: return "供电电压过低";
    case ESP_RST_SDIO: return "SDIO 复位";
    default: return "未知";
  }
}

String formatUptime() {
  uint32_t seconds = millis() / 1000;
  const uint32_t days = seconds / 86400;
  seconds %= 86400;
  const uint8_t hours = seconds / 3600;
  seconds %= 3600;
  const uint8_t minutes = seconds / 60;
  seconds %= 60;
  char value[40];
  snprintf(value, sizeof(value), "%lu天 %02u:%02u:%02lu",
           static_cast<unsigned long>(days), hours, minutes,
           static_cast<unsigned long>(seconds));
  return value;
}

void sendText(int code, const String &text) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(code, "text/plain; charset=utf-8", text);
}

void publishTelemetry() {
  if (!mqtt.connected()) {
    return;
  }
  const String payload =
      "{\"rssi\":" + String(WiFi.RSSI()) +
      ",\"uptime\":" + String(millis() / 1000) +
      ",\"freeHeap\":" + String(ESP.getFreeHeap()) +
      ",\"temperature\":" + String(temperatureRead(), 1) + "}";
  mqtt.publish(telemetryTopic.c_str(), payload.c_str());
}

void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    message += static_cast<char>(payload[i]);
  }

  addLogf("MQTT 收到 [%s]: %s", topic, message.c_str());
  if (message == "status") {
    publishTelemetry();
  } else if (message == "restart") {
    addLog("收到 MQTT 重启命令");
    restartAtMs = millis() + 1000;
  }
}

bool connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqttHost.isEmpty()) {
    return false;
  }

  const String clientId = "esp32s3-" + deviceId;
  bool connected = false;
  if (mqttUser.isEmpty()) {
    connected = mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true,
                             "offline");
  } else {
    connected = mqtt.connect(clientId.c_str(), mqttUser.c_str(),
                             mqttPassword.c_str(), statusTopic.c_str(), 1,
                             true, "offline");
  }

  if (!connected) {
    addLogf("MQTT 连接失败，状态码=%d", mqtt.state());
    return false;
  }

  mqtt.publish(statusTopic.c_str(), "online", true);
  mqtt.subscribe(commandTopic.c_str(), 1);
  addLogf("MQTT 已连接：%s:%u", mqttHost.c_str(), mqttPort);
  return true;
}

void startMdns() {
  if (mdnsStarted || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (MDNS.begin(hostName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    addLogf("局域网域名已启用：http://%s.local", hostName.c_str());
  }
}

void onSystemEvent(arduino_event_t *event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      addLogf("蓝牙配网已启动：%s，PoP=%s", PROV_DEVICE_NAME, PROV_POP);
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      addLog("已收到 Wi-Fi 配网信息");
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      addLog("Wi-Fi 配网成功");
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      addLog("Wi-Fi 配网失败，请检查名称和密码");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      addLogf("Wi-Fi 已连接：%s，IP=%s", WiFi.SSID().c_str(),
              WiFi.localIP().toString().c_str());
      startMdns();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      addLog("Wi-Fi 连接已断开，系统将自动重连");
      if (mqtt.connected()) {
        mqtt.disconnect();
      }
      break;
    case ARDUINO_EVENT_PROV_END:
      addLog("蓝牙配网服务已停止");
      break;
    default:
      break;
  }
}

void handleStatusApi() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const char *sdkVersion = ESP.getSdkVersion();
  const char *wifiHostname = WiFi.getHostname();
  const String buildStamp = String(__DATE__) + " " + __TIME__;

  String json;
  json.reserve(2600);
  json = "{\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  json += "\"chip\":{";
  json += "\"model\":\"" + jsonEscape(ESP.getChipModel()) + "\",";
  json += "\"revision\":" + String(ESP.getChipRevision()) + ",";
  json += "\"cores\":" + String(ESP.getChipCores()) + ",";
  json += "\"cpuMHz\":" + String(ESP.getCpuFreqMHz()) + "},";
  json += "\"memory\":{";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"minFreeHeap\":" + String(ESP.getMinFreeHeap()) + ",";
  json += "\"flash\":" + String(ESP.getFlashChipSize()) + ",";
  json += "\"psram\":" + String(ESP.getPsramSize()) + ",";
  json += "\"sketch\":" + String(ESP.getSketchSize()) + "},";
  json += "\"system\":{";
  json += "\"uptime\":\"" + jsonEscape(formatUptime()) + "\",";
  json += "\"temperature\":" + String(temperatureRead(), 1) + ",";
  json += "\"bootPressed\":" + String(digitalRead(BOOT_BUTTON_PIN) == LOW ? "true" : "false") + ",";
  json += "\"resetReason\":\"" + jsonEscape(resetReasonName()) + "\",";
  json += "\"sdk\":\"" + jsonEscape(sdkVersion ? sdkVersion : "") + "\",";
  json += "\"build\":\"" + jsonEscape(buildStamp) + "\"},";
  json += "\"wifi\":{";
  json += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(wifiConnected ? WiFi.SSID() : "—") + "\",";
  json += "\"ip\":\"" + jsonEscape(wifiConnected ? WiFi.localIP().toString() : "—") + "\",";
  json += "\"rssi\":" + String(wifiConnected ? WiFi.RSSI() : 0) + ",";
  json += "\"mac\":\"" + jsonEscape(WiFi.macAddress()) + "\",";
  json += "\"hostname\":\"" + jsonEscape(wifiHostname ? wifiHostname : hostName.c_str()) + "\"},";
  json += "\"ap\":{";
  json += "\"ssid\":\"" + jsonEscape(setupApSsid) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.softAPIP().toString()) + "\",";
  json += "\"mac\":\"" + jsonEscape(WiFi.softAPmacAddress()) + "\"},";
  json += "\"mqtt\":{";
  json += "\"connected\":" + String(mqtt.connected() ? "true" : "false") + ",";
  json += "\"host\":\"" + jsonEscape(mqttHost) + "\",";
  json += "\"port\":" + String(mqttPort) + ",";
  json += "\"user\":\"" + jsonEscape(mqttUser) + "\",";
  json += "\"state\":" + String(mqtt.state()) + ",";
  json += "\"statusTopic\":\"" + jsonEscape(statusTopic) + "\",";
  json += "\"telemetryTopic\":\"" + jsonEscape(telemetryTopic) + "\",";
  json += "\"commandTopic\":\"" + jsonEscape(commandTopic) + "\"}}";

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json; charset=utf-8", json);
}

void handleLogsApi() {
  String output;
  output.reserve(logLineCount * 65);
  const size_t oldest =
      (logWriteIndex + MAX_LOG_LINES - logLineCount) % MAX_LOG_LINES;
  for (size_t i = 0; i < logLineCount; ++i) {
    output += logLines[(oldest + i) % MAX_LOG_LINES];
    output += '\n';
  }
  sendText(200, output);
}

void handleWifiScanApi() {
  addLog("开始扫描附近 Wi-Fi");
  const int found = WiFi.scanNetworks(false, true);
  String json = "{\"networks\":[";
  const int count = found > 20 ? 20 : found;
  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      json += ',';
    }
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  addLogf("Wi-Fi 扫描完成，发现 %d 个网络", found < 0 ? 0 : found);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json; charset=utf-8", json);
}

void handleWifiConfig() {
  const String ssid = web.arg("ssid");
  const String password = web.arg("password");
  if (ssid.isEmpty()) {
    sendText(400, "请填写 Wi-Fi 名称");
    return;
  }

  addLogf("网页请求连接 Wi-Fi：%s", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  sendText(200, "配置已提交，连接通常需要几秒钟，请观察网络状态和日志");
}

void handleMqttConfig() {
  const String newHost = web.arg("host");
  const uint32_t newPort = web.arg("port").toInt();
  if (newHost.isEmpty() || newPort == 0 || newPort > 65535) {
    sendText(400, "MQTT 地址或端口无效");
    return;
  }

  mqttHost = newHost;
  mqttPort = static_cast<uint16_t>(newPort);
  mqttUser = web.arg("user");
  if (!web.arg("password").isEmpty()) {
    mqttPassword = web.arg("password");
    preferences.putString("mqtt_pass", mqttPassword);
  }
  preferences.putString("mqtt_host", mqttHost);
  preferences.putUShort("mqtt_port", mqttPort);
  preferences.putString("mqtt_user", mqttUser);

  if (mqtt.connected()) {
    mqtt.disconnect();
  }
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  lastMqttAttemptMs = millis() - 5000;
  addLogf("MQTT 设置已保存：%s:%u", mqttHost.c_str(), mqttPort);
  sendText(200, "MQTT 设置已保存，设备正在重新连接");
}

void handleMqttPublish() {
  const String message = web.arg("message");
  if (!mqtt.connected()) {
    sendText(503, "MQTT 尚未连接，请先检查服务器设置");
    return;
  }
  if (message.isEmpty()) {
    sendText(400, "消息不能为空");
    return;
  }
  if (mqtt.publish(commandTopic.c_str(), message.c_str())) {
    addLogf("网页已发送 MQTT 命令：%s", message.c_str());
    sendText(200, "命令已发送");
  } else {
    sendText(500, "发送失败");
  }
}

void setupWebServer() {
  web.on("/", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  web.on("/api/status", HTTP_GET, handleStatusApi);
  web.on("/api/logs", HTTP_GET, handleLogsApi);
  web.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  web.on("/api/wifi", HTTP_POST, handleWifiConfig);
  web.on("/api/mqtt", HTTP_POST, handleMqttConfig);
  web.on("/api/mqtt/publish", HTTP_POST, handleMqttPublish);
  web.on("/api/restart", HTTP_POST, []() {
    sendText(200, "设备正在重新启动");
    addLog("收到网页重启请求");
    restartAtMs = millis() + 1000;
  });
  web.on("/api/wifi/forget", HTTP_POST, []() {
    sendText(200, "Wi-Fi 配网已清除，设备正在重新启动");
    addLog("收到清除 Wi-Fi 配网请求");
    forgetWifiOnRestart = true;
    restartAtMs = millis() + 1000;
  });
  web.onNotFound([]() {
    if (web.method() == HTTP_GET) {
      web.sendHeader("Cache-Control", "no-store");
      web.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
    } else {
      sendText(404, "接口不存在");
    }
  });
  web.begin();
  addLog("设备管理网页已启动");
}

void setupConfigurationAp() {
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(setupApSsid.c_str(), PROV_POP)) {
    dns.start(53, "*", WiFi.softAPIP());
    addLogf("配网热点：%s，密码=%s，页面=http://%s", setupApSsid.c_str(),
            PROV_POP, WiFi.softAPIP().toString().c_str());
  } else {
    addLog("配网热点启动失败");
  }
}

void handleMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqtt.connected()) {
    if (millis() - lastMqttAttemptMs >= 5000) {
      lastMqttAttemptMs = millis();
      connectMqtt();
    }
    return;
  }

  mqtt.loop();
  if (millis() - lastTelemetryMs >= 10000) {
    lastTelemetryMs = millis();
    publishTelemetry();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(500);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  char idBuffer[13];
  snprintf(idBuffer, sizeof(idBuffer), "%012llX",
           static_cast<unsigned long long>(ESP.getEfuseMac()));
  deviceId = idBuffer;
  setupApSsid = "ESP32S3-Setup-" + deviceId.substring(8);
  hostName = "esp32s3-" + deviceId.substring(8);
  hostName.toLowerCase();
  statusTopic = "devices/" + deviceId + "/status";
  telemetryTopic = "devices/" + deviceId + "/telemetry";
  commandTopic = "devices/" + deviceId + "/command";

  preferences.begin("device-app", false);
  mqttHost = preferences.getString("mqtt_host", MQTT_HOST);
  mqttPort = preferences.getUShort("mqtt_port", MQTT_PORT);
  mqttUser = preferences.getString("mqtt_user", MQTT_USER);
  mqttPassword = preferences.getString("mqtt_pass", MQTT_PASSWORD);

  addLogf("ESP32-S3 启动，设备 ID=%s", deviceId.c_str());
  addLogf("重启原因：%s", resetReasonName().c_str());

  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);

  WiFi.onEvent(onSystemEvent);
  WiFi.setHostname(hostName.c_str());

  const char *serviceKey = nullptr;
  constexpr bool resetProvisioning = false;
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE,
                          WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1, PROV_POP,
                          PROV_DEVICE_NAME, serviceKey, nullptr,
                          resetProvisioning);

  setupConfigurationAp();
  setupWebServer();
  addLogf("蓝牙配网设备：%s，PoP=%s", PROV_DEVICE_NAME, PROV_POP);
}

void loop() {
  dns.processNextRequest();
  web.handleClient();
  handleMqtt();

  if (millis() - lastStatusMs >= 10000) {
    lastStatusMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      addLogf("等待配网：热点=%s，蓝牙=%s", setupApSsid.c_str(),
              PROV_DEVICE_NAME);
    } else if (!mqtt.connected()) {
      addLogf("Wi-Fi 正常，等待 MQTT：%s:%u", mqttHost.c_str(), mqttPort);
    }
  }

  if (restartAtMs != 0 &&
      static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    if (forgetWifiOnRestart) {
      WiFi.disconnect(true, true);
      delay(200);
    }
    ESP.restart();
  }

  delay(5);
}
