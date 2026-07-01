# FollowBox 模块拆分计划

> 目标：只规划后续可维护性拆分，不在本文件中改业务代码。当前 TOF 和 LiDAR 已能出数据，后续执行时必须优先保护现有数据链路。

## 推荐执行顺序（总表）

> 一眼看清依赖，避免同一文件被连续拆分导致 diff 混乱。同一文件的多个任务必须串行，前一项真机验证通过后才能开始下一项。

| 顺序 | 任务 | 核心文件 | 前置条件 |
| --- | --- | --- | --- |
| 1 | P0 热点 / WiFi 模块 | `h5_web_server.cpp` | 无（热点最优先，先稳住） |
| 2 | P1 HTTP 公共工具 + 本地 OTA 路由 | `h5_web_server.cpp` | **P0 完成且热点连续 3 分钟不掉线** |
| 3 | P2 H5 控制/API 路由 | `h5_web_server.cpp` | **P1 完成且固件构建通过** |
| 4 | P6 云端 Node 服务 | `cloud/server.js` | 与固件任务无耦合，可并行 |
| 5 | P5 云端 H5 前端 | `cloud/public/app.js` | 与车端 H5 分开执行 |
| 6 | P4 车端 H5 前端 | `firmware/web/app.js` | 先确认固件文件系统打包路径 |
| 7 | P7 控制中心脚本 | `followbox-control-center.ps1` | 低风险 |
| 8 | P7b 控制中心 HTML | `followbox-control-center.html` | **P7 脚本拆完且自检通过** |
| 9 | P8 LiDAR 外壳（末位） | `sensor_task.cpp` | 仅当 sensor_task 再次明显膨胀时才做 |

## 要拆的有哪些

| 优先级 | 拆分对象 | 当前主要位置 | 建议新文件 | 是否现在建议执行 |
| --- | --- | --- | --- | --- |
| P0 | FollowBox 热点 / SoftAP / captive portal / WiFi 状态自愈 | `firmware/src/web/h5_web_server.cpp` | `firmware/src/web/wifi_ap_supervisor.h`, `firmware/src/web/wifi_ap_supervisor.cpp` | 是 |
| P1 | H5 Web Server 的公共 HTTP 工具和静态响应辅助 | `firmware/src/web/h5_web_server.cpp` | `firmware/src/web/h5_http_common.h`, `firmware/src/web/h5_http_common.cpp` | 是 |
| P1 | 车端本地 OTA 路由 | `firmware/src/web/h5_web_server.cpp` | `firmware/src/web/local_ota_routes.h`, `firmware/src/web/local_ota_routes.cpp` | 是 |
| P2 | H5 控制/API 路由注册 | `firmware/src/web/h5_web_server.cpp` | `firmware/src/web/h5_control_routes.h`, `firmware/src/web/h5_control_routes.cpp` | 是，但排在热点稳定之后 |
| P4 | 车端 H5 前端页面逻辑 | `firmware/web/app.js` | `firmware/web/js/state.js`, `firmware/web/js/telemetry.js`, `firmware/web/js/controls.js`, `firmware/web/js/ota.js` | 是，但需先确认固件文件系统打包路径 |
| P5 | 云端 H5 前端页面逻辑 | `cloud/public/app.js` | `cloud/public/js/state.js`, `cloud/public/js/telemetry.js`, `cloud/public/js/ota.js`, `cloud/public/js/ui.js` | 是，和车端 H5 分开执行 |
| P6 | 云端 Node 服务路由与 OTA 逻辑 | `cloud/server.js` | `cloud/routes/device.js`, `cloud/routes/firmware.js`, `cloud/routes/health.js`, `cloud/services/device_store.js`, `cloud/services/firmware_manifest.js` | 是，需保护部署路径 |
| P7 | 本地控制中心 PowerShell 脚本 | `tools/followbox-control-center.ps1` | `tools/control-center/config.ps1`, `tools/control-center/git.ps1`, `tools/control-center/ota.ps1`, `tools/control-center/cloud.ps1`, `tools/control-center/http.ps1` | 是，低风险但要分阶段 |
| P7b | 本地控制中心前端页面（配套 HTML，719 行） | `tools/followbox-control-center.html` | 按面板区块拆分（如 `git`/`ota`/`cloud`/`http` 分区各自的脚本或片段），或抽出公共 fetch/渲染工具 | 是，但排在 P7 脚本拆分完成并自检通过之后 |
| P8（末位） | LiDAR 调度、探测、诊断外壳 | `firmware/src/sensors/sensor_task.cpp`, `firmware/src/sensors/sensor_task.h` | `firmware/src/sensors/lidar_bringup_probe.h`, `firmware/src/sensors/lidar_bringup_probe.cpp` | 暂缓。仅当 `sensor_task.cpp` 因新增传感器再次明显膨胀时才做；只拆外壳，不拆 parser，收益低风险高，排在所有其他项之后 |

