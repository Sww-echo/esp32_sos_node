# Node-RED SOS 多通道通知流程

部署到云服务器的完整步骤见 [DEPLOYMENT.md](DEPLOYMENT.md)。

`sos-flow.json` 是当前 ESP32 SOS 固件对应的 Node-RED 导入模板。

流程现在做五件事：

1. 订阅 `devices/+/alert`。
2. 按 `device_id + event_id` 去重，重复投递仍回 ACK，但不会重复通知。
3. 将 ACK 写回设备的 `devices/<device-id>/alert/ack`。ACK 表示 Node-RED 已接收并提交通知队列，不代表每个第三方渠道已经送达。
4. 把首次报警扩展到 Bark、ntfy、Telegram、短信和电话 HTTP 适配器。
5. 记录 heartbeat 和 retained/LWT status，并在超过 90 秒没有活跃信号时发送离线通知。

每个已配置的 HTTP 通知渠道失败时会在 Node-RED 进程内延迟重试最多 3 次；需要跨重启、跨实例的可靠投递时，仍应把适配器放到具备持久队列的服务后面。

## 网页模拟器

导入流程后打开 `http://<node-red-host>:1880/sos-simulator`。页面可以生成标准 SOS 事件并重复投递同一个 `event_id`，用于验证首次通知和去重 ACK。若设置 `SOS_SIMULATOR_TOKEN`，页面请求必须带对应令牌；建议只在开发环境启用这个入口。

## 导入与配置

1. 优先导入本机生成的 `sos-flow.emqx-local.json`。它已经填好 EMQX Cloud 地址、8883 TLS 端口、CA 证书和 Node-RED 账号。
2. 如果要把流程提交到其他环境，导入 `sos-flow.json`，然后为 Node-RED 进程设置 `EMQX_NODE_RED_PASSWORD`；流程中的 `$(EMQX_NODE_RED_PASSWORD)` 会在部署时展开。
   Node-RED 的 TLS 节点使用 CA 文件路径；换电脑时把 `EMQX Cloud CA` 节点的 `ca` 改成新电脑上的 `emqxsl-ca.crt` 路径。
3. 为 Node-RED 进程配置这些通知环境变量，然后重启 Node-RED：

```text
SOS_NTFY_URL=https://ntfy.example.com/elderly-sos
SOS_BARK_URL=https://api.day.app/<bark-key>
SOS_TELEGRAM_URL=https://api.telegram.org/bot<token>/sendMessage
SOS_TELEGRAM_CHAT_ID=<chat-id>
SOS_SMS_URL=http://notification-adapter:8080/sms
SOS_SMS_TO=<phone-number>
SOS_PHONE_URL=http://notification-adapter:8080/phone
SOS_PHONE_TO=<phone-number>
SOS_SIMULATOR_TOKEN=<development-only-token>
SOS_SIMULATOR_ENABLED=true
SOS_DEDUPE_TTL_MS=86400000
SOS_OFFLINE_TIMEOUT_MS=90000
SOS_HTTP_RETRY_MAX=3
```

`sos-flow.emqx-local.json` 含有当前 Node-RED MQTT 账号密码，只保存在本机并已加入 Git 忽略规则；不要把它复制到公开仓库。

当前电脑的联调实例已启动在 `http://127.0.0.1:1880`，模拟页面是 `http://127.0.0.1:1880/sos-simulator`。

去重和设备活跃状态优先使用名为 `file` 的 Node-RED context store。请在 `settings.js` 中启用持久化 context，例如：

```js
contextStorage: {
    default: { module: 'localfilesystem' },
    file: { module: 'localfilesystem' }
}
```

如果没有配置 `file` store，流程会退回内存 context，重启后无法恢复去重记录。

未配置的 URL 会被流程自动跳过。不要把 Telegram token、短信账号或电话服务商密钥提交到 Git；如果不想让 Node-RED 直接持有第三方凭证，URL 应指向本地通知适配器，由适配器通过服务端环境变量完成调用。

`SOS_DEDUPE_TTL_MS` 控制事件去重记录保留时间，`SOS_OFFLINE_TIMEOUT_MS` 控制设备离线判断时间，`SOS_HTTP_RETRY_MAX` 控制通知 HTTP 渠道的最大重试次数。正式环境可将 `SOS_SIMULATOR_ENABLED=false` 关闭网页模拟器。

Bark 使用 `SOS_BARK_URL` 指向 `https://api.day.app/<key>`，流程通过 POST JSON 发送标题、报警正文、`esp32-sos` 分组和 `alarm` 声音。Bark Token 只应放在本机 Node-RED 环境或被 Git 忽略的本地流程文件中。

## ACK 行为

报警首次到达时会生成 ACK，再分发通知。重复消息会收到：

```json
{
  "event_id": "<event-id>",
  "accepted": true,
  "duplicate": true,
  "delivery": "accepted_by_nodered",
  "server": "node-red"
}
```

这允许 ESP32 停止重试，同时避免 MQTT 重投递导致联系人收到多次报警。第三方 HTTP 渠道的最终失败会在 Node-RED debug 节点中显示。
