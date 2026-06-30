# Follow the Box 当前项目资料

## 当前只看这些文档

| 文件 | 用途 |
|---|---|
| `FIRMWARE-SPEC.md` / `FIRMWARE-SPEC.html` | 固件代码框架总规范：VS Code 手写、AI 辅助、后续维护都按它写 |
| `CURRENT-PROJECT-ARCHITECTURE.md` | 当前项目架构地图：AP/局域网/云端 H5、固件运行链路、页面同步规则和问题定位入口 |
| `PIN-MAP-V1.md` | 当前唯一 Pin Map v1.0，所有代码和接线必须引用 |
| `ESTOP-FEEDBACK-CIRCUIT.md` | GPIO21 急停反馈隔离接线，禁止直读 36V 电门锁线 |
| `POLARITY-DEFINITIONS.md` | GPIO/MOS/光耦/控制器线极性定义 |
| `PWM-OUTPUT-CALIBRATION.md` | PWM→0-5V 油门频率、分辨率、映射公式、校准流程 |
| `protocols/` | DS600、UWB、H5、JY61P 协议/接口文件 |
| `CURRENT-PROJECT-SPEC.md` / `CURRENT-PROJECT-SPEC.html` | 项目总纲：目标、架构、安全、电源 |
| `CURRENT-PARTS-LIST.md` | 当前配件清单：已有、待补、线束分组 |
| `CURRENT-WIRING-AI.md` / `CURRENT-WIRING.html` | 当前接线方案：给 AI/给人看的接线说明 |
| `ASSEMBLY-WIRING-MINDMAP.html` | 当前中文接线思维导图：总接线、ESP32 引脚、分压/上拉/下拉/电容/急停反馈小电路 |
| `complete-wiring.svg` | 当前中文总接线 SVG 大图：模块关系总览，详细施工以思维导图和表格为准 |
| `complete-wiring-table.md` | 当前完整接线表：每个 ESP32 引脚、线束、电阻电容和模块端子 |
| `ASSEMBLY-WIRING-GUIDE.md` | 📦 装配接线完整流程图：按步骤装配思维导图 + 引脚速查表 + 上电顺序 |
| `OTA-UPDATE-SPEC.md` | OTA 正式发布规范：版本递增、构建、manifest/MD5、云端部署、H5 授权安装与失败排查 |
| `PERFBOARD-LAYOUT.html` / `PERFBOARD-LAYOUT.svg` | 当前 10×15cm 洞洞板元器件布局图；由 `gen_perfboard_layout.py` 生成并校验 |
| `CURRENT-BOX-DESIGN.md` | 控制盒外观、内部布局、接口、装配规则 |
| `CURRENT-FIRMWARE-SKILL-PLAN.md` | 固件模块、Profile、安装向导、测试计划、后续 skill 设计 |
| `CURRENT-FIRMWARE-ARCHITECTURE.md` / `CURRENT-FIRMWARE-ARCHITECTURE.html` | 固件代码架构：目录结构、模块边界、状态机、任务频率 |
| `skills/README.md` | 项目内 AI 技能包入口：给 Hermes/Claude/Copilot/其他 AI 调用，按 SaaS-Bench 风格执行开发、审查、排 bug、上电安全门控 |
| `.agents/skills/` | Codex 自动发现的轻量包装技能：负责快速触发、计划、锁定、diff 审查和云端 H5 部署隔离；详细技能仍在 `skills/` |
| `VERIFIED-LOCKS.md` | 已验证/安全关键方案锁定清单：板型、Pin Map、安全链、PWM 出口、UWB、OTA、云端 H5 部署、AI 技能门禁 |
| `AI-HANDOFF-MEMORY.md` | 项目内 AI 交接记忆：任何 AI 修改代码/架构/文档后，必须用 8-12 行短记录写清改动、验证、下一步 |
| `AI-AGENT-RUNBOOK.md` | Codex/Claude/Copilot/其他代码 AI 的固定启动 prompt 和交接记忆门禁；结束后运行 `python3 tools/check_ai_handoff.py` |
| `UWB-LEGACY-MIGRATION-REVIEW.md` | 旧 UWB 自动跟随项目迁移评估：可照抄/可借鉴/禁止照抄清单，以及 FollowBox 后续实施任务包 |
| `CLOUD-ASSIST-BACKLOG.md` | 后续云端困难场景协助待办：WiFi 上传 telemetry/图像，云端 AI/规则引擎给动作建议，ESP32 本地安全裁决 |
| `FOLLOWBOX-VISION-BACKLOG.md` | 三大远景待办：云训练/远程调试、云端轨迹记录→自动驾驶、自主交互(门检测/找人帮忙/买东西) |

## 文件归属与技能路由速查

