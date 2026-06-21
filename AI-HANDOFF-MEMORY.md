# FollowBox AI 交接记忆（必须短）

> 用途：给 Hermes / Claude / Copilot / VS Code AI / 其他代码代理快速知道“上一个 AI 改了什么、验证了什么、下一步该做什么”。  
> 这不是完整日志，不写长篇过程；每次改代码、改架构、修文档、排 bug 后，只追加一条短记录。

## 使用规则

1. 任何 AI 修改代码、架构、接线文档、Profile、协议、技能文件后，必须在本文件顶部追加一条记录。
2. 每条记录控制在 **8-12 行以内**，只写对下一个 AI 有用的信息。
3. 不写长日志、不贴完整 diff、不贴大段代码、不写 token/API key/密码。
4. 必须写清：改了哪些文件、当前状态、验证结果、下一步。
5. 用户要求每次修改文件后都要准备 H5 页面可安装的 OTA 烧录版：涉及设备固件/本地车端 H5/协议/Profile 的改动必须递增 `FOLLOWBOX_FIRMWARE_VERSION` 并运行 `python tools/package_ota.py`；纯云端/纯文档改动也要在 `OTA：` 行说明不需要设备 OTA 的原因。
6. 如果本次只是读取/分析、没有改文件，只有在形成重要架构结论、安全阻塞、关键排错结论或改变下一步路线时才追加“分析结论”；普通问答/例行查看不追加。
7. 新记录放在 `## 最新交接记录` 下面，旧记录往下排。
8. 如果某条记录已经过期，可以移动到 `## 已过期/归档记录`，不要让顶部堆太长。
9. 交接记录只记录“本次新增的信息”，不要重复抄写 `FIRMWARE-SPEC.md`、`PIN-MAP-V1.md`、`skills/README.md` 已经固定的长期事实。

## 推荐格式

```markdown
### YYYY-MM-DD HH:mm - <AI/工具名> - <任务短名>
- 改动：<1 句话概括>
- 文件：`path1`, `path2`, `path3`
- 架构影响：无 / 有，说明是否改模块边界、GPIO、安全链路、协议
- 安全影响：无 / 有，说明是否碰 motor/e-stop/GPIO/ADC/I2C/电源
- OTA：版本 `<version>`，`cloud/firmware/firmware.bin` 与 `manifest.json` 已生成；或说明不需要设备 OTA 的原因
- 验证：<build/test/check/log 结果；没有验证就写 未验证>
- 当前状态：PASS / NEEDS_VERIFICATION / BLOCKED / NEXT_TASK_READY
- 下一步：<给下一个 AI 的最短明确动作>
```

## 最新交接记录
### 2026-06-21 22:31 - Codex - TOF 有效性与刷新修复
- 结论：云端实时遥测确认设备运行 `debug.8`，`init_ok_mask=0x6`、`mux_nack=0`、CH0 持续 `sensor_nack 0x29 wire=2`；已知正常左传感器换到中间仍失败，故 CH0 断点在 TCA CH0 下游口/线束而非传感器本体或 H5。
- 改动：测距必须同时满足 VL53L1X `RangeValid` 与距离阈值；三路改为独立 stale 失效；运行时 I2C 读错误摘除通道并恢复；跳过未就绪通道，周期输出测距诊断；云端遥测由 1Hz 提到 4Hz。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.{h,cpp}`, `firmware/include/config/{cloud_config.h,ota_config.h}`, `cloud/firmware/{manifest.json,firmware.bin}`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块/GPIO/协议边界变化；TOF 仍只写快照，经既有融合与 safety 链使用。
- 安全影响：有，修复旧代码会让坏 `range_status` 或单路旧值保持 valid 的问题；新逻辑 fail-closed，不改 PWM/电机/急停，不执行实体动作。
- OTA：`2026.06.21-tof-debug.9` 已发布云端，size `1134288`，MD5 `698d14b1ec5e61fdc53594d90b432917`，`force=false`；未请求设备安装。
- 验证：PlatformIO 构建 PASS；公网 version API 显示 `current=debug.8`、`available=debug.9`；公网下载后二进制 size/MD5 与 manifest 完全一致。
- 当前状态：NEEDS_H5_INSTALL_AND_HARDWARE_VERIFICATION
- 下一步：用户保持车辆禁止运动，在 H5 手动安装 debug.9；用手前后移动目标比较 4Hz 云端数值和 `TOF range ch=1/2`；CH0 继续用逻辑分析仪/示波器确认 SC0/SD0 是否有 I2C 脉冲，不能仅凭通断与空载 3.3V 判定。

### 2026-06-21 20:18 - Codex - TOF 单路无显示与量程状态诊断
- 结论：云端实时遥测确认设备运行 `2026.06.21-tof-debug.7`，`init_ok_mask=0x6` 表示 CH1/左前与 CH2/右前均已初始化，只有 CH0/前中持续 `sensor_nack addr=0x29 wire=2`；H5 只有左前约 105mm 有效，说明右前是“已初始化但测距无效”，不是与 CH0 相同的 NACK 故障。
- 改动：TOF 每路首次测距以及节流后的无效测距日志增加 `channel`、`raw_mm`、VL53L1X `range_status`、样本/无效计数和距离阈值结果，用于区分 SignalFail/OutOfBoundsFail/HardwareFail/过近或超量程；不改变现有有效性和安全门控行为。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO/遥测协议变更；诊断仍由传感器模块写日志，TOF 失败继续保持 invalid。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；仅增加只读 TOF 测距状态日志，未授权或执行实体运动。
- OTA：版本 `2026.06.21-tof-debug.8` 已发布云端，size `1133984`，MD5 `489dc8ef15a9c612b4df984193835a3b`，`force=false`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；远端 size/MD5 与 manifest 校验一致；H5 API 显示 `current=debug.7`、`available=debug.8`、`update_available=true`。
- 当前状态：NEEDS_H5_INSTALL_AND_HARDWARE_VERIFICATION
- 下一步：用户在 H5 手动安装 debug.8；观察 `TOF range ch=1/2` 的 `raw/status`。断电后按 TCA 通道线束换位定位 CH0 NACK；若最右模块的 VIN/VCC-GND 确为 2.6V，先断开该模块并分别测空载线端与模块电阻，禁止带电换线。

### 2026-06-21 19:10 - Codex - TOF 下游逐通道诊断与恢复轮询
- 结论：云端实时遥测确认设备运行 `2026.06.20-tof-debug.6`，TCA 已响应（`mux_nack=0`），但三路 VL53L1X 均未初始化（`init_ok_mask=0`、`read_count=0`），故障点已下移到 TCA 通道后的传感器侧。
- 改动：每路选通后先探测 VL53L1X 默认地址 `0x29` 并读取 Model ID `0xEACC`，日志区分 `sensor_nack` / `sensor_bad_id` / `sensor_boot_fail`；运行期恢复改为 CH0/CH1/CH2 轮询，不再长期卡在 CH0。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/src/sensors/tof_vl53l1x_array.h`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`
- 架构影响：无模块边界/GPIO/遥测协议变更；TOF 仍是只读快照，失败时保持 invalid。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；仅 I2C 下游探测日志与失败通道重试顺序。
- OTA：版本 `2026.06.21-tof-debug.7` 已发布云端，size `1133360`，MD5 `8e6590f614da5856d097ac3df9e27c9e`，`force=false`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；远端 size/MD5 校验一致；H5 API 显示 `current=debug.6`、`available=debug.7`、`update_available=true`、OTA `idle`。
- 当前状态：NEEDS_H5_INSTALL_AND_HARDWARE_LOG
- 下一步：用户在 H5 手动安装 debug.7；重启后查看三路启动日志，按 `sensor_nack` 查通道供电/SDA/SCL，按 `sensor_bad_id` 核对模块型号，按 `sensor_boot_fail` 查电源稳定与 XSHUT/复位。

### 2026-06-21 15:48 - Codex - 手机 H5 紧凑布局与 AP 同步
- 改动：云端 H5、AP/局域网 H5 移动端总览改为 4 列紧凑宫格，恢复明确纵向滚动，底部导航只响应按钮区域；补齐 AP 静态目录缺失的 `/shared/helpers.js`。
- 文件：`cloud/public/style.css`, `cloud/public/deploy-version.txt`, `firmware/web/style.css`, `firmware/data/index.html`, `firmware/data/app.js`, `firmware/data/style.css`, `firmware/data/shared/helpers.js`
- 架构影响：无固件模块边界/协议/GPIO 变更；`firmware/data/` 已与 `firmware/web/` 前端副本同步。
- 安全影响：无 motor/e-stop/PWM/ADC/I2C/运动链路改动；H5 仍不直接设置 PWM、不清安全锁、不绕过安装向导。
- OTA：不需要应用固件 OTA；本次只改云端静态页面与 LittleFS/AP 静态资源，真机 AP 生效需执行 `pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs`。
- 验证：Playwright Chrome 手机视口截图 PASS：`cloud-mobile-final.png`, `data-mobile-final.png`；全页截图高度云端 `390x2403`、AP `390x2172`，确认页面可纵向滚动；`firmware/web` 与 `firmware/data` CSS/JS/helper 比对一致。
- 当前状态：NEEDS_DEPLOY_OR_UPLOADFS
- 下一步：部署云端静态文件；车端 AP/局域网页执行 uploadfs 后，用手机访问 `http://192.168.4.1/#sensors` 验证真实浏览器滚动。

### 2026-06-21 15:29 - Codex - H5 传感器页补全与滚动修复
- 改动：AP/局域网 H5 与云端 H5 传感器页新增总览、融合细节、电源/视频卡片，并恢复移动端纵向滚动；支持 `#sensors/#status/#settings` 直达页面。
- 文件：`firmware/web/index.html`, `firmware/web/app.js`, `firmware/web/style.css`, `cloud/public/index.html`, `cloud/public/app.js`, `cloud/public/style.css`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`
- 架构影响：无固件模块边界/协议/GPIO 变更；AP 页和局域网页仍共用 `firmware/web/`，`firmware/data/` 保持遗留副本未改。
- 安全影响：无 motor/e-stop/PWM/ADC/I2C/运动链路改动；H5 仍只显示遥测并发低速请求，不新增绕过安全链能力。
- OTA：版本 `2026.06.21-h5-sensors.1` 已生成应用固件包，size `1131904`，MD5 `d211210caa45b1eb89540808a71b6bf9`，`force=false`；注意当前应用 OTA 不更新 LittleFS 页面，真机车端 H5 仍需 `pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs`。
- 验证：`node --check firmware/web/app.js` PASS；`node --check cloud/public/app.js` PASS；`pio run -d firmware -e esp32-s3-devkitc-1 -t buildfs` PASS；`python tools/package_ota.py --notes ...` PASS；Playwright Chrome 截图 `output/playwright/{local,cloud}-{mobile,desktop}.png` PASS。
- 当前状态：NEEDS_DEPLOY_OR_UPLOADFS
- 下一步：部署云端静态文件与 manifest/bin；若要更新车端 AP/局域网页，执行真机 `uploadfs` 后访问 `http://192.168.4.1/#sensors` 验证全量传感器显示。

### 2026-06-20 23:18 - Codex - TOF debug.6 日志复核
- 结论：用户贴回 `2026.06.20-tof-debug.6` 日志，标准 GPIO10=SDA/GPIO11=SCL、反向 GPIO11=SDA/GPIO10=SCL、全地址 `0x08-0x77` 扫描均无 ACK。
- 改动：无固件/云端代码改动；仅复核日志与现有 TOF/I2C 实现，确认软件侧已排除 TCA 地址偏移、SDA/SCL 反接和总线卡低。
- 文件：`AI-HANDOFF-MEMORY.md`
- 架构影响：无；TOF 仍保持只读快照，失败时 invalid，不进入运动输出链路。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；下一步仅允许断电万用表检查 I2C 主干。
- OTA：不需要设备 OTA；本次不改固件、车端 H5、云端运行代码或协议。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；用户日志显示 `scan-all: no ACK devices` 与 `swap-check: no ACK`。
- 当前状态：BLOCKED_NEED_HARDWARE_EVIDENCE
- 下一步：断电测 TCA9548A VCC/GND、A0/A1/A2 是否接 GND、GPIO10/11 到 TCA SDA/SCL 通断、SDA/SCL 对 3V3 上拉约 4.7k；先只接 ESP32+TCA，确认扫描出现 `0x70` 后再逐路接 TOF。

### 2026-06-20 23:07 - Codex - TOF SDA/SCL 反接诊断
- 结论：用户新日志仍为 SDA/SCL 高电平但 `0x70-0x77` 全部 address NACK，断点在 TCA9548A 主干 ACK 前，尚未触达 VL53L1X。
- 改动：标准 GPIO10=SDA/GPIO11=SCL 扫描全 NACK 时，临时反向 GPIO11=SDA/GPIO10=SCL 扫一次并打印 `TOF swap-check`，随后恢复正常 I2C 配置；版本递增到 `2026.06.20-tof-debug.6`。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO 常量/协议变更；TOF 仍只读快照，反接扫描只做一次诊断，不作为正式运行配置。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；仅短暂重配 TOF I2C 引脚用于诊断，完成后恢复 GPIO10/11。
- OTA：版本 `2026.06.20-tof-debug.6` 已本地生成，size `1131904`，MD5 `9dd269b632a7464758250ed3d9a0b263`，`force=false`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools/package_ota.py --skip-build --notes "TOF SDA/SCL reversed-pin diagnostic scan"` PASS。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：安装新版后抓启动 15 秒日志；若 `TOF swap-check` 有 ACK，断电改线为 GPIO10=SDA/GPIO11=SCL；若反扫也无 ACK，查 TCA 3V3/GND、共地、模块方向、A0/A1/A2 和主干通断。

### 2026-06-20 22:50 - Codex - TOF I2C 扫描错误码细化
- 结论：新日志仍显示 `0x70-0x77` 全无 TCA9548A ACK，软件已进入硬件主干排故阶段，尚未触达 VL53L1X 测距。
- 改动：TOF 扫描日志增加 Wire 错误码统计、恢复前后 SDA/SCL 电平、首次全 I2C 总线 ACK 扫描；固件版本递增到 `2026.06.20-tof-debug.5`。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO/协议变更；TOF 仍只读快照，失败时保持 invalid。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；仅 I2C 诊断日志，运行期 Bus Clear 边界不变。
- OTA：版本 `2026.06.20-tof-debug.5` 已本地生成，size `1131312`，MD5 `646c43d99e213be186bfdaa9ba670c3f`，`force=false`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools/package_ota.py --skip-build --notes "TOF I2C scan error breakdown"` PASS。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：安装新版后抓启动 15 秒日志；若 `scan-all` 也无 ACK，断电量 TCA 3V3/GND、GPIO10/11、4.7k 上拉与 A0/A1/A2；若 `scan-all` 有 0x29，检查 TOF 是否绕过 TCA 直连主干。