## 当前不建议单独拆的模块

| 模块 | 当前位置 | 结论 |
| --- | --- | --- |
| TOF | `firmware/src/sensors/tof_vl53l1x_array.h`, `firmware/src/sensors/tof_vl53l1x_array.cpp` | 暂不拆。当前能出数据，驱动、诊断、恢复逻辑集中在一个独立模块里，比拆散更安全。 |
| UWB | `firmware/src/sensors/uwb_gc_p2304.h`, `firmware/src/sensors/uwb_gc_p2304.cpp` | 暂不拆。文件不大，parser/filter 边界清楚，协议未变时不要增加层级。 |
| 超声波 | `firmware/src/sensors/ultrasonic_array.h`, `firmware/src/sensors/ultrasonic_array.cpp` | 暂不拆。当前已经是单独模块，行数低，维护成本可控。 |
| 手柄遥控器 / DS600 | `firmware/src/control/rc_input_ds600.h`, `firmware/src/control/rc_input_ds600.cpp` | 暂不拆。当前已独立，后续只有新增多遥控协议时再抽象接口。 |
| 电机输出 | `firmware/src/drive/drive_adapter_analog_bldc.h`, `firmware/src/drive/drive_adapter_analog_bldc.cpp` | 不作为普通拆分任务。它是唯一 PWM 出口，任何改动都必须单独走 safety-critical。 |
| 安全门控 | `firmware/src/safety/safety_manager.h`, `firmware/src/safety/safety_manager.cpp` | 不作为维护性拆分任务。安全链路必须保持 `safety_manager -> applyFinalGate() -> drive_adapter`。 |
| 设备端云 OTA | `firmware/src/ota/cloud_ota_manager.h`, `firmware/src/ota/cloud_ota_manager.cpp`（约 377 行） | 不作为普通拆分任务。内部直接调用 `Update.h` 写 Flash，属安全敏感刷写路径。任何改动走 safety-critical，禁止以“文件偏大”为由维护性拆分。 |
| 设备→云遥测上行 | `firmware/src/cloud/cloud_client.h`, `firmware/src/cloud/cloud_client.cpp`（约 451 行） | 暂不拆。行数偏大但当前是内聚单模块，上行/命令下行边界清楚，能稳定出数据；拆散反而增加网络链路风险。后续若要拆，先补诊断，不改 payload/字段/重试策略。 |

## 怎么拆

### P0 热点 / WiFi 模块

1. 从 `h5_web_server.cpp` 中只搬走 SoftAP 启动、WiFi 状态、captive portal、DNS、AP 自愈和 `/api/wifi/status` 所需辅助逻辑。
2. 新模块暴露小接口，例如 `beginWifiApSupervisor(...)`, `loopWifiApSupervisor(...)`, `registerWifiRoutes(...)`, `wifiStatusJson(...)`。
3. `h5_web_server.cpp` 只保留路由装配和调用，不再直接塞热点细节。
4. 第一阶段只做“原样搬迁”，禁止顺手改热点策略、超时、重连退避、SSID、IP、DNS 行为。

### P1 HTTP 公共工具与 OTA 路由

1. 把 JSON 响应、错误响应、鉴权/请求解析辅助放进 `h5_http_common.*`。
2. 把 `/api/ota/status`、`/api/ota/local-upload`、OTA 进度响应放进 `local_ota_routes.*`。
3. 保持接口入参来自现有 `H5WebServer` 依赖，不新增全局单例。
4. OTA 相关拆分后必须重新跑固件构建和本地 OTA API smoke check。

### P2 H5 控制/API 路由

1. 把安装向导、模式切换、控制命令、校准/Profile 相关路由注册拆到 `h5_control_routes.*`。
2. 不改变 `h5_command_handler`、`h5_request_parser` 的输入输出结构。
3. H5 仍不能设置 PWM、清安全锁、绕过安装向导。
4. 控制路由拆分必须单独复查 `safety_manager` 调用链没有新增旁路。

