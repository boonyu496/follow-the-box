# UWB 旧项目迁移评估与 FollowBox 落地任务包

> 任务来源：评估 `D:\car\UWB outocar` 与 `D:\autofloowcar` 两个旧 UWB 自动跟随项目，判断对当前 `D:\car\Follow the box` 的可借鉴内容、可照抄代码、禁止照抄部分，并给出后续实施任务包。
>
> 结论先行：旧项目的 **UWB GC-P2304 二进制帧解析、滤波思路、距离/方位跟随控制经验、避障状态机经验、Unity 纯逻辑测试思路** 有价值；但旧项目运动输出直接 `car_move/car_stop`、视觉云台耦合、2S 小车电池/电机参数、旧 GPIO/旧底盘假设不能照搬到 FollowBox 实车主线。

---

## 1. 已审查材料

### FollowBox 当前权威材料

- `README.md`
- `FIRMWARE-SPEC.md`
- `CURRENT-FIRMWARE-ARCHITECTURE.md`
- `CURRENT-WIRING-AI.md`
- `PIN-MAP-V1.md`
- `protocols/UWB-GC-P2304.md`
- `firmware/README.md`
- `firmware/include/core/types.h`
- `firmware/include/core/system_state.h`
- `firmware/include/config/profile_defaults.h`
- `firmware/src/app/app.cpp`
- `firmware/src/app/command_pipeline.cpp`
- `firmware/src/app/mode_manager.cpp`
- `firmware/src/safety/safety_manager.cpp`
- `firmware/src/control/motion_mixer.cpp`
- `firmware/src/drive/drive_adapter_analog_bldc.cpp`
- `firmware/src/control/rc_input_ds600.cpp`

### 旧项目材料

`D:\car\UWB outocar\autocar` / `/mnt/d/car/UWB outocar/autocar`：

- `drivers/uwb_module.cpp`
- `control/auto_follow.cpp`
- `control/motion_control.cpp`
- `control/obstacle_avoidance.cpp`
- `control/sensor_fusion.cpp`
- `control/auto_follow_types.h`
- `include/SystemConfig.h`
- `test/test_algorithms/test_main.cpp`

`D:\autofloowcar` / `/mnt/d/autofloowcar`：

- `autocar/drivers/uwb_module.cpp`
- `autocar/control/auto_follow.cpp`
- `autocar/control/motion_control.cpp`
- `autocar/control/obstacle_avoidance.cpp`
- `zhilliao/_tmp_gc_p2304_base_spec_utf8.txt`

---

## 2. 三个项目定位对比

| 项目 | 当前定位 | 对 FollowBox 价值 | 风险 |
|---|---|---|---|
| `UWB outocar` | 更像后期重构版，UWB/跟随/避障/测试较集中 | 高：作为主参考 | 仍然是旧小车/旧底盘/旧输出链，不能直接接 FollowBox 电机输出 |
| `autofloowcar` | 更早期/更重视觉云台和 H5 版本 | 中：补充看视觉/云台/日志历史，UWB 代码与 outocar 基本同源 | 视觉依赖重、耦合多、旧参数多，照搬会污染 FollowBox 分层架构 |
| `Follow the box` | 当前正式项目，已冻结 `FIRMWARE-SPEC.md` 架构 | 权威主线 | AUTO_FOLLOW 目前仍是空意图；UWB/TOF/超声/IMU 模块尚未落地 |

---

## 3. 可直接照抄或低改移植的部分

> “可照抄”不等于直接启用实车自动跟随；只能进入对应模块，且必须通过 `safety_manager -> motion_mixer -> applyFinalGate -> drive_adapter_analog_bldc` 主链。

### 3.1 GC-P2304 二进制帧解析

旧项目 `drivers/uwb_module.cpp` 已实现并与官方规格书一致的帧结构：

```text
F0 06 ID_L ID_H DIST_L DIST_H ANG_L ANG_H RSSI AA
```

官方资料证据位于：

```text
/mnt/d/autofloowcar/zhilliao/_tmp_gc_p2304_base_spec_utf8.txt
```

关键字段：

| 字段 | 长度 | 含义 |
|---|---:|---|
| `0xF0` | 1 byte | 帧头 |
| `0x06` | 1 byte | 有效数据长度/类型 |
| `ID_L ID_H` | 2 bytes | 测距模块 ID，小端 |
| `DIST_L DIST_H` | 2 bytes | 距离 cm，小端 |
| `ANG_L ANG_H` | 2 bytes | 角度 degree，小端；负角度编码仍需实测确认 |
| `RSSI` | 1 byte | dBm = RSSI - 256 |
| `0xAA` | 1 byte | 帧尾 |

