# Node-RED 部署流程

本文记录 ESP32 SOS 项目的本机联调方式和正式上线方式。当前消息链路为：

```text
ESP32 -- MQTT over TLS 8883 --> EMQX Cloud <-- MQTT over TLS 8883 -- Node-RED
                                                                          |
                                                                          +--> Bark
                                                                          +--> 其他通知适配器
```

Node-RED 负责订阅 SOS、heartbeat 和在线状态主题，按 `device_id + event_id` 去重，回写 ACK，再调用 Bark、ntfy、Telegram、短信或电话适配器。ESP32 收到 ACK 后才会结束当前 SOS 事件。

## 当前本机联调环境

当前 Node-RED 运行在开发电脑上，地址是：

```text
http://127.0.0.1:1880
http://127.0.0.1:1880/sos-simulator
```

运行数据保存在：

```text
integrations/node-red/runtime/
```

其中包括 `flows.json`、`flows_cred.json`、`settings.js`、`context/` 和 EMQX CA 证书。这个目录已被 Git 忽略，里面可能包含 MQTT 密码和 Bark Token。

本机启动方式：

```bash
cd /Users/leeee/project/esp32-s3-ble-mqtt
node-red --userDir integrations/node-red/runtime
```

本机流程导入文件是 `sos-flow.emqx-local.json`。它只用于本机联调，不能复制到公开仓库或直接交给第三方。通用模板是 `sos-flow.json`。

## 正式环境建议

正式运行建议把 Node-RED 部署到长期在线的云服务器或托管 Node-RED 服务。当前本机方案依赖电脑开机、联网且 Node-RED 进程持续运行；电脑休眠或关机后，ESP32 仍能连接 EMQX，但 Node-RED 不会回 ACK，也不会发送 Bark。

云服务器不需要和 EMQX Cloud 在同一台机器上。Node-RED 只需主动访问 EMQX Cloud 的 TLS 端口 `8883`，不需要把 MQTT 端口暴露给公网。

建议的公网入口：

```text
公网用户 -- HTTPS 443 --> Nginx/Caddy -- localhost:1880 --> Node-RED
ESP32、Node-RED -- TLS 8883 --> EMQX Cloud
Node-RED -- HTTPS 443 --> Bark
```

防火墙至少应做到：

- 对公网开放 `443`；
- `22` 只允许管理员 IP 或通过 VPN 访问；
- 不开放 Node-RED 的 `1880`；
- 允许服务器出站访问 EMQX Cloud `8883` 和 Bark `443`。

## Docker 部署

下面的示例假设云服务器目录为 `/opt/esp32-sos`：

```text
/opt/esp32-sos/
├── docker-compose.yml
├── .env                       # 仅服务器保存，权限设为 600
├── runtime/
│   ├── settings.js
│   ├── flows.json
│   ├── flows_cred.json
│   └── context/
└── certs/
    └── emqxsl-ca.crt
```

`docker-compose.yml`：

```yaml
services:
  node-red:
    image: nodered/node-red:latest
    container_name: esp32-sos-node-red
    restart: unless-stopped
    ports:
      - "127.0.0.1:1880:1880"
    environment:
      EMQX_NODE_RED_PASSWORD: ${EMQX_NODE_RED_PASSWORD}
      SOS_BARK_URL: ${SOS_BARK_URL}
      SOS_NTFY_URL: ${SOS_NTFY_URL:-}
      SOS_TELEGRAM_URL: ${SOS_TELEGRAM_URL:-}
      SOS_TELEGRAM_CHAT_ID: ${SOS_TELEGRAM_CHAT_ID:-}
      SOS_SMS_URL: ${SOS_SMS_URL:-}
      SOS_SMS_TO: ${SOS_SMS_TO:-}
      SOS_PHONE_URL: ${SOS_PHONE_URL:-}
      SOS_PHONE_TO: ${SOS_PHONE_TO:-}
      SOS_SIMULATOR_TOKEN: ${SOS_SIMULATOR_TOKEN:-}
      SOS_SIMULATOR_ENABLED: ${SOS_SIMULATOR_ENABLED:-true}
      SOS_DEDUPE_TTL_MS: ${SOS_DEDUPE_TTL_MS:-86400000}
      SOS_OFFLINE_TIMEOUT_MS: ${SOS_OFFLINE_TIMEOUT_MS:-90000}
      SOS_HTTP_RETRY_MAX: ${SOS_HTTP_RETRY_MAX:-3}
      NODE_RED_CREDENTIAL_SECRET: ${NODE_RED_CREDENTIAL_SECRET}
    volumes:
      - ./runtime:/data
      - ./certs/emqxsl-ca.crt:/data/certs/emqxsl-ca.crt:ro
```

`.env` 只放在云服务器上，不提交 Git：

```dotenv
EMQX_NODE_RED_PASSWORD=替换成Node-RED的EMQX账号密码
SOS_BARK_URL=https://api.day.app/替换成BarkKey
SOS_NTFY_URL=
SOS_TELEGRAM_URL=
SOS_TELEGRAM_CHAT_ID=
SOS_SMS_URL=
SOS_SMS_TO=
SOS_PHONE_URL=
SOS_PHONE_TO=
SOS_SIMULATOR_TOKEN=设置一个随机的联调令牌
SOS_SIMULATOR_ENABLED=true
SOS_DEDUPE_TTL_MS=86400000
SOS_OFFLINE_TIMEOUT_MS=90000
SOS_HTTP_RETRY_MAX=3
NODE_RED_CREDENTIAL_SECRET=设置一个长期固定的随机值
```