### 2026-06-20 22:28 - Codex - TOF I2C 降速与恢复重扫
- 结论：用户日志仍为 `wire=2 mux_nack ch=0`、`read_count=0`，断点在 TCA9548A 主干 ACK/选通，尚未进入 VL53L1X 测距。
- 改动：TOF I2C 时钟从 400k 降到 100k；每次 Bus Clear 后重新扫描 `0x70-0x77` 并更新可用 TCA 地址。
- 文件：`firmware/include/config/profile_defaults.h`, `firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO/协议变更；TOF 仍只读快照，失败时保持 invalid。
- 安全影响：无 motor/e-stop/PWM/ADC/运动链路改动；仅 I2C 诊断与恢复策略调整。
- OTA：版本 `2026.06.20-tof-debug.4` 已本地生成，size `1130640`，MD5 `4e1fc5a2dc07328e470de836c3e5ade8`，`force=false`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools/package_ota.py --skip-build --notes "TOF I2C 100k and recovery rescan"` PASS。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：安装新版后抓启动起 15 秒日志，重点看 `TOF begin` 电平与 `TOF scan` 是否 ACK；若仍无 ACK，断电复核 TCA 3V3/GND、GPIO10/11、4.7k 上拉、A0/A1/A2 地址脚。

### 2026-06-20 22:05 - Codex - TOF TCA 地址自动识别
- 结论：用户新日志仍为 `wire=2 mux_nack ch=0`、`read_count=0`，断点仍在 TCA9548A 主地址/主干 I2C，尚未进入三只 VL53L1X。
- 改动：TOF 启动扫描 `0x70-0x77` 后自动采用第一个 ACK 的 TCA 地址；若无 ACK 继续按 `0x70` 并打印无 TCA。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO/协议变更；TOF 仍只读快照，不伪造距离。
- 安全影响：无 motor/e-stop/PWM/ADC 变更；仅 I2C 诊断/地址选择，Bus Clear 逻辑保持原边界。
- OTA：版本 `2026.06.20-tof-debug.3` 已本地生成，size `1130608`，MD5 `c090fe97e38cfb04bf70e42a7839ef32`，`force=false`。
- 验证：`pio run -d firmware` PASS；`python tools/package_ota.py --notes ...` PASS。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：安装新版后查看启动日志；若出现 `using detected TCA addr=0x7x` 则复测 TOF，若仍 `no TCA9548A ACK` 则断电量 TCA 3V3/GND、GPIO10/11 到 SDA/SCL、4.7k 上拉和 A0/A1/A2。

### 2026-06-20 21:31 - Codex - DevConsole路径修复与OTA发布
- 改动：修复 DevConsole 旧包误把 cloud 路径指到 `tools_local/cloud` 的问题，OTA 发布改为调用 `tools/package_ota.py` 并重打包 `FollowBox-DevConsole.exe`。
- 文件：`tools_local/dev-console.py`, `tools_local/FollowBox-DevConsole.exe`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件模块边界变更；本地工具链改为以项目根 `D:\car\Follow the box` 为源，并保持 manifest/bin/固件版本一致。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 运行逻辑改动；OTA `force=false`，不会自动触发设备安装。
- OTA：版本 `2026.06.20-tof-debug.2` 已生成并上传云端，size `1130496`，MD5 `9e4871a2cae7225622fc0b3e4f7924ab`，远端校验 PASS。
- 验证：`python tools/package_ota.py --notes ...` PASS；`python -m py_compile` PASS；新 exe `/api/git/status` 路径冒烟 PASS；SSH/SCP 上传与远端 manifest/bin 校验 PASS。
- 当前状态：NEEDS_H5_INSTALL
- 下一步：在 `https://www.boonai.cn/fb/` 点击检查更新，确认可用版本 `2026.06.20-tof-debug.2` 后手动授权安装。

### 2026-06-20 21:11 - Codex - Codex 插件配置启用
- 改动：按用户确认启用 GitHub 与 Product Design 插件，并保留现有 browser/chrome/pdf/documents/spreadsheets/presentations 插件。
- 文件：`C:\Users\陈雨\.codex\config.toml`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无 FollowBox 固件/云端/协议模块边界变更；仅调整 Codex 工作台能力入口。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C/电源改动。
- OTA：不需要设备 OTA；本次不改固件、车端 H5、云端运行代码或协议。
- 验证：TOML 解析 PASS；确认 `github@openai-curated-remote`、`product-design@openai-curated-remote`、browser/chrome 均 enabled；`python tools/check_ai_handoff.py` PASS。
- 当前状态：PASS
- 下一步：确认 `github@openai-curated-remote` 与 `product-design@openai-curated-remote` enabled 后重启/刷新 Codex 会话使用。

### 2026-06-20 21:07 - Codex - 固定每次改文件后产出 H5 OTA 版
- 改动：把“修改设备相关文件后必须递增固件版本并生成 H5 可安装 OTA 包”写入运行门禁和交接记忆模板。
- 文件：`AI-AGENT-RUNBOOK.md`, `AI-HANDOFF-MEMORY.md`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`, `cloud/firmware/firmware.bin`
- 架构影响：无运行链路变更；强化发布流程，H5 仍只显式检查/授权安装。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 逻辑改动；OTA `force=false`，不自动安装。
- OTA：版本 `2026.06.20-tof-debug.1`，`firmware.bin` 已生成，size `1130496`，MD5 `8125b9ce00f7825d7ab1da27fc836cfd`，`force=false`。
- 验证：`python tools/package_ota.py --notes ...` PASS；`node --check cloud/server.js` PASS；manifest/bin/version 一致性 PASS；`python tools/check_ai_handoff.py` PASS。
- 当前状态：NEEDS_DEPLOY_AND_H5_INSTALL
- 下一步：部署 `cloud/firmware/manifest.json` 与 `cloud/firmware/firmware.bin` 到服务器后，在 H5 点击“检查更新”再授权安装。

### 2026-06-20 20:44 - Codex - TOF 三路 NACK 排故日志增强
- 结论：用户日志 `init_attempt == mux_nack` 且 `read_count=0`，说明失败卡在 TCA9548A 通道选择阶段，尚未进入 VL53L1X 初始化/测距。
- 改动：TOF 启动时打印 SDA/SCL 电平与 TCA 0x70-0x77 地址扫描；初始化/重试失败打印通道、Wire 返回码、累计计数；TLM 补 `timeout/busclr/reinit`。
- 文件：`firmware/src/sensors/tof_vl53l1x_array.cpp`, `firmware/src/telemetry/telemetry_logger.cpp`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界变更；TOF 仍只读快照，失败不会伪造有效距离。
- 安全影响：无 motor/e-stop/PWM/GPIO 输出改动；涉及 I2C 诊断日志与现有 Bus Clear 计数，不改变运动链路。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools/check_ai_handoff.py` PASS。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：烧录后查看 `TOF scan` 是否有 0x70 ACK；若 `wire=2` 持续，优先查 TCA 地址/供电/主干 SDA/SCL，而非三路 VL53L1X。

### 2026-06-20 16:54 - Codex - 项目架构地图补齐
- 改动：新增 `CURRENT-PROJECT-ARCHITECTURE.md`，梳理 AP/局域网/云端 H5 入口、固件运行链路、云端链路、视频边界、同步矩阵和常见问题定位。
- 文件：`CURRENT-PROJECT-ARCHITECTURE.md`, `README.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：明确 `firmware/web/` 是本地 AP/STA H5 唯一源，`cloud/public/` 是云端页面源，`firmware/data/` 为遗留非权威副本。
- 安全影响：无代码/引脚/PWM/运动链路变更；文档重申 H5/云端不能绕过 safety/applyFinalGate。
- 验证：`python tools/check_ai_handoff.py` 通过。
- 当前状态：文档已写入，后续 H5 改动应按同步矩阵检查本地与云端页面。
- 下一步：可进一步清理或归档 `firmware/data/` 遗留副本，避免误改。

### 2026-06-20 16:35 - Codex - 云端遥控链路显示修复
- 改动：云端 H5 状态页新增 DS600 遥控与云端上报卡片，并修正链路状态按 5 秒遥测新鲜度显示；服务器 SSE 事件补 `commandAt` 用于命令下发时间。
- 文件：`cloud/public/index.html`, `cloud/public/app.js`, `cloud/public/deploy-version.txt`, `cloud/server.js`, `firmware/src/cloud/cloud_client.cpp`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无模块边界/GPIO/PWM 变更；云端仍原样转发固件 `state.rc/state.cloud`，固件云端客户端仅刷新同 seq 轮询的链路时间。
- 安全影响：无 motor/e-stop/PWM/ADC/I2C 改动；同 seq 不重复应用动作，deadman 过期仍等服务器返回新 stop seq 后走安全链停车。
- 验证：`node --check cloud/server.js cloud/public/app.js` PASS；`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools\check_ai_handoff.py` PASS；`git diff --check` 仅 CRLF 提示。
- 当前状态：NEEDS_DEPLOY_AND_HARDWARE_VERIFY
- 下一步：部署云端静态/后端后，打开云端状态页确认 `DS600 遥控` 在线/离线与 `云端上报 seq` 随设备上报变化。

### 2026-06-20 18:24 - Codex - JY62固件链路复核补入图一
- 改动：在 `ASSEMBLY-WIRING-MINDMAP.html` 的雷达复核块后新增 JY62/JY61P IMU 固件链路统一复核。
- 文件：`ASSEMBLY-WIRING-MINDMAP.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码变更；仅把 GPIO42/UART0→SensorTask→Jy61pImu→SystemState.imu→H5/云端 state 的检查路径写入装配图。
- 安全影响：有文档/装配安全影响；强调保持急停、禁止电机使能，TX 高于 3.3V 未转换或 GPIO42 误接时立即断电。
- 验证：HTML 脚本语法检查 PASS；关键词检索确认 JY62 复核块存在；`git diff --check` 仅 CRLF 警告。
- 当前状态：NEXT_TASK_READY
- 下一步：现场按图一确认 JY62 TX→GPIO42 电平转换、共地、9600 8N1/0x55帧后，再启用 `UART_NUM_IMU=0` 做实测。

### 2026-06-20 18:05 - Codex - JY62 姿态遥测接入 H5/云端
- 结论：JY62 未显示的主因是 `/ws/state` 未输出 `imu` 节点，云端后端只原样转发 state；同时 `UART_NUM_IMU=-1` 仍让 IMU 串口默认禁用。
- 改动：`buildStateJson()` 增加只读 `imu` 字段，本地 H5 与云端 H5 传感器页新增 JY62 姿态卡，协议和烟测断言同步更新。
- 文件：`firmware/src/web/telemetry_api.cpp`, `firmware/{web,data}/{index.html,app.js}`, `cloud/public/{index.html,app.js}`, `protocols/H5-API.md`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有协议字段扩展；不改模块边界，不启用 IMU UART，不改变运动链路。
- 安全影响：无 motor/e-stop/PWM/ADC/I2C/GPIO 使能改动；`FOLLOW_YAW_DAMP_GAIN=0`，IMU 仍只读诊断，不绕过 `safety_manager`。
- 验证：`node --check` 通过云端/本地/打包 H5 JS；`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`git diff --check` 仅 CRLF 警告。
- 验证补充：Windows `g++` 主机烟测编译返回 1 但 stdout/stderr 为空，未得到可用逻辑烟测结果。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：若要看到实时姿态，确认 JY62 TX→GPIO42 已做 3.3V 电平转换且共地，再把 `UART_NUM_IMU` 从 `-1` 改为 `0` 并按实物波特率校准。

### 2026-06-20 16:06 - Codex - 雷达RX0硬件链路复核写入图一
- 结论：线上 `boonai.cn/fb` 已运行 `2026.06.20-lidar-s2.1`，H5 雷达仍显示 `RX 0 / 包 0 / 圈 0`，优先判定为雷达 UART 无字节输入而非云端部署失败。
- 改动：在 `ASSEMBLY-WIRING-MINDMAP.html` 新增“激光雷达 UART 链路统一复核”，写明 GPIO3/GPIO43、150000 8N1、断电逐线、低能量上电和 H5 分流判断。
- 文件：`ASSEMBLY-WIRING-MINDMAP.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码变更；仅把现场排查步骤写入装配图一，便于后续统一查线。
- 安全影响：有文档/装配安全影响；要求保持急停、禁止电机使能，TX 电平超 3.3V 或接线不确定时立即断电。
- 验证：关键词检索确认图一新增块存在；`git diff --check` 无空白错误（仅CRLF提示）；`python tools\check_ai_handoff.py` PASS；未做实物测量。
- 当前状态：NEXT_TASK_READY
- 下一步：按图一用万用表/USB-TTL/逻辑分析仪确认雷达 TX 是否进入 ESP32 GPIO3；若 H5 RX 增长但包仍为0，再核对波特率和实物 S2 型号。

