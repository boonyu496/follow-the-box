# FollowBox 项目内 AI 技能包

这些技能文件放在项目目录内，供 Hermes、Claude Code、Copilot CLI、VS Code 内 AI、其他代码代理直接读取。  
入口文件：`D:\car\Follow the box\skills\README.md`

Codex 自动发现入口另有一层轻量包装：`D:\car\Follow the box\.agents\skills\`。
`.agents/skills/` 只负责触发、路由、计划、锁定和云端部署护栏；详细项目事实仍以本目录和权威文档为准，避免重复上下文。

## 使用方式

任何 AI 接到 FollowBox 开发/审查/排 bug 任务时，先读本文件，然后按任务类型加载对应 `SKILL.md`。如果要运行 Codex/Claude/Copilot/其他代码代理，先读项目根目录 `AI-AGENT-RUNBOOK.md`，使用其中的固定启动 prompt，并在结束后运行 `python3 tools/check_ai_handoff.py` 做交接门禁。

```text
1. 先读 skills/README.md
2. Codex 优先读取 `.agents/skills/` 中匹配任务的包装 skill，再按路由表选择一个或多个详细技能
3. 读 `VERIFIED-LOCKS.md` 判断是否触及已验证/安全关键方案
4. 再读 README.md、FIRMWARE-SPEC.md、CURRENT-WIRING-AI.md、PIN-MAP-V1.md、profiles/example_bldc_analog_36v.yaml
5. 输出 SaaS-Bench 风格任务包/checkpoints/阻塞条件/验证步骤
```

### 轻量读取规则

为避免每个小任务都吞掉过多上下文，按风险分层读取：

- 只读问答/文档定位：读 `README.md`、`AI-HANDOFF-MEMORY.md`、本文件和相关 skill；需要引用事实时再读对应权威文件。
- 普通文档/任务包修改：再读 `FIRMWARE-SPEC.md`、`CURRENT-WIRING-AI.md`、`PIN-MAP-V1.md` 和相关协议/校准文件。
- 涉及固件代码、安全门控、GPIO、电源、上电、运动测试：必须全量读取本节列出的权威文件，并按对应 skill 的 Blocking Conditions 执行。

### 权威来源规则

本 README 和各 `skills/*/SKILL.md` 中的“当前样机事实”只是便于快速恢复上下文的摘要。若与权威文件冲突，必须以以下顺序裁决：

```text
PIN-MAP-V1.md
  -> FIRMWARE-SPEC.md
  -> CURRENT-WIRING-AI.md
  -> protocols/*.md / POLARITY-DEFINITIONS.md / PWM-OUTPUT-CALIBRATION.md / ESTOP-FEEDBACK-CIRCUIT.md
  -> profiles/example_bldc_analog_36v.yaml
  -> skills/*.md 摘要
```

发现冲突时，不要直接按 skill 摘要继续实现；应输出 `FAIL` 或 `BLOCKED_NEED_CONTEXT`，并先修正权威文档/摘要的一致性。


## AI 交接记忆规则（必须执行）

任何 AI 修改代码、架构、接线文档、Profile、协议、测试方案或技能文件后，必须更新项目根目录：

```text
AI-HANDOFF-MEMORY.md
```

规则：
- 在 `## 最新交接记录` 下方追加到顶部。
- 每条只写 8-12 行，避免浪费 token。
- 必须包含：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 不贴完整 diff，不贴大段代码，不写密钥。
- 如果没有验证，明确写 `验证：未验证` 和下一步验证动作。
- 只读分析任务只有在发现重要结论、安全阻塞或改变后续路线时才写；普通问答不写，避免记忆文件膨胀。

所有具体技能都必须把“更新 AI-HANDOFF-MEMORY.md”作为完成条件。

## 已验证锁定规则（必须执行）

任何 AI 在修改前都要检查：

```text
VERIFIED-LOCKS.md
```

- 触及锁定项：任务计划和交接记录必须写 `锁定影响` 与 `解锁理由`。
- 未触及锁定项：交接记录写 `锁定影响：无`。
- 收尾运行：`python tools/check_verified_locks.py`。
- 干净工作区/CI 可使用严格模式：`python tools/check_verified_locks.py --strict`。

## 云端 H5 部署隔离规则（必须执行）

涉及 `cloud/`、`cloud/public/`、`cloud/firmware/`、DevSpace、SCP、PM2、Kubernetes、云端 OTA 发布时：

- 只允许部署 FollowBox 的 `cloud/` 服务和 `cloud/public/` H5，不得把仓库父目录或其他项目目录一起上传。
- 目标目录必须是 FollowBox 云端根目录（通常末尾为 `followbox-cloud`）；禁止覆盖 `/www/wwwroot`、`/www/server`、用户 home 或多项目目录。
- 禁止 `pm2 restart all`；只能重启 FollowBox 对应进程/容器。
- 禁止上传 `.env`、`.env.local`、PEM/key、token、`node_modules/`、日志、缓存或其他项目文件。
- 部署后必须验证 `/api/health`、`deploy-version.txt` 或 `devspace run cloud-check`。

## 技能路由表

| 任务类型 | 读取技能 | 作用 |
|---|---|---|
| 不确定该找哪个 AI 角色 | `00-dispatcher/SKILL.md` | 总调度，按风险和材料分派 |
| 固件目录/模块边界/架构冻结 | `01-firmware-architecture-guardian/SKILL.md` | 防止代码变成大 main.cpp，守住 FIRMWARE-SPEC |
| 写代码任务包/委托 Claude-Copilot | `02-firmware-implementation-planner/SKILL.md` | 把需求变成可执行开发任务 brief |
| safety/mode/mixer/drive 输出审查 | `03-safety-control-reviewer/SKILL.md` | 审查运动链路、安全门控、PWM 输出所有危险点 |
| UWB/TOF/超声/IMU/DS600 协议与驱动 | `04-sensor-protocol-integrator/SKILL.md` | 防止编造协议、阻塞 I2C、串错 GPIO |
| 油门 PWM、ADC、电池、电源、校准 | `05-drive-power-calibration-engineer/SKILL.md` | 审查 0-5V、分压、斜率、地线、预充 |
| H5/WebSocket/遥测/页面 | `06-h5-telemetry-ui-engineer/SKILL.md` | H5 只能请求低速点动，不能绕过安全 |
| 代码审查/排 bug/日志分析 | `07-code-review-debugger/SKILL.md` | 读 diff/log/build 输出，给根因和修复任务 |
| 上电/台架/试车前安全许可 | `08-bringup-test-safety-officer/SKILL.md` | 决定是否允许逻辑上电、动力上电、架空试车 |

## 推荐调用顺序

### 新功能开发
```text
00-dispatcher
  -> 01-firmware-architecture-guardian
  -> 02-firmware-implementation-planner
  -> 实施代码
  -> 07-code-review-debugger
  -> 08-bringup-test-safety-officer（若涉及硬件/运动）
```

### 传感器/协议问题
```text
00-dispatcher
  -> 04-sensor-protocol-integrator
  -> 07-code-review-debugger
  -> 08-bringup-test-safety-officer（若要上电验证）
```

### 电机/油门/急停/安全门控问题
```text
00-dispatcher
  -> 03-safety-control-reviewer
  -> 05-drive-power-calibration-engineer
  -> 07-code-review-debugger
  -> 08-bringup-test-safety-officer
```


## FollowBox 项目硬约束（所有技能共同遵守）

项目路径：
- Windows: `D:\car\Follow the box`
- WSL: `/mnt/d/car/Follow the box`

每次任务必须优先读取：
- `README.md`
- `FIRMWARE-SPEC.md`
- `CURRENT-WIRING-AI.md`
- `PIN-MAP-V1.md`
- `profiles/example_bldc_analog_36v.yaml`
- 对应协议/校准/极性文档：`protocols/*.md`、`POLARITY-DEFINITIONS.md`、`PWM-OUTPUT-CALIBRATION.md`、`ESTOP-FEEDBACK-CIRCUIT.md`

下面事实是快速摘要，不是最终权威；若与上面文件冲突，以 `PIN-MAP-V1.md`、`FIRMWARE-SPEC.md`、`CURRENT-WIRING-AI.md` 和协议/校准/极性文件为准。

当前样机事实：
- 主控：普通 ESP32-S3-DevKitC-1；ESP32-S3-CAM 只做视频，不做安全主控。
- 驱动：左右两个 36/48V 350W 有霍尔无刷控制器；ESP32 通过两路 PWM→0-5V 模拟量模块控制转把输入。
- 遥控：HOTRC DS600 PWM，P0 只接 CH1-CH5 到 GPIO4-GPIO8；GPIO9 是超声共享 TRIG，CH6 首版不接。
- 输出：GPIO12/13 左右油门 PWM；GPIO14 刹车；GPIO15/16 左右独立倒车；GPIO39 软件使能。
- 禁止旧方案：GPIO35/36/37/47/48 不得作为电机驱动输出。
- 安全：Schneider XB5AS542C 1NC 急停切控制器电门锁/使能线，不串两个控制器 BAT+ 主电流；GPIO21 急停反馈必须隔离或第二触点干接点。
- 电池 ADC：统一 220k/10k，覆盖 36V-60V；旧 130k/10k 对 48V 满电不安全。
- I2C：TCA9548A + VL53L1X ×3，SDA/SCL 必须 4.7kΩ 外接上拉到 3.3V；固件必须 Bus Clear，因首版无 XSHUT。
- 地线：动力 BAT- 必须星型/主负极汇流；转把 GND/ESP32 GND 只能作小电流参考，不能成为控制器 BAT- 断线回流路径。
- IMU：JY61P 上电静止 3 秒后再信任 yaw。
- UWB：GC-P2304-GS-2 UART，协议未抓包/冻结前禁止编造 parser；UWB 与 DC-DC/Buck 至少 50mm 并做 5V 输出滤波。
- 运动测试：首次调试必须架空车轮；任何运动许可必须走安全门控。



## SaaS-Bench 风格任务契约

所有角色输出必须包含：
1. `task_id`：唯一任务 ID。
2. `stage`：architecture / coding / review / debug / bench / calibration / test。
3. `artifacts`：读取过的文档、代码、日志、照片、测试结果。
4. `risk_level`：low / medium / high / safety-critical。
5. `weighted checkpoints`：加权评分，不凭感觉给结论。
6. `blocking conditions`：证据不足时必须 BLOCKED，不许硬猜。
7. `next action`：下一步明确到文件、命令、照片、测量或任务 brief。

标准状态：
- `PASS`：证据充分，可继续。
- `BLOCKED_NEED_CONTEXT`：缺文件/日志/照片/实测。
- `BLOCKED_SAFETY`：安全条件不足，不允许上电/运动/写危险输出。
- `NEEDS_IMPLEMENTATION`：任务设计完成，等待实现。
- `NEEDS_VERIFICATION`：已改但未验证。
- `FAIL`：发现违反架构/安全/硬件事实的问题。


## 全局禁止

- 禁止跳过 `FIRMWARE-SPEC.md` 直接写固件。
- 禁止把多个模块塞进一个 `main.cpp`。
- 禁止任何模块绕过 `safety_manager.applyFinalGate()` 直接写电机输出。
- 禁止恢复旧 GPIO47/48/35/36/37 输出方案；完整禁用清单：GPIO35、GPIO36、GPIO37、GPIO47、GPIO48 均不得作为电机驱动输出。
- 禁止编造 UWB 协议帧格式。
- 禁止未架空车轮时做电机运动测试。
- 禁止从不完整照片/日志得出“安全可上电/可试车”。
