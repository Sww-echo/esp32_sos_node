# ESP32-S3 设备管理中心

这是一个由 ESP32-S3 自己托管的本地管理网页，不需要额外服务器或互联网。

## 当前功能

- 响应式中文设备管理页面，手机和电脑都可使用
- 芯片型号、版本、核心数、CPU、Flash、PSRAM 和固件大小
- 运行时间、内部温度、空闲内存、重启原因和 BOOT 按钮状态
- Wi-Fi 连接状态、IP、信号、MAC 和主机名
- 最近 100 条实时设备日志
- 扫描附近 Wi-Fi 并直接在网页配网
- 保留 Espressif BLE Wi-Fi 配网
- MQTT 参数持久化设置、连接状态和命令测试
- MQTT 在线状态、遗嘱、遥测上报和自动重连
- SOS 按钮模拟、报警事件持久化、ACK、重试和心跳
- SOS 报警状态网页展示与测试报警入口
- 网页重新启动设备、清除 Wi-Fi 配网
- 局域网 mDNS 地址

## 打开管理页面

### 尚未配网

1. 手机或电脑连接热点 `ESP32S3-Setup-xxxx`（后四位以实际显示为准）。
2. 开发固件热点密码为 `12345678`；生产固件首次启动会生成独立密码，并在串口日志显示。
3. 浏览器打开 `http://192.168.4.1`。
4. 扫描并填写一个 2.4 GHz Wi-Fi。

热点名称最后四位来自这块开发板的设备 ID。

### 已连接 Wi-Fi

在同一局域网内打开页面显示的 IP 地址，或尝试：

`http://esp32s3-xxxx.local`（后四位与配网热点相同）

## 蓝牙配网

使用 Espressif 的 ESP BLE Provisioning 手机应用：

- 蓝牙设备：`PROV_ESP32S3`
- 安全模式：Security 1
- PoP：开发固件为 `12345678`；生产固件使用首次启动时生成的独立 PoP。
- Wi-Fi：仅支持 2.4 GHz

## MQTT

当前开发板已配置为 EMQX Cloud TLS：`af0111c7.ala.cn-shenzhen.emqxsl.cn:8883`。
设备账号密码保存在设备 NVS 中，不写入仓库；更换 Broker 时可以在管理页面修改主机、端口、账号和密码。TLS 开关与 CA 证书随固件编译配置。

设备管理页还可以修改设备名称、安装位置、SOS 防抖和冷却时间、ACK 超时、重试次数与间隔、心跳/遥测间隔以及蜂鸣器和 LED 开关。这些运行参数保存在设备 NVS，配置版本会随固件自动迁移；密码只支持写入，不会通过状态接口或日志返回。

- `devices/<设备ID>/status`
- `devices/<设备ID>/telemetry`
- `devices/<设备ID>/command`
- `devices/<设备ID>/alert`
- `devices/<设备ID>/alert/ack`
- `devices/<设备ID>/heartbeat`

设备 heartbeat 会携带 `display_name`、`location`、`firmware` 和 `config_version`，云端 Node-RED 会保存这些信息并通过受保护的 `/api/device-status` 提供设备状态查询。

向 command 主题发送 `status` 会立即上报一次遥测，发送 `restart` 会重启设备。

### SOS 报警逻辑（当前开发阶段）

- 当前使用 BOOT 键（GPIO0）模拟求助按钮，稳定按下后触发一次 SOS。
- 报警事件会写入 NVS，带稳定的 `event_id`；最多保存当前事件加 4 条等待事件，MQTT 断线或设备重启后仍会尝试发送。
- 服务端应在 `alert/ack` 主题返回包含相同 `event_id` 的 ACK，例如：

```json
{"event_id":"<设备ID>-000001","accepted":true,"server":"node-red"}
```

- 报警消息不使用 retained；在线状态沿用 retained + LWT；heartbeat 包含设备在线和报警队列状态。
- Node-RED 的 ACK 表示服务端已经接收并提交通知队列；第三方通知渠道的最终送达由服务端队列和重试策略负责。
- 管理网页中的“发送测试报警”会走和按钮相同的事件管线。
- PubSubClient 当前发布接口为 QoS 0，首版通过 NVS、ACK、重试和服务端 `event_id` 幂等实现可靠事件语义。

生产版本必须更换为独立 GPIO，不能把 GPIO0 作为老人求助按钮。

## Node-RED 通知流程

项目提供了可导入的 [Node-RED SOS 流程](integrations/node-red/sos-flow.json)、[配置说明](integrations/node-red/README.md) 和 [部署流程](integrations/node-red/DEPLOYMENT.md)。流程已切换到 EMQX Cloud TLS 8883，并会按 `event_id` 去重、回传 ACK，支持 Bark、ntfy、Telegram、短信、电话适配器和通用 Webhook。通用 Webhook 用于接入后续的钉钉、企业微信、飞书、邮件等渠道，保持 ESP32 和 MQTT 协议不变。设备清单接口提供固件版本、配置版本和心跳状态。

导入流程后可打开 `http://<Node-RED 地址>:1880/sos-simulator`，用网页生成 SOS 事件并重复投递同一个 `event_id` 来验证通知和去重交互。

当前电脑的联调实例地址是 `http://127.0.0.1:1880/sos-simulator`。

当前开发板使用 EMQX Cloud 的 TLS 端口 8883。正式部署前应在 `app_config.h` 设置 `PRODUCTION_BUILD=true`，更换 SOS GPIO，再给每台设备配置独立账号和主题 ACL；生产构建缺少 TLS/CA 或仍使用 GPIO0 会直接编译失败。

管理网页现在使用 HTTP Basic Authentication。首次启动会生成每台设备独立的管理员密码，并只在串口日志中显示一次；请在串口日志中保存密码。也可以在 `app_config.h` 中为受控开发设备设置 `WEB_ADMIN_PASSWORD`。

## VS Code / PlatformIO

- 编译：PlatformIO 的 Build 按钮
- 烧录：PlatformIO 的 Upload 按钮
- 串口：115200 baud