可以迁移的代码思路：

- 10 字节滑动解析。
- `buf[1] == 0x06 && buf[9] == 0xAA` 校验。
- `dist_cm = (buf[5] << 8) | buf[4]`。
- `angle_raw = int16_t((buf[7] << 8) | buf[6])`，但负角度必须用左右移动抓包验证。
- `rssi = buf[8] - 256`。
- 超时后 `valid=false`、滤波器 reset。
- 解析统计：帧数、错误数、字节数、协议标签，便于 H5/串口诊断。

建议落地文件：

```text
firmware/src/sensors/uwb_gc_p2304.h
firmware/src/sensors/uwb_gc_p2304.cpp
firmware/tools/uwb_parser_smoke_test.cpp
```

### 3.2 UWB 角度/距离滤波

旧项目有价值的滤波点：

- 角度归一化到 `[-180, 180]`。
- `angle_delta_deg()` 处理 ±180° 跨界。
- 短窗口中值滤波去毛刺。
- 方位自适应 EMA：大角度变化提高 alpha，小角度稳定时降低 alpha。
- 距离中值 + EMA。
- 超时 reset 滤波器，避免旧方向残留。

可直接移植为纯函数/小类，但要去掉旧项目全局变量和 `Serial/web_log` 强耦合。

建议 FollowBox 新增结构：

```text
UwbParserStats
UwbRawFrame
UwbFilterState
```

输出仍映射到现有 `UwbTarget`：

```cpp
struct UwbTarget {
  bool valid;
  uint32_t last_update_ms;
  int distance_mm;
  float bearing_deg;
  uint8_t confidence;
};
```

### 3.3 纯逻辑测试方式

旧项目 `test/test_algorithms/test_main.cpp` 里用 stub 替代硬件函数，对 PID、sensor_fusion、避障、运动控制做纯逻辑测试。FollowBox 已有 `firmware/tools/logic_smoke_test.cpp`，应扩展为：

- UWB parser 样例帧测试。
- UWB timeout 测试。
- `AUTO_FOLLOW` 距离过近停车测试。
- `AUTO_FOLLOW` 方位过大先转/限速测试。
- 障碍 stop 距离优先级测试。

---

## 4. 可借鉴但必须重写适配的部分

### 4.1 UWB 跟随控制器

旧项目 motion_control 的有效经验：

- 3D 距离减去手持标签高度，得到水平距离：
  `flat = sqrt(raw3d^2 - height^2)`。
- 近距停车带 + 迟滞恢复：例如 0.8m 停车、0.9m 后恢复。
- 目标横向偏角过大时，不应高速前冲，应先转向/限速。
- UWB 方位无效时，不能沿用旧方位高速走。
- 接近速度前馈：距离快速变小时提前减速。
- 转向越大，前进速度越低。

但是 FollowBox 的输出是 `MotionIntent.forward/turn`，不是旧项目 `speed_output 0..255` 和 `car_move()`。因此只能迁移控制策略，不能照搬函数。

建议落地模块：

```text
firmware/src/control/follow_controller_uwb.h
firmware/src/control/follow_controller_uwb.cpp
```

职责：输入 `UwbTarget + ImuSnapshot + ObstacleSnapshot + profile`，输出 `MotionIntent`，不写 GPIO、不调用 drive adapter。

### 4.2 避障状态机

旧项目有价值经验：

- `AVOID_PHASE_IDLE / TURN / FORWARD / REVERSE / TRAIL_ESCAPE`。
- 选择避障方向时比较左右距离。
- 进入避障会话后锁定方向，避免左右超声交替触发导致振荡。
- `clear_since_ms` 要求持续净空一段时间后才退出。
- 卡住超时后后退脱困。
- 左右超声全失效时要 fail-safe，但如果有更强感知源，不能无脑每 500ms 死循环停车。
- 走廊/虚拟前方/障碍置信度能抑制 HC-SR04 毛刺。

FollowBox 现有 `SafetyManager::hasStopObstacle()` 只有“前方小于 stop 距离就停”。后续应新增：

```text
firmware/src/control/obstacle_manager.h
firmware/src/control/obstacle_manager.cpp
```

先做 P0：限速/停车/方向建议，不做复杂绕障；P1 再引入 TURN/FORWARD/REVERSE 状态机。

### 4.3 sensor_fusion 里的方位阻尼

旧项目 `sensor_fusion.cpp` 的价值：

