#pragma once

// BLE provisioning identity. Use a unique PoP for each production device.
static constexpr char PROV_DEVICE_NAME[] = "PROV_ESP32S3";
static constexpr char PROV_POP[] = "12345678";

// Replace this with the LAN IP or domain name of your MQTT broker.
// Do not use localhost: on the ESP32, localhost means the ESP32 itself.
static constexpr char MQTT_HOST[] = "192.168.1.20";
static constexpr uint16_t MQTT_PORT = 1883;

// Empty values are accepted by an anonymous test broker only.
static constexpr char MQTT_USER[] = "";
static constexpr char MQTT_PASSWORD[] = "";

// SOS input/output pins. GPIO0 is the development-only BOOT button on this
// board. Replace it with a dedicated GPIO before connecting a production SOS
// button; GPIO0 can affect the boot mode.
static constexpr int8_t SOS_BUTTON_PIN = 0;
static constexpr int8_t SOS_LED_PIN = -1;
static constexpr int8_t SOS_BUZZER_PIN = -1;

// The first implementation keeps one unacknowledged SOS event in NVS. This is
// enough to make the end-to-end event contract reliable before we introduce a
// multi-item queue or battery/deep-sleep behavior.
static constexpr uint32_t SOS_BUTTON_DEBOUNCE_MS = 60;
static constexpr uint32_t SOS_TRIGGER_COOLDOWN_MS = 5000;
static constexpr uint32_t SOS_ACK_TIMEOUT_MS = 15000;
static constexpr uint32_t SOS_RETRY_BASE_MS = 5000;
static constexpr uint32_t SOS_RETRY_MAX_MS = 60000;
static constexpr uint8_t SOS_MAX_ATTEMPTS = 5;
static constexpr uint32_t SOS_BEEP_MS = 800;
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;

// Firmware version is included in heartbeat and SOS events so the service
// side can diagnose behavior without reading the device dashboard.
static constexpr char FIRMWARE_VERSION[] = "0.2.0-sos-logic";