### 2026-06-20 15:36 - Codex - EAI S2 雷达波特率与 OTA 发布
- 改动：按 `zhiliao/EaiLidarTest-V1.12.3-20241220/config/config.json` 将 S2-YJ/S2-YD 默认雷达 UART 从 115200 改为 150000，并递增 OTA 版本。
- 文件：`firmware/include/config/profile_defaults.h`, `firmware/include/config/board_pins.h`, `profiles/example_bldc_analog_36v.yaml`, `firmware/include/config/ota_config.h`, `cloud/firmware/manifest.json`
- 架构影响：无模块边界变化；雷达仍由 `SensorTask` 喂 `LidarEaiS2`，只产出只读 `ObstacleSnapshot`/H5 诊断。
- 安全影响：无 motor/e-stop/PWM/ADC/I2C 改动；雷达只影响避障输入，不绕过 `safety_manager`，OTA `force=false` 不自动安装。
- OTA：版本 `2026.06.20-lidar-s2.1`，`firmware.bin` 已复制到 `cloud/firmware/`，manifest size `1129024`、MD5 `cf8dd232319afbd841bde2e80b30dd70`。
- 验证：`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`python tools\package_ota.py --skip-build ...` PASS。
- 验证补充：host `logic_smoke_test.exe` 当前失败在既有 `SafetyManager` UWB_LOST 断言（line 100），与本次雷达 UART 改动无直接关系，未假装通过。
- 当前状态：NEEDS_HARDWARE_VERIFICATION
- 下一步：云端部署 manifest/bin 后在 H5 点击检查更新并授权安装；安装后看雷达面板 RX 是否增长，若 RX>0 但包=0，再按 S2 实物型号核对 protoVersion/协议。

### 2026-06-20 15:10 - Codex - Docker Desktop Kubernetes 启用
- 改动：通过 Docker Desktop 设置文件启用 Kubernetes kind 模式，并将 Docker daemon `max-concurrent-downloads` 设为 1 以缓解镜像层 EOF。
- 文件：环境配置 `AppData\Roaming\Docker\settings-store.json`, `%USERPROFILE%\.docker\daemon.json`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无仓库代码/固件/云端 API/H5 边界变更；仅补本机 DevSpace/Kubernetes 运行环境。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；未触发 OTA 安装、固件烧录或硬件动作。
- 验证：`docker desktop kubernetes status` running；`kubectl config current-context` = `docker-desktop`；`kubectl get nodes` Ready；`devspace run doctor` PASS。
- 验证补充：Docker Hub 直连拉取 `envoyproxy/envoy:v1.36.4` 多次 EOF，使用 `docker.1ms.run` 代理补齐 Envoy 和 `desktop-cloud-provider-kind` 后集群启动成功。
- 当前状态：PASS
- 下一步：可继续运行 `devspace dev`；若镜像拉取再次 EOF，优先检查 Docker Desktop 代理/网络或预拉依赖镜像。

### 2026-06-20 14:35 - Codex - Docker/DevSpace/K8s 健康检查补齐
- 改动：云服务新增 `/api/health`，Dockerfile 改为非 root 运行并加入 HEALTHCHECK，K8s 探针与 DevSpace 检查统一到健康接口。
- 文件：`cloud/server.js`, `cloud/Dockerfile`, `k8s/followbox-cloud.yaml`, `devspace.yaml`, `DEVSPACE-AI-WORKFLOW.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件/运动/协议链路变更；只完善 `cloud/` 容器化和 DevSpace/Kubernetes 前置检查。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；未触发 OTA 安装或硬件动作。
- 验证：`node --check cloud\server.js` PASS；PyYAML 解析 `devspace.yaml`/K8s manifest PASS；`docker build -t followbox-cloud:local-test cloud` PASS；容器 `/api/health` 200 且 Docker HEALTHCHECK=healthy。
- 验证补充：`devspace run doctor` 已确认 Docker daemon 可用，但停在 `kubectl config current-context`，因为 Kubernetes context 尚未设置。
- 当前状态：NEEDS_K8S_CONTEXT
- 下一步：在 Docker Desktop 启用 Kubernetes，或导入 kubeconfig 后确认 `kubectl config current-context`，再运行 `devspace run doctor` 和 `devspace dev`。

### 2026-06-20 13:06 - Codex - DevSpace/kubectl/Docker 工具链安装
- 改动：安装 DevSpace v6.3.21 到 `C:\Users\陈雨\bin`，winget 安装 kubectl v1.36.2 与 Docker Desktop 4.78.0，并把 kubectl/Docker CLI 路径补入用户 PATH。
- 文件：`AI-HANDOFF-MEMORY.md`；环境：`devspace.exe`, `kubectl.exe`, Docker Desktop。
- 架构影响：无仓库代码/固件/云端/H5 边界变更；仅补本机 DevSpace/Kubernetes/Docker 工具链。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；未触发部署、OTA 安装或硬件动作。
- 验证：`devspace --version` PASS；`kubectl version --client` PASS；`devspace run plan` PASS；`devspace run cloud-check` 因无 Kubernetes current-context/daemon 不可用而失败；Docker CLI 已安装但 Docker Desktop daemon 报 unable to start。
- 当前状态：NEEDS_REBOOT_OR_K8S_CONTEXT
- 下一步：重启 Windows 后启动 Docker Desktop，启用/选择 Kubernetes context，再运行 `kubectl config current-context` 和 `devspace run cloud-check`。

### 2026-06-20 12:22 - Codex - DevSpace 命令补齐与 OTA 打包脚本
- 改动：新增 `tools/package_ota.py`，用于构建固件、复制 `firmware.bin` 到 `cloud/firmware/`、写入并校验 manifest；`devspace.yaml` 新增 `cloud-check`、`cloud-logs`、`package-ota`、`release-check`。
- 文件：`tools/package_ota.py`, `devspace.yaml`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件运行链路/云端 API/H5 权限变更；新增 DevSpace 命令入口和本地 OTA 发布打包脚本。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；脚本只发布固件文件与 manifest，不触发设备安装。
- 验证：`python -m py_compile tools\package_ota.py` PASS；`python tools\package_ota.py --help` PASS；PyYAML 解析 `devspace.yaml` PASS；AI handoff 门禁 PASS。
- 当前状态：NEEDS_TOOLCHAIN_SETUP
- 下一步：本机安装 DevSpace/kubectl 后运行 `devspace run cloud-check`、`devspace run package-ota`；当前只检测到 `pio` 和 `curl`。

### 2026-06-20 12:19 - Codex - Python 控制台状态面板补齐
- 改动：`dev-console.py` 的 `git/status` 新增当前分支、upstream ahead/behind、本地 dirty 文件、固件版本、本地 manifest 摘要和云端 URL 可达性；`dev-console.html` 同步展示。
- 文件：`tools_local/dev-console.py`, `tools_local/dev-console.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件/云端/H5 协议边界变更；只增强本机控制台只读状态面板。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；状态检查不执行 fetch/pull/push/deploy/OTA 安装。
- 验证：`python -m py_compile tools_local\dev-console.py` PASS；直接调用 `api_git_status()` 返回 branch/ahead/behind/dirty/version/manifest/cloud 字段；AI handoff 门禁 PASS。
- 当前状态：PASS
- 下一步：按 Phase 2 开始添加 DevSpace `cloud-check` / `cloud-logs` / `package-ota` 命令，或把 PowerShell 状态面板补到同等字段。

### 2026-06-20 12:16 - Codex - 控制台 pull 策略统一为显式处理
- 改动：`dev-console.py` 与 PowerShell 控制中心取消 pull 前自动 stash、分叉自动 rebase；统一为脏工作区拒绝、仅允许 ff-only，同步更新 Python 控制台确认文案。
- 文件：`tools_local/dev-console.py`, `tools_local/dev-console.html`, `tools_local/followbox-control-center.ps1`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件/云端/H5 协议边界变更；只调整本机控制台 Git 操作策略。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；降低本地改动被隐式改写或 stash/rebase 冲突的风险。
- 验证：`python -m py_compile tools_local\dev-console.py` PASS；PowerShell `PSParser` 解析 PASS；直接调用 `safe_pull_repo()` 在脏工作区返回 `dirty-blocked`；危险命令扫描无残留；AI handoff 门禁 PASS。
- 当前状态：PASS
- 下一步：继续 Phase 1 状态面板增强：显示当前分支、dirty 文件、ahead/behind、固件版本、manifest 版本、云端可达性。

### 2026-06-20 12:10 - Codex - CMD 控制中心安全 Git 同步
- 改动：把 `tools_local/start-followbox-control-center.cmd` 的 `pull` 从 `reset --hard origin/master` 改为当前分支 `fetch --prune` + `merge --ff-only`，并让 push 使用当前分支。
- 文件：`tools_local/start-followbox-control-center.cmd`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件/云端/H5 模块边界变更；仅收敛本机控制台 Git 工作流。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；脏工作区会拒绝 pull，不再丢弃本地文件。
- 验证：`cmd /c tools_local\start-followbox-control-center.cmd pull` 在脏工作区按预期拒绝；`python tools/check_ai_handoff.py` PASS；目标文件 `git diff --check` PASS；残留 `reset --hard` / `origin/master` 扫描无匹配。
- 当前状态：PASS
- 下一步：继续 Phase 1，统一 Python/PowerShell 控制台 pull 策略为“显式 stash/commit 后再拉取”。

### 2026-06-20 12:05 - Codex - DevSpace/OTA/本地控制台流程方案落文档
- 改动：扩展 `DEVSPACE-AI-WORKFLOW.md`，把宿主机/VM/Git/cloud/OTA/tools_local 的目标流程、角色分工、迁移阶段和后续 Codex 实施规则写成正式方案。
- 文件：`DEVSPACE-AI-WORKFLOW.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件模块边界变更；新增开发流程路线，明确 DevSpace 管 cloud/H5，PlatformIO 管固件，`tools_local` 收敛为薄控制台。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；方案继续要求 OTA 安装显式授权，硬件验证本地安全门控。
- 验证：未运行构建；本次仅文档方案更新。
- 当前状态：NEXT_TASK_READY
- 下一步：优先实现 Phase 1：把 `tools_local` 的破坏性 pull 改成安全同步，再补 DevSpace `package-ota` / `cloud-check` 命令。

### 2026-06-20 09:35 - Codex - DevSpace 与 GPT/Codex 工作流配置
- 改动：新增 DevSpace 云控开发配置，并把 GPT 规划、Codex 实操、DevSpace 云端开发边界写入仓库规则。
- 文件：`devspace.yaml`, `cloud/Dockerfile`, `cloud/.dockerignore`, `k8s/followbox-cloud.yaml`, `DEVSPACE-AI-WORKFLOW.md`, `.gitignore`, `AGENTS.md`, `AI-HANDOFF-MEMORY.md`。
- 架构影响：开发工作流新增 Kubernetes/DevSpace 入口；未改固件模块边界、GPIO、协议或 H5 嵌入路径。
- 安全影响：无 motor/e-stop/PWM/GPIO/ADC/I2C 改动；明确 DevSpace 不承载烧录、串口监控或真机安全验证。
- 验证：YAML 解析、`node --check cloud/server.js`、`npm install --package-lock-only --dry-run`、`git diff --check`、AI handoff 门禁均 PASS；当前机器未发现 `devspace`、`kubectl`、`docker` 命令，PlatformIO `pio` 可用。
- 当前状态：NEEDS_TOOLCHAIN_SETUP。
- 下一步：安装 DevSpace/Docker/kubectl 后运行 `devspace dev`；固件侧继续用 `devspace run-pipeline firmware-check` 或本机 `pio run` 验证。

### 2026-06-20 00:20 - Codex - OTA 正式发布规范落库
- 改动：新增当前实现的 OTA 发布规范，并把它加入 README 权威文档入口。
- 文件：`OTA-UPDATE-SPEC.md`, `README.md`, `AI-HANDOFF-MEMORY.md`。
- 覆盖：版本递增、PlatformIO 构建、`firmware.bin` 同步、manifest size/MD5 校验、云端部署和 H5 验收。
- 关键坑：`*.bin` 被 Git 忽略，不能只靠 pull；云端 H5 只负责检查/授权安装，不提供固件上传入口。
- 能力边界：当前 OTA 只写应用分区；`firmware/web` 的 LittleFS 变化不能随应用 `firmware.bin` 更新。
- 架构影响：无；未修改 OTA 协议、服务端路由或设备行为。
- 安全影响：无实体操作；规范明确发布不等于安装，安装必须显式授权并保持安全停车。
- 验证：发布包一致性命令实跑 PASS；Node 语法、必需章节、`git diff --check`、AI handoff 门禁均 PASS。
- 当前状态：PASS。

### 2026-06-18 23:37 - Codex - TOF 云端/本地 H5 无数据根因定位与诊断恢复修复
- 结论：boonai.cn/fb 实时遥测确认 ESP32 已发送 `state.tof`，但 `init_ok_mask=000`、`read_count=0`，断点在三路 TOF 初始化而非云端/AP/局域网传输。
- 改动：区分 TCA9548A 选择 NACK 与 VL53L1X 初始化失败，新增初始化尝试/失败计数并上送云端 H5。
- 恢复：运行期连续 MUX NACK 复用既有受限 Bus Clear；传感器级初始化失败仅重试，不误清正常总线。
- 日志：周期 TLM 增加 TOF mask/read/init/NACK 摘要，后续无需只靠页面猜测。
- 文件：`tof_vl53l1x_array.*`, `types.h`, `sensor_task.cpp`, `telemetry_api.cpp`, `telemetry_logger.cpp`, `cloud/public/app.js`, `firmware/web/*`, `H5-API.md`, smoke test。
- 架构/安全：只改传感器只读快照与诊断；未改 GPIO、PWM、运动许可、`applyFinalGate()` 或驱动出口。
- 验证：ESP32-S3 PlatformIO 完整构建 PASS（RAM 22.4%、Flash 23.9%）；Node JS 语法与 `git diff --check` PASS。
- 未验证：本机无 host `g++`，纯逻辑 smoke 未执行；未烧录、未部署云端、未验证实际 I2C 电气链路。
- 当前状态：NEEDS_HARDWARE_VERIFICATION；允许 L3 传感器只读验证，不授权任何电机动作。
- 下一步：部署 H5并刷入固件后看“尝试/传感器失败/NACK”；据计数检查 3.3V、共地、4.7k 上拉、TCA 地址和 CH0/1/2。

