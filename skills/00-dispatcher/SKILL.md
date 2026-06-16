---
name: followbox-project-dispatcher
description: Project-local dispatcher for FollowBox AI skills. Use first for any coding, review, debugging, wiring, calibration, or test-safety task; routes to the right SaaS-Bench-style project skill and blocks unsafe shortcuts.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 项目 AI 总调度员

## Overview

本技能是项目内所有 AI 技能入口。任何 AI 接到 FollowBox 任务时先读它，再分派给具体角色。

## When to Use

- 用户说“开发代码”“审查代码”“排 bug”“帮我让 AI 写任务”“看日志”“能不能上电/试车”。
- 任务跨越固件、硬件、传感器、安全、H5、校准多个领域。
- 输入材料不完整，需要判断该先要什么证据。


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


## 路由规则

| 条件 | 下一个技能 |
|---|---|
| 要创建/修改固件架构、目录、模块边界 | `01-firmware-architecture-guardian/SKILL.md` |
| 要写开发任务、委托 Claude/Copilot | `02-firmware-implementation-planner/SKILL.md` |
| 涉及 safety_manager/mode_manager/motion_mixer/drive_adapter | `03-safety-control-reviewer/SKILL.md` |
| 涉及 UWB/TOF/I2C/超声/IMU/DS600 协议驱动 | `04-sensor-protocol-integrator/SKILL.md` |
| 涉及 PWM→0-5V、电池 ADC、地线、电源、预充、校准 | `05-drive-power-calibration-engineer/SKILL.md` |
| 涉及 H5 页面、WebSocket、遥测、状态 API | `06-h5-telemetry-ui-engineer/SKILL.md` |
| 有代码 diff、build log、serial log、异常现象 | `07-code-review-debugger/SKILL.md` |
| 准备上电、架空试车、遥控/自动跟随测试 | `08-bringup-test-safety-officer/SKILL.md` |

## 输出格式

```markdown
# FollowBox AI 调度结果

## 结论
- 状态：PASS / BLOCKED_NEED_CONTEXT / BLOCKED_SAFETY
- 应调用技能：...
- 原因：...

## 任务包
| 字段 | 内容 |
|---|---|
| task_id | ... |
| stage | ... |
| risk_level | ... |
| artifacts | ... |

## 缺失材料
- ...

## 下一步
- ...
```

## Checkpoints

| 检查项 | 权重 | PASS 条件 |
|---|---:|---|
| 任务分类 | 3 | 明确分到正确技能 |
| 权威文件 | 3 | 指定需读取的项目文件 |
| 安全风险 | 4 | 识别是否涉及上电/运动/电机输出 |
| 缺失证据 | 3 | 缺什么列清楚，不猜 |
| 下一步 | 2 | 可执行、具体 |

## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。

## Blocking Conditions

- 不知道任务类型且缺材料 → `BLOCKED_NEED_CONTEXT`。
- 涉及电机输出/动力上电但未说明安全状态 → `BLOCKED_SAFETY`。
- 需要代码实现但没有项目路径/目标文件且无法检索 → `BLOCKED_NEED_CONTEXT`。
