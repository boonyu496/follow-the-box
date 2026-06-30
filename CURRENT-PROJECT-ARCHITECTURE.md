# FollowBox 当前项目架构地图

> 目标：给人和 AI 一张可查问题的项目地图。后续改 H5、AP、局域网、云端、固件遥测或安全链路时，先按这里定位入口，避免只改了一份页面或一端字段。

## 1. 权威文件优先级

安全、固件和接线判断按以下顺序：

```text
FIRMWARE-SPEC.md
  > CURRENT-WIRING-AI.md
  > PIN-MAP-V1.md
  > protocols/*.md / profiles/example_bldc_analog_36v.yaml
  > 当前代码
```

常用入口：

| 入口 | 用途 |
|---|---|
| `README.md` | 项目资料总入口 |
| `CURRENT-PROJECT-ARCHITECTURE.md` | 当前这份项目地图，定位页面/云端/固件边界 |
| `FIRMWARE-SPEC.md` | 固件目录、任务、安全链路、接口总规范 |
| `protocols/H5-API.md` | 本地 H5 / 云端共享的状态 JSON 和控制 API |
| `CLOUD-TELEMETRY-SPEC.md` | 云端遥测、远程低速点动、安全边界 |
| `AI-HANDOFF-MEMORY.md` | 每次 AI 改动后的短交接记录 |

### 1.1 文件归属和技能路由

| 文件/区域 | 归属 | 优先技能 | 当前状态 |
|---|---|---|---|
| `firmware/src/app/`, `firmware/src/safety/`, `firmware/src/control/`, `firmware/src/drive/` | 固件运行链路与运动安全 | `skills/01-firmware-architecture-guardian`, `skills/03-safety-control-reviewer` | 已验证边界，改动前先看 `VERIFIED-LOCKS.md` |
| `firmware/include/config/board_pins.h`, `PIN-MAP-V1.md`, `CURRENT-WIRING-AI.md` | GPIO、接线和电气事实 | `skills/01-firmware-architecture-guardian`, `skills/05-drive-power-calibration-engineer` | 锁定源，禁止顺手清理 |
| `firmware/src/sensors/`, `protocols/` | UWB/TOF/IMU/超声/雷达协议和快照 | `skills/04-sensor-protocol-integrator` | 协议证据优先，禁止补猜 parser |
| `firmware/web/`, `firmware/src/web/`, `protocols/H5-API.md` | 车端 AP/局域网 H5 和本地控制 API | `skills/06-h5-telemetry-ui-engineer` | `firmware/web/` 是权威静态源 |
| `cloud/`, `cloud/public/`, `cloud/firmware/`, `devspace.yaml` | 云端控制台、遥测中转和云端 OTA | `skills/06-h5-telemetry-ui-engineer`, `.agents/skills/followbox-cloud-h5-deploy` | 部署隔离区，不能和车端 H5 混用 |
| `AI-HANDOFF-MEMORY.md`, `skills/`, `.agents/skills/`, `VERIFIED-LOCKS.md` | AI 协作、交接和锁定门禁 | `skills/00-dispatcher`, `skills/07-code-review-debugger` | 修改后必须跑 handoff/lock 检查 |
| `plans/cleanup/` | 临时整理计划 | `skills/00-dispatcher` | 临时文档，完成或被替代后可删除 |
| `firmware/data/` | 旧车端 H5 副本 tombstone | `skills/06-h5-telemetry-ui-engineer` | 只保留 README 说明；不要放 H5 源码或生成资源 |
| `output/`, `v/`, `zhiliao/` | 历史日志、截图、视频、供应商资料或工具 | `skills/07-code-review-debugger` | 证据目录，先建索引再决定归档/删除 |
| `firmware/.pio-core/`, `firmware/.pio/`, `vision_cam/.pio/`, `.codebuddy/db/` | 本地可重建缓存 | `skills/07-code-review-debugger` | 只在确认 ignored 后按 Phase B 删除 |

## 2. 三个 H5 访问入口

这里的“三个页面”按访问方式分，不按源码份数分：

