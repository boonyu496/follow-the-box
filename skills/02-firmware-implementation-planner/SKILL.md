---
name: followbox-firmware-implementation-planner
description: Project-local implementation planner for FollowBox firmware. Use to turn requirements into Claude/Copilot/VS Code implementation tasks with exact files, constraints, and verification, without dumping code examples.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 固件实现任务设计员

## Overview

把需求拆成可交给 Claude/Copilot/VS Code 的小任务，输出正式任务 brief。它不负责盲目写代码；先冻结范围、文件、接口、验收。

## When to Use

- 用户要“开始写固件”“让 Claude/Copilot 实现”。
- 需要创建或修改某个模块。
- 需要把文档规范转成任务清单。
- 需要分阶段开发计划。


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


## 推荐开发阶段

| 阶段 | 内容 | 是否碰硬件输出 |
|---|---|---:|
| P0-1 | PlatformIO 骨架、目录、board_pins、types、system_state | 否 |
| P0-2 | safety_manager/mode_manager/motion_mixer 纯逻辑测试 | 否 |
| P0-3 | DS600 PWM 输入、H5 低速命令、profile_store | 只读/低风险 |
| P0-4 | drive_adapter_analog_bldc，架空车轮低占空比验证 | 是，高风险 |
| P0-5 | TOF/超声/IMU/UWB 只读快照和超时 | 只读 |
| P0-6 | 安装向导、校准、AUTO_FOLLOW 低速解锁 | 是，安全关键 |


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


## Claude/Copilot 任务 brief 模板

```markdown
# FollowBox 实现任务

目标：
- ...

必须先读：
- /mnt/d/car/Follow the box/FIRMWARE-SPEC.md
- /mnt/d/car/Follow the box/CURRENT-WIRING-AI.md
- /mnt/d/car/Follow the box/PIN-MAP-V1.md
- ...

涉及文件：
- create/modify exact paths

接口/连线依据：
- ...

硬约束：
- main.cpp 只做入口
- GPIO 只在 board_pins.h
- 不绕过 safety_manager.applyFinalGate()
- 不恢复旧 GPIO47/48/35/36/37
- 不编造 UWB parser
- 不跳过测试/验证

验收：
- 编译命令：...
- 测试命令：...
- 必须返回 changed-file list
- 必须返回验证结果
```

## Weighted Checkpoints

| 检查项 | 权重 | PASS 条件 |
|---|---:|---|
| 目标清晰 | 3 | 一次只做一个可验证任务 |
| 文件明确 | 4 | 列出准确 create/modify 路径 |
| 接口依据 | 4 | GPIO/Profile/协议/文档依据明确 |
| 安全边界 | 5 | 标记是否碰电机/急停/安全门控 |
| 测试计划 | 4 | build/test/bench 验收具体 |
| 代理约束 | 3 | 给 Claude/Copilot 的禁止项明确 |
| 用户审批 | 2 | 高风险任务先等待用户确认 |

## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。

## Blocking Conditions

- 目标过大，如“把全部固件写完” → 拆分后再执行。
- 涉及 motor output/e-stop/GPIO 安全但未审批 → `BLOCKED_SAFETY`。
- 缺目标文件/现有代码树 → 先检索项目。
