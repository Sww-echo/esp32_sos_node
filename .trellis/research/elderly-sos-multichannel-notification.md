# 独居老人 SOS 按钮多通道通知：方案与开源参考

## 结论

当前项目最适合沿用现有 ESP32-S3 BLE/MQTT 固件，新增可靠报警事件层，再由服务端自动化平台承担多通道通知：

```text
实体大按钮 → ESP32-S3 → MQTT/TLS → Node-RED 或 Home Assistant
                                      ├→ 手机推送
                                      ├→ Telegram
                                      ├→ 短信
                                      └→ 电话
```

第一版先用当前 BOOT 键模拟触发，打通 MQTT + Node-RED + ntfy/Telegram 的端到端链路，再接实体按钮、蜂鸣器、电池和 4G。生产版本不使用 GPIO0 作为求助按钮，因为它承担 BOOT/启动模式功能。

## 当前项目基线

项目路径：`/Users/leeee/project/esp32-s3-ble-mqtt`

当前固件已经包含：

- BLE Wi-Fi 配网、ESP32 自建热点和管理网页
- Wi-Fi 扫描、MQTT 配置、连接/重连、LWT 在线状态和遥测
- MQTT `status`、`telemetry`、`command` 主题
- `status`、`restart` 命令、实时日志、mDNS 管理地址

已观测到的板卡信息：ESP32-S3 QFN56 rev 0.2、约 8 MB PSRAM、运行时识别为 16 MB Flash。报警改造的主要入口是 `src/main.cpp`、`include/app_config.h`、`src/dashboard_html.h` 和 `platformio.ini`。

## 建议的设备端能力

### 触发与本地反馈

- 外接常开大按钮，GPIO 上拉输入；开发阶段用 BOOT 键模拟。
- 防抖、长按和冷却时间，避免误触发和连续通知风暴。
- LED/蜂鸣器分别表示触发、已发布、等待 ACK 和失败。

### MQTT 事件协议

```text
devices/<device-id>/alert
devices/<device-id>/alert/ack
devices/<device-id>/heartbeat
devices/<device-id>/status
devices/<device-id>/telemetry
devices/<device-id>/command
```

报警事件示例：

```json
{
  "event_id": "3865D38FCBA4-000021",
  "device_id": "3865D38FCBA4",
  "type": "sos",
  "source": "button",
  "uptime": 1280,
  "rssi": -48,
  "firmware": "1.0.0"
}
```

设计原则：报警 QoS 1；ACK QoS 1；在线状态 retained；报警事件不 retained；服务端以 `event_id` 幂等去重；未 ACK 事件保存到 Flash，断线或重启后继续发送；heartbeat 和 LWT 分别用于活跃性与离线告警。

### 通知分层

- 第一层：Node-RED 或 Home Assistant 订阅 MQTT 并执行去重、ACK、联系人和升级策略。
- 第二层：ntfy、Gotify 或 Home Assistant Companion 做手机推送。
- 第三层：Telegram 作为低成本多联系人渠道。
- 第四层：Twilio 等服务提供短信/电话，凭证只放服务端。

## 参考开源项目

### 1. MrT-coder/alarma-ya

链接：[https://github.com/MrT-coder/alarma-ya](https://github.com/MrT-coder/alarma-ya)

- MIT 许可证。
- ESP32 + MQTT/TLS，带网页控制和手机快捷指令思路。
- 可重点借鉴 MQTT QoS、重连、报警自动关闭、网页与通知端的边界。
- 这是最适合当前项目参考的代码结构来源，但仍应按当前项目的配网、主题和板卡重新实现。

### 2. xavanty/esp32_sos_button

链接：[https://github.com/xavanty/esp32_sos_button](https://github.com/xavanty/esp32_sos_button)

- GPL-3.0 许可证。
- ESP32-C3 老人 SOS 按钮，包含深度睡眠、Telegram 多联系人、声音报警、网页配网和 NVS 配置。
- 适合借鉴老人使用场景、触发行为、本地反馈和联系人通知设计。
- GPL-3.0 会影响直接复制代码的许可边界；本项目只记录设计参考，不直接复制实现。

### 3. JohnMarts/esp32-emergency-pendant

链接：[https://github.com/JohnMarts/esp32-emergency-pendant](https://github.com/JohnMarts/esp32-emergency-pendant)

- 面向低功耗 ESP32 求助挂件。
- 可借鉴深睡眠唤醒、HTTP 重试、事件去重/幂等 ID，以及 Twilio 电话/SMS 的服务端思路。
- 仓库未见明确许可证；只参考公开设计思路，不复制代码或资产。

### 4. ESPHome

链接：[https://github.com/esphome/esphome](https://github.com/esphome/esphome)

- 可作为后续 Home Assistant 接入和设备抽象的参考。
- 当前项目已有自定义 BLE 配网、管理网页和 MQTT 逻辑，第一版不建议直接换成 ESPHome；可借鉴实体按钮、二进制传感器和自动化模型。

### 5. Home Assistant

链接：[https://github.com/home-assistant/core](https://github.com/home-assistant/core)

- 适合承载设备状态、自动化、移动端推送和通知升级策略。
- 若家庭已有 Home Assistant，优先让它订阅 MQTT 并回传 ACK。

### 6. Node-RED

链接：[https://github.com/node-red/node-red](https://github.com/node-red/node-red)

- 适合第一版快速编排 MQTT → 去重 → 多通知 → ACK 的流程。
- 比把 Telegram、短信和电话逻辑全部固化在 ESP32 里更容易迭代。

### 7. ntfy 与 Gotify

链接：[https://github.com/binwiederhier/ntfy](https://github.com/binwiederhier/ntfy)、[https://github.com/gotify/server](https://github.com/gotify/server)

- 两者都适合做可自托管的手机推送服务候选。
- 第一版可选 ntfy 或 Gotify 完成手机推送闭环，再加入 Telegram、短信和电话。

## 任务关联

- PRD：`.trellis/tasks/10-05-elderly-sos-multichannel-notification/prd.md`
- 技术设计：`.trellis/tasks/10-05-elderly-sos-multichannel-notification/design.md`
- 实施计划：`.trellis/tasks/10-05-elderly-sos-multichannel-notification/implement.md`