```bash
chmod 600 .env
docker compose up -d
docker compose logs -f node-red
```

首次部署时，把 `sos-flow.json` 导入 Node-RED 编辑器并部署。导入后需要把 EMQX TLS 配置节点中的 CA 路径改成容器内路径：

```text
/data/certs/emqxsl-ca.crt
```

服务器上的 `settings.js` 至少应启用持久化 context，并设置固定的凭据密钥：

```js
module.exports = {
    flowFile: 'flows.json',
    flowFilePretty: true,
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET,
    contextStorage: {
        default: { module: 'localfilesystem' },
        file: { module: 'localfilesystem' }
    }
};
```

`NODE_RED_CREDENTIAL_SECRET` 必须长期保持不变。更换它可能导致已有 `flows_cred.json` 无法解密。

## HTTPS 和管理权限

Node-RED 编辑器和模拟器不应直接暴露在公网。建议使用 Caddy 或 Nginx 终止 HTTPS，并在 Node-RED `settings.js` 中启用 `adminAuth`，或者只通过 VPN 访问管理界面。

Nginx 的核心代理配置示例：

```nginx
server {
    server_name sos.example.com;

    location / {
        proxy_pass http://127.0.0.1:1880;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

HTTPS 证书可以由 Caddy 或 Let's Encrypt 自动维护。不要直接把 Node-RED 的编辑器端口映射为公网 `0.0.0.0:1880`。

## EMQX 账号和 ACL

云端 Node-RED 账号只授予流程需要的主题权限：

| 操作 | 主题过滤器 |
| --- | --- |
| 订阅 SOS | `devices/+/alert` |
| 订阅心跳 | `devices/+/heartbeat` |
| 订阅在线状态 | `devices/+/status` |
| 发布 ACK | `devices/+/alert/ack` |

模拟器还需要向 `devices/+/alert` 发布消息。正式环境建议关闭模拟器入口，或给模拟器单独创建账号和 ACL，避免把测试接口暴露给公网。

设备账号应单独设置权限：发布自己的 `status`、`telemetry`、`alert`、`heartbeat`，订阅自己的 `command` 和 `alert/ack`。不要让所有设备共用同一个 MQTT 密码。

## 上线验收

按下面顺序验收：

1. 在服务器查看 Node-RED 日志，确认 MQTT TLS 连接成功。
2. 在 EMQX 监控或 MQTTX 订阅 `devices/+/alert`。
3. 打开受保护的 `/sos-simulator`，发送唯一 `event_id` 的测试事件。
4. 确认 Node-RED 返回 ACK，Bark 收到一次通知。
5. 用相同 `event_id` 重复发送，确认仍 ACK 但不重复推送。
6. 从 ESP32 管理页面发送测试 SOS，确认设备状态变为 `acked`，待处理数量为 `0`。
7. 重启 Node-RED，再重复一次去重和心跳测试，确认 `context/` 持久化有效。
8. 停止 Node-RED，确认设备端按预期重试；恢复 Node-RED 后确认事件能够 ACK。

设备状态接口：

```text
GET https://你的域名/api/device-status
```

接口由当前 HTTPS Basic Auth 保护，返回 Node-RED 已记录的设备在线状态、最近心跳、固件版本和配置版本。

设备端管理页当前可检查：

```text
Wi-Fi：已连接
MQTT：已连接，状态码 0
SOS：acked，pending=0
```

## 备份和密钥管理

需要备份的内容：

- `runtime/flows.json`；
- `runtime/flows_cred.json`；
- `runtime/settings.js`；
- `runtime/context/`；
- EMQX CA 证书；
- `.env` 的加密备份。

MQTT 密码、Bark Token、Telegram Token 和短信/电话服务商密钥只放在云服务器的密钥管理或受限 `.env` 中。不要把 `sos-flow.emqx-local.json`、`.env` 或 `flows_cred.json` 提交到公开仓库。若凭证曾经出现在公开日志、截图或仓库中，应立即在对应服务端轮换。

## 常见问题

### ESP32 有 MQTT 连接，但没有 ACK

检查 Node-RED 是否在线、MQTT TLS CA 路径是否正确，以及 Node-RED 账号是否有 `devices/+/alert` 的订阅权限和 `devices/+/alert/ack` 的发布权限。

### Bark 没有收到通知

检查 `SOS_BARK_URL` 是否为 `https://api.day.app/<key>`，不要把示例中的尖括号保留在实际值中；再查看 Node-RED 的 `Bark 结果` debug 节点中的 HTTP 状态码。只有 2xx 才算 Bark 接收成功。

### 重启后重复通知

确认 `contextStorage.file` 已启用并且 `/data/context` 可写。没有持久化 context 时，Node-RED 重启后无法恢复 `event_id` 去重记录。

### 只能在本机打开模拟器

这是当前本机地址 `127.0.0.1` 的正常行为。正式环境应通过 HTTPS 域名访问，并启用认证；开发环境也可以临时使用电脑局域网 IP，但不应长期暴露编辑器端口。