### 2026-06-18 22:05 - Codex - 控制中心迁移到 tools_local
- 改动：把本机专用控制中心文件从 `tools/` 挪到忽略目录 `tools_local/`，仓库内只保留一个薄启动壳 `tools/start-followbox-control-center.cmd` 指向本地目录。
- 文件：`.gitignore`, `tools/start-followbox-control-center.cmd`, `tools_local/*`, `AI-HANDOFF-MEMORY.md`
- 架构影响：共享工具和本机私有工具拆分；`tools/` 继续保留团队脚本，控制中心运行资产不再作为仓库内容同步。
- 安全影响：无 motor/e-stop/PWM/GPIO 改动；只是本机工具位置和 Git 跟踪边界调整。
- 验证：旧入口 `tools/start-followbox-control-center.cmd` 启动后，本地 `http://127.0.0.1:7791/api/git/status` PASS；状态里已只显示 `.gitignore`、`tools/start...` 和 `tools/` 下控制中心文件删除。
- 未验证：尚未把“从仓库移除这些 tools 文件”的变更再次提交并推送；远端当前仍保留旧 `tools/dev-console*` 与 `followbox-control-center*`。
- 当前状态：NEEDS_REPO_SYNC。
- 下一步：把 `.gitignore`、`tools/start...` 以及 `tools/` 下这些删除提交到远端，之后新 clone / pull 就不会再带这批本机专用文件。

### 2026-06-18 21:45 - Codex - 控制中心启动链与安全同步修复
- 改动：`start-followbox-control-center.cmd` 改为只负责启动，不再启动前自动 `git push`；优先拉起 Python Dev Console，EXE 与 PowerShell 仅作后备。
- 文件：`tools/start-followbox-control-center.cmd`, `tools/dev-console.py`, `tools/dev-console.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：开发控制台主启动链从不可运行的 PowerShell `HttpListener` 后端切到可运行的 Python `http.server` 后端；未改固件模块边界、GPIO 或运动链路。
- 安全影响：无 motor/e-stop/PWM/GPIO 改动；Git `pull` 新增自动 stash + `--ff-only` + stash pop 流程，避免脏工作区被直接覆盖；云端/仓库动作改为显式确认，不再隐式执行。
- 验证：`python -m py_compile tools/dev-console.py` PASS；批处理启动 `tools/start-followbox-control-center.cmd` 后本地 `http://127.0.0.1:7788/api/git/status` PASS；临时 Git 仓库验证 `safe_pull_repo()` 在存在未跟踪文件时可 stash/pull/pop 并保留本地文件 PASS。
- 未验证：未在真实 GitHub/云端服务器执行 push/pull/deploy；`tools/followbox-control-center.ps1` 仍保留为 legacy fallback，当前环境 `HttpListener` 构造失败未修。
- 当前状态：NEEDS_REMOTE_VERIFICATION。
- 下一步：如需真正同步，先批准联网/SSH 操作，再分别执行仓库 pull/push 与云端 deploy，最后检查 `https://www.boonai.cn/fb/` 和远端目录版本戳。

### 2026-06-18 00:55 - Codex - WSL PlatformIO 工具链引导脚本
- 改动：新增 WSL/Ubuntu 下的 PlatformIO 工具链脚本，统一安装依赖、隔离 Linux `.pio-core-wsl` 缓存，并提供 `install/env/run/smoke` 子命令覆盖 `firmware` 与 `vision_cam` 两个工程。
- 文件：`tools/followbox-wsl-toolchain.sh`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无业务模块边界/GPIO/安全链路变更；仅新增开发环境脚本，避免 WSL 与 Windows 共用同一 PlatformIO home。
- 安全影响：无 motor/e-stop/PWM/GPIO 改动；脚本只安装工具链并提示 WSL2 + `usbipd` 的 USB 烧录前置条件。
- 验证：Git Bash `bash -n tools/followbox-wsl-toolchain.sh` PASS；`bash tools/followbox-wsl-toolchain.sh --help` PASS。
- 未验证：当前机器仍无可用 Ubuntu 发行版，未在真实 WSL 中执行 `install` / `smoke`，未跑 `pio run`、`buildfs` 或 USB/OTA 烧录。
- 当前状态：NEXT_TASK_READY。
- 下一步：管理员修好 WSL/Ubuntu 后，在 Ubuntu 里执行 `bash tools/followbox-wsl-toolchain.sh install`，再用 `run firmware ...` / `run vision_cam ...` 继续构建与烧录。

### 2026-06-17 22:40 - Codex - 云端缓存与传感器通道遥测修复
- 改动：云端首页给 JS/CSS 注入部署版本防旧资源缓存；`/ws/state` 补 UWB/障碍/TOF/超声 `last_update_ms` 与 TOF/超声单路 valid；H5 按通道显示“有效/部分/无效”。
- 文件：`cloud/server.js`, `cloud/public/index.html`, `cloud/public/app.js`, `cloud/deploy-clean-cache.sh`, `firmware/src/web/telemetry_api.cpp`, `firmware/src/cloud/cloud_client.cpp`, `firmware/data/app.js`, `firmware/web/app.js`, `protocols/H5-API.md`
- 架构影响：协议向后兼容扩展字段；云端静态资源版本化；未改变传感器驱动、融合算法、控制模式或模块边界。
- 安全影响：无 PWM/GPIO/急停/安全门控变更；H5 仍只显示遥测或发既有低速请求。
- 验证：Node `--check` 通过 `cloud/server.js`, `cloud/public/app.js`, `firmware/data/app.js`, `firmware/web/app.js`；本地云端首页版本注入 PASS；`buildStateJson` snprintf 占位符/参数计数 PASS。
- 未验证：本机无 PlatformIO，WSL 未安装发行版，未跑 `pio run`/`buildfs`/真机传感器。
- 当前状态：NEEDS_HARDWARE_VERIFICATION。
- 下一步：在 PlatformIO 环境跑固件构建和 LittleFS，再部署云端并打开 `/fb/deploy-version.txt` 确认时间戳更新；真机看 raw JSON 的 `*_valid` 与 `last_update_ms` 判断 UWB/TOF/LiDAR 是无数据、过期还是硬件链路问题。

### 2026-06-17 20:06 - Codex - H5 遥测显示与日志兜底修复
- 改动：修复云端/本地 H5 HTML 入口结构，补 AP/LAN 本地 `/api/state` HTTP 状态兜底和 `/api/logs` 只读日志接口。
- 文件：`cloud/public/index.html`, `firmware/web/index.html`, `firmware/web/app.js`, `firmware/src/web/h5_web_server.cpp`, `firmware/src/telemetry/debug_console.*`
- 架构影响：无运动链路变更；新增只读诊断 API，`/ws/state` 仍为主遥测通道，HTTP 仅兜底。
- 安全影响：无 PWM/GPIO/急停/安全门控变更；H5 仍不能绕过 `safety_manager/applyFinalGate`。
- 验证：Node REPL 解析 `cloud/public/app.js` 与 `firmware/web/app.js` PASS；HTML 关键 id 检查 PASS。
- 未验证：本机 PATH 无 `pio`/`node`，未完成 `pio run` 和 `buildfs`；需在 PlatformIO 环境复跑。
- 当前状态：NEEDS_VERIFICATION。
- 下一步：安装/打开 PlatformIO 后运行 `pio run -e esp32-s3-devkitc-1` 与 `pio run -e esp32-s3-devkitc-1 -t buildfs`，再真机访问 AP 和云端页面确认遥测/日志。

### 2026-06-14 - Codex - 云端 H5 布局优化已部署
- 改动：优化云端 H5 为宽屏控制台布局，遥测页增加最后上传/云端指令状态，遥控页拆分视频与低速点动面板，并修复摄像头离线破图和摇杆居中算法。
- 文件：`cloud/public/index.html`, `cloud/public/style.css`, `cloud/public/app.js`, `cloud/public/deploy-version.txt`, `test/05-云端部署文档.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无后端协议变更；仍使用 Node 8080 + SSE/REST + PM2 `followbox-cloud`。
- 安全影响：无新增运动权限；云端仍只发 deadman 低速点动和 `safe_idle`，本地 safety chain 最终裁决。
- 验证：`node --check cloud/public/app.js` PASS；`node --check cloud/server.js` PASS；本地 Edge 截图验证桌面/390px 手机遥测和遥控页无横向溢出；线上 `/fb/`, `/fb/style.css`, `/fb/app.js`, `/fb/deploy-version.txt` 均 200。
- 部署：使用 `C:\Users\陈雨\Downloads\codex.pem` + ssh-rsa 兼容参数上传到 `/www/wwwroot/followbox-cloud/`，PM2 新建/保存 `followbox-cloud`，状态 online。
- 当前状态：PASS。
- 下一步：手机打开 `https://www.boonai.cn/fb/` 检查实机视觉；真机上线后验证 SSE 遥测、摄像头流地址和云端 deadman 点动。

### 2026-06-13 21:12 - Codex - ESP32-S3-CAM OV5640 独立固件
- 改动：新增 `vision_cam` PlatformIO 工程，ESP32-S3-N16R8 CAM + OV5640 以 STA 加入 `FollowBox` AP，静态 `192.168.4.2`，提供 `/stream` MJPEG、`/capture`、`/status`。
- 文件：`vision_cam/platformio.ini`, `vision_cam/src/main.cpp`, `vision_cam/README.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无主控业务变更；摄像头仍是独立视频模块，主控只通过既有 telemetry `camera.stream_url` 让 H5 拉流。
- 安全影响：无；未接入 safety/motion/PWM/UART，视频断流不影响 `safety_manager` 或电机门控。
- 验证：`pio run -d vision_cam` PASS；SDK 已内置 `esp32-camera`，不依赖外部 Git 包。
- 当前状态：NEEDS_HARDWARE_VERIFICATION。
- 下一步：USB 烧录摄像头板，先串口确认 `Camera ready` 和 `WiFi connected`，再用浏览器访问 `http://192.168.4.2:81/stream`；若黑屏，优先核对板载摄像头 pin map 和 5V 供电。

### 2026-06-13 09:30 - Codex - H5 分区遥控控制台重构
- 改动：车端 H5 从一页到底改为底部导航四区：驾驶、传感器、状态、设置；驾驶页把低速摇杆覆盖到视频画面右下角。
- 文件：`firmware/web/index.html`, `firmware/web/app.js`, `firmware/web/style.css`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无后端协议变更；仅使用既有 `/ws/state`, `/api/jog`, `/api/mode-request`, reset/calibrate/wizard/wifi API。
- 安全影响：无新增 PWM/急停绕过能力；H5 仍只发低速 jog 和模式请求，安全停/门控仍由固件安全链执行。
- 验证：`node --check firmware\web\app.js` PASS；`pio run -e esp32-s3-devkitc-1 -t buildfs` PASS；`pio run -e esp32-s3-devkitc-1` PASS；COM18 `uploadfs` PASS 且 hash verified。
- 视觉验证：本机 HTTP 预览 + 内置浏览器手机视口检查 PASS；摇杆在视频内，模式按钮不被底部导航遮挡，UWB/雷达/TOF/超声页可见且画布非空。
- 当前状态：PASS。
- 下一步：真机连接 `FollowBox` 后用手机确认视频 URL、摇杆方向、UWB/TOF/雷达实时字段。

### 2026-06-13 08:55 - Codex - N32R16V 真机 USB 首烧
- 改动：按用户要求对 `COM18` CP210x 直连 ESP32-S3-DevKitC-1-N32R16V 执行 USB 首烧；先整片擦除，再烧固件，再烧 LittleFS。
- 文件：无代码变更；使用当前 `firmware/platformio.ini` 的 `esp32-s3-devkitc-1` 环境。
- 架构影响：无。
- 安全影响：固件烧录会启动真实控制程序；本轮未验证车轮架空/驱动电源/急停物理状态。
- 验证：`pio run -e esp32-s3-devkitc-1 -t erase --upload-port COM18` PASS；`upload` PASS；`uploadfs` 后台完成，`esptool.py verify_flash --flash_size 32MB 0x910000 littlefs.bin` PASS digest matched。
- 串口：擦除/烧录识别 `ESP32-S3 rev v0.2`、`Embedded PSRAM 16MB (AP_1v8)`；启动日志不再出现 `Detected size(512k)`/assert 重启。
- 当前状态：NEEDS_FIELD_CHECK；电脑 WLAN 为 `Software Off`，无法从本机扫描 `FollowBox` SoftAP；应用日志可能走 USB CDC，CP210x 仅看到 ROM/bootloader。
- 下一步：用手机或开启电脑 WLAN 搜索 `FollowBox`，连接后访问 `http://192.168.4.1/`；现场确认急停、默认 PWM/使能/刹车安全态。