| 文件/区域 | 先找谁 | 说明 |
|---|---|---|
| `CURRENT-PROJECT-ARCHITECTURE.md`, `FIRMWARE-SPEC.md`, `firmware/src/app/` | `skills/01-firmware-architecture-guardian` | 架构地图、模块边界、`main.cpp` 拆分和运行链路 |
| `firmware/src/safety/`, `firmware/src/control/`, `firmware/src/drive/` | `skills/03-safety-control-reviewer` | 安全门控、模式、混控、最终输出链；改动前看 `VERIFIED-LOCKS.md` |
| `PIN-MAP-V1.md`, `CURRENT-WIRING-AI.md`, `firmware/include/config/board_pins.h`, Profile | `skills/05-drive-power-calibration-engineer` | GPIO、电源、ADC、PWM、极性和校准；禁止旧 GPIO35/36/37/47/48 |
| `firmware/src/sensors/`, `protocols/` | `skills/04-sensor-protocol-integrator` | UWB/TOF/IMU/超声/雷达协议与 `SensorSnapshot` |
| `firmware/web/`, `firmware/src/web/`, `protocols/H5-API.md` | `skills/06-h5-telemetry-ui-engineer` | 车端 AP/局域网 H5；`firmware/data/` 只保留 tombstone，不是源码 |
| `cloud/`, `cloud/public/`, `cloud/firmware/`, `devspace.yaml` | `skills/06-h5-telemetry-ui-engineer` + `.agents/skills/followbox-cloud-h5-deploy` | 云端 H5、遥测、OTA 发布和 DevSpace；不能和车端 H5 混用部署 |
| `AI-HANDOFF-MEMORY.md`, `skills/`, `.agents/skills/`, `VERIFIED-LOCKS.md` | `skills/00-dispatcher`, `skills/07-code-review-debugger` | AI 交接、技能入口和锁检查；修改后必须跑门禁脚本 |
| `plans/cleanup/` | `skills/00-dispatcher` | 临时整理计划，完成或被新计划替代后可删除 |
| `output/`, `v/`, `zhiliao/` | `skills/07-code-review-debugger` | 历史证据/供应商资料/调试产物；先索引，不能第一轮盲删 |
| `firmware/.pio-core/`, `firmware/.pio/`, `vision_cam/.pio/`, `.codebuddy/db/` | `skills/07-code-review-debugger` | 本地可重建缓存；只在确认 ignored 后删除 |

## 当前硬件架构

```text
HOTRC DS600 接收机 PWM
  -> ESP32-S3-DevKitC-1
  -> PWM 转 0-5V 模拟量模块 ×2
  -> 左/右 36V 350W 有霍尔无刷控制器
  -> 左/右轮毂电机
```

辅助模块：

```text
UWB GC-P2304-GS-2 -> 跟随距离/方位
VL53L1X ×3 + TCA9548A -> 前向避障
HC-SR04 ×2 -> 左右侧辅助避障
JY61P -> 姿态/yaw 阻尼
ESP32-S3-CAM + OV5640 -> 独立视频
```

## 安全红线

- Antigravity P0 修订已写入 `FIRMWARE-SPEC.md`：GPIO35/36/37/47/48 不再作为电机驱动输出；急停反馈 GPIO21 必接；左右倒车线独立；需要预充/防火花；油门输出要死区/限幅/斜率控制。

- ESP32-S3 只接 5V/3.3V 和低压信号。
- ESP32-S3 没有真 DAC，左右油门必须通过 PWM 转 0-5V 模拟量模块。
- Schneider XB5AS542C 1NC 急停切控制器电门锁/使能线。
- 电机主电流不走洞洞板、排针、JST 小插头、杜邦线。
- DS600 PWM、HC-SR04 Echo、JY61P TX 若为 5V 信号，进 ESP32 前必须分压或电平转换；该物料属于 P0。
- 电池 ADC 统一使用 220k/10k 分压，覆盖 36V-60V；旧 130k/10k 在 48V 满电会超 ESP32 ADC 安全范围，禁止作为当前方案。
- 控制器 BAT- 主回路必须星型接地，转把 GND/ESP32 GND 只能作小电流参考，不能成为主负极断线后的回流路径。
- TCA9548A/VL53L1X I2C 必须外接 4.7k 上拉到 3.3V，固件必须实现 Bus Clear。
- JY61P 上电后必须静止 3 秒；UWB 与 DC-DC/Buck 至少保持 50mm 并做 5V 输出滤波。
- 10×15cm 洞洞板布局以 `PERFBOARD-LAYOUT.html` / `PERFBOARD-LAYOUT.svg` 为准；实际接线以 `ASSEMBLY-WIRING-MINDMAP.html`、`complete-wiring.svg` 和 `complete-wiring-table.md` 为准。旧错误 SVG/JSON 图作废，不能再引用。
- GPIO14/15/16/39 只能驱动 MOS/光耦/继电器输入，未装 MOS/光耦前禁止接控制器开关量线。
- GPIO21 急停反馈必须用第二触点干接点或光耦隔离，禁止直接读取 36V 电门锁线。
- ESP32-S3-CAM 只做视频，不做主控。
- 首次调试必须架空车轮。

## 下一步

1. 补齐 `CURRENT-PARTS-LIST.md` 里的 P0 物料。
2. 到货后拍 DS600 接收机、无刷控制器线标、轮毂电机线束、电池铭牌、急停背面。
3. 按 `CURRENT-WIRING-AI.md` 分阶段上电。
4. 写代码时以 `FIRMWARE-SPEC.md` 为唯一代码框架依据；不符合它的实现需要重构。
5. 让任何 AI 参与开发/审查/排 bug 前，先读 `.agents/skills/` 包装、`skills/README.md` 和 `VERIFIED-LOCKS.md`，再按路由加载对应 `skills/*/SKILL.md`。
6. 任何 AI 修改代码、架构、文档、Profile、协议或技能后，必须在 `AI-HANDOFF-MEMORY.md` 顶部追加 8-12 行短交接记录。
7. 用 Codex/Claude/Copilot/其他代码 AI 前先读 `AI-AGENT-RUNBOOK.md`；任务结束后运行 `python3 tools/check_ai_handoff.py` 和 `python3 tools/check_verified_locks.py`，不通过或未解释 warning 就不能算完成。
