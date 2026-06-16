# Follow the Box 固件与 Skill 设计草案

> `FIRMWARE-SPEC.md` 已经是正式代码框架总规范；本文件只保留固件目标、Profile、安装向导、测试计划和后续 skill 规划。写代码时以 `FIRMWARE-SPEC.md` 为准。

## 1. 固件目标

首版固件只做低速安全闭环：

- DS600 实体遥控输入。
- H5 本地状态和低速控制。
- UWB 跟随距离/方位。
- 前向 TOF + 侧向超声避障。
- JY61P 姿态/yaw 阻尼。
- 双路无刷控制器油门输出。
- 急停/故障/断线/低电压保护。
- ESP32-S3-CAM 在线状态和视频链接显示。

不做复杂无人车导航、高速远程驾驶、AI 自主决策。

## 2. 代码架构与模块划分

固件必须按模块拆文件，不能把业务逻辑堆进 `main.cpp`。正式代码框架以 `FIRMWARE-SPEC.md` 为准；架构草案可参考 `CURRENT-FIRMWARE-ARCHITECTURE.md`。

核心分层如下：

```text
main.cpp
  -> app/app.cpp                    # 初始化与任务创建
  -> app/scheduler.cpp              # 固定周期调度
  -> safety/safety_manager.cpp      # 急停/故障/断线/低电压/障碍安全门
  -> app/mode_manager.cpp           # 手动/网页/自动跟随模式选择
  -> app/command_pipeline.cpp       # 输入合成和优先级裁决
  -> control/motion_mixer.cpp       # 前进/转向 -> 左右轮
  -> drive/drive_adapter_analog_bldc.cpp
  -> sensors/*                      # UWB/TOF/超声/IMU/电池/摄像头在线
  -> web/*                          # H5 状态和低速点动
  -> storage/*                      # Profile/校准参数
```

硬规则：

- `main.cpp` 只做入口，目标 80-120 行。
- `drive_adapter_analog_bldc` 是唯一能写安全重映射后油门 PWM 输出的模块；旧 GPIO47/GPIO48 方案作废。
- DS600、H5、UWB 跟随都只能输出 `MotionIntent`，不能直接控制电机。
- 所有运动命令必须先经过 `safety_manager`。
- H5 页面源码放 `web/`，不要写成 `main.cpp` 里的大 raw string。

## 3. Drive Adapter

当前样机使用 `ANALOG_BLDC`：

```text
MotorCommand:
  left_target: -1.0..1.0
  right_target: -1.0..1.0
  left_reverse: bool
  right_reverse: bool
  enable: bool
  brake: bool

MotorFeedback:
  bus_voltage
  fault
  feedback_valid
```

输出：

```text
GPIO12 -> 左 PWM -> PWM转0-5V -> 左控制器转把信号
GPIO13 -> 右 PWM -> PWM转0-5V -> 右控制器转把信号
GPIO14 -> 刹车输出，经 MOS/光耦
GPIO15 -> 左倒车输出，经 MOS/光耦
GPIO16 -> 右倒车输出，经 MOS/光耦
GPIO39 -> 软件使能，经 MOS/光耦
GPIO21 -> 急停状态反馈，P0 必接
```

安全默认：

- 上电 PWM = 0。
- 未解锁自动跟随前 PWM = 0。
- 急停、故障锁定、关键任务心跳超时、障碍过近 -> PWM = 0。
- DS600/H5/UWB 丢失必须按当前模式裁决：MANUAL_RC 中 DS600 丢失停车；MANUAL_H5_LOW_SPEED 中 H5 丢失停车；AUTO_FOLLOW 中 UWB 丢失停车。非当前控制源离线只更新状态或禁用对应模式，不得全局停车。
- 故障后需要人工恢复，不自动继续走。

## 4. Profile 配置

当前唯一 P0 样机 Profile：`profiles/example_bldc_analog_36v.yaml`。本文件不再复制完整 YAML，避免与正式 Profile/Schema 分叉。

正式写代码时必须读取：

- `FIRMWARE-SPEC.md` 的 Profile Schema v1.0。
- `profiles/example_bldc_analog_36v.yaml` 的当前样机配置。
- `PIN-MAP-V1.md` 的唯一 Pin Map。
- `POLARITY-DEFINITIONS.md` 的 MOS/光耦/控制器线极性定义。
- `PWM-OUTPUT-CALIBRATION.md` 的 PWM 频率、分辨率、占空比到毫伏映射和 NVS 校准字段。

