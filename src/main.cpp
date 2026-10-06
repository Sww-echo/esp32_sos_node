#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiProv.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdlib>

#include "app_config.h"
#include "dashboard_html.h"

namespace {
constexpr size_t MAX_LOG_LINES = 100;
constexpr uint16_t DEVICE_CONFIG_VERSION = 1;
constexpr int8_t BOOT_BUTTON_PIN = SOS_BUTTON_PIN;

WiFiClient networkClient;
WiFiClientSecure secureNetworkClient;
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
String alertTopic;
String alertAckTopic;
String heartbeatTopic;

String mqttHost;
String mqttUser;
String mqttPassword;
uint16_t mqttPort = MQTT_PORT;
bool mqttTls = MQTT_TLS;
bool mqttTransportReady = false;
String deviceDisplayName;
String deviceLocation;
uint32_t sosDebounceMs = SOS_BUTTON_DEBOUNCE_MS;
uint32_t sosCooldownMs = SOS_TRIGGER_COOLDOWN_MS;
uint32_t ackTimeoutMs = SOS_ACK_TIMEOUT_MS;
uint32_t retryBaseMs = SOS_RETRY_BASE_MS;
uint32_t retryMaxMs = SOS_RETRY_MAX_MS;
uint8_t maxAlertAttempts = SOS_MAX_ATTEMPTS;
uint32_t heartbeatIntervalMs = HEARTBEAT_INTERVAL_MS;
uint32_t telemetryIntervalMs = 10000;
uint32_t buzzerDurationMs = SOS_BEEP_MS;
uint32_t configVersion = DEVICE_CONFIG_VERSION;
bool ledEnabled = SOS_LED_PIN >= 0;
bool buzzerEnabled = SOS_BUZZER_PIN >= 0;
String webAdminPassword;
String provisioningPop;

String logLines[MAX_LOG_LINES];
size_t logWriteIndex = 0;
size_t logLineCount = 0;

uint32_t lastMqttAttemptMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastStatusMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t restartAtMs = 0;
bool forgetWifiOnRestart = false;
bool mdnsStarted = false;

enum class AlertState : uint8_t {
  Idle,
  Queued,
  WaitingAck,
  RetryWait,
  Acked,
  Failed,
};

AlertState alertState = AlertState::Idle;
String activeAlertEventId;
String activeAlertSource;
String activeAlertPayload;
String lastAlertEventId;
String lastAlertError;
uint32_t activeAlertCreatedUptime = 0;
uint8_t activeAlertAttempts = 0;
uint32_t activeAlertLastAttemptMs = 0;
uint32_t activeAlertNextAttemptMs = 0;
uint32_t lastAlertAckUptime = 0;

struct AlertRecord {
  String eventId;
  String source;
  String payload;
  uint32_t createdUptime = 0;
  uint8_t attempts = 0;
};

AlertRecord queuedAlerts[SOS_QUEUE_CAPACITY];
uint8_t queuedAlertCount = 0;

bool lastButtonReading = HIGH;
bool stableButtonReading = HIGH;
uint32_t buttonChangedAtMs = 0;
uint32_t buttonIgnoreUntilMs = 0;
uint32_t lastAlertTriggerMs = 0;
bool hasAlertTriggered = false;
uint32_t alertBeepUntilMs = 0;
uint32_t alertFeedbackUntilMs = 0;
bool alertFailureFeedback = false;

String alertStateName();
void publishHeartbeat();
void handleAlertDelivery();
void addLog(const String &message);
String buildAlertPayload();

bool configureMqttTransport() {
  if (mqttTls) {
    if (MQTT_TLS_CA_CERT[0] == '\0') {
      addLog("MQTT TLS 已启用但没有 CA 证书，拒绝连接");
      mqttTransportReady = false;
      return false;
    }
    secureNetworkClient.setCACert(MQTT_TLS_CA_CERT);
    mqtt.setClient(secureNetworkClient);
  } else {
    mqtt.setClient(networkClient);
  }
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqttTransportReady = true;
  return true;
}

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

uint32_t readConfigUInt(const char *key, uint32_t fallback, uint32_t minimum,
                        uint32_t maximum) {
  const uint32_t value = preferences.getULong(key, fallback);
  return value >= minimum && value <= maximum ? value : fallback;
}

bool parseConfigUIntArg(const char *name, uint32_t minimum, uint32_t maximum,
                        uint32_t &value) {
  if (!web.hasArg(name)) {
    return true;
  }
  String raw = web.arg(name);
  raw.trim();
  if (raw.isEmpty() || raw[0] == '-' || raw[0] == '+') {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(raw.c_str(), &end, 10);
  if (end == raw.c_str() || *end != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseConfigBoolArg(const char *name, bool &value) {
  if (!web.hasArg(name)) {
    return true;
  }
  String raw = web.arg(name);
  raw.trim();
  raw.toLowerCase();
  if (raw == "true" || raw == "1" || raw == "on") {
    value = true;
    return true;
  }
  if (raw == "false" || raw == "0" || raw == "off") {
    value = false;
    return true;
  }
  return false;
}

void saveRuntimeConfig() {
  preferences.putUShort("cfg_ver", DEVICE_CONFIG_VERSION);
  preferences.putString("display_name", deviceDisplayName);
  preferences.putString("location", deviceLocation);
  preferences.putString("mqtt_host", mqttHost);
  preferences.putUShort("mqtt_port", mqttPort);
  preferences.putString("mqtt_user", mqttUser);
  preferences.putString("mqtt_pass", mqttPassword);
  preferences.putULong("sos_debounce", sosDebounceMs);
  preferences.putULong("sos_cooldown", sosCooldownMs);
  preferences.putULong("ack_timeout", ackTimeoutMs);
  preferences.putULong("retry_base", retryBaseMs);
  preferences.putULong("retry_max", retryMaxMs);
  preferences.putUChar("max_attempts", maxAlertAttempts);
  preferences.putULong("heartbeat_ms", heartbeatIntervalMs);
  preferences.putULong("telemetry_ms", telemetryIntervalMs);
  preferences.putULong("buzzer_ms", buzzerDurationMs);
  preferences.putBool("led_enabled", ledEnabled);
  preferences.putBool("buzzer_enabled", buzzerEnabled);
  configVersion = DEVICE_CONFIG_VERSION;
}

void loadRuntimeConfig() {
  configVersion = preferences.getUShort("cfg_ver", DEVICE_CONFIG_VERSION);
  deviceDisplayName = preferences.getString("display_name", "");
  deviceLocation = preferences.getString("location", "");
  mqttHost = preferences.getString("mqtt_host", MQTT_HOST);
  mqttPort = preferences.getUShort("mqtt_port", MQTT_PORT);
  mqttUser = preferences.getString("mqtt_user", MQTT_USER);
  mqttPassword = preferences.getString("mqtt_pass", MQTT_PASSWORD);
  if (mqttPort == 0 || mqttPort > 65535) {
    mqttPort = MQTT_PORT;
  }
  sosDebounceMs = readConfigUInt("sos_debounce", SOS_BUTTON_DEBOUNCE_MS,
                                20, 5000);
  sosCooldownMs = readConfigUInt("sos_cooldown", SOS_TRIGGER_COOLDOWN_MS,
                                0, 3600000);
  ackTimeoutMs = readConfigUInt("ack_timeout", SOS_ACK_TIMEOUT_MS,
                               1000, 300000);
  retryBaseMs = readConfigUInt("retry_base", SOS_RETRY_BASE_MS,
                               1000, 3600000);
  const uint32_t retryMaxFallback =
      retryBaseMs > SOS_RETRY_MAX_MS ? retryBaseMs : SOS_RETRY_MAX_MS;
  retryMaxMs = readConfigUInt("retry_max", retryMaxFallback,
                              retryBaseMs, 86400000);
  maxAlertAttempts = static_cast<uint8_t>(readConfigUInt(
      "max_attempts", SOS_MAX_ATTEMPTS, 1, SOS_QUEUE_CAPACITY + 8));
  heartbeatIntervalMs = readConfigUInt("heartbeat_ms", HEARTBEAT_INTERVAL_MS,
                                       5000, 86400000);
  telemetryIntervalMs = readConfigUInt("telemetry_ms", 10000,
                                       5000, 86400000);
  buzzerDurationMs = readConfigUInt("buzzer_ms", SOS_BEEP_MS, 0, 60000);
  ledEnabled = preferences.getBool("led_enabled", SOS_LED_PIN >= 0);
  buzzerEnabled = preferences.getBool("buzzer_enabled", SOS_BUZZER_PIN >= 0);

  // Migrate the previous development broker setting and persist sanitized
  // defaults once the configuration is first loaded.
  if (MQTT_TLS && mqttHost == "192.168.1.20") {
    mqttHost = MQTT_HOST;
    mqttPort = MQTT_PORT;
    addLog("已将旧开发 MQTT 配置迁移到 TLS 云端地址");
  }
  saveRuntimeConfig();
}

void loadWebCredentials() {
  if (WEB_ADMIN_PASSWORD[0] != '\0') {
    webAdminPassword = WEB_ADMIN_PASSWORD;
    return;
  }

  webAdminPassword = preferences.getString("web_pass", "");
  if (!webAdminPassword.isEmpty()) {
    return;
  }

  char generated[17];
  snprintf(generated, sizeof(generated), "%08lX%08lX",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  webAdminPassword = generated;
  preferences.putString("web_pass", webAdminPassword);
  addLogf("网页管理员账号=%s，初始密码=%s（请从串口保存）",
          WEB_ADMIN_USER, webAdminPassword.c_str());
}

void loadProvisioningPop() {
  if (!PRODUCTION_BUILD) {
    provisioningPop = PROV_POP;
    return;
  }

  provisioningPop = preferences.getString("prov_pop", "");
  if (!provisioningPop.isEmpty()) {
    return;
  }

  char generated[9];
  snprintf(generated, sizeof(generated), "%08lX",
           static_cast<unsigned long>(esp_random()));
  provisioningPop = generated;
  preferences.putString("prov_pop", provisioningPop);
  addLogf("生产配网 PoP 已生成，请从串口保存：%s", provisioningPop.c_str());
}

bool requireWebAuth() {
  if (webAdminPassword.isEmpty() ||
      web.authenticate(WEB_ADMIN_USER, webAdminPassword.c_str())) {
    return true;
  }
  web.requestAuthentication();
  return false;
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

String alertStateName() {
  switch (alertState) {
    case AlertState::Idle: return "idle";
    case AlertState::Queued: return "queued";
    case AlertState::WaitingAck: return "waiting_ack";
    case AlertState::RetryWait: return "retry_wait";
    case AlertState::Acked: return "acked";
    case AlertState::Failed: return "failed";
    default: return "unknown";
  }
}

bool hasActiveAlert() {
  return !activeAlertEventId.isEmpty();
}

uint8_t pendingAlertCount() {
  return queuedAlertCount + (hasActiveAlert() ? 1 : 0);
}

void updateAlertOutputs() {
  bool ledOn = false;
  if (hasActiveAlert()) {
    if (alertState == AlertState::RetryWait) {
      ledOn = (millis() / 300) % 2 == 0;
    } else {
      ledOn = true;
    }
  } else if (millis() < alertFeedbackUntilMs) {
    ledOn = (millis() / (alertFailureFeedback ? 250 : 120)) % 2 == 0;
  }
  if (SOS_LED_PIN >= 0 && ledEnabled) {
    digitalWrite(SOS_LED_PIN, ledOn ? HIGH : LOW);
  }
  if (SOS_BUZZER_PIN >= 0 && buzzerEnabled) {
    bool buzzerOn = millis() < alertBeepUntilMs;
    if (!buzzerOn && alertFailureFeedback &&
        millis() < alertFeedbackUntilMs) {
      buzzerOn = (millis() / 500) % 2 == 0;
    }
    digitalWrite(SOS_BUZZER_PIN, buzzerOn ? HIGH : LOW);
  }
}

String alertQueueKey(uint8_t index, const char *suffix) {
  return "sos_q" + String(index) + "_" + suffix;
}

void clearActiveAlertFields() {
  activeAlertEventId = "";
  activeAlertSource = "";
  activeAlertPayload = "";
  activeAlertCreatedUptime = 0;
  activeAlertAttempts = 0;
  activeAlertLastAttemptMs = 0;
  activeAlertNextAttemptMs = 0;
}

void removePreferenceIfPresent(const String &key) {
  if (preferences.isKey(key.c_str())) {
    preferences.remove(key.c_str());
  }
}

void persistAlertQueue() {
  preferences.putUChar("sos_q_count", queuedAlertCount);
  for (uint8_t i = 0; i < SOS_QUEUE_CAPACITY; ++i) {
    const String idKey = alertQueueKey(i, "id");
    const String sourceKey = alertQueueKey(i, "source");
    const String payloadKey = alertQueueKey(i, "payload");
    const String createdKey = alertQueueKey(i, "created");
    const String attemptsKey = alertQueueKey(i, "attempts");
    if (i < queuedAlertCount) {
      preferences.putString(idKey.c_str(), queuedAlerts[i].eventId);
      preferences.putString(sourceKey.c_str(), queuedAlerts[i].source);
      preferences.putString(payloadKey.c_str(), queuedAlerts[i].payload);
      preferences.putULong(createdKey.c_str(), queuedAlerts[i].createdUptime);
      preferences.putUChar(attemptsKey.c_str(), queuedAlerts[i].attempts);
    } else {
      removePreferenceIfPresent(idKey);
      removePreferenceIfPresent(sourceKey);
      removePreferenceIfPresent(payloadKey);
      removePreferenceIfPresent(createdKey);
      removePreferenceIfPresent(attemptsKey);
    }
  }
}

void persistActiveAlert() {
  if (!hasActiveAlert()) {
    removePreferenceIfPresent("sos_id");
    removePreferenceIfPresent("sos_source");
    removePreferenceIfPresent("sos_payload");
    removePreferenceIfPresent("sos_created");
    removePreferenceIfPresent("sos_attempts");
  } else {
    preferences.putString("sos_id", activeAlertEventId);
    preferences.putString("sos_source", activeAlertSource);
    preferences.putString("sos_payload", activeAlertPayload);
    preferences.putULong("sos_created", activeAlertCreatedUptime);
    preferences.putUChar("sos_attempts", activeAlertAttempts);
  }
  preferences.putUChar("sos_state", static_cast<uint8_t>(alertState));
  preferences.putString("sos_last_id", lastAlertEventId);
  preferences.putString("sos_last_error", lastAlertError);
  persistAlertQueue();
}

bool activateNextQueuedAlert() {
  if (hasActiveAlert() || queuedAlertCount == 0) {
    return false;
  }

  const AlertRecord next = queuedAlerts[0];
  activeAlertEventId = next.eventId;
  activeAlertSource = next.source;
  activeAlertPayload = next.payload;
  activeAlertCreatedUptime = next.createdUptime;
  activeAlertAttempts = next.attempts;
  if (activeAlertPayload.isEmpty()) {
    activeAlertPayload = buildAlertPayload();
  }
  for (uint8_t i = 1; i < queuedAlertCount; ++i) {
    queuedAlerts[i - 1] = queuedAlerts[i];
  }
  --queuedAlertCount;
  activeAlertNextAttemptMs = 0;
  alertState = AlertState::Queued;
  persistActiveAlert();
  addLogf("恢复队列中的 SOS 事件：%s", activeAlertEventId.c_str());
  return true;
}

void loadActiveAlert() {
  activeAlertEventId = preferences.getString("sos_id", "");
  activeAlertSource = preferences.getString("sos_source", "button");
  activeAlertPayload = preferences.getString("sos_payload", "");
  activeAlertCreatedUptime = preferences.getULong("sos_created", 0);
  activeAlertAttempts = preferences.getUChar("sos_attempts", 0);
  lastAlertEventId = preferences.getString("sos_last_id", "");
  lastAlertAckUptime = preferences.getULong("sos_ack_uptime", 0);
  lastAlertError = preferences.getString("sos_last_error", "");
  const uint8_t savedState = preferences.getUChar(
      "sos_state", static_cast<uint8_t>(AlertState::Idle));
  alertState = savedState <= static_cast<uint8_t>(AlertState::Failed)
                   ? static_cast<AlertState>(savedState)
                   : AlertState::Idle;

  queuedAlertCount = preferences.getUChar("sos_q_count", 0);
  if (queuedAlertCount > SOS_QUEUE_CAPACITY) {
    queuedAlertCount = SOS_QUEUE_CAPACITY;
  }
  for (uint8_t i = 0; i < queuedAlertCount; ++i) {
    const String idKey = alertQueueKey(i, "id");
    const String sourceKey = alertQueueKey(i, "source");
    const String payloadKey = alertQueueKey(i, "payload");
    const String createdKey = alertQueueKey(i, "created");
    const String attemptsKey = alertQueueKey(i, "attempts");
    queuedAlerts[i].eventId = preferences.getString(idKey.c_str(), "");
    queuedAlerts[i].source = preferences.getString(sourceKey.c_str(), "button");
    queuedAlerts[i].payload = preferences.getString(payloadKey.c_str(), "");
    queuedAlerts[i].createdUptime = preferences.getULong(createdKey.c_str(), 0);
    queuedAlerts[i].attempts = preferences.getUChar(attemptsKey.c_str(), 0);
  }

  if (hasActiveAlert()) {
    if (activeAlertAttempts >= maxAlertAttempts) {
      if (lastAlertEventId.isEmpty()) {
        lastAlertEventId = activeAlertEventId;
      }
      lastAlertError = "设备重启时已达到最大尝试次数";
      clearActiveAlertFields();
      alertState = AlertState::Failed;
      alertFailureFeedback = true;
      alertFeedbackUntilMs = millis() + 10000;
      persistActiveAlert();
      addLog("SOS 事件已失败：设备重启时已达到最大尝试次数");
    } else {
      alertState = AlertState::Queued;
      activeAlertNextAttemptMs = 0;
      addLogf("恢复未确认 SOS 事件：%s", activeAlertEventId.c_str());
    }
  }
  if (!hasActiveAlert()) {
    activateNextQueuedAlert();
  }
}

uint32_t nextAlertSequence() {
  uint32_t sequence = preferences.getULong("sos_seq", 0) + 1;
  if (sequence == 0) {
    sequence = 1;
  }
  preferences.putULong("sos_seq", sequence);
  return sequence;
}

String buildAlertEventId(uint32_t sequence) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%s-%06lu", deviceId.c_str(),
           static_cast<unsigned long>(sequence));
  return buffer;
}

String buildAlertPayload(const String &eventId, const String &source,
                         uint32_t createdUptime) {
  return "{\"event_id\":\"" + jsonEscape(eventId) +
         "\",\"device_id\":\"" + jsonEscape(deviceId) +
         "\",\"display_name\":\"" + jsonEscape(deviceDisplayName) +
         "\",\"location\":\"" + jsonEscape(deviceLocation) +
         "\",\"type\":\"sos\",\"source\":\"" +
         jsonEscape(source) + "\",\"uptime\":" +
         String(createdUptime) + ",\"rssi\":" +
         String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) +
         ",\"firmware\":\"" + jsonEscape(FIRMWARE_VERSION) + "\"}";
}

String buildAlertPayload() {
  return buildAlertPayload(activeAlertEventId, activeAlertSource,
                           activeAlertCreatedUptime);
}

uint32_t alertRetryDelayMs() {
  const uint8_t exponent = activeAlertAttempts > 4 ? 4 : activeAlertAttempts;
  uint32_t delayMs = retryBaseMs * (1UL << exponent);
  return delayMs > retryMaxMs ? retryMaxMs : delayMs;
}

void markAlertFailed(const String &reason) {
  const String failedEventId = activeAlertEventId;
  if (lastAlertEventId.isEmpty()) {
    lastAlertEventId = failedEventId;
  }
  lastAlertError = reason;
  clearActiveAlertFields();
  alertFailureFeedback = true;
  alertFeedbackUntilMs = millis() + 10000;
  alertBeepUntilMs = 0;
  if (queuedAlertCount > 0) {
    alertState = AlertState::Queued;
    activateNextQueuedAlert();
  } else {
    alertState = AlertState::Failed;
    persistActiveAlert();
  }
  addLogf("SOS 发送失败：%s，事件=%s", reason.c_str(),
          failedEventId.c_str());
  updateAlertOutputs();
}

void scheduleAlertRetry(const String &reason) {
  if (activeAlertAttempts >= maxAlertAttempts) {
    markAlertFailed(reason + "，已达到最大尝试次数");
    return;
  }

  alertState = AlertState::RetryWait;
  activeAlertNextAttemptMs = millis() + alertRetryDelayMs();
  lastAlertError = reason;
  persistActiveAlert();
  addLogf("SOS 将重试：%s，约 %lu ms 后，事件=%s", reason.c_str(),
          static_cast<unsigned long>(alertRetryDelayMs()),
          activeAlertEventId.c_str());
  updateAlertOutputs();
}

bool publishActiveAlert() {
  if (!hasActiveAlert() || !mqtt.connected()) {
    return false;
  }
  if (activeAlertAttempts >= maxAlertAttempts) {
    markAlertFailed("已达到最大尝试次数");
    return false;
  }

  ++activeAlertAttempts;
  activeAlertPayload = buildAlertPayload();
  activeAlertLastAttemptMs = millis();
  persistActiveAlert();

  // PubSubClient 2.8 publishes QoS 0. Persistence, stable event_id and
  // server-side idempotency provide the retry semantics for this first pass.
  if (!mqtt.publish(alertTopic.c_str(), activeAlertPayload.c_str(), false)) {
    scheduleAlertRetry("MQTT 发布失败");
    return false;
  }

  alertState = AlertState::WaitingAck;
  activeAlertNextAttemptMs = 0;
  lastAlertError = "";
  persistActiveAlert();
  addLogf("SOS 已发布，等待 ACK：%s（第 %u 次）",
          activeAlertEventId.c_str(), activeAlertAttempts);
  updateAlertOutputs();
  return true;
}

String jsonStringField(const String &json, const char *key) {
  const String marker = "\"" + String(key) + "\"";
  const int keyIndex = json.indexOf(marker);
  if (keyIndex < 0) {
    return "";
  }
  const int colonIndex = json.indexOf(':', keyIndex + marker.length());
  if (colonIndex < 0) {
    return "";
  }
  const int start = json.indexOf('"', colonIndex + 1);
  if (start < 0) {
    return "";
  }
  const int end = json.indexOf('"', start + 1);
  if (end < 0) {
    return "";
  }
  return json.substring(start + 1, end);
}

bool jsonBoolField(const String &json, const char *key, bool defaultValue) {
  const String marker = "\"" + String(key) + "\"";
  const int keyIndex = json.indexOf(marker);
  if (keyIndex < 0) {
    return defaultValue;
  }
  const int colonIndex = json.indexOf(':', keyIndex + marker.length());
  if (colonIndex < 0) {
    return defaultValue;
  }
  const String value = json.substring(colonIndex + 1, colonIndex + 12);
  String trimmed = value;
  trimmed.trim();
  return trimmed.startsWith("true") ? true :
         trimmed.startsWith("false") ? false : defaultValue;
}

void acknowledgeActiveAlert(const String &eventId) {
  if (!hasActiveAlert() || eventId != activeAlertEventId) {
    addLogf("忽略不匹配的 SOS ACK：%s", eventId.c_str());
    return;
  }

  const String acknowledgedEventId = activeAlertEventId;
  lastAlertAckUptime = millis() / 1000;
  preferences.putULong("sos_ack_uptime", lastAlertAckUptime);
  clearActiveAlertFields();
  lastAlertError = "";
  alertFailureFeedback = false;
  alertFeedbackUntilMs = millis() + 1000;
  alertBeepUntilMs = millis() + 250;
  if (queuedAlertCount > 0) {
    alertState = AlertState::Queued;
    activateNextQueuedAlert();
  } else {
    alertState = AlertState::Acked;
    persistActiveAlert();
  }
  addLogf("SOS 已收到 ACK：%s", acknowledgedEventId.c_str());
  updateAlertOutputs();
}

void handleAlertAck(const String &message) {
  String normalized = message;
  normalized.trim();
  if (!normalized.startsWith("{")) {
    addLog("忽略非 JSON SOS ACK");
    return;
  }
  String eventId = jsonStringField(message, "event_id");
  if (eventId.isEmpty()) {
    addLog("SOS ACK 缺少 event_id");
    return;
  }
  const String ackDeviceId = jsonStringField(message, "device_id");
  if (ackDeviceId != deviceId) {
    addLog("忽略 device_id 不匹配的 SOS ACK");
    return;
  }
  if (!hasActiveAlert() || eventId != activeAlertEventId) {
    addLogf("忽略不匹配的 SOS ACK：%s", eventId.c_str());
    return;
  }
  if (!jsonBoolField(message, "accepted", false)) {
    scheduleAlertRetry("服务端拒绝 SOS 或 ACK 缺少 accepted=true");
    return;
  }
  acknowledgeActiveAlert(eventId);
}

void sendText(int code, const String &text) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(code, "text/plain; charset=utf-8", text);
}

bool requestSosAlert(const String &source) {
  if (!hasActiveAlert() && queuedAlertCount > 0) {
    activateNextQueuedAlert();
  }
  if (hasActiveAlert() && queuedAlertCount >= SOS_QUEUE_CAPACITY) {
    lastAlertError = "报警队列已满";
    persistActiveAlert();
    addLog("忽略新的 SOS：报警队列已满");
    return false;
  }
  if (hasAlertTriggered &&
      millis() - lastAlertTriggerMs < sosCooldownMs) {
    addLog("忽略过快的重复 SOS 触发");
    return false;
  }

  const String eventId = buildAlertEventId(nextAlertSequence());
  const uint32_t createdUptime = millis() / 1000;
  const String payload = buildAlertPayload(eventId, source, createdUptime);
  lastAlertEventId = eventId;
  lastAlertError = "等待 MQTT";
  lastAlertTriggerMs = millis();
  hasAlertTriggered = true;
  alertFailureFeedback = false;
  alertBeepUntilMs = millis() + buzzerDurationMs;

  if (!hasActiveAlert()) {
    activeAlertEventId = eventId;
    activeAlertSource = source;
    activeAlertCreatedUptime = createdUptime;
    activeAlertAttempts = 0;
    activeAlertLastAttemptMs = 0;
    activeAlertNextAttemptMs = 0;
    activeAlertPayload = payload;
    alertState = AlertState::Queued;
  } else {
    AlertRecord &queued = queuedAlerts[queuedAlertCount++];
    queued.eventId = eventId;
    queued.source = source;
    queued.payload = payload;
    queued.createdUptime = createdUptime;
    queued.attempts = 0;
  }
  persistActiveAlert();
  addLogf("SOS 已触发：事件=%s，来源=%s，队列=%u", eventId.c_str(),
          source.c_str(), static_cast<unsigned>(queuedAlertCount));
  updateAlertOutputs();
  return true;
}

void handleButton() {
  if (SOS_BUTTON_PIN < 0) {
    return;
  }

  const bool reading = digitalRead(SOS_BUTTON_PIN) == HIGH;
  if (static_cast<int32_t>(millis() - buttonIgnoreUntilMs) < 0) {
    lastButtonReading = reading;
    stableButtonReading = reading;
    buttonChangedAtMs = millis();
    return;
  }
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    buttonChangedAtMs = millis();
  }

  if (millis() - buttonChangedAtMs < sosDebounceMs ||
      reading == stableButtonReading) {
    return;
  }

  stableButtonReading = reading;
  // INPUT_PULLUP: LOW means the button is pressed. Trigger once on the
  // debounced edge; holding the large button cannot generate a storm.
  if (!stableButtonReading) {
    requestSosAlert("button");
  }
}

void handleAlertDelivery() {
  if (!hasActiveAlert()) {
    if (!activateNextQueuedAlert()) {
      updateAlertOutputs();
      return;
    }
  }

  const uint32_t now = millis();
  if (alertState == AlertState::WaitingAck &&
      now - activeAlertLastAttemptMs >= ackTimeoutMs) {
    scheduleAlertRetry("等待 ACK 超时");
  }

  if ((alertState == AlertState::Queued ||
       alertState == AlertState::RetryWait) &&
      mqtt.connected() &&
      (activeAlertNextAttemptMs == 0 ||
       static_cast<int32_t>(now - activeAlertNextAttemptMs) >= 0)) {
    publishActiveAlert();
  }

  updateAlertOutputs();
}

void publishHeartbeat() {
  if (!mqtt.connected()) {
    return;
  }
  const String payload =
      "{\"device_id\":\"" + jsonEscape(deviceId) +
      "\",\"display_name\":\"" + jsonEscape(deviceDisplayName) +
      "\",\"location\":\"" + jsonEscape(deviceLocation) +
      "\",\"uptime\":" + String(millis() / 1000) +
      ",\"rssi\":" + String(WiFi.RSSI()) +
      ",\"firmware\":\"" + jsonEscape(FIRMWARE_VERSION) +
      "\",\"config_version\":" + String(configVersion) +
      ",\"alert_state\":\"" + alertStateName() +
      "\",\"pending_alerts\":" + String(pendingAlertCount()) +
      "}";
  if (mqtt.publish(heartbeatTopic.c_str(), payload.c_str(), false)) {
    addLog("MQTT 心跳已发送");
  } else {
    addLog("MQTT 心跳发送失败");
  }
}

void handleHeartbeat() {
  if (!mqtt.connected()) {
    return;
  }
  if (millis() - lastHeartbeatMs >= heartbeatIntervalMs) {
    lastHeartbeatMs = millis();
    publishHeartbeat();
  }
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
  if (String(topic) == alertAckTopic) {
    handleAlertAck(message);
    return;
  }

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
  if (!mqttTransportReady && !configureMqttTransport()) {
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
  if (!mqtt.subscribe(alertAckTopic.c_str(), 1)) {
    addLog("MQTT 报警 ACK 主题订阅失败");
  }
  lastHeartbeatMs = millis() - heartbeatIntervalMs;
  publishHeartbeat();
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
      addLogf("蓝牙配网已启动：%s", PROV_DEVICE_NAME);
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
      if (!PRODUCTION_BUILD && DEV_WIFI_SSID[0] != '\0') {
        preferences.putBool("dev_wifi_seeded", true);
      }
      startMdns();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      addLog("Wi-Fi 连接已断开，系统将自动重连");
      // Leave the TCP session unclean so the broker can publish the MQTT LWT
      // and change the retained status to offline.
      break;
    case ARDUINO_EVENT_PROV_END:
      addLog("蓝牙配网服务已停止");
      break;
    default:
      break;
  }
}

void handleStatusApi() {
  if (!requireWebAuth()) {
    return;
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const char *sdkVersion = ESP.getSdkVersion();
  const char *wifiHostname = WiFi.getHostname();
  const String buildStamp = String(__DATE__) + " " + __TIME__;

  String json;
  json.reserve(3400);
  json = "{\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  json += "\"displayName\":\"" + jsonEscape(deviceDisplayName) +
          "\",\"location\":\"" + jsonEscape(deviceLocation) +
          "\",\"configVersion\":" + String(configVersion) + ",";
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
  const bool buttonPressed = BOOT_BUTTON_PIN >= 0 &&
                             digitalRead(BOOT_BUTTON_PIN) == LOW;
  json += "\"bootPressed\":" + String(buttonPressed ? "true" : "false") + ",";
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
  json += "\"commandTopic\":\"" + jsonEscape(commandTopic) + "\",";
  json += "\"alertTopic\":\"" + jsonEscape(alertTopic) + "\",";
  json += "\"alertAckTopic\":\"" + jsonEscape(alertAckTopic) + "\",";
  json += "\"heartbeatTopic\":\"" + jsonEscape(heartbeatTopic) + "\"},";
  json += "\"alert\":{";
  json += "\"state\":\"" + jsonEscape(alertStateName()) + "\",";
  json += "\"activeEventId\":\"" + jsonEscape(activeAlertEventId) + "\",";
  json += "\"lastEventId\":\"" + jsonEscape(lastAlertEventId) + "\",";
  json += "\"attempts\":" + String(activeAlertAttempts) + ",";
  json += "\"pending\":" + String(pendingAlertCount()) + ",";
  json += "\"lastAckUptime\":" + String(lastAlertAckUptime) + ",";
  json += "\"error\":\"" + jsonEscape(lastAlertError) + "\"}}";

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json; charset=utf-8", json);
}

void handleLogsApi() {
  if (!requireWebAuth()) {
    return;
  }
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
  if (!requireWebAuth()) {
    return;
  }
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
  if (!requireWebAuth()) {
    return;
  }
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
  if (!requireWebAuth()) {
    return;
  }
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
  }
  saveRuntimeConfig();

  if (mqtt.connected()) {
    mqtt.publish(statusTopic.c_str(), "offline", true);
    mqtt.disconnect();
  }
  mqttTransportReady = false;
  configureMqttTransport();
  lastMqttAttemptMs = millis() - 5000;
  addLogf("MQTT 设置已保存：%s:%u", mqttHost.c_str(), mqttPort);
  sendText(200, "MQTT 设置已保存，设备正在重新连接");
}

void handleMqttPublish() {
  if (!requireWebAuth()) {
    return;
  }
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

void handleConfigApi() {
  if (!requireWebAuth()) {
    return;
  }

  if (web.method() == HTTP_GET) {
    String json;
    json.reserve(1800);
    json = "{\"version\":" + String(configVersion) + ",";
    json += "\"device\":{\"displayName\":\"" +
            jsonEscape(deviceDisplayName) + "\",\"location\":\"" +
            jsonEscape(deviceLocation) + "\"},";
    json += "\"mqtt\":{\"host\":\"" + jsonEscape(mqttHost) +
            "\",\"port\":" + String(mqttPort) +
            ",\"user\":\"" + jsonEscape(mqttUser) +
            "\",\"tls\":" + String(mqttTls ? "true" : "false") +
            ",\"passwordConfigured\":" +
            String(mqttPassword.isEmpty() ? "false" : "true") + "},";
    json += "\"sos\":{\"debounceMs\":" + String(sosDebounceMs) +
            ",\"cooldownMs\":" + String(sosCooldownMs) +
            ",\"ackTimeoutMs\":" + String(ackTimeoutMs) +
            ",\"retryBaseMs\":" + String(retryBaseMs) +
            ",\"retryMaxMs\":" + String(retryMaxMs) +
            ",\"maxAttempts\":" + String(maxAlertAttempts) +
            ",\"queueCapacity\":" + String(SOS_QUEUE_CAPACITY) + "},";
    json += "\"runtime\":{\"heartbeatIntervalMs\":" +
            String(heartbeatIntervalMs) +
            ",\"telemetryIntervalMs\":" + String(telemetryIntervalMs) +
            ",\"buzzerDurationMs\":" + String(buzzerDurationMs) +
            ",\"ledEnabled\":" + String(ledEnabled ? "true" : "false") +
            ",\"buzzerEnabled\":" +
            String(buzzerEnabled ? "true" : "false") +
            ",\"ledAvailable\":" +
            String(SOS_LED_PIN >= 0 ? "true" : "false") +
            ",\"buzzerAvailable\":" +
            String(SOS_BUZZER_PIN >= 0 ? "true" : "false") + "}}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", json);
    return;
  }

  if (web.method() != HTTP_POST) {
    sendText(405, "仅支持 GET 或 POST");
    return;
  }

  String nextDisplayName = deviceDisplayName;
  String nextLocation = deviceLocation;
  uint32_t nextDebounceMs = sosDebounceMs;
  uint32_t nextCooldownMs = sosCooldownMs;
  uint32_t nextAckTimeoutMs = ackTimeoutMs;
  uint32_t nextRetryBaseMs = retryBaseMs;
  uint32_t nextRetryMaxMs = retryMaxMs;
  uint32_t nextMaxAttempts = maxAlertAttempts;
  uint32_t nextHeartbeatMs = heartbeatIntervalMs;
  uint32_t nextTelemetryMs = telemetryIntervalMs;
  uint32_t nextBuzzerMs = buzzerDurationMs;
  bool nextLedEnabled = ledEnabled;
  bool nextBuzzerEnabled = buzzerEnabled;

  if (web.hasArg("display_name")) {
    nextDisplayName = web.arg("display_name");
    nextDisplayName.trim();
    if (nextDisplayName.length() > 64) {
      sendText(400, "设备名称不能超过 64 个字符");
      return;
    }
  }
  if (web.hasArg("location")) {
    nextLocation = web.arg("location");
    nextLocation.trim();
    if (nextLocation.length() > 64) {
      sendText(400, "设备位置不能超过 64 个字符");
      return;
    }
  }

  bool valid = parseConfigUIntArg("sos_debounce_ms", 20, 5000,
                                 nextDebounceMs) &&
               parseConfigUIntArg("sos_cooldown_ms", 0, 3600000,
                                 nextCooldownMs) &&
               parseConfigUIntArg("ack_timeout_ms", 1000, 300000,
                                 nextAckTimeoutMs) &&
               parseConfigUIntArg("retry_base_ms", 1000, 3600000,
                                 nextRetryBaseMs) &&
               parseConfigUIntArg("retry_max_ms", 1000, 86400000,
                                 nextRetryMaxMs) &&
               parseConfigUIntArg("max_attempts", 1, SOS_QUEUE_CAPACITY + 8,
                                 nextMaxAttempts) &&
               parseConfigUIntArg("heartbeat_interval_ms", 5000, 86400000,
                                 nextHeartbeatMs) &&
               parseConfigUIntArg("telemetry_interval_ms", 5000, 86400000,
                                 nextTelemetryMs) &&
               parseConfigUIntArg("buzzer_duration_ms", 0, 60000,
                                 nextBuzzerMs) &&
               parseConfigBoolArg("led_enabled", nextLedEnabled) &&
               parseConfigBoolArg("buzzer_enabled", nextBuzzerEnabled);
  if (!valid || nextRetryMaxMs < nextRetryBaseMs) {
    sendText(400, "配置参数无效，请检查数值范围和重试间隔关系");
    return;
  }

  deviceDisplayName = nextDisplayName;
  deviceLocation = nextLocation;
  sosDebounceMs = nextDebounceMs;
  sosCooldownMs = nextCooldownMs;
  ackTimeoutMs = nextAckTimeoutMs;
  retryBaseMs = nextRetryBaseMs;
  retryMaxMs = nextRetryMaxMs;
  maxAlertAttempts = static_cast<uint8_t>(nextMaxAttempts);
  heartbeatIntervalMs = nextHeartbeatMs;
  telemetryIntervalMs = nextTelemetryMs;
  buzzerDurationMs = nextBuzzerMs;
  ledEnabled = nextLedEnabled;
  buzzerEnabled = nextBuzzerEnabled;
  saveRuntimeConfig();

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json; charset=utf-8",
           "{\"saved\":true,\"version\":" + String(configVersion) +
               ",\"reconnectRequired\":false,\"rebootRequired\":false}");
}

