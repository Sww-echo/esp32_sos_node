# 独居老人 SOS 按钮多通道通知

## Goal

在现有 ESP32-S3 BLE/MQTT 项目上增加一个面向独居老人场景的求助按钮：老人按下按钮后，设备立即本地提示并发布可靠的 MQTT 报警事件，由 Node-RED 或 Home Assistant 负责向家属/照护人发送手机推送、Telegram、短信和电话等多通道通知。

## Scope

本任务继续改造当前项目，不替换现有 BLE 配网、热点配网、管理网页和 MQTT 基础能力。

当前固件已经具备：

- ESP32-S3 BLE Wi-Fi 配网和自建配网热点
- Wi-Fi 扫描、网页管理和实时日志
- MQTT 连接、重连、在线状态、遗嘱、遥测和命令
- mDNS 管理地址以及网页保存 MQTT 配置

本任务新增：

- 外接大按钮输入，第一阶段可用 BOOT 键进行端到端模拟
- 按键防抖、短按/长按策略和重复触发抑制
- 本地 LED/蜂鸣器提示
- 带唯一 `event_id` 的 MQTT SOS 报警事件
- 服务端 ACK、重试和断线期间的 Flash 暂存
- 周期心跳、离线检测和报警状态展示
- 管理网页中的“测试报警”和当前报警状态
- Node-RED/Home Assistant 的通知编排接口文档

## Functional Requirements

1. 按钮按下后，设备在本地立即记录事件，并在 MQTT 可用时发布 SOS 事件。
2. 每个报警事件必须包含设备 ID、唯一事件 ID、事件类型、触发来源、运行时间、Wi-Fi RSSI 和固件版本。
3. MQTT 短暂断开时，事件不得静默丢失；设备应重连并按顺序重发未确认事件。
4. 服务端必须能够用 `event_id` 幂等处理重复消息，并通过 ACK 主题确认接收。
5. 设备应提供在线状态和周期心跳，使服务端能够区分“无人按键”和“设备离线”。
6. 本地 LED/蜂鸣器应在报警触发、发送成功、等待 ACK 和发送失败时提供可辨识状态。
7. 管理网页应显示报警状态、最近事件 ID、最后 ACK 时间和发送失败原因，并提供受保护的测试报警入口。
8. MQTT 正式部署使用 TLS、独立设备账号、最小权限 ACL，禁止将匿名明文连接作为生产方案。
9. 第一版通知编排优先支持 MQTT + Node-RED + ntfy 或 Telegram；短信和电话作为后续通道接入。

## Non-functional Requirements

- 不阻塞主循环，不因通知重试影响网页、配网和 MQTT 保活。
- 报警链路要能在设备重启后恢复未完成事件。
- 日志应能在串口和管理网页中追踪：触发、入队、发布、ACK、重试、丢弃原因。
- 生产按钮不得使用 GPIO0；GPIO0 仅作为开发阶段 BOOT 键模拟输入。
- 不把隐私敏感内容直接写入 MQTT 日志或公开网页。

## Acceptance Criteria

- [ ] 使用 BOOT 键模拟一次 SOS，设备本地有提示，串口/网页日志有完整事件链路。
- [ ] MQTT 在线时，报警消息出现在 `devices/<device-id>/alert`，字段和 `event_id` 符合设计。
- [ ] 模拟服务端 ACK 后，设备停止该事件重试并显示已确认状态。
- [ ] 报警发布期间断开 MQTT，恢复连接后事件自动重发且 `event_id` 不变。
- [ ] 重启设备后，未 ACK 事件仍可恢复处理；重复投递不会在服务端产生重复通知。
- [ ] 心跳和 retained 在线状态可被 Node-RED/Home Assistant 用于离线告警。
- [ ] 网页“测试报警”能够走同一条事件管线，并有权限/二次确认保护。
- [ ] 使用 ntfy 或 Telegram 完成一次真实端到端通知演示。
- [ ] PlatformIO 编译通过，并完成按钮、断网、重启、ACK 和通知链路测试。

## Out of Scope for First Version

- 电池供电、深度睡眠和低功耗优化
- 4G/NB-IoT 备份链路
- OTA 固件升级
- 医疗级可靠性认证、呼叫中心和自动拨号硬件

## Notes

- 详细技术设计见同目录 `design.md`。
- 分阶段实施顺序见同目录 `implement.md`。
- 研究依据和参考开源项目见 `.trellis/research/elderly-sos-multichannel-notification.md`。