| 访问入口 | 用户看到的页面 | 源码 | 运行服务 | 数据/控制通道 | 备注 |
|---|---|---|---|---|---|
| AP 热点本地页 | `http://192.168.4.1/` | `firmware/web/` | ESP32 `H5WebServer` + LittleFS | `/ws/state`, `/api/jog`, `/api/mode-request`, `/api/*` | 车端 SoftAP 常驻，配网/本地控制入口 |
| 局域网页 | `http://<小车STA-IP>/` | `firmware/web/` | 同一个 ESP32 `H5WebServer` + LittleFS | 同上 | 与 AP 页是同一份静态资源，只是从 STA 网络访问 |
| 云端页 | `http://localhost:8080/` 或部署域名 | `cloud/public/` | `cloud/server.js` / DevSpace / Kubernetes | SSE `/api/device/:id/events`，命令 `/api/device/:id/command` | 云端只能低速点动/安全停，最终仍由车端安全链裁决 |

关键结论：

- AP 页和局域网页不是两套前端。改本地车端 H5 时，只改 `firmware/web/`，然后重新 `pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs`。
- `firmware/data/` 已收敛为 tombstone。当前 `firmware/platformio.ini` 已设置 `data_dir = web`，所以 `firmware/data/` 不再包含或同步 H5 源码。
- 云端页是独立前端。凡是状态字段、新传感器卡片、文案标签、OTA 显示等跨端体验改动，通常要同时检查 `firmware/web/` 和 `cloud/public/`。

## 3. 车端固件运行链路

```text
硬件/传感器/输入
  -> firmware/src/sensors/*, control/rc_input_ds600.*
  -> SensorBundle
  -> SharedState::publishSensors()
  -> controlTaskEntry() / App::ingest*
  -> safety_manager.evaluate()
  -> mode_manager.selectMode()
  -> command_pipeline.buildIntent()
  -> obstacle_manager.apply()
  -> motion_mixer.mix()
  -> safety_manager.applyFinalGate()
  -> drive_adapter_analog_bldc.writeCommand()
  -> GPIO12/13/14/15/16/39
```

任务边界：

| 任务 | Core | 文件入口 | 职责 |
|---|---:|---|---|
| control | 1 | `firmware/src/main.cpp` `controlTaskEntry()` | 唯一运动安全闭环，唯一调用 `drive.writeCommand()` |
| sensor | 0 | `firmware/src/main.cpp` `sensorTaskEntry()` | 采集 UWB/LiDAR/IMU/TOF/超声/电池/RC，发布快照 |
| comm | 0 | `firmware/src/main.cpp` `commTaskEntry()` | 推送 H5 状态、云端遥测、OTA、日志 |
| AsyncTCP | 0 | `firmware/src/web/h5_web_server.cpp` | HTTP/WS 回调，只收请求，不直接写电机 |

运动红线：

- 所有运动必须经过 `safety_manager.applyFinalGate()`。
- `drive_adapter_analog_bldc` 是唯一 PWM/GPIO 输出出口。
- H5、云端、UWB、DS600 都只能形成输入或 `MotionIntent`，不能直接写 PWM。

## 4. 本地 H5 架构

```text
firmware/web/index.html
firmware/web/app.js
firmware/web/style.css
firmware/web/shared/helpers.js
  -- pio uploadfs -->
ESP32 LittleFS
  -- h5_web_server.cpp serveStatic("/") -->
AP/局域网浏览器
```

本地页面 API：

| 类型 | 路径 | 固件入口 | 说明 |
|---|---|---|---|
| 状态推送 | `GET /ws/state` | `H5WebServer::pushState()` | 主要实时状态通道 |
| HTTP 兜底 | `GET /api/state` | `h5_web_server.cpp` | WebSocket 被拦截时兜底 |
| 日志 | `GET /api/logs` | `DebugConsole::copyRecentJson()` | 只读 |
| 低速点动 | `POST /api/jog` | `H5CommandHandler::onJog()` | deadman；后续仍进安全链 |
| 模式请求 | `POST /api/mode-request` | `H5CommandHandler::onModeRequest()` | 只能请求，不能强切安全 |
| 复位软件故障 | `POST /api/reset-fault` | `H5CommandHandler::onResetFault()` | 不能复位物理急停 |
| 配网 | `POST /api/wifi` | `WifiStore` + `WiFi.begin()` | AP 继续保留 |
| OTA | `/api/ota/*` | `CloudOtaManager` | 检查与安装分离 |