- 用 UWB 方位角直接乘 KP 得到转向。
- 用 IMU yaw_delta/yaw_rate 做负反馈阻尼，避免左右摆头。
- 方位 D 项减少过冲。
- 切入 AUTO 前 500ms 限制方位控制突变。

FollowBox 现阶段可先不用复杂全局目标 yaw 锁定，只迁移最低风险版本：

```text
turn = clamp(bearing_deg / TURN_FULL_SCALE_DEG, -1, 1)
turn -= yaw_rate_dps * YAW_DAMP_GAIN
```

后续通过日志再调参。

---

## 5. 禁止照抄的部分

| 旧代码/思路 | 为什么不能照抄到 FollowBox |
|---|---|
| `car_move()`, `car_stop()`, `car_brake_drive_short()` 直接在跟随/避障模块调用 | 违反 FollowBox 固定链路；会绕过 `safety_manager.applyFinalGate()` 和 `drive_adapter_analog_bldc` |
| 旧项目 `speed_output/turn_output` 0..255 直接映射 | FollowBox 使用 `MotionIntent` -1..1，再由 `motion_mixer` 和油门校准映射到 PWM→0-5V |
| 旧项目 2S 电池参数、ADC 分压、电压阈值 | FollowBox 是 36/48V 控制器，当前 ADC 为 220k/10k，旧 2S 参数危险 |
| 旧 GPIO / `PinConfig.h` | FollowBox 唯一引脚表是 `PIN-MAP-V1.md` 和 `board_pins.h`；GPIO35/36/37/47/48 已作废 |
| 视觉云台强依赖逻辑 | 当前 FollowBox ESP32-S3-CAM 只做视频/状态，不做安全主控；视觉不能成为 P0 自动跟随前置条件 |
| `web_log`/H5 与控制逻辑耦合 | FollowBox Web/H5 低优先级，不能阻塞或参与高优先级运动裁决 |
| 避障中直接后退/转向 | 后退动作属于高风险动作，必须作为 `MotionIntent` 经过安全门和低速限制，且首次必须架空测试 |
| 文本协议 parser 作为正式协议 | 官方规格书只确认二进制 `F0 06 ... AA`；文本 `dist:/angle:` 只能保留为 debug/mock，不能作为正式 GC-P2304 主协议 |

---

## 6. 对 FollowBox 当前代码的落地入口

当前 FollowBox 固件状态：

- 已有安全主链：`App::tick()` → `safety_manager.evaluate()` → `mode_manager.selectMode()` → `command_pipeline.buildIntent()` → `motion_mixer.mix()` → `safety_manager.applyFinalGate()`。
- 已有 `MotionIntent`、`UwbTarget`、`ObstacleSnapshot`、`MotorCommand`。
- 已有 `drive_adapter_analog_bldc`，且只由它写 GPIO12/13/14/15/16/39，方向正确。
- `AUTO_FOLLOW` 当前在 `command_pipeline.cpp` 中只输出 0/0 空意图，这是正确的安全占位。
- 尚缺 `uwb_gc_p2304`、`follow_controller_uwb`、`obstacle_manager`、`tof_vl53l1x_array`、`ultrasonic_array`、`imu_jy61p` 等模块。

推荐最小落地顺序：

1. **P0-A：UWB 协议文档更新 + parser 纯逻辑模块**  
   不接电机，不启用 AUTO_FOLLOW，只能 raw/log/mock test。
2. **P0-B：follow_controller_uwb 纯逻辑**  
   输入假数据，输出 `MotionIntent`，用 g++ 本地测试验证近停/迟滞/转向限幅。
3. **P0-C：command_pipeline 接入 follow_controller_uwb**  
   仍要求 `install_wizard_complete && uwb.valid && safety.motion_allowed`，并保持默认低速。
4. **P0-D：obstacle_manager P0 限速/停车**  
   先不自动绕障，只做 `max_speed_scale` 或 `MotionIntent` 限制。
5. **P1：避障状态机 TURN/FORWARD/REVERSE**  
   必须架空轮 + 急停验证后才允许实车运动测试。

---

## 7. 给 Claude/Copilot 的实施任务包

### 任务包 A：UWB GC-P2304 parser + 文档证据

