# 技术设计：独居老人 SOS 按钮多通道通知

## 1. 总体架构

```text
外接大按钮
    |
    v
ESP32-S3 固件
  ├─ 防抖/长按/重复触发抑制
  ├─ LED + 蜂鸣器本地提示
  ├─ 事件 ID、Flash 队列、重试、ACK
  ├─ MQTT/TLS
  └─ BLE/热点配网 + 管理网页 + 日志
    |
    v
MQTT Broker
    |
    v
Node-RED 或 Home Assistant
  ├─ 手机推送（优先 ntfy / Home Assistant Companion）
  ├─ Telegram
  ├─ 短信（Twilio 等）
  └─ 电话（Twilio 等）
```

设备只负责可靠地产生和交付事件，不直接在 ESP32 上实现短信、电话等外部服务认证。这样能减少固件复杂度，也能在不重新烧录设备的情况下调整通知顺序、静默时间和联系人。

## 2. 与当前项目的衔接

当前项目的关键入口：

- `src/main.cpp`：Wi-Fi、MQTT、HTTP、日志和主循环
- `include/app_config.h`：配网身份和 MQTT 默认配置
- `src/dashboard_html.h`：本地管理页面
- `platformio.ini`：PlatformIO 构建和依赖

第一版建议沿用现有 `PubSubClient`、`Preferences`、`WebServer` 和 `WiFiProv`，把 SOS 逻辑拆成独立函数/模块，避免把报警状态机继续堆在 HTTP handler 中。

## 3. 硬件输入输出

### 开发阶段

- 用当前 BOOT 键验证完整链路。
- BOOT 键对应 GPIO0，存在启动模式风险，只能用于开发测试。

### 生产阶段

- 外接常开大按钮：GPIO 输入上拉，按钮另一端接 GND。
- 使用独立可配置 GPIO，并在 `app_config.h` 中明确标注板卡引脚。
- LED 用于状态指示；蜂鸣器通过合适的限流/驱动电路连接，不能直接让 ESP32 GPIO 承担过大电流。
- 后续若改为电池供电，再增加电量采样、深睡眠唤醒和低电量事件。

## 4. MQTT 主题与载荷

主题约定：

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
  "firmware": "1.0.0",
  "created_at": "2026-10-05T00:00:00Z"
}
```

ACK 示例：

```json
{
  "event_id": "3865D38FCBA4-000021",
  "device_id": "3865D38FCBA4",
  "accepted": true,
  "server": "node-red",
  "received_at": "2026-10-05T00:00:03Z"
}
```

建议报警使用 QoS 1；ACK 使用 QoS 1；状态使用 retained 消息；报警消息不使用 retained，避免新订阅者收到过期求助事件后误触发通知。服务端以 `event_id` 做幂等去重。

## 5. 设备状态机

```text
IDLE
  -> TRIGGERED      按钮通过防抖确认
  -> QUEUED         写入 Flash 待发送队列
  -> PUBLISHED      MQTT 发布成功
  -> WAITING_ACK    等待 ACK 超时计时
  -> ACKED          收到匹配 ACK，结束事件
  -> RETRY_WAIT     超时/断线，指数退避后重试
  -> FAILED         超过上限，网页和日志标记失败
```

要点：

- `event_id` 在整个生命周期内保持不变。
- 设备重启时扫描未完成事件并继续发送。
- 队列容量、最大重试次数和退避上限写入配置，避免 Flash 无限磨损。
- 只有匹配设备 ID 和事件 ID 的 ACK 才能结束事件。
- 当队列满时，优先保留未确认 SOS，普通遥测可以丢弃。

## 6. 心跳与离线

- 周期发布 heartbeat，包含 uptime、RSSI、固件版本和当前报警队列长度。
- `status` 主题沿用现有 MQTT LWT：上线发布 `online`，异常离线由 broker 发布 `offline`。
- Node-RED/Home Assistant 对 heartbeat 超时和 `offline` 分别告警，避免把“设备断电”误认为“老人未按键”。

## 7. 安全与运维

- 生产环境改用 MQTT/TLS 8883、每台设备独立账号和 ACL。
- 管理网页的测试报警接口至少需要局域网访问限制和二次确认；后续增加管理密码。
- PoP、MQTT 密码和联系人信息不写入公共仓库。
- 日志记录事件 ID 和状态，不记录不必要的个人信息。
- 多通道通知必须配置静默/升级策略，避免同一事件无限重复骚扰联系人。

## 8. 参考项目如何影响设计

- `MrT-coder/alarma-ya`：借鉴 ESP32 + MQTT/TLS、QoS、重连、网页和手机快捷指令的组织方式。
- `xavanty/esp32_sos_button`：借鉴老人 SOS 行为、声音报警、NVS 配置、网页配网和 Telegram 多联系人；许可证为 GPL-3.0，采用“参考思路、不直接复制代码”的方式。
- `JohnMarts/esp32-emergency-pendant`：借鉴低功耗挂件的重试、去重 ID 和 Twilio SMS/电话思路；仓库许可证不明确，不直接复制代码。
- ESPHome/Home Assistant/Node-RED：作为现成的设备接入、自动化编排和通知承载层。
- ntfy/Gotify：作为自托管手机推送的优先候选，降低第一版短信/电话接入复杂度。