### 2026-06-13 08:36 - Codex - ESP32-S3 N32R16V 首烧 WiFi 不出现修复
- 改动：默认 PlatformIO 环境改为 ESP32-S3-DevKitC-1-N32R16V：`opi_opi`、OPI boot、32MB flash、16MB PSRAM；新增 32MB OTA+LittleFS 分区；旧 N8 配置改为显式备用环境；弃用会禁用 OPI 检测的旧脚本。
- 文件：`firmware/platformio.ini`, `firmware/partitions/ota_32MB.csv`, `firmware/patch_sdkconfig.py`, `firmware/README.md`
- 架构影响：无业务模块边界变化；只改烧录/存储布局，WiFi/H5 代码不变。
- 安全影响：无新增运动权限；未触碰 GPIO、PWM、急停、安全门控。
- 验证：`pio run -e esp32-s3-devkitc-1` PASS；`pio run -e esp32-s3-devkitc-1 -t buildfs` PASS；`pio run -e esp32-s3-devkitc-1-n8` PASS；`esptool.py image_info --version 2 firmware.bin` 显示 `Flash size: 32MB`, `Flash mode: DOUT`, checksum/hash valid；`node --check firmware/web/app.js` PASS。
- 当前状态：NEEDS_VERIFICATION（未真机 erase/upload/uploadfs；用户日志表明旧固件在 flash probe 阶段崩溃，WiFi 尚未启动）。
- 下一步：对 N32R16V 先执行 `pio run -d firmware -e esp32-s3-devkitc-1 -t erase`，再 USB `upload` 和 `uploadfs`，串口确认不再出现 `Detected size(512k)` 后查找 `FollowBox` SoftAP。

### 2026-06-13 - Codex - H5 视频流接入与云端部署包
- 改动：固件遥测 `camera` 增加 `stream_url`；车端 H5 增加 MJPEG 视频卡片；云端 H5 自动从 SSE 遥测填充摄像头地址并修复 TOF/超声 DOM 字段未注册导致渲染中断的问题。
- 文件：`firmware/include/config/camera_config.h`, `firmware/src/web/telemetry_api.cpp`, `firmware/web/{index.html,app.js,style.css}`, `cloud/public/{index.html,app.js,deploy-version.txt}`, `H5-VIDEO-WIRING-SOLUTION.md`
- 架构影响：有；视频仍由 ESP32-S3-CAM 独立推流，主控只下发 URL，不承载/转发视频，不把视频作为安全输入。
- 安全影响：无新增运动权限；H5 仍只发低速点动/安全停，摄像头断流不影响 `safety_manager` 或 PWM 输出。
- 验证：`node --check firmware/web/app.js cloud/public/app.js cloud/server.js` PASS；`pio run -e esp32-s3-devkitc-1` PASS；`pio run -e esp32-s3-devkitc-1 -t buildfs` PASS；本地云端 smoke `/`, `/app.js`, `/firmware/version`, `/ingest` PASS。
- 当前状态：NEEDS_DEPLOY；云端包已生成 `output/followbox-cloud-video-20260613-004728.zip`，但 SSH 到 `82.156.85.60:51400/22` 均在 KEX 前被远端关闭，HTTPS 当前也握手失败，未能上传服务器。
- 下一步：恢复 SSH/宝塔入口后上传该包到 `/www/wwwroot/followbox-cloud/` 并执行 `bash deploy-clean-cache.sh`；摄像头侧设置 STA 加入 `FollowBox` AP，静态 IP `192.168.4.2`，MJPEG `:81/stream`。

### 2026-06-13 - Codex - P0 上线修复与 OTA 基础链路
- 改动：新增修复执行文档；LittleFS 改为打包 `firmware/web`；同步完整 H5；本地危险 POST 加 `X-FollowBox-Key` 鉴权；新增云端 OTA manifest/download/result 与固件 OTA manager。
- 文件：`PROJECT-REPAIR-EXECUTION-2026-06-13.md`, `firmware/platformio.ini`, `firmware/src/web/h5_web_server.cpp`, `firmware/web/{index.html,app.js}`, `firmware/src/ota/*`, `cloud/server.js`, `cloud/firmware/manifest.json`
- 架构影响：有；新增 OTA 通信模块但不新增 PWM 出口，OTA 期间通过全局安全标志让控制任务持续 `drive.stopNow()`。
- 安全影响：有；H5 改为可强制本地 API key，PWM 频率按规格回到 1000Hz，UART HAL 仅补 UART0 支持，`UART_NUM_IMU` 仍默认禁用。
- 验证：`node --check cloud/server.js` PASS；`node --check firmware/web/app.js` PASS；`pio run -e esp32-s3-devkitc-1` PASS；`pio run -e esp32-s3-devkitc-1 -t buildfs` PASS；本地 OTA manifest 路由返回 size/md5。
- 当前状态：NEEDS_VERIFICATION（发现 COM18 CP210x，但未取得车轮架空/驱动电源断开/急停反馈实测证据，未执行首烧与上车测试）。
- 下一步：确认安全前置条件后用 `pio run -e esp32-s3-devkitc-1 -t upload --upload-port COM18` 首烧，再 `pio run -e esp32-s3-devkitc-1 -t uploadfs --upload-port COM18`，之后走云端 OTA 或 `env:ota`。

### 2026-06-12 - Codex - 按 05 文档部署云端到 boonai.cn/fb
- 改动：按 `test/05-云端部署文档.md` 使用 WSL 密钥 `~/.ssh/id_ed25519_boonai`、SSH 端口 51400 部署 `cloud/` 到 `/www/wwwroot/followbox-cloud`，备份旧目录并重启 PM2。
- 文件：`cloud/server.js`, `cloud/followbox-nginx.conf`, `cloud/public/deploy-version.txt`, `cloud/deploy-clean-cache.sh`, `output/followbox-cloud-deploy.{zip,tar.gz}`
- 架构影响：无路由协议变更；仍是 Node 8080 + nginx `/fb/` 反代 + PM2 `followbox-cloud`。
- 安全影响：无新增运动权限；云端仍只发低速 deadman 命令，本地 safety gate 裁决；部署 smoke POST 使用 token 后已删除本地测试 JSON。
- 修复：`server.js` 的 POST `/ingest` 和 `/command` 去掉二次 `JSON.parse(await readBody(req))`，否则合法 JSON 会变 `[object Object]` 并 500。
- 验证：线上 `/fb/`, `/fb/style.css`, `/fb/app.js`, `/fb/deploy-version.txt` 均 200 且 no-store；POST `/fb/api/device/followbox-001/ingest` 返回 `{"ok":true}`；SSE `/events` 返回 smoke 状态；PM2 online，8080 LISTEN；根站 `/` 仍 200。
- 当前状态：PASS（云端已部署，待真机 ESP32 云端上传/命令轮询联调）。
- 下一步：配置固件 `cloud_config.h` API_BASE_URL 指向 `https://www.boonai.cn/fb`，启用 STA + CLOUD build 后架空验证遥测和云端低速点动。

### 2026-06-12 - Codex - 云端部署包与缓存策略准备（SSH 阻塞）
- 改动：为云端 `https://www.boonai.cn/fb/` 准备部署包，修正 `cloud/server.js` 支持环境变量 token，并加强 no-store/no-cache 响应头；新增部署版本文件和远端清缓存/PM2/nginx 重启脚本。
- 文件：`cloud/server.js`, `cloud/followbox-nginx.conf`, `cloud/public/deploy-version.txt`, `cloud/deploy-clean-cache.sh`, `output/followbox-cloud-deploy.{zip,tar.gz}`
- 架构影响：无协议变更；Node 服务仍提供静态 H5 + REST/SSE，Nginx 仍反代 `/fb/` 到 `127.0.0.1:8080`。
- 安全影响：无新增运动能力；token 可从环境变量注入，云端仍只发低速 deadman 命令，本地安全链最终裁决。
- 验证：`node --check cloud/server.js` 与 `cloud/public/app.js` PASS；本地临时服务 `/deploy-version.txt` 返回 no-store/no-cache；线上 `/fb/` 可访问但 `/fb/deploy-version.txt` 仍 404。
- 当前状态：BLOCKED_NEED_CONTEXT（`ssh root@82.156.85.60` 和 `root@www.boonai.cn` 在 KEX 前被远端关闭，未取得可用服务器登录入口）。
- 下一步：提供可用 SSH 主机/端口/账号/密钥或宝塔面板上传入口后，上传 `output/followbox-cloud-deploy.tar.gz` 到 `/www/wwwroot/followbox-cloud`，清空旧文件后解包并执行 `bash deploy-clean-cache.sh`。

### 2026-06-12 - Codex - H5 控制台 UI 优化 + OTA 首烧后上传入口
- 改动：重做 H5 控制台布局（首屏状态、遥测卡、链路状态、点动、标定向导），同步 `firmware/data` 与 `firmware/web`；启用双 OTA 分区和 ArduinoOTA/PlatformIO `env:ota`。
- 文件：`firmware/data/*`, `firmware/web/*`, `firmware/platformio.ini`, `firmware/partitions/ota_8MB.csv`, `firmware/include/config/network_config.h`, `firmware/src/main.cpp`, `firmware/README.md`
- 架构影响：Web 静态资源仍由 LittleFS 提供；OTA 仅新增通信服务和 PIO 上传环境，不新增 PWM 出口。
- 安全影响：有；OTA 开始后 `controlTask` 每 20ms `drive.stopNow()`，上传失败保持安全态；H5 仍只发模式/低速点动/标定/向导请求，不能直接 PWM 或清急停。
- 验证：`node --check firmware/data/app.js` PASS；`pio run -d firmware -e esp32-s3-devkitc-1` PASS；`pio run -d firmware -e esp32-s3-devkitc-1 -t buildfs` PASS；`pio run -d firmware -e ota` PASS；Playwright Edge 桌面/390px 手机截图无横向溢出。
- 当前状态：NEEDS_VERIFICATION（未真机 USB 首烧、未 OTA 实传、未手机触控实车验证）。
- 下一步：USB 首烧固件 + `uploadfs` 后连接 FollowBox SoftAP，访问 `http://192.168.4.1/` 验证页面，再执行 `pio run -d firmware -e ota -t upload` 做首次 OTA 回环。

### 2026-06-11 - Codex - 云端遥测与低速远程点动 P1 骨架
- 改动：新增 `MANUAL_CLOUD_LOW_SPEED`/`CLOUD_LOST`、云端低速输入链路、日志 ring buffer、HTTP 云端上传/命令轮询客户端、Node 参考云端服务和云端 H5 调试页。
- 文件：`firmware/src/cloud/*`, `firmware/include/config/cloud_config.h`, `firmware/src/{app,safety,web,telemetry}/...`, `cloud/*`, `CLOUD-TELEMETRY-SPEC.md`, `protocols/H5-API.md`
- 架构影响：新增云端输入源但不新增 PWM 出口；远程命令走 `command_pipeline -> motion_mixer -> safety_manager.applyFinalGate() -> drive_adapter`。
- 安全影响：云端不能直接 PWM/急停/安装向导/标定；仅 deadman+TTL 低速点动，默认编译关闭，需 STA WiFi + token。
- 验证：`node --check` 通过；host `logic_smoke_test.exe` 通过；`pio run -d firmware` 通过；`pio run -d firmware -t buildfs` 通过；Node ingest/command/static smoke 通过。
- 当前状态：NEEDS_VERIFICATION（未真机联网、未云服务器 HTTPS 部署、未架空远程点动测试）。
- 下一步：配置云服务器 HTTPS/token，ESP32 STA WiFi 与 `cloud_config.h` endpoint；架空车轮验证云端日志、命令 TTL、断网停车、急停优先级。

### 2026-06-07 - Codex - P0 安全/H5/遥测修复闭环
- 改动：修复 RC lost-link 非全局锁定、H5 AUTO 请求可达、H5 SAFE_IDLE 可退出 AUTO、标定 NVS 默认值写入、遥测字段缺失、安装向导四项确认，并恢复/扩展 host 烟测。
- 文件：`firmware/src/{safety,app,web,storage}/...`, `firmware/data/{index.html,app.js,style.css}`, `firmware/tools/logic_smoke_test.cpp`, `protocols/H5-API.md`, `FIRMWARE-FLASH-FIX-HANDOFF.md`
- 架构影响：无新增电机输出；模式选择支持已确认 AUTO 持续保持，H5 AUTO 仍受 wizard/calibration/UWB/safety gate 约束。
- 安全影响：有，改善 lost-link 真值表与安装向导确认；OTA/云端仍只按文档分阶段验证，未启用。
- 验证：host `logic_smoke_test.exe` 通过；`pio run -d firmware` 通过（RAM 15.0%，Flash 26.7%）；`pio run -d firmware -t buildfs` 通过；`node --check firmware/data/app.js` 通过。
- 当前状态：NEEDS_VERIFICATION
- 下一步：本地 USB 首烧 + `uploadfs`，架空验证急停/PWM 默认态/H5 页面/NVS 标定持久化；随后设计 `OTA-UPDATE-SPEC.md` 和云端遥测接口。

### 2026-06-07 - Codex - 补充 OTA 更新与云端 H5 双通道
- 改动：更新 `FIRMWARE-FLASH-FIX-HANDOFF.md`，明确 OTA 远程更新也是必需能力；与云端 H5 遥测调试分成两条通道。
- 文件：`FIRMWARE-FLASH-FIX-HANDOFF.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无代码变更；路线改为“本地首烧验证后，分别建设 OTA 更新通道与云端 H5 只读遥测通道”。
- 安全影响：有流程约束；OTA 前必须进入安全态，OTA 中禁止运动输出，失败后保持安全并可 USB 恢复。
- 验证：文档更新，未重新构建固件。
- 当前状态：NEXT_TASK_READY
- 下一步：修完 P0 后设计 `OTA-UPDATE-SPEC.md`（版本/校验/回滚/权限/日志）和云端遥测接口。

### 2026-06-07 - Codex - 澄清云端 H5 遥测调试边界
- 改动：修正 `FIRMWARE-FLASH-FIX-HANDOFF.md` 中“云端烧录”表述，明确用户意图是云端托管/采集 H5 遥测状态用于调试，不是 OTA 固件远程烧录。
- 文件：`FIRMWARE-FLASH-FIX-HANDOFF.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无代码变更；后续路线改为“本地首烧验证后接入云端只读遥测/H5调试”。
- 安全影响：有流程约束；云端只显示/采集/建议，不能直接 PWM、清急停、改安全红线或绕过本地门控。
- 验证：文档更新，未重新构建固件。
- 当前状态：NEXT_TASK_READY
- 下一步：先修 P0 代码问题并补齐可信遥测字段，再设计云端 H5 状态采集接口。

