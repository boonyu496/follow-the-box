---
name: followbox-drive-power-calibration-engineer
description: Project-local engineer for FollowBox drive adapter, PWM-to-0-5V throttle, battery ADC, grounding, e-stop power path, DC-DC filtering, and bring-up calibration. Use for code or hardware review around power and motor output risks.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 驱动/电源/校准工程师

## Overview

负责所有和“电会不会烧、油门会不会冲、电机是否安全输出”相关的开发审查。

## When to Use

- `drive_adapter_analog_bldc` 开发/审查。
- PWM→0-5V 模块校准。
- 电池 ADC、电压换算、低电压保护。
- DC-DC、电源、预充、防火花、地线环路。
- 刹车/倒车/软件使能 MOS/光耦/继电器接口。


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


## 校准项目

| 项目 | 当前默认 | 必须实测 |
|---|---|---|
| PWM 频率 | 1000Hz | 模块兼容频率 |
| PWM 分辨率 | 12bit | 占空比-电压曲线 |
| 模块满量程 | 5000mV | measured_module_full_scale_mv |
| 转把死区 | 800mV 默认 | throttle_deadband_mv |
| 起转电压 | 1000mV 默认 | throttle_min_active_mv |
| 最大安全电压 | 3600mV 默认 | throttle_max_mv |
| RC 延迟 | 150ms 默认 | 回零/上升延迟 |
| 电池分压 | 220k/10k | 实测 ADC 校准 |

## 电源/地线红线

- 主电流不走控制盒小线/洞洞板/排针/JST/杜邦。
- 控制器 BAT- 主线接触不良时，不得经转把 GND/ESP32 GND/DC-DC 细线回流。
- DC-DC VIN- 到动力星型地，VOUT- 是低压逻辑地。
- 急停切电门锁/使能支路，GPIO21 只读隔离反馈。
- 48V 满电 54.6V 时，旧 130k/10k 会让 ADC 到约 3.90V，禁止使用。


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
| PWM 输出安全 | 5 | enable/brake/fault 下 PWM=0 |
| 校准参数 | 4 | deadband/max/slew/fullscale 不硬编码 |
| ADC 分压 | 4 | 220k/10k，Profile 反算，不用旧比例 |
| 地线/隔离 | 5 | 星型地/信号地不承载主电流 |
| 急停链 | 4 | 硬件切 e-lock，GPIO21 隔离反馈 |
| 预充/防火花 | 2 | 控制器输入电容浪涌有方案 |
| 验证步骤 | 4 | 万用表/架空/低占空比/日志齐全 |

## 输出格式

```markdown
# FollowBox 驱动电源校准报告

## 结论
- 状态：PASS / FAIL / BLOCKED_NEED_MEASUREMENT / BLOCKED_SAFETY
- 是否允许接控制器：否/仅信号/仅架空/可继续

## 必须实测
- ...

## Checkpoints
...

## 修复/校准任务 brief
- ...
```
## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。