本地 H5 修改检查：

1. 改 UI/JS/CSS：只改 `firmware/web/`。
2. 改状态字段：同步 `protocols/H5-API.md`、`firmware/src/web/telemetry_api.cpp`、`firmware/web/app.js`、必要时 `cloud/public/app.js`。
3. 改控制请求：同步 `protocols/H5-API.md`、`h5_request_parser.*`、`h5_command_handler.*`、`firmware/web/app.js`。
4. 真机或 OTA 后若 AP 页没更新，先确认是否执行了 `pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs`。只烧录固件不会更新 LittleFS 页面。

## 5. 云端架构

```text
ESP32 CloudClient
  -> POST /api/device/:deviceId/ingest
  -> GET  /api/device/:deviceId/command
cloud/server.js
  -> SSE /api/device/:deviceId/events
  -> cloud/public/index.html + app.js + style.css
浏览器云端控制台
```

云端只负责中转和展示：

- 设备上传的 `state` 必须使用 `protocols/H5-API.md` 同一套 schema。
- 云端操作员命令只允许 `deadman + forward/turn` 或 `safe_idle`。
- 固件进入 `MANUAL_CLOUD_LOW_SPEED` 后仍由 `SafetyManager` 和 `applyFinalGate()` 最终裁决。
- 视频云端转发走 `POST /api/device/:id/video/upload` 和 `/video/latest.jpg`/`/video/stream`，视频断流不影响运动安全。
- OTA 发布走 `tools/package_ota.py` -> `cloud/firmware/firmware.bin` + `manifest.json`，安装必须由 H5/云端显式点击授权。

DevSpace 边界：

| 命令 | 作用 |
|---|---|
| `devspace dev` | 进入 `followbox-dev` namespace，转发本地 `http://localhost:8080` |
| `devspace run-pipeline firmware-check` | 固件 PlatformIO 构建 |
| `devspace run-pipeline vision-check` | `vision_cam` 构建 |
| `devspace run-pipeline ai-handoff-check` | 检查交接记录 |

## 6. 视觉与视频边界

| 模块 | 路径 | 职责 | 安全边界 |
|---|---|---|---|
| ESP32-S3-CAM 固件 | `vision_cam/` | 独立视频板固件 | 不做主控，不进安全闭环 |
| 车端视频状态 | `firmware/src/sensors/camera_link.*` | 上报 `camera.online` 和流地址 | 只读遥测 |
| 本地 H5 视频 | `firmware/web/app.js` | 显示 `camera.stream_url` 或手动地址 | 视频断流不停车 |
| 云端视频 | `cloud/server.js`, `cloud/public/app.js` | 接收/展示云端转发帧 | 只显示，不裁决运动 |

## 7. 修改同步矩阵

| 想改的内容 | 必看/必改 | 验证 |
|---|---|---|
| 本地 AP/局域网 H5 布局 | `firmware/web/index.html`, `firmware/web/app.js`, `firmware/web/style.css` | 浏览器打开 `firmware/web/index.html` 或真机 `uploadfs` 后访问 AP |
| 云端 H5 布局 | `cloud/public/index.html`, `cloud/public/app.js`, `cloud/public/style.css`, `cloud/public/deploy-version.txt` | `node cloud/server.js` 后访问 `http://localhost:8080` |
| 新增遥测字段 | `include/core/types.h`, `include/core/system_state.h`, `telemetry_api.cpp`, `protocols/H5-API.md`, 本地/云端 `app.js` | `pio run -d firmware` + 页面显示检查 |
| 新增传感器 | `firmware/src/sensors/*`, `SensorBundle`, `App::ingestSensorInputs()`, `telemetry_api.cpp`, `protocols/H5-API.md` | smoke test + 固件构建 + H5/云端字段显示 |
| 本地控制 API | `protocols/H5-API.md`, `h5_request_parser.*`, `h5_command_handler.*`, `h5_web_server.cpp`, `firmware/web/app.js` | 请求返回码 + `App::tick()` 后状态变化 |
| 云端远程控制 | `CLOUD-TELEMETRY-SPEC.md`, `cloud/server.js`, `cloud/public/app.js`, `firmware/src/cloud/cloud_client.*` | `node cloud/server.js` + 设备/模拟请求 |
| 安全/运动链路 | `FIRMWARE-SPEC.md`, `safety/*`, `app/*`, `control/*`, `drive/*` | `firmware/tools/logic_smoke_test.exe` + `pio run -d firmware` |
| 引脚/接线 | `PIN-MAP-V1.md`, `CURRENT-WIRING-AI.md`, `include/config/board_pins.h`, Profile | 禁止只改代码；文档和 Profile 必须同步 |
| OTA 发布 | `firmware/include/config/ota_config.h`, `tools/package_ota.py`, `cloud/firmware/manifest.json` | `python tools/package_ota.py` |