### 2026-06-07 - Codex - 首烧/云端烧录与修复清单交接
- 改动：新增 `FIRMWARE-FLASH-FIX-HANDOFF.md`，整理首次本地烧录、后续云端烧录边界、当前审查发现的 P0/P1 修复清单和最低验收命令。
- 文件：`FIRMWARE-FLASH-FIX-HANDOFF.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无代码架构变更；文档明确首烧必须本地 USB/串口验证，通过后才允许云端烧录。
- 安全影响：有安全流程约束；文档强调云端不得直接改 PWM/急停/GPIO/安全红线，不得绕过 `applyFinalGate()`。
- 验证：文档更新；此前实测 `pio run -d firmware` 与 `pio run -d firmware -t buildfs` 通过，host `logic_smoke_test` 编译失败已记录。
- 当前状态：NEXT_TASK_READY
- 下一步：按文档先修 safety/H5 AUTO/标定 NVS/遥测字段/烟测，再执行首次本地首烧验证；首烧通过后再建立云端烧录流程。

### 2026-06-02 - Antigravity - 连通故障复位网络链路+开机看门狗+H5标定向导交互
- 改动：连通了网页端与固件逻辑层故障复位 `/api/reset-fault` API；给心跳看门狗加入 3000ms 开机宽限期规避初始化盲区；建立了异步 Flash 写入与 Core 1 控制环自旋锁参数同步注入的 `/api/calibrate` 和 `/api/wizard-complete` API 端点；前端 H5 页面扩展了故障一键复位、向导状态显示和油门标定表单交互。三项关键工业级缺陷完美闭环。
- 文件：`firmware/src/web/{h5_request_parser,h5_command_handler,h5_web_server}.{h,cpp}`、`firmware/src/main.cpp`、`firmware/src/safety/safety_manager.cpp`、`firmware/data/{index.html,style.css,app.js}`
- 架构影响：有。H5WebServer 注入存储层依赖；pollInput 方法在自旋锁内同步取出 pending 配置，在 Core 1 控制环中实现严格无并发竞争的参数重载。
- 安全影响：有。心跳看门狗在启动 Grace Period 之后强制捕获挂起故障；复位故障时强校验运动无请求以防窜车。无新增电机输出。
- 验证：`pio run` 成功（RAM 15.0%，Flash 26.7%）；`pio run -t buildfs` 成功生成 littlefs.bin。
- 当前状态：PASS
- 下一步：真机验证 NVS 标定持久化写入及在控制环的加载应用；实车观察看门狗状态及急停复位性能。

### 2026-06-01 - Copilot - 补齐存储层+遥测层（profile_store/calibration_store/telemetry_logger/debug_console）
- 改动：实现 FIRMWARE-SPEC 仅剩的两层空目录。`debug_console`(分级 printf 门面 FB_LOGE/W/I/D，固定栈缓冲走 USB-CDC，不阻塞)、`telemetry_logger`(限速+模式/停车原因切换即时触发的 SystemState 快照行日志)、`profile_store`(NVS 持久化 install_wizard_complete 安全门控字段)、`calibration_store`(NVS 持久化 ThrottleCalibration，加载即钳制到 0-5V 模块窗口)。校验表三项中 camera_link 早已实现，本次补完其余两层 → 存储层/遥测层均 ✓。
- 文件：`firmware/src/storage/{profile_store,calibration_store}.{h,cpp}`(新)、`firmware/src/telemetry/{debug_console,telemetry_logger}.{h,cpp}`(新)、`drive/drive_adapter_analog_bldc.h`(+setCalibration)、`app/app.h`(+setInstallWizardComplete)、`main.cpp`(setup 加载两 store 并应用；commTask 调 telemetry_logger.update)
- 架构影响：有。新增 storage/telemetry 两层，App 仍无存储依赖（main.cpp 注入）；telemetry_logger 在 commTask(Core0,10Hz) 调用，绝不进 Core1 控制环。两 store 均 begin/load 一次 + save() 带 dirty-check（正常运行零 Flash 写）。
- 安全影响：有但保守。calibration_store 加载时 sanitize() 钳制 deadband≤min_active≤max≤full_scale 且 ≤5000mV、slew>0，防 NVS 损坏致油门失控；profile_store 空白/坏档默认 wizard=false 锁死 AUTO_FOLLOW。drive_adapter.setCalibration 只改 mV 映射，输出仍受 applyFinalGate/enable/brake 门控。无新 GPIO/PWM 路径。
- 验证：`pio run -d firmware` SUCCESS，RAM 14.7%/Flash 26.5%；VS Code diagnostics 无错。storage/telemetry 为 Arduino-only(Preferences/Serial)，不入 g++ 烟测清单。
- 当前状态：NEEDS_VERIFICATION（编译通过，未真机）
- 下一步：把 H5 安装向导完成动作接 profile_store.save；油门校准向导写 calibration_store.save；真机验证 NVS 持久化 + 调试日志输出；可选 telemetry_logger 周期/日志级别按需调。


- 改动：实现 FIRMWARE-SPEC 缺的 3 个传感器驱动并接入融合与遥测。`tof_vl53l1x_array`(TCA9548A+3×VL53L1X，连续模式轮询单路非阻塞，首版无 XSHUT→init/恢复走 I2C Bus Clear)、`ultrasonic_array`(2×HC-SR04 共享 TRIG GPIO9，Echo GPIO40/41 中断捕获回波，不用 pulseIn)、`camera_link`(纯逻辑在线状态，永不入安全门控)；新增 `obstacle_fusion`(LiDAR+TOF+超声按扇区取最近有效读数→单个 ObstacleSnapshot)。
- 文件：`firmware/src/sensors/{tof_vl53l1x_array,ultrasonic_array,camera_link,obstacle_fusion}.{h,cpp}`(新)、`sensor_task.*`、`core/types.h`、`config/profile_defaults.h`、`app/{app.*,shared_state.h}`、`core/system_state.h`、`main.cpp`、`web/telemetry_api.*`、`web/h5_web_server.cpp`、`platformio.ini`(+pololu/VL53L1X)、`firmware/data/{index.html,app.js}`、`protocols/H5-API.md`、`firmware/README.md`、`tools/logic_smoke_test.cpp`
- 架构影响：有。sensor_task.obstacle() 现返回融合快照；新增 TofSnapshot/UltrasonicSnapshot/CameraStatus 贯通 SensorBundle→SystemState→buildStateJson。SafetyManager/ObstacleManager 逻辑零改动，仅消费更丰富的同形 ObstacleSnapshot，运动门控路径未扩张。
- 安全影响：有（safety-critical）。融合保守取最近有效读数，任一传感器见近即触发既有 slow/stop；超声仅入侧扇区(P0 侧向不停车)；摄像头永不参与门控。新增唯一 I2C 出口仍是 hal/i2c_bus；无新电机/PWM 路径。建议提交 safety-reviewer 复核融合真值表与扇区映射后再上电。
- 验证：`pio run` SUCCESS，RAM 14.7%/Flash 26.4%；g++ 烟测 PASS（新增 testObstacleFusion 6 断言 + 遥测 JSON 断言）；check_ai_handoff PASS；VS Code diagnostics 无错。
- 当前状态：NEEDS_VERIFICATION（编译+烟测通过，未真机）
- 下一步：safety-reviewer 复核融合；真机台架标定 VL53L1X 互串/安装几何、超声左右挂载方位；摄像头在线探测属视觉 P1（无端点前如实显示离线）；注意 firmware/web/ 仍是 P0 占位骨架，真实面板在 firmware/data/，二者已漂移待统一。

### 2026-06-01 - Copilot - 双核 FreeRTOS 任务化：control(Core1) / sensor+comm(Core0) + 跨核邮箱
- 改动：把单核 loop 轮询改为 3 个 pinned 任务——controlTask(Core1, 最高优先级, 50Hz, 唯一持有 SystemState + 唯一写电机)、sensorTask(Core0, prio3, 50Hz, 排 UART/ADC/RC → 发布 SensorBundle)、commTask(Core0, prio1, 10Hz, 读已提交 SystemState 副本 → pushState)；新增 SharedState 双 portMUX 单槽邮箱。
- 文件：`firmware/src/main.cpp`（重写为任务模型）、`firmware/src/app/shared_state.*`(新)
- 架构影响：有，落实 FIRMWARE-SPEC §7/§7.1——电机只在 control_task 更新；阻塞 I/O（串口/ADC/WiFi）全在 Core0，不阻塞 50Hz 控制环；SystemState 仅 control_task 拥有/写，comm 只读提交副本；临界区仅结构体拷贝、锁内无 I/O。Arduino loop() 空转。心跳经 SensorBundle 传入 control_task 供看门狗。
- 安全影响：运动链路语义不变（仍 safety→mode→pipeline→obstacle→mixer→applyFinalGate→drive_adapter），仅执行上下文从 loopTask 移到固定周期 Core1 任务；H5 pollInput/pushState 经各自 portMUX 跨核锁，AsyncTCP 回调在 Core0。
- 验证：g++ 烟测 PASS（纯逻辑不受影响，shared_state 仅 Arduino）；`pio run -d firmware` SUCCESS（exit 0），RAM 14.5% / Flash 25.8%。
- 当前状态：NEEDS_VERIFICATION（编译+烟测通过，未真机；任务栈 4096/优先级/周期为初值，需真机观察 control 抖动与栈高水位）
- 下一步：真机测 control_task 周期抖动 + uxTaskGetStackHighWaterMark 调栈；comm_task 心跳接入 state.heartbeat.comm_task_ms；硬件 WDT 兜底（esp_task_wdt）；考虑 control 周期超时告警。

### 2026-06-01 - Copilot - 结论记录：IMU 硬件 UART 方案 = 重映射空闲 UART0→GPIO42
- 改动：修正之前「UART0/1/2 已占满」的不准确记录——调试日志走原生 USB-CDC（ARDUINO_USB_CDC_ON_BOOT=1）而非 UART0，故 UART0 控制器实际空闲；更新 board_pins.h 注释记录启用路径。
- 文件：`firmware/include/config/board_pins.h`（仅注释）、repo 记忆 build-notes.md
- 架构影响：无代码行为变更（UART_NUM_IMU 仍=-1，IMU 仍禁用）；确立正式方案：启用时把空闲 UART0 经 GPIO Matrix 重映射到 GPIO42（合法非启动脚），真硬件 UART、不与 UWB(UART1)/lidar(UART2) 抢中断，优于 SoftwareSerial（对 230400 lidar 易丢帧）与共用 UART2（lidar 须常开）。
- 安全影响：无；IMU 仍 invalid、yaw 阻尼仍关。
- 验证：无需构建（仅注释/文档）。
- 当前状态：DECIDED（方案锁定，未实施）
- 下一步：启用时 UART_NUM_IMU=0 + UartBus 加 Serial0 分支 + 确认 JY61P TX 5V→GPIO42 电平转换 + 安装向导实测 yaw_sign/波特率/静止窗口；前提是真机调试不再依赖 UART0/GPIO43-44 串口（已走 USB-CDC）。

### 2026-06-01 - Copilot - JY61P IMU 纯逻辑解析器 + 接入 SensorTask/App（默认禁用）
- 改动：新增 WitMotion 0x55 解析器（0x51 加速度忽略 / 0x52 角速度→yaw_rate_dps / 0x53 角度→yaw/pitch/roll，校验和=前10字节和&0xFF），接入 sensor_task（drainImu，限速 512B/tick）与 App.ingestSensorInputs(+ImuSnapshot)→state.imu；加 config（IMU_UART_BAUD/超时/全量程/IMU_YAW_SIGN）。
- 文件：`firmware/src/sensors/jy61p_imu.*`(新)、`firmware/src/sensors/sensor_task.*`、`firmware/src/app/app.*`、`firmware/src/main.cpp`、`firmware/include/config/board_pins.h`、`firmware/include/config/profile_defaults.h`、`firmware/tools/logic_smoke_test.cpp`
- 架构影响：有但默认零运行影响——`UART_NUM_IMU=-1`（ESP32-S3 仅 UART0/1/2，UART1=UWB/UART2=lidar 已占满，无空闲硬件 UART），IMU 流默认禁用、imu.valid 恒 false；解析器纯逻辑、不碰 GPIO，喂字节模式同 lidar。
- 安全影响：无；imu 无效时 yaw 阻尼保持关闭（FOLLOW_YAW_DAMP_GAIN=0），不改变任何运动输出；yaw_sign/真实波特率仍待安装向导实测；上电静止 3 秒要求未实现。
- 验证：g++ 烟测 PASS（testImuParser：角度/角速度缩放、校验和拒绝、超时清零）；`pio run -d firmware` SUCCESS（exit 0），RAM 14.4% / Flash 25.8%。
- 当前状态：NEEDS_VERIFICATION（解析器就绪+编译通过，未启用、未真机；需先腾出硬件 UART 或选 SoftwareSerial）
- 下一步：**需定 IMU 硬件 UART 方案**（腾 UART2 与 lidar 互斥 / SoftwareSerial / 暂不启用）→ 启用后安装向导实测 yaw_sign + 波特率 + 上电静止窗口；H5-API schema 冻结，IMU 暂不入遥测 JSON。

### 2026-06-01 - Copilot - H5 静态面板：LittleFS 提供 + 功能化控制页
- 改动：platformio.ini 启用 LittleFS（board_build.filesystem=littlefs, partitions=default_8MB.csv）；h5_web_server 挂载 LittleFS 并 serveStatic("/")→index.html（挂载失败非致命，API/WS 仍可用）；新增功能化面板（WS /ws/state 实时遥测、模式按钮、按住 deadman 才点动的 /api/jog）。
- 文件：`firmware/platformio.ini`、`firmware/src/web/h5_web_server.cpp`、`firmware/data/index.html`(新)、`firmware/data/app.js`(新)、`firmware/data/style.css`(新)
- 架构影响：无控制链变更；静态资源走 LittleFS（烧录命令 `pio run -t uploadfs`），与已有传输层共用 g_server，仍只读 state 推送/POST 接 parser。面板 jog 按住 deadman 时 150ms 心跳、松手发 deadman=false 显式停。
- 安全影响：面板只发 jog/mode 请求，仍下游过 mode_manager+safety+applyFinalGate；不能设 PWM/清急停/绕过向导；WS 断连自动重连不影响 H5_LOST_STOP_MS 超时停车。
- 验证：`pio run -d firmware` SUCCESS（exit 0），RAM 14.4% / Flash 25.7%；`pio run -t buildfs` SUCCESS（exit 0，littlefs.bin 从 data/ 打包）。
- 当前状态：NEEDS_VERIFICATION（编译+FS 镜像通过，未真机；需 `uploadfs` 烧录文件系统后浏览器联调）
- 下一步：真机 uploadfs + 浏览器验证遥测/jog 时延；/api/reset-fault 软件锁复位路径（safety_manager 加 reset 钩子，safety-reviewer 审）；softAP 默认密码现场前更换。

### 2026-06-01 - Copilot - H5 传输层 socket 胶水：WiFi + 异步 HTTP/WebSocket 接入 main
- 改动：新增 `H5WebServer`（ESPAsyncWebServer+AsyncTCP，softAP 默认/STA 可切）：/ws/state 限频推送 buildStateJson、POST /api/jog、/api/mode-request 接 parser→H5CommandHandler、/api/reset-fault 诚实拒绝（reset 未接线）；wire 进 main.cpp（setup h5_web.begin、loop pollInput→ingestH5Input + pushState）。
- 文件：`firmware/platformio.ini`(lib_deps)、`firmware/include/config/network_config.h`(新)、`firmware/src/web/h5_web_server.*`(新)、`firmware/src/main.cpp`
- 架构影响：有；回调跑在 AsyncTCP 任务、控制环跑 loopTask，共享 H5CommandHandler 用 portMUX 自旋锁保护（临界区仅 1 次 handler 调用/结构体拷贝，符合 SPEC 7.1）；状态 JSON 仅在 loopTask 内构建推送（state 由 loop 独占，无需额外锁）；POST 体上限 256B，超限/缺字段 fail-safe 拒绝。
- 安全影响：H5 仍不能设 PWM/清急停/绕过向导——jog 经 deadman+replay 防护后只写 h5.throttle/steering，仍下游过 mode_manager+safety+applyFinalGate；/api/reset-fault 明确不动安全锁/物理急停。WiFi softAP 默认密码需现场前更换。
- 验证：`pio run -d firmware` SUCCESS（exit 0），RAM 14.4% / Flash 24.4%（WiFi/AsyncTCP 链入致体积上升）；get_errors 三文件无错。
- 当前状态：NEEDS_VERIFICATION（编译通过，未真机联调；index.html/app.js 静态文件尚未经 LittleFS 烧录提供，根路径暂返回 404 JSON）
- 下一步：经 LittleFS/SPIFFS 提供 web/ 静态面板；真机验证 WS 推送/jog 时延；reset-fault 软件锁复位路径设计（需 safety_manager 增 reset 钩子，safety-reviewer 审）。

### 2026-06-01 - Copilot - H5 传输层纯逻辑：状态 JSON 序列化 + 请求解析
- 改动：新增无依赖、可 g++ 测的传输内核——buildStateJson（按 H5-API.md 冻结 schema 输出 /ws/state）+ parseJogRequest/parseModeRequest（/api/jog、/api/mode-request 体解析），含 enum→字符串映射。
- 文件：`firmware/src/web/telemetry_api.*`, `firmware/src/web/h5_request_parser.*`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；JSON 用 snprintf 写调用方缓冲、无堆分配，可在 comm_task 栈上调用；解析只支持平铺顶层键，缺字段=整体 invalid（不猜值）。不碰 socket/WiFi/GPIO/PWM。
- 安全影响：无硬件输出变更；jog 缺 seq/forward/turn 即 invalid，deadman 缺省 fail-safe=false（停）；mode 只认 SAFE_IDLE/MANUAL_H5_LOW_SPEED/AUTO_FOLLOW_REQUEST，其它→NONE；缓冲过小不吐残帧。
- 验证：g++ 烟测 PASS（testTelemetryJson + testRequestParser，含端到端喂 handler）；`pio run -d firmware` SUCCESS（exit 0），RAM 6.4% / Flash 9.3%。
- 当前状态：NEEDS_VERIFICATION（仍缺 WiFi + WebServer/WebSocket socket 胶水；运行时 state.h5 仍未写入）
- 下一步：**需用户定库选型**（ESPAsyncWebServer+AsyncTCP vs 内置 WebServer+links2004/WebSockets）+ WiFi softAP 配置 → comm 层接 telemetry_api/h5_request_parser → handler → app.ingestH5Input（Core0 低优先级，不阻塞控制环）。

### 2026-06-01 - Copilot - H5 命令处理器（纯逻辑）+ ingestH5Input
- 改动：新增纯逻辑 H5CommandHandler（jog/mode-request → H5ControlInput，含 deadman/超时/seq 重放保护），App 增 ingestH5Input 写 state.h5；打通 MANUAL_H5_LOW_SPEED 逻辑层。
- 文件：`firmware/src/web/h5_command_handler.*`, `firmware/src/app/app.*`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；H5 逻辑落 web/，只产出 H5ControlInput 快照，不碰 socket/GPIO/PWM；速度在此夹 -1..1，H5_MAX_SPEED_SCALE 仍由下游施加。**Arduino WebServer/WebSocket/JSON 传输层仍缺**（platformio.ini 无 web 库），handler 未接入 main loop。
- 安全影响：无硬件输出变更；遵守 H5-API.md 红线（H5 不设 PWM、不清急停、不绕安装向导）；deadman=false 或超时 H5_LOST_STOP_MS 立即停；AUTO_FOLLOW 仅置 auto_request 由 mode_manager 复核。
- 验证：g++ 烟测 PASS（新增 testH5CommandHandler：连接/夹幅/重放/deadman/超时/ingest）；`pio run -d firmware` SUCCESS（exit 0），RAM 6.4% / Flash 9.3%。
- 当前状态：NEEDS_VERIFICATION（传输层未实现；state.h5 实际仍未在固件运行时被写入，仅逻辑+测试就绪）
- 下一步：选定并加 WebServer/WebSocket 库 → 解析 /ws/state、/api/jog、/api/mode-request → 调 handler → app.ingestH5Input；IMU 仍待让出 UART。

### 2026-06-01 - Copilot - 接通电机输出（drive_adapter 上链）[safety-critical]
- 改动：主循环在 app.tick 后调用 drive.writeCommand(app.state().motor_command)，把此前一直空算、从未下发的安全门控指令真正驱动到 PWM 出口。
- 文件：`firmware/src/main.cpp`
- 架构影响：有；补全 FIRMWARE-SPEC 主链最后一段（applyFinalGate → drive_adapter_analog_bldc 唯一 PWM 出口）。App 仍纯逻辑、不碰 GPIO；输出只在 loop（控制环）发生。
- 安全影响：有，safety-critical；这是首次真正给电机 PWM 上电的代码路径。传入指令已是 applyFinalGate 输出，adapter 自身再查 enable/brake；begin() 默认 enable 关、刹车有效、油门 0；setup 即调用确保上电安全。**真机首测必须架空轮子、低速、人手急停在侧。**
- 验证：`pio run -d firmware` SUCCESS（exit 0），RAM 6.4% / Flash 9.3%；g++ 烟测不受影响（App 仍纯逻辑）。未做硬件在环验证。
- 当前状态：NEEDS_VERIFICATION（实物必须架空台架先验油门方向/倒车/刹车/使能极性，再落地）
- 下一步：safety-officer 审批后架空台架首测；标定 PWM 满量程/死区与电池分压；H5/IMU 仍空缺。

### 2026-06-01 - Copilot - 接入 DS600 RC → state.rc
- 改动：把已有 rc_input_ds600（5 路 PWM 中断输入）接进主循环，新增 App.ingestRcInput 写 state.rc，打通 MANUAL_RC 控制源。
- 文件：`firmware/src/app/app.*`, `firmware/src/main.cpp`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；RC 作为控制输入单独走 ingestRcInput（与 sensor 快照分开）；App 仍无硬件、唯一持有 SystemState。
- 安全影响：有（正向补全）；state.rc 此前无人写入，safety/mode 的 MANUAL_RC online/throttle/stop_switch/auto_request 分支全是空跑；现接通真实 DS600 通道（含 100ms 失帧→online=false、stop_switch=true 安全默认）。
- 验证：g++ 烟测 PASS（testSensorIngestion 增 RC 断言）；`pio run -d firmware` SUCCESS（exit 0），RAM 6.4% / Flash 9.1%。
- 当前状态：NEEDS_VERIFICATION（单核 loop 轮询；实物 DS600 PWM 脉宽/通道映射、CH4 auto/CH5 stop 阈值未实测）
- 下一步：现场标定 DS600 五通道脉宽与中点/死区；接 H5 输入写 state.h5（MANUAL_H5 仍空跑）；IMU 待让出硬件 UART。

### 2026-06-01 - Copilot - sensor_task 接入电源监控 + state.power
- 改动：把已有 power_monitor（电池ADC+控制器故障GPIO）并入 sensor_task，并扩展 App.ingestSensorInputs 写 state.power，补齐 safety 的低电压/电机故障数据源。
- 文件：`firmware/src/sensors/sensor_task.*`, `firmware/src/app/app.*`, `firmware/src/main.cpp`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；sensor_task 现产出 uwb/obstacle/power 三类快照；App 仍无硬件、唯一持有 SystemState。IMU(JY61P) 暂缓——UART0/1/2 已被 USB/UWB/激光雷达占满，无空闲硬件 UART，且 yaw 阻尼默认关，未编造 UART。
- 安全影响：有（正向补全）；state.power 此前无人写入，safety_manager 的 low_battery/motor_fault 分支等于空跑；现接通真实 ADC/故障脚。无新增电机输出。
- 验证：g++ 烟测 PASS（testSensorIngestion 增 power 断言）；`pio run -d firmware` SUCCESS（exit 0），RAM 6.2% / Flash 9.0%。
- 当前状态：NEEDS_VERIFICATION（单核 loop 轮询；IMU 未接；实物 ADC 分压/电压标定与 UART 未验）
- 下一步：解决 IMU 硬件 UART 占用（让出激光雷达 UART 或软串口）后再做 JY61P parser；实物标定电池分压 220k/10k。

### 2026-06-01 - Copilot - 传感器任务接入 UART→解析器→state
- 改动：新增 UART HAL 与传感器任务，把 UWB(+可选激光雷达)字节流喂解析器并写入 SystemState；App 增加 ingestSensorInputs 入口，main.cpp 主循环驱动。
- 文件：`firmware/src/hal/uart_bus.*`, `firmware/src/sensors/sensor_task.*`, `firmware/src/app/app.*`, `firmware/src/main.cpp`, `firmware/include/config/board_pins.h`, `firmware/include/config/profile_defaults.h`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；sensor_task 只产出快照+心跳，App 保持无硬件、仍是唯一 SystemState 持有者；UWB=UART1(115200)、激光雷达=UART2(230400, PIN_LIDAR_RX=-1 默认禁用)；未碰电机/GPIO 输出。
- 安全影响：无硬件输出变更；心跳 sensor_task_ms/uwb_task_ms 每轮刷新喂给 safety 看门狗；UART 每轮限流 512 字节防卡控制环；超时清快照不留陈旧航向。
- 验证：g++ 烟测 PASS（新增 testSensorIngestion：UWB帧→parser→App.state）；`pio run -d firmware` SUCCESS，RAM 6.0% / Flash 8.7%。
- 当前状态：NEEDS_VERIFICATION（单核 loop 轮询，FreeRTOS 双核任务/双缓冲未做；激光雷达引脚未定，实物 UART 未验）
- 下一步：确认激光雷达 RX 引脚后设 PIN_LIDAR_RX；接 JY61P/电池/超声入 sensor_task；现场标定 UWB 负角度、激光雷达朝向。

### 2026-06-01 - Copilot - obstacle_manager P0 限速/停车
- 改动：新增前向避障限速器，接入 app.tick 主链（command_pipeline -> obstacle_manager -> motion_mixer）。
- 文件：`firmware/src/control/obstacle_manager.*`, `firmware/src/app/app.*`, `firmware/tools/logic_smoke_test.cpp`
- 架构影响：有；按 FIRMWARE-SPEC 第4节插入限速节点，只衰减正向 forward，保留 turn/倒车；不写 GPIO、不做绕障/自动后退。
- 安全影响：无硬件输出变更；safety_manager.hasStopObstacle 仍是权威停车，本限速器为冗余渐进减速；obstacle.valid=false 时透传（传感器未接线）。
- 验证：g++ 烟测 PASS（透传/净空/限速/停车/倒车不挡）；`pio run -d firmware` SUCCESS，RAM 5.9% / Flash 8.0%。
- 当前状态：NEEDS_VERIFICATION（前向扇区来自激光雷达解析器，尚未由传感器任务写入 state.obstacle）
- 下一步：传感器任务把 lidar/UWB 字节流喂解析器并写 state；TODO：obstacle.valid=false 接 sensor heartbeat 判定为超时而非净空。

### 2026-06-01 - Copilot - UWB parser + 跟随控制器 + 激光雷达解析器
- 改动：实施迁移评估任务包 A+B 并新增 LD19/LD06 激光雷达解析；AUTO_FOLLOW 接入纯逻辑跟随控制器。
- 文件：`firmware/src/sensors/uwb_gc_p2304.*`, `firmware/src/control/follow_controller_uwb.*`, `firmware/src/sensors/lidar_ld19.*`, `firmware/src/app/command_pipeline.*`, `firmware/include/config/profile_defaults.h`, `firmware/tools/logic_smoke_test.cpp`, `firmware/README.md`
- 架构影响：有；UWB/激光雷达解析落到 sensors，跟随策略落到 control，只输出快照或 MotionIntent，未碰 GPIO；激光雷达把 360° 折叠进 ObstacleSnapshot 扇区。
- 安全影响：无硬件输出变更；AUTO_FOLLOW 仍受 safety_manager 全门控（安装向导/UWB丢失/障碍/急停），默认低速 0.30。
- 验证：g++ 烟测 PASS（含规格书样例帧 + LD19 CRC8）；`pio run -d firmware` SUCCESS，RAM 5.9% / Flash 8.0%。
- 当前状态：NEEDS_VERIFICATION（解析器仅字节流，未接 UART/传感器任务）
- 下一步：传感器任务把 UART 字节喂给解析器并写入 state；实现 obstacle_manager P0 限速；现场标定 UWB 负角度、激光雷达安装朝向、标签高度。
- 待确认：UWB 负角度二进制编码、`LIDAR_MOUNT_YAW_OFFSET_DEG` 符号、`FOLLOW_TAG_HEIGHT_MM`。

### 2026-05-30 - Hermes - 三大远景待办文档
- 改动：新增 `FOLLOWBOX-VISION-BACKLOG.md`，三个远景的详细待办路线图
- 文件：`FOLLOWBOX-VISION-BACKLOG.md`, `README.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码变更；远景作为 P0 之后的待办路线
- 安全影响：无
- 验证：README.md 文档入口表格格式正确，新文档 155 行
- 当前状态：NEXT_TASK_READY
- 下一步：确认 TOF LiDAR 型号（扫描式还是单点），决定远景二的 P3-P4 是否需要额外硬件；远景一 P1 的 telemetry_logger 可以在当前 P0 代码里直接铺