```text
目标：
- 在 FollowBox firmware 中新增 GC-P2304 二进制帧 parser，输出 UwbTarget 和统计信息。

现有依据：
- /mnt/d/car/Follow the box/FIRMWARE-SPEC.md
- /mnt/d/car/Follow the box/PIN-MAP-V1.md
- /mnt/d/car/Follow the box/protocols/UWB-GC-P2304.md
- /mnt/d/autofloowcar/zhilliao/_tmp_gc_p2304_base_spec_utf8.txt 第 261-301 行
- /mnt/d/car/UWB outocar/autocar/drivers/uwb_module.cpp 第 339-379 行

涉及文件：
- 新建 firmware/src/sensors/uwb_gc_p2304.h
- 新建 firmware/src/sensors/uwb_gc_p2304.cpp
- 新建或扩展 firmware/tools/logic_smoke_test.cpp 或 firmware/tools/uwb_parser_smoke_test.cpp
- 如需参数，修改 firmware/include/config/profile_defaults.h

硬约束：
- 不写 GPIO12/13/14/15/16/39。
- 不改 drive_adapter。
- 不让 AUTO_FOLLOW 因 parser 存在而自动动起来。
- 只解析 UART 字节流/样例数组；硬件串口接入可留接口，但不阻塞 control_task。
- 二进制帧格式固定：F0 06 ID_L ID_H DIST_L DIST_H ANG_L ANG_H RSSI AA。
- 负角度两字节编码必须标注“待左右实测确认”；单元测试至少覆盖正角度样例 F0 06 03 00 73 00 14 00 BC AA。

验收：
- g++ 纯逻辑测试通过。
- pio run -d firmware 通过。
- changed-file list。
```

### 任务包 B：UWB follow_controller_uwb 纯逻辑

```text
目标：
- 新增 follow_controller_uwb，把 UwbTarget/ImuSnapshot 转成 MotionIntent，不直接控制电机。

涉及文件：
- 新建 firmware/src/control/follow_controller_uwb.h
- 新建 firmware/src/control/follow_controller_uwb.cpp
- 修改 firmware/src/app/command_pipeline.cpp，使 AUTO_FOLLOW 调用 controller 输出 intent
- 扩展 firmware/tools/logic_smoke_test.cpp

硬约束：
- 只输出 MotionIntent.forward/turn。
- 不能调用 car_move/car_stop，不能写 GPIO/PWM。
- 必须保留 safety_manager.evaluate 和 applyFinalGate 最终门控。
- 默认 AUTO_FOLLOW_MAX_SPEED_SCALE 不超过 0.30。
- 距离过近停车、UWB 丢失停车由 safety_manager 兜底；controller 不能绕过安全。

建议控制策略：
- 目标水平距离进入近停阈值：forward=0。
- 近停退出阈值使用迟滞。
- 方位角大于阈值时降低 forward，优先转向。
- turn = bearing_deg / TURN_FULL_SCALE_DEG，限幅 [-1,1]。
- yaw_rate 阻尼可选，但默认保守。

验收：
- 纯逻辑测试覆盖：近距停车、迟滞退出、远距前进、左/右转向、方位过大限速、UWB invalid 不请求运动。
- pio run -d firmware 通过。
```

### 任务包 C：Obstacle Manager P0 限速/停车

```text
目标：
- 新增 obstacle_manager P0 版本，只根据前左/前中/前右/左右侧距离做限速/停车，不做自动后退绕障。

涉及文件：
- 新建 firmware/src/control/obstacle_manager.h
- 新建 firmware/src/control/obstacle_manager.cpp
- 修改 app/control pipeline 接入 obstacle_manager 的限速或 intent 修正
- 扩展 logic smoke test

硬约束：
- 首版不自动后退。
- 不直接 car_stop/car_move，不写 GPIO。
- obstacle stop 必须能覆盖 RC/H5/AUTO 的前进命令。
- 传感器 invalid 不应误判为安全高速；按模式和当前 sensor heartbeat 处理。

验收：
- 前方 < stop_distance：MotionIntent forward 被置 0 或 SafetyDecision stop。
- slow_distance 内：forward 被限幅。
- 左右侧只给 turn 建议或降速，不直接执行绕障。
```

---

## 8. 当前建议结论

状态：`READY_FOR_IMPLEMENTATION_AFTER_APPROVAL`

建议先让 Claude/Copilot 做 **任务包 A + B**，不要一次性做避障绕障和实车运动。这样风险最低：先把 UWB 数据读准，再把 AUTO_FOLLOW 变成可测试的纯逻辑，最后再接硬件传感器与架空测试。

如果用户批准，下一步实施顺序：

1. Hermes 把任务包 A/B 发给 Copilot/Claude。
2. 代码代理修改后返回 changed-file list、build/test 输出。
3. Hermes 审查 diff，运行 `pio run -d firmware` 和 g++ smoke test。
4. 通过后再准备架空测试计划，不直接落地实车跟随。
