# FollowBox 首烧、OTA 更新、云端 H5 遥测调试与修复交接文档

> 目的：把当前代码审查发现的问题、首版本地烧录顺序、后续 OTA 更新与云端 H5 遥测调试边界整理成可执行交接，方便后续 AI/开发者准确修复。

## 术语澄清

- “首烧”指 ESP32 固件和 LittleFS 文件系统第一次通过本地 USB/串口写入。
- “OTA 更新”指首烧通过后，车辆无需每次插电脑即可远程更新固件或 LittleFS；这是需要规划的能力。
- “云端 H5 遥测调试”指把 H5 调试端/遥测页面或数据采集服务放到云端，用来查看小车输出状态、采集日志、辅助调试。
- OTA 与云端 H5 是两条不同通道：OTA 负责更新，H5/云端负责状态采集和调试。
- 云端 H5 可以显示状态、收集遥测、生成调试建议；不应直接写 PWM、清急停、绕过安装向导或绕过本地安全门控。

## 当前结论

- 已新增 P1 云端遥测/低速远程点动骨架：固件默认关闭，启用后 ESP32 以 STA WiFi 主动上传状态/日志并轮询云端命令；云端命令只进入 `MANUAL_CLOUD_LOW_SPEED`，仍受本地安全链最终裁决。
- 固件主体可编译：`pio run -d firmware` 已通过，RAM 约 15.9%，Flash 约 26.8%。
- LittleFS 镜像可构建：`pio run -d firmware -t buildfs` 已通过，包含 `/app.js`、`/index.html`、`/style.css`。
- Host 逻辑烟测已恢复：`firmware/tools/logic_smoke_test.exe` 已重新编译并运行通过。
- H5 JS 语法已检查：`node --check firmware/data/app.js` 通过。
- 本轮未执行真机烧录、未执行 `uploadfs`、未验证 H5 浏览器联调、未做架空电机测试。
- 结论：P0 代码链路已可进入首次本地/USB 首烧准备；真机首烧、H5 浏览器联调、OTA 和云端遥测仍需按下方流程分阶段验证。

## 2026-06-07 P0 修复结果

- 已修复 DS600 丢失在非当前控制源时不应成为全局锁定故障的问题；`MANUAL_RC` 中仍停车，但不再 latched。
- 已修复 H5 `AUTO_FOLLOW_REQUEST` 因 `unlock_request=false` 不可达的问题；合法 AUTO 请求仍受安装向导、油门标定、UWB 和安全门控约束。
- 已补充 H5 `SAFE_IDLE` one-shot，确保 AUTO 保持状态下仍可被“安全停”请求退出。
- 已修复标定参数等于默认值时跳过 NVS schema 写入的问题，避免重启后丢失“已标定”状态。
- 已补齐 `/ws/state` 的 `install_wizard_complete` 和 `throttle_calibrated` 字段，并同步 H5 前端和 `protocols/H5-API.md`。
- 已增强安装向导完成请求：`complete=true` 时必须同时确认急停、架空、方向、油门标定四项。
- 已恢复并扩展 host 烟测，覆盖 RC lost-link、H5 AUTO、遥测字段、安装向导确认解析。

## 烧录策略

### 阶段 1：第一次本地首烧

第一次烧录必须走本地 USB/串口，原因是急停反馈、PWM 输出默认态、WiFi/H5、NVS、上传文件系统都需要现场确认。

建议命令：

```powershell
pio run -d firmware
pio run -d firmware -t buildfs
pio run -d firmware -t upload
pio run -d firmware -t uploadfs
```

首烧验证必须满足：

- 上电默认电机不动：GPIO12/13 PWM 为 0，GPIO39 使能关闭，刹车有效。
- GPIO21 急停反馈有效：急停按下进入 ESTOP/FAULT，松开后不会自动恢复运动。
- H5 页面可打开，WebSocket `/ws/state` 正常推送。
- 低速点动必须按住 deadman 才有请求，松手停车。
- AUTO_FOLLOW 未完成安装向导、未标定、UWB 无效时不能进入。
- 车轮必须架空，先单侧控制器/单轮测试，再双轮低速测试。

### 阶段 2：首烧通过后启用 OTA 更新通道

只有在第一次本地首烧验证通过后，才允许增加 OTA。OTA 必须遵守：

