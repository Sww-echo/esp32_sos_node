# 实施跟踪：ESP32 设备与云端服务配置中心

## 目标

让设备和云端服务的可变参数可以在运行时配置，减少重新编译和手工改流程的次数，同时保留现有 MQTT 主题、SOS 事件格式、ACK 校验和 Node-RED 去重逻辑。

## 阶段 0：基线和边界

- [x] 确认当前设备使用 EMQX Cloud TLS `8883`。
- [x] 确认 Node-RED 云端通过同一 EMQX 实例订阅 `alert`、`heartbeat`、`status`。
- [x] 确认设备运行时 MQTT 账号保存在 NVS，源码不保存设备密码。
- [x] 确认当前设备管理页已有 Wi-Fi、MQTT、日志和 SOS 测试入口。
- [x] 建立本任务记录和相关文件清单。

## 阶段 1：设备配置管理器

- [x] 增加统一运行配置字段，集中描述设备信息、MQTT 和运行策略。
- [x] 增加配置默认值、范围校验和配置版本。
- [x] 将配置写入 `device-app` NVS，并兼容现有 `mqtt_*` 字段。
- [x] 启动时执行读取、迁移、校验和应用。
- [x] 修改 MQTT 配置后自动断开、重建 TLS 客户端并重连。
- [x] 密码只支持写入，不通过 API 和日志返回明文。

## 阶段 2：设备管理接口和网页

- [x] 增加非敏感配置读取接口。
- [x] 增加设备信息、SOS 策略和运行策略保存接口。
- [x] 在管理页增加设备信息和 SOS 策略表单。
- [x] 显示配置版本、连接状态和校验错误。
- [x] 保留现有 `/api/mqtt`、`/api/wifi`、`/api/alert/test` 兼容接口。
- [ ] Wi-Fi 新配置连接成功后再提交为有效配置，避免错误配置导致设备失联。

## 阶段 3：Node-RED 云端配置

- [x] 将通知 URL、重试、去重和离线阈值整理为环境变量。
- [x] 为 Docker Compose 传递 `NODE_RED_CREDENTIAL_SECRET`。
- [x] 保持 EMQX CA 路径为容器内 `/data/certs/emqxsl-ca.crt`。
- [x] 更新部署文档和 `.env` 示例，禁止提交真实密钥。
- [x] 增加 `SOS_SIMULATOR_ENABLED` 开关，生产环境可关闭网页模拟器。

## 阶段 4：验证

- [x] PlatformIO 编译通过。
- [x] Node-RED 流程 JSON、Function 节点和设备管理页脚本语法检查通过。
- [ ] 旧 NVS 配置升级后 MQTT 仍能连接。
- [ ] 修改 SOS 参数后下一次事件使用新参数。
- [ ] 重启后配置和未确认事件仍然存在。
- [ ] Node-RED 重启后去重 context 仍然有效。
- [x] 云端流程已通过 `/flows` 更新，模拟器发布事件返回 `202 published`，EMQX Node-RED 客户端保持在线并出现消息计数。
- [x] heartbeat/alert 增加设备名称、位置和配置版本字段。
- [x] 云端增加受 Basic Auth 保护的 `GET /api/device-status` 设备状态接口。
- [x] 设备端刷写新固件后完成配置页和真实 ESP32 ACK 验收。
- [x] 验证密码不会出现在状态接口、日志和页面文本中。

### 2026-10-06 现场验收记录

- 临时将 `heartbeat_interval_ms` 从 30000 改为 35000，软件重启后仍读取到 35000，随后恢复为 30000，确认运行配置已持久化到 NVS。
- 重启后设备重新连接 `YH-194023-2.4G` 和 EMQX Cloud，云端 `device-status` 持续显示在线。
- 重启后真实 SOS 事件 `3865D38FCBA4-000023` 收到 Node-RED ACK，设备状态为 `acked` 且待处理队列为 0。

## 生产化后续

- [ ] 设置 `PRODUCTION_BUILD=true`。
- [ ] 更换 GPIO0 为独立 SOS 按键引脚。
- [ ] 每台设备使用独立 MQTT 账号和 ACL。
- [ ] 增加设备清单、固件版本和配置版本管理。
- [x] Node-RED 已保存设备最近心跳、固件和配置版本，提供状态查询接口。
- [ ] 评估 NVS 加密、OTA 和证书轮换。

## 阶段 5：设备清单与通知扩展

- [x] 增加 `GET /api/devices` 和 `GET /api/devices/:device_id` 设备清单接口。
- [x] 保存首次发现时间、上报固件版本、上报配置版本和目标版本字段。
- [x] 增加通用 Webhook 通知适配器，保留 Bark、ntfy、Telegram、短信和电话渠道。
- [x] 增加 Docker Compose 自动重启、健康检查和日志轮转配置。
- [x] 增加 `backup.sh`，备份 Node-RED 流程、context、凭据、证书和环境变量。
- [ ] 将更新后的流程重新部署到公网 Node-RED，并用真实 Webhook 适配器完成一次 2xx 验收。