## 8. 常见问题定位

### 改了 H5，但 AP 页面没更新

按顺序查：

1. 是否改的是 `firmware/web/`，而不是 `cloud/public/` 或 `firmware/data/`。
2. 是否执行 `pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs`。烧录 `firmware.bin` 不会更新 LittleFS。
3. 是否访问的是车端 AP/STA IP，而不是云端页面。
4. 浏览器是否缓存旧资源。车端已设置 no-store，但手机浏览器可强刷或换无痕窗口。
5. 如果代码里新增字段但页面仍为空，查 `telemetry_api.cpp` 是否真的输出该字段，再查 `protocols/H5-API.md` 和 `app.js` 字段名是否一致。

### 云端有数据，本地页没有

1. 云端显示的是设备上传的 `state` 缓存；本地页必须直接连车端 `/ws/state`。
2. 查浏览器是否在同一网络：AP 模式访问 `192.168.4.1`，STA 模式访问 `/api/wifi/status` 里显示的 IP。
3. 查 `H5WebServer::pushState()` 是否有状态，`/api/state` 可作为 HTTP 兜底。

### 本地页有数据，云端没有

1. 查 `FOLLOWBOX_CLOUD_ENABLED`、`cloud_config.h` 和 STA WiFi。
2. 查云端 `/api/health`。
3. 查 `cloud_client.update()` 是否上报、`cloud/server.js` 是否收到 `/ingest`。
4. 云端 SSE 只显示当前 `deviceId`，确认设备 ID 一致。

### 字段名不一致

用 `rg` 横向查：

```powershell
rg -n "字段名" protocols firmware/src firmware/web cloud/public
```

状态字段以 `protocols/H5-API.md` 为冻结 schema；页面不得自行猜字段。

## 9. 后续任务路由

| 任务 | 优先读 | 主要文件 |
|---|---|---|
| H5 显示/点动问题 | `protocols/H5-API.md`, 本文件第 4 节 | `firmware/web/*`, `firmware/src/web/*` |
| 云端遥测/远控问题 | `CLOUD-TELEMETRY-SPEC.md`, 本文件第 5 节 | `cloud/*`, `firmware/src/cloud/*` |
| 运动/安全问题 | `FIRMWARE-SPEC.md` | `safety/*`, `app/*`, `control/*`, `drive/*` |
| 传感器字段不上报 | `protocols/H5-API.md` | `sensors/*`, `types.h`, `system_state.h`, `telemetry_api.cpp` |
| 接线/引脚疑问 | `PIN-MAP-V1.md`, `CURRENT-WIRING-AI.md` | `include/config/board_pins.h`, Profile |

## 10. 当前架构状态

- 本地 AP/局域网页面的权威源：`firmware/web/`。
- 云端页面的权威源：`cloud/public/`。
- 遗留路径 tombstone：`firmware/data/README.md`。
- 固件状态 schema 输出点：`firmware/src/web/telemetry_api.cpp`。
- 本地静态资源服务点：`firmware/src/web/h5_web_server.cpp`。
- 云端服务入口：`cloud/server.js`。
- 云端部署入口：`devspace.yaml`。
- 所有运动输出最终出口：`firmware/src/drive/drive_adapter_analog_bldc.*`。