- OTA 只能更新已本地构建、审查、验收通过的固件或 LittleFS 镜像。
- OTA 开始前必须让车辆进入安全态：电机 `enable=false`、PWM=0、刹车有效、禁止运动命令。
- OTA 更新中禁止任何运动输出，更新失败必须保持安全态并可通过本地 USB 恢复。
- OTA 产物必须区分 firmware 与 filesystem，避免只更新 H5 却误刷固件，或只刷固件忘记更新 LittleFS。
- OTA 完成后必须重启并上报版本、构建时间、文件系统版本和最近一次更新结果。
- 涉及运动、安全门控、引脚、ADC、UWB parser 的 OTA 包，仍必须先本地编译 + 逻辑烟测 + 架空验证。
- 推荐后续单独设计 `OTA-UPDATE-SPEC.md`，包括版本号、校验、回滚、断电恢复、权限和日志。

### 阶段 3：首烧通过后再接入云端 H5 遥测调试

只有在第一次本地首烧验证通过后，后续才允许把 H5 调试端或遥测采集接入云端。云端 H5 遥测必须遵守：

- 云端只采集和展示 `SystemState`/日志/调试指标，不能作为安全主控。
- 云端不得直接改 PWM、急停、GPIO、安全红线、安装向导结果。
- 云端不得绕过 `safety_manager -> applyFinalGate() -> drive_adapter_analog_bldc`。
- 云端如提供动作建议，也只能作为“建议/诊断”，本地 ESP32 必须最终裁决并可拒绝。
- 云端调试前必须确认 H5 遥测字段完整可信，尤其是 `install_wizard_complete`、`throttle_calibrated`、`safety.stop_reason`、`motor` 输出状态。
- 涉及运动、安全门控、引脚、ADC、UWB parser 的变更，仍必须先本地编译 + 逻辑烟测 + 架空验证，然后再把状态接入云端查看。

## 云端 H5 遥测建议接口

第一阶段优先做“遥测/日志上传”；P1 远程控制只保留受限低速点动，不做公网高权限控制：

- 设备端继续保留本地 H5：`/ws/state` 用于局域网调试。
- 云端调试端接收周期状态快照，字段以 `protocols/H5-API.md` 为准。
- 上传内容建议包含：时间戳、模式、安全状态、停止原因、电池、电机命令、UWB、障碍物、TOF、超声、RC/H5 在线状态。
- 云端页面可以显示曲线、最近故障、状态时间线、原始 JSON 和下载日志。
- P1 已加入受限云端控制骨架：仅允许带 `deadman` 和短 TTL 的低速点动/安全停，不能直接设 PWM/清急停/改标定/绕过安装向导。
- 后续若放开更多云端控制，必须单独经过 `architect + safety-reviewer + safety-officer` 审批。

## 修复/待办问题清单

### P0 safety-critical

1. `firmware/src/safety/safety_manager.cpp`
   - 状态：已修复并由 host 烟测覆盖。
   - 问题：`MANUAL_RC` 下 DS600 丢失被写成 latched fault，且 `hasActiveLatchedFault()` 中也会锁定。
   - 风险：与真值表/恢复条件不完全一致，可能导致模式恢复逻辑错误。
   - 修复目标：按 `FIRMWARE-SPEC.md` 真值表重新定义 DS600 lost-link 在各模式的锁定/非锁定行为，并补测试。

2. `firmware/src/web/h5_command_handler.cpp` + `firmware/src/app/mode_manager.cpp`
   - 状态：已修复并由 host 烟测覆盖。
   - 问题：H5 `AUTO_FOLLOW_REQUEST` 调用 `stopMotion()` 清掉 `unlock_request`，但 `mode_manager` 只有 `unlock_request=true` 时才检查 `auto_request`。
   - 风险：H5 请求自动跟随基本不可达。
   - 修复目标：明确 H5 AUTO 请求的授权语义，保证不会绕过向导/标定/UWB/安全门控，同时让合法请求可达。

3. `firmware/src/storage/calibration_store.cpp`
   - 状态：已修复；仍需真机验证 NVS 持久化。
   - 问题：保存参数等于默认值时直接返回 true，不写 NVS schema。
   - 风险：本次会话看似已标定，重启后仍可能 `throttle_calibrated=false`，AUTO_FOLLOW 无法可靠启用。
   - 修复目标：即使数值未变化，只要 NVS 未写 schema 或未标定，也必须写入标定完成记录。

4. `firmware/src/web/telemetry_api.cpp` + `firmware/data/app.js`
   - 状态：已修复并由 host 烟测 + JS 语法检查覆盖。
   - 问题：前端读取 `install_wizard_complete`，但遥测 JSON 未输出该字段，也未输出 `throttle_calibrated`。
   - 风险：H5 显示状态不可信，后续判断自动跟随条件困难。
   - 修复目标：遥测输出两个字段，前端同时显示安装向导和油门标定状态。