void handleAlertTest() {
  if (!requireWebAuth()) {
    return;
  }
  if (web.arg("confirm") != "SOS") {
    sendText(400, "测试报警需要 confirm=SOS");
    return;
  }
  if (!requestSosAlert("web")) {
    sendText(409, "已有未确认报警或触发过于频繁");
    return;
  }
  sendText(202, "测试报警已进入发送队列");
}

void setupWebServer() {
  web.on("/", HTTP_GET, []() {
    if (!requireWebAuth()) {
      return;
    }
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  web.on("/api/status", HTTP_GET, handleStatusApi);
  web.on("/api/config", HTTP_GET, handleConfigApi);
  web.on("/api/config", HTTP_POST, handleConfigApi);
  web.on("/api/logs", HTTP_GET, handleLogsApi);
  web.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  web.on("/api/wifi", HTTP_POST, handleWifiConfig);
  web.on("/api/mqtt", HTTP_POST, handleMqttConfig);
  web.on("/api/mqtt/publish", HTTP_POST, handleMqttPublish);
  web.on("/api/alert/test", HTTP_POST, handleAlertTest);
  web.on("/api/restart", HTTP_POST, []() {
    if (!requireWebAuth()) {
      return;
    }
    sendText(200, "设备正在重新启动");
    addLog("收到网页重启请求");
    restartAtMs = millis() + 1000;
  });
  web.on("/api/wifi/forget", HTTP_POST, []() {
    if (!requireWebAuth()) {
      return;
    }
    sendText(200, "Wi-Fi 配网已清除，设备正在重新启动");
    addLog("收到清除 Wi-Fi 配网请求");
    preferences.putBool("dev_wifi_seeded", false);
    forgetWifiOnRestart = true;
    restartAtMs = millis() + 1000;
  });
  web.onNotFound([]() {
    if (!requireWebAuth()) {
      return;
    }
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
  if (WiFi.softAP(setupApSsid.c_str(), provisioningPop.c_str())) {
    dns.start(53, "*", WiFi.softAPIP());
    addLogf("配网热点：%s，页面=http://%s", setupApSsid.c_str(),
            WiFi.softAPIP().toString().c_str());
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
  if (millis() - lastTelemetryMs >= telemetryIntervalMs) {
    lastTelemetryMs = millis();
    publishTelemetry();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(500);

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
  alertTopic = "devices/" + deviceId + "/alert";
  alertAckTopic = "devices/" + deviceId + "/alert/ack";
  heartbeatTopic = "devices/" + deviceId + "/heartbeat";

  preferences.begin("device-app", false);
  loadRuntimeConfig();
  loadWebCredentials();
  loadProvisioningPop();
  loadActiveAlert();

  if (SOS_BUTTON_PIN >= 0) {
    pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
    stableButtonReading = digitalRead(SOS_BUTTON_PIN) == HIGH;
    lastButtonReading = stableButtonReading;
    buttonIgnoreUntilMs = millis() + SOS_BUTTON_STARTUP_GUARD_MS;
  }
  if (SOS_LED_PIN >= 0) {
    pinMode(SOS_LED_PIN, OUTPUT);
    digitalWrite(SOS_LED_PIN, LOW);
  }
  if (SOS_BUZZER_PIN >= 0) {
    pinMode(SOS_BUZZER_PIN, OUTPUT);
    digitalWrite(SOS_BUZZER_PIN, LOW);
  }

  addLogf("ESP32-S3 启动，设备 ID=%s", deviceId.c_str());
  addLogf("重启原因：%s", resetReasonName().c_str());

  configureMqttTransport();
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);

  WiFi.onEvent(onSystemEvent);
  WiFi.setHostname(hostName.c_str());

  if (!PRODUCTION_BUILD && DEV_WIFI_SSID[0] != '\0' &&
      !preferences.getBool("dev_wifi_seeded", false)) {
    addLogf("开发 Wi-Fi 自动配网：%s", DEV_WIFI_SSID);
    WiFi.begin(DEV_WIFI_SSID, DEV_WIFI_PASSWORD);
  }

  const char *serviceKey = nullptr;
  constexpr bool resetProvisioning = false;
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE,
                          WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1, provisioningPop.c_str(),
                          PROV_DEVICE_NAME, serviceKey, nullptr,
                          resetProvisioning);

  setupConfigurationAp();
  setupWebServer();
  addLogf("蓝牙配网设备：%s", PROV_DEVICE_NAME);
}

void loop() {
  dns.processNextRequest();
  web.handleClient();
  handleMqtt();
  handleButton();
  handleAlertDelivery();
  handleHeartbeat();

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
