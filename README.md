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
- 网页重新启动设备、清除 Wi-Fi 配网
- 局域网 mDNS 地址

## 打开管理页面

### 尚未配网

1. 手机或电脑连接热点 `ESP32S3-Setup-xxxx`（后四位以实际显示为准）。
2. 热点密码为 `12345678`。
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
- PoP：`12345678`
- Wi-Fi：仅支持 2.4 GHz

## MQTT

默认服务器只是占位地址 `192.168.1.20:1883`，可以直接在管理页面修改并保存。

- `devices/<设备ID>/status`
- `devices/<设备ID>/telemetry`
- `devices/<设备ID>/command`

向 command 主题发送 `status` 会立即上报一次遥测，发送 `restart` 会重启设备。

当前 MQTT 使用明文 TCP，适合局域网测试。正式部署应改用 TLS 8883、独立设备账号和主题访问控制。

## VS Code / PlatformIO

- 编译：PlatformIO 的 Build 按钮
- 烧录：PlatformIO 的 Upload 按钮
- 串口：115200 baud