### 2026-05-29 - Copilot - 图一嵌入接线小电路
- 改动：将 `ASSEMBLY-WIRING-MINDMAP.html` 顶部“图一”改成详细版思维导图，每个模块节点内直接显示引脚接线方案和小电路示例。
- 文件：`ASSEMBLY-WIRING-MINDMAP.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码变更；接线事实仍按 `PIN-MAP-V1.md` 和 `CURRENT-WIRING-AI.md`。
- 安全影响：有装配安全影响；图一内嵌 DS600/JY61P/HC-SR04 10k/20k 分压、I2C 4.7k 上拉、PWM/MOS 10k 下拉、5V 电容、GPIO21 急停反馈、ADC 220k/10k。
- 验证：VS Code diagnostics 无错误；grep 确认图一包含 1600x1120 详细 SVG、关键 GPIO、电阻和电容文字；`python tools\check_ai_handoff.py` PASS。
- 当前状态：PASS
- 下一步：实物接线时优先看图一，表格用于复核；到货后按实物端子线序微调。

### 2026-05-29 - Copilot - 重写接线思维导图
- 改动：恢复并重写中文接线资料，新增总接线 SVG、接线思维导图 HTML 和完整逐引脚接线表。
- 文件：`ASSEMBLY-WIRING-MINDMAP.html`, `complete-wiring.svg`, `complete-wiring-table.md`, `README.md`, `README.html`, `ASSEMBLY-WIRING-GUIDE.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码变更；接线资料以 `PIN-MAP-V1.md` / `CURRENT-WIRING-AI.md` 为准，替代此前错误占位接线图。
- 安全影响：有装配安全影响；明确 5V 信号分压、I2C 4.7k 上拉、PWM/MOS 10k 下拉、GPIO21 急停隔离、ADC 220k/10k、主电流不上洞洞板。
- 验证：VS Code diagnostics 无错误；grep 确认 HC-SR04 左右模块、GPIO9、GPIO40/41、Echo 分压已覆盖；`python tools\check_ai_handoff.py` PASS。
- 当前状态：PASS
- 下一步：实物到货后按表核对 DS600/控制器/急停端子线序，再按新图逐项接线。

