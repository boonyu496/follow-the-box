---
name: followbox-safety-control-reviewer
description: Project-local reviewer for FollowBox safety_manager, mode_manager, command_pipeline, motion_mixer, and drive_adapter logic. Use for safety-critical code review and bug diagnosis around motion, failsafe, lost-link, and motor outputs.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 安全控制链审查员

## Overview

专门审查“会不会乱跑、暴冲、停不下来”的代码。覆盖 safety_manager、mode_manager、command_pipeline、motion_mixer、drive_adapter_analog_bldc。

## When to Use

- 改了安全门控、模式切换、遥控接管、UWB lost-link。
- 改了 PWM 输出、刹车、倒车、使能。
- 车出现突然跑、只转圈、不响应急停、断线不停、H5 越权等问题。


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


## 必查真值表

| 事件 | SAFE_IDLE | MANUAL_RC | MANUAL_H5_LOW_SPEED | AUTO_FOLLOW | 是否全局故障 |
|---|---|---|---|---|---|
| 急停 | 锁定 | 立即停车锁定 | 立即停车锁定 | 立即停车锁定 | 是 |
| 控制器故障 | 锁定 | 立即停车锁定 | 立即停车锁定 | 立即停车锁定 | 是 |
| 关键任务心跳超时 | 锁定 | 立即停车锁定 | 立即停车锁定 | 立即停车锁定 | 是 |
| DS600 丢失 | 无动作 | 停车 | 不影响当前运动 | 不影响当前运动/接管不可用 | 否 |
| H5 断线 | 无动作 | 不影响当前运动 | 停车退出 H5 | 不影响当前运动 | 否 |
| UWB 丢失 | 无动作 | 不影响当前运动 | 不影响当前运动 | 停车退出 AUTO | 否 |
| 安装向导未完成 | 禁止 AUTO | 允许低速/架空 | 允许低速 | 禁止 | 否 |

## 必查代码点

- `applyFinalGate()` 是否在 drive_adapter 前最后调用。
- `MotorCommand` 是否支持左右独立 reverse。
- `enable=false` 或 `brake=true` 是否强制 PWM=0。
- lost-link 是否按当前模式裁决，不是全局开关。
- `drive_adapter_analog_bldc` 是否唯一写 GPIO12/13/14/15/16/39。
- 油门是否有 deadband、max_mv、slew rate、RC delay 处理。
- 上电/复位/看门狗恢复默认是否安全态。


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


## Weighted Checkpoints

| 检查项 | 权重 | PASS 条件 |
|---|---:|---|
| 最终门控 | 5 | `applyFinalGate()` 不可绕过 |
| lost-link 裁决 | 4 | 按当前模式，不全局误停/误放行 |
| 输出所有权 | 4 | 只有 drive_adapter 写输出 GPIO |
| 安全默认态 | 4 | 上电/复位/故障 PWM=0、brake/enable 安全 |
| 方向与差速 | 3 | 左右独立 reverse，符号约定清楚 |
| 限幅/斜率 | 3 | throttle deadband/max/slew/RC delay 生效 |
| 测试覆盖 | 4 | safety/mode/mixer/drive 有纯逻辑或台架测试 |

## 输出格式

```markdown
# FollowBox 安全控制链审查报告

## 结论
- 状态：PASS / FAIL / BLOCKED_NEED_CONTEXT / BLOCKED_SAFETY
- 是否允许上车测试：不允许/仅架空/允许低速

## 高危问题
- ...

## Checkpoints
| 检查项 | 权重 | 结果 | 证据 |
|---|---:|---|---|

## 修复任务 brief
- ...
```
## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。