### P8 LiDAR 调度诊断外壳（末位，暂缓）

0. 本项优先级已下调为所有任务之后。只有当 `sensor_task.cpp` 因新增传感器再次明显膨胀、且前面高收益拆分均已完成时，才启动本项。
1. 只从 `sensor_task.*` 拆 LiDAR UART 候选探测、启动命令重发、raw preview、诊断日志节流这些“调度/诊断外壳”。
2. 不拆 `lidar_eai_s2.*` parser，不改帧格式、波特率候选、有效距离字段、`ObstacleSnapshot` 字段。
3. 拆分后 `SensorTask::snapshot()` 对外字段必须完全不变。
4. 因 LiDAR 当前能出数据，若构建后真机读数和拆分前不一致，立即回滚本项。

### P4 车端 H5 前端

1. 先确认 `firmware/web/index.html` 对多 JS 文件的加载路径能被当前文件系统/打包流程正确带入固件。
2. 第一阶段按职责拆：状态缓存、遥测渲染、控制按钮、OTA 弹窗。
3. 不改 API 路径、不改 WebSocket 消息格式、不改安装向导流程。
4. 拆完后必须用浏览器访问车端 H5，验证静态资源 200、WebSocket 正常、控制按钮状态正常。

### P5 云端 H5 前端

1. 和车端 H5 分开拆，避免把 `cloud/public/` 和 `firmware/web/` 混在一次任务里。
2. 保持云端部署路径只来自 `cloud/public/`。
3. 拆前后对比 `cloud/public/index.html` 引用资源，确保公网 `/fb/` 子路径不丢资源。
4. 拆完后至少跑 `node --check cloud/server.js`，并访问 `/api/health` 与云端页面。

### P6 云端 Node 服务

1. 先抽 `device_store`，只管理设备状态、日志、在线判断。
2. 再抽 `firmware_manifest`，只负责读取/校验 `cloud/firmware/manifest.json`。
3. 最后抽路由文件，保持 `server.js` 作为入口和路由组装。
4. 禁止改变 namespace、PM2 进程名、部署目录、token 读取方式。

### P7 控制中心脚本

1. 先拆只读配置和工具函数，不动实际执行命令。
2. 再拆 git、OTA、cloud deploy、HTTP API 四类动作。
3. 每次拆分后跑一次脚本自检或至少打开本地控制中心页面，确认按钮和 API 仍可用。
4. 严禁扩大 SCP/SSH/删除命令作用范围。

### P7b 控制中心前端 HTML

1. 必须在 P7 脚本拆分完成并自检通过后才做，避免后端拆了、前端还是巨块的不对称。
2. 按面板区块拆分（git/ota/cloud/http），或先抽出公共 fetch/渲染工具，再拆各区块交互。
3. 不改控制中心调用的本地/云端 API 路径与参数，只搬迁不改行为。
4. 拆完后重新打开 `tools/followbox-control-center.html`，逐个面板验证按钮、状态回显、日志输出与拆分前一致。

## 注意事项

1. 每个子任务最多修改 3 个文件；超过 3 个文件必须重新拆任务。
2. 同一源文件（尤其 `h5_web_server.cpp`）的多个拆分项必须严格按“推荐执行顺序（总表）”串行，禁止并行或跳序；前一项真机验证通过后才能开下一项，避免连续拆分导致 diff 混乱、难回滚。
3. 第一轮所有拆分只做搬迁，不做行为优化。热点问题要修时，也应先把热点模块独立出来，再单独改策略。
3. `main.cpp` 继续只做入口，不能把业务逻辑搬进去。
4. GPIO 常量仍只允许在 `firmware/include/config/board_pins.h`。
5. 禁止改板型、Flash、PSRAM、电压、分区配置。
6. 禁止旧 GPIO35/36/37/47/48 回流。
7. TOF、LiDAR 当前能出数据，任何拆分都不能改传感器字段语义、采样周期、bus recovery、parser、有效性判断。
8. 云端 H5 和车端 H5 不能混合部署或复用路径。
9. 涉及固件、车端 H5、协议、Profile 的实际改动，需要递增固件版本并生成 OTA；纯文档或纯云端不需要设备 OTA，但必须在交接记录写明原因。
10. 触及 `VERIFIED-LOCKS.md` 锁定路径时，交接记录必须写解锁理由和验证证据。

