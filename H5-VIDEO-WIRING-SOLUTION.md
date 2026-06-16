# FollowBox H5 视频方案与接线

## 方案结论

首版视频采用 **ESP32-S3-CAM 独立 WiFi MJPEG 推流**：

```text
ESP32-S3-CAM + OV5640
  -> 加入 FollowBox 主控 softAP
  -> 固定 IP 192.168.4.2
  -> MJPEG: http://192.168.4.2:81/stream
  -> H5 <img> 直接显示
```

主控 ESP32-S3 不接收视频、不转发视频、不把视频作为安全输入。主控只在遥测 JSON 中下发 `camera.stream_url`，H5 浏览器自己去拉摄像头流。

## 为什么不让云服务器直接承载视频

MJPEG 视频码流大，ESP32 主控和 Node 云端现有链路只适合遥测、日志、低速点动和 OTA。云端 H5 可以自动显示 `camera.stream_url`，但公网浏览器不能直接访问小车局域网里的 `192.168.4.2`。如果需要公网实时视频，需要另做以下之一：

- 给摄像头提供 HTTPS 公网转发地址。
- 用 FRP/Tailscale/VPN 把摄像头流暴露给操作端。
- 后续实现 WebRTC/RTSP relay，不能让主控 ESP32-S3 代转视频。

## 当前公网查看方案

已实现一个低帧率云端转发通道，用于公网临时查看：

```text
ESP32-S3 主控
  -> 从本地摄像头抓 http://192.168.4.2/capture
  -> POST 到云端 /api/device/<id>/video/upload
  -> 云端 H5 加载 /api/device/<id>/video/stream
```

该链路只上传 JPEG 快照并在云端拼成 MJPEG，默认约 2.5 秒一帧；它不进入 `safety_manager`，也不参与运动控制。公网 H5 默认使用云端转发地址，不再直接加载 `192.168.4.2`。

启用条件：

- 主控已通过 H5 配网连上外网 WiFi。
- 摄像头已连接 `FollowBox` 热点，且 `http://192.168.4.2/capture` 可打开。
- 云端 `FOLLOWBOX_DEVICE_TOKEN` 与固件 `FOLLOWBOX_CLOUD_DEVICE_TOKEN` 一致。
- 云端操作页面使用正确的 `FOLLOWBOX_OPERATOR_TOKEN`。

## ESP32-S3-CAM 接线

| ESP32-S3-CAM | 接到 | 要求 |
|---|---|---|
| 5V | DC-DC 5V 输出 | 建议独立 5V 支路，至少 1A 余量 |
| GND | 系统低压 GND | 与主控低压 GND 共地，走小电流地线 |
| UART | 不接主控 | P0 不占主控 UART，不发运动指令 |
| 摄像头排线 | OV5640/对应模组 | 排线压紧，避免振动松脱 |

注意：

- 不接 GPIO15/16/42/43/44；这些已经分配给倒车、IMU/下载调试等用途。
- 摄像头断流只影响 H5 画面，不影响 `safety_manager` 和电机门控。
- 摄像头板电源线建议加短线、粗线和近端去耦，避免 WiFi 发射时掉压重启。

## 摄像头 WiFi 配置

摄像头固件建议配置为 STA 模式，加入主控热点：

| 项 | 值 |
|---|---|
| SSID | `FollowBox` |
| Password | 与主控 `SOFT_AP_PASSWORD` 一致 |
| Static IP | `192.168.4.2` |
| Gateway | `192.168.4.1` |
| Netmask | `255.255.255.0` |
| Stream URL | `http://192.168.4.2:81/stream` |

如果摄像头改成加入现场 WiFi，也可以把固件 build flag `FOLLOWBOX_CAMERA_STREAM_URL` 改成现场可访问的地址。

## H5 行为

- 车端 H5：从 `/ws/state` 读取 `camera.stream_url`，自动加载 `<img>`。
- 云端 H5：从 SSE 遥测读取 `camera.stream_url`，自动填入视频地址；若操作端浏览器不能访问该地址，会显示离线。
- 手动输入框仍保留，用于填 FRP/VPN/公网 HTTPS 转发地址；保存后当前浏览器会优先使用手动地址，不再被遥测默认值覆盖。

## 本地查看步骤

1. 先烧录主控固件和 LittleFS 页面，确认手机/电脑能连接 `FollowBox` 热点并打开 `http://192.168.4.1/`。
2. 烧录 `vision_cam` 固件。串口应看到 `WiFi connected: 192.168.4.2` 和 `MJPEG stream: http://192.168.4.2:81/stream`。
3. 操作端必须连接 `FollowBox` 热点，直接打开 `http://192.168.4.2/status` 查看状态 JSON。
4. 再打开 `http://192.168.4.2:81/stream` 查看 MJPEG 实时画面。
5. 回到车端 H5 的“设置 -> 视频地址”，填 `http://192.168.4.2:81/stream` 后点“保存查看”。

如果第 3 步打不开，优先查摄像头是否真的连上 `FollowBox` 热点、电源是否稳定、摄像头固件串口 IP 是否仍是旧现场 WiFi 地址。如果第 3 步能打开但第 4 步黑屏，优先查 OV5640 排线、pin map 和 5V 供电。