5. `firmware/data/index.html` + `firmware/src/web/h5_web_server.cpp`
   - 状态：已加四项确认并更新 parser；仍需浏览器和真机流程验证。
   - 问题：安装向导目前只是“完成安装向导”按钮，缺少急停、架空、方向、油门实测等分步确认。
   - 风险：用户可能未完成安全检查就持久化完成状态。
   - 修复目标：至少把“完成向导”改成多条件确认；关键安全步骤未确认前不允许写 `install_wizard_complete=true`。

### P1 architecture / consistency

6. `firmware/src/main.cpp`
   - 问题：当前 145 行，并持有任务函数、对象、调度逻辑。
   - 约束：`FIRMWARE-SPEC.md` 要求 `main.cpp` 只做启动入口，目标 80-120 行。
   - 修复目标：把任务编排迁到 `app/` 的 runtime/scheduler 模块，`main.cpp` 只调用初始化入口。

7. `firmware/include/config/profile_defaults.h` + `profiles/example_bldc_analog_36v.yaml` + `FIRMWARE-SPEC.md`
   - 问题：代码 PWM 频率是 2000Hz，Profile/规范默认是 1000Hz。
   - 风险：PWM->0-5V 标定不可复现。
   - 修复目标：统一频率，并在 `PWM-OUTPUT-CALIBRATION.md` 中记录实测值。

8. `firmware/include/config/board_pins.h` + `profiles/example_bldc_analog_36v.yaml`
   - 问题：Profile 标记 IMU enabled，但代码 `UART_NUM_IMU=-1`，IMU 实际禁用。
   - 修复目标：要么文档改为 disabled/pending，要么实现 UART0->GPIO42 启用路径，并做电平/波特率/静止窗口验证。

9. `firmware/web/*` + `firmware/data/*`
   - 问题：两个 H5 资源目录已经漂移，LittleFS 实际使用 `firmware/data/`。
   - 修复目标：明确唯一源目录；建议 `firmware/data/` 为烧录源，`firmware/web/` 删除或改为生成/预览说明，避免后续改错文件。

10. `firmware/src/web/h5_web_server.cpp`
    - 问题：POST body 缓冲区是函数内 `static char buf[]`，异步请求交错时可能互相覆盖。
    - 修复目标：改为每请求独立缓冲，或限制并发并明确保护策略。

11. `firmware/README.md`
    - 问题：仍写“Hardware output modules are intentionally not implemented yet”，但代码已接入 `drive_adapter_analog_bldc`。
    - 修复目标：更新 README，反映当前运行代码真实状态。

## 修复后的最低验收

```powershell
pio run -d firmware
pio run -d firmware -t buildfs
$env:Path = 'C:\msys64\mingw64\bin;' + $env:Path
g++ -std=c++17 -Ifirmware/include -Ifirmware/src `
  firmware/tools/logic_smoke_test.cpp `
  firmware/src/safety/safety_manager.cpp `
  firmware/src/app/mode_manager.cpp `
  firmware/src/app/command_pipeline.cpp `
  firmware/src/control/motion_mixer.cpp `
  firmware/src/control/follow_controller_uwb.cpp `
  firmware/src/control/obstacle_manager.cpp `
  firmware/src/sensors/uwb_gc_p2304.cpp `
  firmware/src/sensors/lidar_ld19.cpp `
  firmware/src/sensors/jy61p_imu.cpp `
  firmware/src/sensors/obstacle_fusion.cpp `
  firmware/src/app/app.cpp `
  firmware/src/web/telemetry_api.cpp `
  firmware/src/web/h5_command_handler.cpp `
  firmware/src/web/h5_request_parser.cpp `
  -o firmware/tools/logic_smoke_test.exe
firmware\tools\logic_smoke_test.exe
```

验收结果必须记录到 `AI-HANDOFF-MEMORY.md`。若做了真机首烧，还必须记录：

- 固件是否烧录成功；
- LittleFS 是否烧录成功；
- H5 是否可访问；
- 急停是否有效；
- 上电默认 PWM/使能/刹车状态；
- 是否允许进入下一阶段 OTA 更新通道；
- 是否允许进入下一阶段云端 H5 遥测调试；
- 云端采集到的状态字段是否与本地 `/ws/state` 一致。
