---
name: followbox-sensor-protocol-integrator
description: Project-local integrator for FollowBox DS600, UWB, TOF/I2C, ultrasonic, IMU, and camera-link code. Use for sensor driver development/review/debugging and protocol handling without hallucinating hardware protocols.
version: 1.0.0
author: FollowBox Project
license: MIT
---

# FollowBox 传感器与协议集成员

## Overview

负责所有输入/传感器/协议驱动，核心目标是：只读快照、不卡控制环、不编造协议、不让错误传感器拖死系统。

## When to Use

- DS600 PWM 输入、失控保护、通道映射。
- UWB GC-P2304-GS-2 串口解析/抓包/距离方位。
- TCA9548A + VL53L1X 三路 TOF。
- HC-SR04 超声。
- JY61P IMU。
- ESP32-S3-CAM 在线状态/视频链接。


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


## 传感器硬规则

- UWB：UART GPIO17/18；协议未冻结前只允许 raw log/mock parser，禁止猜帧头/校验/字段。
- TOF：VL53L1X ×3 经 TCA9548A；I2C GPIO10/11；外接 4.7kΩ 上拉；必须 Bus Clear；非阻塞读取。
- 超声：GPIO9 共享 TRIG；Echo GPIO40/41，必须分压/电平转换。
- IMU：JY61P TX -> GPIO42；若 TX 5V 必须分压；上电静止 3 秒；yaw_sign 安装向导确认。
- DS600：CH1-CH5 -> GPIO4-GPIO8；若 PWM 5V 必须分压；CH6 首版不接。
- 摄像头：ESP32-S3-CAM 独立视频，不能做安全主控；视频断流不影响运动安全门控。


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
| 协议依据 | 4 | 读取对应 `protocols/*.md`，没有编造 |
| 引脚/电平 | 4 | 与 PIN-MAP 一致，5V 风险处理 |
| 非阻塞 | 4 | 不阻塞 control_task/sensor_task |
| 超时/在线状态 | 3 | 每个传感器有 stale/online/error |
| I2C 恢复 | 3 | Bus Clear/重初始化策略明确 |
| 快照边界 | 3 | 只产生 snapshot，不直接控制电机 |
| 测试/日志 | 4 | 有 raw log、单模块测试或仿真输入 |

## Debug Patterns

| 症状 | 优先假设 | 下一步 |
|---|---|---|
| 三个 TOF 全不读 | I2C 上拉/供电/地址/TCA 通道 | 万用表 + I2C scan + Bus Clear log |
| 单个 TOF 卡死 | 该通道传感器拉低 SDA | Bus Clear + 换通道验证 |
| UWB 跳变/丢包 | 协议未冻结/EMI/DC-DC 纹波/天线遮挡 | raw 串口 log + 供电纹波 + 距离 1m/2m 测试 |
| DS600 无响应 | PWM 电平/通道顺序/GPIO/分压 | 示波器/逻辑分析或 pulseIn 测试 |
| IMU yaw 漂移 | 上电晃动/波特率/坐标方向 | 静置重启 + yaw_rate log |

## 输出格式

```markdown
# FollowBox 传感器/协议审查报告

## 结论
- 状态：PASS / FAIL / BLOCKED_NEED_LOGS / BLOCKED_NEED_HARDWARE_EVIDENCE

## Checkpoints
...

## 协议/引脚依据
- ...

## 风险与修复 brief
- ...
```
## AI 交接记忆完成条件

如果本技能执行过程中修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须更新项目根目录 `AI-HANDOFF-MEMORY.md`：

- 在 `## 最新交接记录` 下方追加到顶部。
- 控制在 8-12 行以内。
- 写清：改动、文件、架构影响、安全影响、验证、当前状态、下一步。
- 没有验证就写 `验证：未验证`，不能假装通过。
