---
name: followbox-firmware-architecture-guardian
description: Project-local architecture guardian for FollowBox firmware. Use before creating or reviewing firmware structure, module boundaries, task scheduling, data flow, or any change that might violate FIRMWARE-SPEC.md.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 固件架构守门员

## Overview

负责守住固件代码框架，防止 AI 把项目写成一个大 `main.cpp`、散落 GPIO、绕过安全门控或恢复旧方案。

## When to Use

- 创建 `firmware/` 工程骨架。
- 审查目录结构、模块边界、include 关系。
- 重构 app/safety/control/drive/sensors/web/storage/telemetry。
- 判断某个实现是否违反 `FIRMWARE-SPEC.md`。


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


## 必须检查的架构链路

唯一运动链路：

```text
输入/传感器 -> SystemState -> safety_manager 预裁决
-> mode_manager -> command_pipeline -> obstacle_manager/speed_limiter
-> motion_mixer -> safety_manager.applyFinalGate()
-> drive_adapter_analog_bldc -> GPIO/PWM 输出
```

## 硬性架构红线

- `main.cpp` 只做启动入口，不写业务逻辑。
- 每个模块独立 `.h/.cpp`。
- `board_pins.h` 是唯一 GPIO 定义位置。
- `drive_adapter_analog_bldc` 是唯一能写 GPIO12/13/14/15/16/39 的模块。
- 所有运动命令必须经过 `applyFinalGate()`。
- lost-link 必须按当前模式裁决：DS600/H5/UWB 只在各自当前模式丢失时停车，非当前源离线不全局停车。
- H5 源码在 `firmware/web/`，不能大段 raw HTML 塞进 C++。


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
| 权威文件读取 | 3 | 已读取 FIRMWARE-SPEC、CURRENT-WIRING、PIN-MAP |
| 目录结构合规 | 4 | 模块目录和职责符合规范 |
| 运动链路合规 | 5 | 没有绕过 safety/mixer/drive_adapter |
| GPIO 所有权 | 4 | 引脚只在 board_pins 定义，输出只在 drive_adapter 写 |
| FreeRTOS/共享状态 | 3 | 控制环不被 I2C/Web/日志阻塞，共享状态线程安全 |
| HTML/Web 资源 | 2 | H5 不塞 main.cpp，不绕过安全 |
| 可测试性 | 4 | 纯逻辑测试、模块边界、构建命令明确 |

## 输出格式

```markdown
# FollowBox 架构审查报告

## 结论
- 状态：PASS / FAIL / BLOCKED_NEED_CONTEXT
- 最大风险：...

## Checkpoints
| 检查项 | 权重 | 结果 | 证据 |
|---|---:|---|---|

## 架构问题
- ...

## 必改项
- ...

## 给实现 AI 的约束
- ...
```
## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。