## 怎么保证其他的安全

### 运动安全

1. 拆分任务不得新增任何绕过 `safety_manager -> applyFinalGate() -> drive_adapter` 的运动输出路径。
2. 复查命令：

```powershell
rg -n "writeCommand|applyFinalGate|ledcWrite|analogWrite|digitalWrite|drive_adapter" firmware/src firmware/include
```

3. 只允许 `drive_adapter_analog_bldc` 作为 PWM 出口。发现新增 PWM 调用，本任务停止。

### TOF 安全

1. `tof_vl53l1x_array.*` 暂不拆、不改。
2. 如后续必须改，只允许先补诊断或注释，不改变 I2C 初始化、TCA 通道、读取周期、bus clear、reinit。
3. 验证必须包含遥测字段：front left、front center、front right 三路距离和 valid/stale 状态。

### LiDAR 安全

1. `lidar_eai_s2.*` parser 暂不拆、不改。
2. `sensor_task.*` 里的 LiDAR 外壳拆分后，对外 `SensorSnapshot` 字段必须逐项一致。
3. 真机验证必须确认 `lidar_valid`、rx bytes、packets、scans、front left/center/right 数据仍更新。

### 热点安全

1. 热点模块拆分第一阶段不改 `192.168.4.1`、SSID、AP channel、DNS/captive probe 响应策略。
2. 拆分后必须确认手机或电脑仍能打开 `http://192.168.4.1/`。
3. `/api/wifi/status` 必须继续返回 AP ready、client count、recovery/retry 诊断字段。

### 云端和部署安全

1. `cloud/` 服务只从 `cloud/` 部署，云端 H5 只从 `cloud/public/` 部署，OTA 只从 `cloud/firmware/` 部署。
2. 禁止 `pm2 restart all`，只能操作 `followbox-cloud`。
3. 禁止上传 `.env`、key、token、`node_modules/`、日志和其他项目目录。

## 怎么保证拆了以后是正确的

### 每个固件拆分任务后

```powershell
pio run -d firmware -e esp32-s3-devkitc-1
firmware/tools/logic_smoke_test.exe
python tools/check_verified_locks.py
python tools/check_ai_handoff.py
```

### 热点 / 车端 H5 拆分后

```powershell
curl http://192.168.4.1/
curl http://192.168.4.1/api/state
curl http://192.168.4.1/api/wifi/status
curl http://192.168.4.1/api/ota/status
```

验收标准：

1. 首页、CSS、JS 返回 200。
2. WebSocket 或轮询遥测能更新。
3. `/api/wifi/status` 中 AP 仍 ready，client 数和诊断字段合理。
4. 热点连接至少连续观察 3 分钟无主动掉线。

### 传感器相关拆分后

1. 对比拆分前后的 `/api/state` 或云端遥测字段。
2. TOF 三路距离继续更新，valid/stale 逻辑不倒退。
3. LiDAR rx bytes、packets、scans 持续增加，front left/center/right 数据继续更新。
4. UWB、超声波、遥控器字段不能因为本次拆分丢失或改名。

### 云端拆分后

```powershell
node --check cloud/server.js
node --check cloud/public/app.js
python tools/check_ai_handoff.py
python tools/check_verified_locks.py
```

如发生部署，还必须验证：

```powershell
curl https://www.boonai.cn/fb/api/health
curl https://www.boonai.cn/fb/deploy-version.txt
```

### 正确性判定

1. 构建通过。
2. 静态检查通过。
3. 现有 API 路径、JSON 字段、WebSocket 消息格式不变。
4. TOF、LiDAR、UWB、超声波、遥控器遥测字段仍存在且更新。
5. 安全链路没有新增旁路。
6. 交接记录说明改了什么、验证了什么、是否需要 OTA。

## 技能树同步结论

1. 本计划不需要修改 `skills/` 或 `.agents/skills/`，因为拆分规则没有改变技能职责，只是给后续执行提供任务清单和验收标准。
2. “以后用户说写 md 文档，默认写成本地 `.md` 文件”属于项目协作约定，已同步到 `AGENTS.md`，不需要放进单个技能文件。
3. 如果后续要让某个专用 skill 强制执行这个约定，再单独修改对应 `skills/*/SKILL.md` 或 `.agents/skills/*/SKILL.md`，并按 `AI_SKILLS_HANDOFF` 锁定规则写解锁理由。
