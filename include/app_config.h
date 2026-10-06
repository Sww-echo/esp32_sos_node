#pragma once

// BLE provisioning identity. Development keeps this fixed; production builds
// generate and persist a unique PoP on first boot.
static constexpr char PROV_DEVICE_NAME[] = "PROV_ESP32S3";
static constexpr char PROV_POP[] = "12345678";

// The device generates a per-device web password on first boot when this is
// empty. Set an explicit value only for controlled development fixtures.
static constexpr char WEB_ADMIN_USER[] = "admin";
// Controlled development fixture: use a known password while bringing up a
// board over its setup hotspot. Production builds should leave this empty so
// each device generates and prints a one-time password.
static constexpr char WEB_ADMIN_PASSWORD[] = "12345678";

// Optional development-only first-boot Wi-Fi handoff. Leave empty for
// production; provisioned credentials are restored by WiFiProv from NVS.
static constexpr char DEV_WIFI_SSID[] = "";
static constexpr char DEV_WIFI_PASSWORD[] = "";

// Set true only for a production build after replacing the development pins
// and supplying the broker CA certificate below.
static constexpr bool PRODUCTION_BUILD = false;

// Replace this with the LAN IP or domain name of your MQTT broker.
// Do not use localhost: on the ESP32, localhost means the ESP32 itself.
static constexpr char MQTT_HOST[] = "af0111c7.ala.cn-shenzhen.emqxsl.cn";
static constexpr uint16_t MQTT_PORT = 8883;
static constexpr bool MQTT_TLS = true;
static constexpr char MQTT_TLS_CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

// Empty values are accepted by an anonymous test broker only.
static constexpr char MQTT_USER[] = "";
static constexpr char MQTT_PASSWORD[] = "";

// SOS input/output pins. GPIO0 is the development-only BOOT button on this
// board. Replace it with a dedicated GPIO before connecting a production SOS
// button; GPIO0 can affect the boot mode.
static constexpr int8_t SOS_BUTTON_PIN = 0;
static constexpr int8_t SOS_LED_PIN = -1;
static constexpr int8_t SOS_BUZZER_PIN = -1;
static_assert(!PRODUCTION_BUILD || SOS_BUTTON_PIN > 0,
              "Production builds must not use GPIO0 as the SOS button");
static_assert(!PRODUCTION_BUILD || MQTT_TLS,
              "Production builds must enable MQTT TLS");
static_assert(!PRODUCTION_BUILD || MQTT_TLS_CA_CERT[0] != '\0',
              "Production builds must provide an MQTT CA certificate");
static_assert(!PRODUCTION_BUILD || MQTT_USER[0] != '\0',
              "Production builds must configure an MQTT username");
static_assert(!PRODUCTION_BUILD || SOS_LED_PIN >= 0,
              "Production builds must configure an SOS LED pin");
static_assert(!PRODUCTION_BUILD || SOS_BUZZER_PIN >= 0,
              "Production builds must configure an SOS buzzer pin");

// Keep a bounded SOS queue in NVS so a second button press is not silently
// discarded while an earlier event is waiting for ACK.
static constexpr uint32_t SOS_BUTTON_DEBOUNCE_MS = 60;
static constexpr uint32_t SOS_BUTTON_STARTUP_GUARD_MS = 1000;
static constexpr uint32_t SOS_TRIGGER_COOLDOWN_MS = 5000;
static constexpr uint32_t SOS_ACK_TIMEOUT_MS = 15000;
static constexpr uint32_t SOS_RETRY_BASE_MS = 5000;
static constexpr uint32_t SOS_RETRY_MAX_MS = 60000;
static constexpr uint8_t SOS_MAX_ATTEMPTS = 5;
static constexpr uint32_t SOS_BEEP_MS = 800;
static constexpr uint8_t SOS_QUEUE_CAPACITY = 4;
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;

// Firmware version is included in heartbeat and SOS events so the service
// side can diagnose behavior without reading the device dashboard.
static constexpr char FIRMWARE_VERSION[] = "0.2.0-sos-logic";