Profile 必须包含这些关键组：

```yaml
pins:        # DS600 CH1-CH5、GPIO9 超声 TRIG、GPIO12/13 油门、GPIO14/15/16/39 开关量、GPIO21 急停反馈
polarity:    # mcu_*_active_level 与 controller_*_line_active 分开
throttle:    # pwm_frequency_hz、pwm_resolution_bits、deadband/min/max/slew/rc_delay/calibrated_required_for_auto_follow
safety:      # estop_feedback_required、estop_feedback_type、estop_fault_on_open_wire
remote:      # physical_lost_stop_ms、h5_lost_stop_ms
sensors:     # uwb/tof/ultrasonic/imu 超时和安装向导参数
```

## 5. 避障状态机

| 状态 | 条件 | 动作 |
|---|---|---|
| CLEAR | 前方安全 | 正常遥控/跟随 |
| SLOW | 前方 < 1000mm | 限速 |
| BLOCKED | 前方 < 500mm | 停止前进，判断左右空间 |
| BYPASS_TURN | 一侧可通 | 低速偏转 30-45° |
| BYPASS_FORWARD | 偏转完成 | 低速绕过障碍 |
| RECOVER | 障碍解除 | 重新对准 UWB 方位 |
| WAIT | 无安全通道/传感器异常 | 停车等待/报警 |

优先级：

```text
急停 > 故障 > 手动接管 > 避障 > 自动跟随
```

## 6. 安装向导

未完成安装向导时：

```text
允许：查看状态、传感器只读、低速点动
禁止：自动跟随、绕障前进、远程公网遥控
```

必过项目：

1. 架空车轮确认。
2. 急停测试。
3. 左轮方向测试。
4. 右轮方向测试。
5. 油门 0-5V 输出校准。
6. UWB 1m/2m 距离测试。
7. UWB 左右方向测试。
8. 前方 1m 限速、0.5m 停车测试。
9. DS600 遥控丢失停车测试。
10. H5 断线停车测试。
11. 保存 Profile。
12. 解锁自动跟随。

## 7. 视频和语音边界

视频模块独立运行：

```text
ESP32-S3 主控：控制、安全、传感器、H5 遥测
ESP32-S3-CAM：MJPEG 视频流，可通过 UART/WiFi 上报在线状态
```

规则：

- 视频断流只提示视频离线。
- 视频不参与安全闭环。
- 视频/语音任务优先级低于安全、电机、传感器。
- 后续 PTT 语音只能做对讲和提示，不能解除急停。

## 8. 测试计划

### 台架安全

- 上电电机不动。
- ESP32 复位电机不动。
- 看门狗复位电机不动。
- 急停按下硬件失能。
- 急停松开后不自动恢复。
- PWM/模拟输出上电为 0。

### 手动遥控

- DS600 前进/后退/左转/右转/停止。
- 遥控器关机或信号丢失后停车。
- 手动接管时自动跟随暂停。

### UWB 跟随

- Tag 正前方 1m/2m 距离稳定。
- Tag 左/右方位符号正确。
- 人突然停止，小车不冲撞。
- UWB 丢失 1s 内停车。

### 避障

- 前方 1m 限速。
- 前方 0.5m 停车。
- 左右都堵时停车等待。
- 传感器断线进入保守策略。

### 长时间

- 手动遥控 30 分钟。
- 自动跟随 30 分钟。
- 视频开启 30 分钟。
- 记录 DC-DC、控制器、电池温升。

## 9. 后续 Skill 设计

后续可以把项目工作拆成 4 个数字员工 Skill：

| Skill | 输入 | 输出 |
|---|---|---|
| 采购审查 | 购物车截图/链接/订单表 | 是否合适、缺什么、数量是否正确 |
| 接线质检 | 接线图/控制盒照片/线标照片 | 能否上电、哪里危险、缺什么证据 |
| 固件日志分析 | 编译日志/串口日志/H5异常 | 根因、修复建议、需委托代码任务 |
| 试车安全官 | 测试计划/现场照片/日志 | 是否允许台架/空载/低速试车 |

当前已经生成正式 `FIRMWARE-SPEC.md`。后续 VS Code 手写、Claude/Copilot 辅助、代码审查都必须以它为准。