### 2026-05-29 - Copilot - 重建洞洞板布局图
- 改动：重写 `gen_perfboard_layout.py`，重新生成当前唯一 10×15cm 洞洞板布局图，并删除旧错误 SVG/接线图支撑文件。
- 文件：`gen_perfboard_layout.py`, `PERFBOARD-LAYOUT.svg`, `PERFBOARD-LAYOUT.html`, `README.md`, `README.html`, `ASSEMBLY-WIRING-GUIDE.md`, 删除旧 `complete-wiring*`/`circuit-*`/`wiring-overview.svg`/`ASSEMBLY-WIRING-MINDMAP.html`。
- 架构影响：无固件代码变更；布局依据当前 BOM、Pin Map、接线方案和盒内分区，不复用旧图数据。
- 安全影响：有文档/装配安全影响；图中强化 JY61P 居中、UWB 远离 DC-DC、5V 输入独立分压、GPIO21 隔离、动力主电流不上洞洞板。
- 验证：`python gen_perfboard_layout.py` 通过，生成器校验板边界、重叠、JY61P 中心和 UWB/DC-DC 间距；VS Code diagnostics 无错误。
- 当前状态：PASS
- 下一步：实物到货后按新图核对模块实物尺寸、孔位和接插件方向，必要时只调 `gen_perfboard_layout.py` 后重生成。

### 2026-05-28 - Copilot - 细化装配接线思维导图
- 改动：将 `ASSEMBLY-WIRING-MINDMAP.html` 从阶段概览改为可执行核对版，补齐物料、接线表、GPIO、急停、ADC、动力、调试步骤。
- 文件：`ASSEMBLY-WIRING-MINDMAP.html`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件架构变更；HTML 内容仅同步现有 `ASSEMBLY-WIRING-GUIDE.md` / `PIN-MAP-V1.md` / `CURRENT-WIRING-AI.md`。
- 安全影响：无硬件方案变更；强化 5V 分压、GPIO21 隔离、220k/10k ADC、星型地、架空调试等安全红线展示。
- 验证：VS Code HTML diagnostics 无错误；PowerShell 检查 article/table 标签成对并确认 `</html>` 完整；已用默认浏览器打开。
- 当前状态：PASS
- 下一步：实物到货后仍需拍照确认 DS600 电平、控制器线色、JY61P TX 电平和急停第二触点/光耦方案。

### 2026-05-27 - Hermes - 旧 UWB 项目迁移评估
- 改动：新增旧 UWB 自动跟随项目迁移评估，补齐 GC-P2304 官方帧格式证据，并给出 A/B/C 三个实施任务包。
- 文件：`UWB-LEGACY-MIGRATION-REVIEW.md`, `protocols/UWB-GC-P2304.md`, `README.md`
- 架构影响：未改固件主链；明确后续 UWB parser/follow_controller/obstacle_manager 应落到 sensors/control 模块且只输出快照或 MotionIntent。
- 安全影响：无硬件输出变更；文档禁止旧项目 `car_move/car_stop`、旧 GPIO、2S 电池参数、视觉云台耦合直接进 FollowBox。
- 验证：`python3 tools/check_ai_handoff.py` PASS；WSL `g++` 逻辑烟测 PASS；Windows 侧 `pio run -d firmware` SUCCESS。
- 当前状态：NEXT_TASK_READY
- 下一步：用户批准后先实施任务包 A+B；parser 与 follow_controller 先做纯逻辑测试，不直接实车跟随。

### 2026-05-27 - Hermes - 记录云端困难场景协助待办
- 改动：新增云端协助 backlog，明确基础完成后再做 WiFi telemetry/图像上传、云端建议、本地 command_guard 裁决路线。
- 文件：`CLOUD-ASSIST-BACKLOG.md`, `README.md`
- 架构影响：当前固件无变更；后续可能新增 `REMOTE_ASSIST`、`cloud_command` 协议、H5 人确认执行流程。
- 安全影响：无硬件输出变更；文档规定云端禁止直接 PWM/高速闭环/绕过急停，本地 ESP32 最终否决。
- 验证：已读取 backlog 和 README diff；待运行 `python3 tools/check_ai_handoff.py`。
- 当前状态：NEXT_TASK_READY
- 下一步：先完成本地手动/安全链/UWB/本地避障/H5 telemetry，再写 `CLOUD-ASSIST-SPEC.md`。

### 2026-05-27 20:44 - Codex - 补齐 HAL/驱动/只读输入基础模块
- 改动：新增 GPIO/PWM/ADC/I2C HAL、`drive_adapter_analog_bldc`、`command_pipeline`、电源监控、DS600 中断式 PWM 输入。
- 文件：`firmware/src/hal/*`, `firmware/src/drive/*`, `firmware/src/app/command_pipeline.*`, `firmware/src/control/rc_input_ds600.*`, `firmware/src/sensors/power_monitor.*`
- 架构影响：进一步贴合 `FIRMWARE-SPEC.md`；`App` 不再直接合成 MotionIntent，输出 GPIO 仍封在 drive adapter/HAL。
- 安全影响：新增真实输出适配器代码，但尚未接入 `App` 主循环；`begin/stopNow` 默认油门 0、使能关、刹车有效。
- 验证：`pio run -d firmware` 通过；RAM 5.9%，Flash 8.0%；`rg` 确认旧 GPIO35/36/37/47/48 未用于电机输出。
- 当前状态：NEEDS_VERIFICATION
- 下一步：接入调度/传感器快照前补台架日志；驱动适配器接入前必须架空车轮并完成 PWM→0-5V 万用表校准。

### 2026-05-27 20:23 - Codex - 建立固件 P0 骨架与纯逻辑链路
- 改动：新增 `firmware/` PlatformIO 工程骨架，落地 board_pins、核心类型、SystemState、safety/mode/mixer/app 首批代码。
- 文件：`firmware/platformio.ini`, `firmware/README.md`, `firmware/include/*`, `firmware/src/*`, `firmware/web/*`, `firmware/tools/logic_smoke_test.cpp`, `firmware/.gitignore`
- 架构影响：按 `FIRMWARE-SPEC.md` 建立模块边界；`main.cpp` 只做入口，运动命令链路保留 `SafetyManager::applyFinalGate()`。
- 安全影响：未实现真实 GPIO/PWM 输出；急停默认 active，禁用时所有 MotorCommand 强制 `enable=false`、`brake=true`、左右目标 0。
- 验证：`pio run -d firmware` 通过；首次需设置 `PLATFORMIO_CORE_DIR/PLATFORMIO_HOME_DIR=firmware\.pio-core` 避免 `C:\.platformio` 权限问题。
- 验证补充：`g++` 主机烟测未通过，原因是本机 MSYS2 `cc1plus.exe` 以 `-1073741511` 退出，不是固件编译错误。
- 当前状态：NEEDS_VERIFICATION
- 下一步：实现 HAL/drive_adapter 前先补正式单元测试；真实电机输出仍需安全审批和架空验证。

### 2026-05-27 - Codex - 优化 AI 交接与技能入口
- 改动：收紧交接记忆追加条件，新增 skills 轻量读取规则和权威来源裁决顺序。
- 文件：`AI-HANDOFF-MEMORY.md`, `skills/README.md`, `skills/*/SKILL.md`, `AI-AGENT-RUNBOOK.md`
- 架构影响：无固件模块边界变更；优化 AI 协作流程，降低 skill 摘要与权威文档漂移风险。
- 安全影响：无硬件输出变更；冲突时要求以 PIN-MAP/FIRMWARE/WIRING 等权威文件为准并先阻塞。
- 验证：`python tools\check_ai_handoff.py` 通过；确认 9 个 skills 都加入权威摘要提示；HTML 镜像未同步生成。
- 当前状态：PASS
- 下一步：如需要给人直接打开 HTML，再补 Markdown->HTML 同步流程。

### 2026-05-27 - Hermes - 增加 Codex/Claude/Copilot 交接门禁
- 改动：新增 AI 运行说明和交接检查脚本，说明单靠记忆文件不能强制所有 AI，需 prompt + skill + 门禁三层约束。
- 文件：`AI-AGENT-RUNBOOK.md`, `tools/check_ai_handoff.py`, `README.md`, `skills/README.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无固件代码架构变更；新增 AI 协作流程门禁。
- 安全影响：无硬件输出变更；降低代码 AI 忘写交接导致误接续的风险。
- 验证：`python3 tools/check_ai_handoff.py` 已通过。
- 当前状态：PASS
- 下一步：运行 Codex/Claude/Copilot 前复制 `AI-AGENT-RUNBOOK.md` 中固定 prompt，结束后跑交接检查脚本。

### 2026-05-27 - Hermes - 建立项目内 AI 技能包和交接记忆规则
- 改动：新增 `skills/` 项目内 AI 技能包，并新增本交接记忆文件规则。
- 文件：`skills/README.md`, `skills/*/SKILL.md`, `README.md`, `AI-HANDOFF-MEMORY.md`
- 架构影响：无代码架构变更；新增 AI 协作流程入口。
- 安全影响：无硬件输出变更；技能中强化 safety/applyFinalGate/GPIO/ADC/I2C/UWB 红线。
- 验证：已脚本检查 skills 共 10 个文件，关键约束覆盖 PASS。
- 当前状态：NEXT_TASK_READY
- 下一步：任何 AI 开发/审查前先读 `skills/README.md` 和本文件，再按路由读对应技能。

## 已过期/归档记录

暂无。
