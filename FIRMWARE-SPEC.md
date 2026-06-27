# Follow the Box Firmware Specification

> **代码框架冻结版。** 这份不是给某个 AI 的临时任务，而是后续用 VS Code 手写、Claude/Copilot 辅助、多人维护时共同遵守的固件架构规范。代码框架必须照这里做。

## 0. 权威输入文件

正式写代码前必须读取：

1. `PIN-MAP-V1.md`：唯一 Pin Map v1.0。
2. `CURRENT-WIRING-AI.md`：接线实施依据。
3. `POLARITY-DEFINITIONS.md`：GPIO/MOS/控制器线极性定义。
4. `ESTOP-FEEDBACK-CIRCUIT.md`：GPIO21 急停反馈隔离接线。
5. `PWM-OUTPUT-CALIBRATION.md`：PWM→0-5V 油门校准。
6. `protocols/DS600-PWM.md`、`protocols/UWB-GC-P2304.md`、`protocols/H5-API.md`、`protocols/JY61P.md`：接口协议。

## 0. 项目范围

当前固件目标是 Follow the Box 首台自动跟随小车/载物车控制盒样机，平台为普通 ESP32-S3-DevKitC-1，驱动两个 36V/48V 350W 有霍尔无刷控制器。

第一版只做低速安全闭环：

- HOTRC DS600 六通道 PWM 实体遥控。
- H5 本地状态页和低速点动。
- GC-P2304-GS-2 UWB 跟随距离/方位。
- VL53L1X ×3 + TCA9548A 前向避障。
- HC-SR04 ×2 左右侧辅助避障。
- JY61P 姿态/yaw 阻尼。
- PWM 转 0-5V 模拟量模块 ×2，输出左右油门。
- 急停、断线、低电压、传感器超时、故障锁定。
- ESP32-S3-CAM 只做视频在线状态和链接显示，不做安全主控。
- P1 可选云端遥测/日志上传和受限低速远程点动调试。

不做：高速遥控、复杂 SLAM、无人车路径规划、AI 自动决策、远程公网驾驶或任何云端直接 PWM 控制。

## 1. 架构原则：必须执行

1. `main.cpp` 只做启动入口，不写业务逻辑，目标 80-120 行以内。
2. 每个硬件驱动、功能模块、调度模块单独 `.h/.cpp`。
3. 所有模块通过统一数据结构交换状态，不允许互相直接改内部变量。
4. 所有运动命令必须经过 `safety_manager`。
5. `drive_adapter_analog_bldc` 是唯一能写安全重映射后油门 PWM 引脚的模块；旧 GPIO47/GPIO48 油门方案作废。
6. DS600、H5、云端低速点动、UWB 自动跟随都只能输出 `MotionIntent`，不能直接控制电机。
7. 传感器模块只能读传感器，不能写电机输出。
8. Web/H5 模块只能发请求和显示状态，不能绕过安全锁。
9. H5 源码独立放 `web/`，不允许大段 raw HTML 塞进 `main.cpp`。
10. 配置常量集中在 `include/config/`，不能散落到业务文件里。
11. 第一版优先可验证、安全、可调试，不追求复杂功能。

## 2. 固件目录结构

正式代码目录使用 `firmware/`：

```text
firmware/
  platformio.ini
  README.md

  include/
    config/
      board_pins.h
      build_flags.h
      profile_defaults.h
      timing_config.h
      safety_limits.h

    core/
      types.h
      system_state.h
      error_codes.h
      time_utils.h
      math_utils.h

  src/
    main.cpp

    app/
      app.h
      app.cpp
      scheduler.h
      scheduler.cpp
      mode_manager.h
      mode_manager.cpp
      command_pipeline.h
      command_pipeline.cpp

    safety/
      safety_manager.h
      safety_manager.cpp
      failsafe_rules.h
      failsafe_rules.cpp
      self_test.h
      self_test.cpp
      install_wizard.h
      install_wizard.cpp

    control/
      rc_input_ds600.h
      rc_input_ds600.cpp
      h5_control_input.h
      h5_control_input.cpp
      follow_controller_uwb.h
      follow_controller_uwb.cpp
      obstacle_manager.h
      obstacle_manager.cpp
      speed_limiter.h
      speed_limiter.cpp
      motion_mixer.h
      motion_mixer.cpp

    drive/
      drive_adapter.h
      drive_adapter_analog_bldc.h
      drive_adapter_analog_bldc.cpp
      motor_output.h
      motor_output.cpp
      brake_output.h
      brake_output.cpp

    sensors/
      uwb_gc_p2304.h
      uwb_gc_p2304.cpp
      tof_vl53l1x_array.h
      tof_vl53l1x_array.cpp
      ultrasonic_array.h
      ultrasonic_array.cpp
      imu_jy61p.h
      imu_jy61p.cpp
      power_monitor.h
      power_monitor.cpp
      camera_link.h
      camera_link.cpp

    hal/
      gpio_in.h
      gpio_in.cpp
      gpio_out.h
      gpio_out.cpp
      pwm_input.h
      pwm_input.cpp
      pwm_output.h
      pwm_output.cpp
      adc_reader.h
      adc_reader.cpp
      i2c_bus.h
      i2c_bus.cpp
      uart_bus.h
      uart_bus.cpp
      watchdog.h
      watchdog.cpp

    web/
      h5_web_server.h
      h5_web_server.cpp
      telemetry_api.h
      telemetry_api.cpp
      web_assets.h

    storage/
      profile_store.h
      profile_store.cpp
      calibration_store.h
      calibration_store.cpp

    telemetry/
      telemetry_logger.h
      telemetry_logger.cpp
      debug_console.h
      debug_console.cpp

  web/
    index.html
    app.js
    style.css

  tools/
    build_web_assets.py
    serial_log_parser.py

  test/
    test_safety_manager/
    test_motion_mixer/
    test_mode_manager/
    test_command_pipeline/
    test_follow_controller/
```

## 3. 文件职责边界

| 层 | 文件/模块 | 职责 | 禁止 |
|---|---|---|---|
| 启动层 | `main.cpp` | 调用 `App::begin()` / `App::tick()` 或启动 FreeRTOS 任务 | 禁止写业务逻辑 |
| 应用层 | `app/app.*` | 系统初始化、模块持有、任务创建 | 禁止直接写 GPIO 油门 |
| 调度层 | `app/scheduler.*` | 固定周期调用控制/传感器/通信更新 | 禁止做运动决策 |
| 模式层 | `app/mode_manager.*` | BOOT/IDLE/RC/H5/AUTO/FAULT/ESTOP 模式切换 | 禁止输出 PWM |
| 管线层 | `app/command_pipeline.*` | 输入合成、优先级裁决、生成运动意图 | 禁止绕过 safety |
| 安全层 | `safety/*` | 急停、故障、断线、低电压、障碍、安装向导 | 禁止被任何模块跳过 |
| 控制层 | `control/*` | 遥控解释、UWB 跟随、避障限速、差速混控 | 禁止直接读写硬件输出 |
| 驱动层 | `drive/*` | 油门 PWM、刹车、倒车、使能输出 | 禁止决定是否安全 |
| 传感器层 | `sensors/*` | UWB/TOF/超声/IMU/电池/摄像头状态 | 禁止控制电机 |
| HAL 层 | `hal/*` | GPIO/PWM/ADC/UART/I2C/Watchdog 封装 | 禁止写业务策略 |
| Web 层 | `web/*` | H5 状态、API、低速点动请求 | 禁止解除急停/故障 |
| 存储层 | `storage/*` | Profile、安装向导结果、校准参数 | 禁止高频写 Flash |
| 日志层 | `telemetry/*` | 调试日志、状态快照输出 | 禁止阻塞控制循环 |

## 4. 运行主链路

所有运动控制只能走这一条链：

```text
传感器/输入采集
  -> SystemState 快照
  -> safety_manager 计算 SafetyDecision（预裁决：是否允许进入某模式/某控制源）
  -> mode_manager 选择当前控制来源
  -> command_pipeline 生成 MotionIntent
  -> obstacle_manager / speed_limiter 限速或停车
  -> motion_mixer 转成左右轮 MotorCommand
  -> safety_manager.applyFinalGate(MotorCommand)（最终门控：故障/急停/超时强制 enable=false、PWM=0、brake=true）
  -> drive_adapter_analog_bldc 输出左右油门 PWM/刹车/左右倒车/软件使能
```

禁止链路：

```text
DS600 -> 直接写油门 PWM 引脚      # 禁止
H5 -> 直接写油门 PWM 引脚         # 禁止
UWB -> 直接写油门 PWM 引脚        # 禁止
sensor -> 直接 stop/start 电机      # 禁止
web api -> clear fault + run motor  # 禁止
```

## 5. 核心数据结构

代码先定义数据结构，再实现模块。结构体放在 `include/core/types.h` 和 `include/core/system_state.h`。

### 5.1 枚举

```cpp
enum class RunMode {
  BOOT_SELF_TEST,
  SAFE_IDLE,
  MANUAL_RC,
  MANUAL_H5_LOW_SPEED,
  MANUAL_CLOUD_LOW_SPEED,
  AUTO_FOLLOW,
  FAULT_LOCKOUT,
  ESTOP_ACTIVE
};

enum class StopReason {
  NONE,
  ESTOP,
  RC_LOST,
  H5_LOST,
  CLOUD_LOST,
  UWB_LOST,
  OBSTACLE_STOP,
  LOW_BATTERY,
  SENSOR_TIMEOUT,
  MOTOR_FAULT,
  INSTALL_WIZARD_NOT_DONE,
  WATCHDOG_TIMEOUT
};

enum class ControlSource {
  NONE,
  DS600_RC,
  H5_LOCAL,
  CLOUD_REMOTE,
  UWB_FOLLOW
};
```

### 5.2 输入和传感器状态

```cpp
struct RcInput {
  bool online;
  uint32_t last_update_ms;
  uint16_t ch_us[6];
  float throttle;       // -1.0..1.0
  float steering;       // -1.0..1.0
  float speed_limit;    // 0.0..1.0
  bool stop_switch;
  bool auto_request;
};

struct H5ControlInput {
  bool connected;
  uint32_t last_update_ms;
  bool unlock_request;
  bool auto_request;
  float throttle;       // -1.0..1.0, low speed only
  float steering;       // -1.0..1.0
};

struct CloudControlInput {
  bool connected;
  uint32_t last_update_ms;
  uint32_t last_seq;
  bool unlock_request;
  bool safe_idle_request;
  float throttle;       // -1.0..1.0, lower speed than local H5
  float steering;       // -1.0..1.0
};

struct UwbTarget {
  bool valid;
  uint32_t last_update_ms;
  int distance_mm;
  float bearing_deg;
  uint8_t confidence;   // 0..100
};

struct ObstacleSnapshot {
  bool valid;
  uint32_t last_update_ms;
  int front_left_mm;
  int front_center_mm;
  int front_right_mm;
  int side_left_mm;
  int side_right_mm;
};

struct ImuSnapshot {
  bool valid;
  uint32_t last_update_ms;
  float yaw_deg;
  float yaw_rate_dps;
  float pitch_deg;
  float roll_deg;
};

struct PowerStatus {
  bool valid;
  uint32_t last_update_ms;
  float battery_voltage;
  bool low_battery;
  bool motor_fault_left;
  bool motor_fault_right;
};
```

### 5.3 决策和输出

```cpp
struct SafetyDecision {
  bool motion_allowed;
  bool fault_latched;
  float max_speed_scale;    // 0.0..1.0
  StopReason stop_reason;
};

struct MotionIntent {
  ControlSource source;
  bool request_motion;
  float forward;            // -1.0..1.0
  float turn;               // -1.0..1.0
};

struct MotorCommand {
  bool enable;
  bool brake;
  float left_target;        // -1.0..1.0, signed command before analog output mapping
  float right_target;       // -1.0..1.0, signed command before analog output mapping
  bool left_reverse;        // output to PIN_LEFT_REVERSE_OUT / GPIO15
  bool right_reverse;       // output to PIN_RIGHT_REVERSE_OUT / GPIO16
};

struct SystemState {
  RunMode mode;
  RcInput rc;
  H5ControlInput h5;
  CloudControlInput cloud;
  UwbTarget uwb;
  ObstacleSnapshot obstacle;
  ImuSnapshot imu;
  PowerStatus power;
  SafetyDecision safety;
  MotionIntent intent;
  MotorCommand motor_command;
  uint32_t now_ms;
};
```

## 6. 模式状态机

```text
BOOT_SELF_TEST
  -> SAFE_IDLE
  -> MANUAL_RC
  -> MANUAL_H5_LOW_SPEED
  -> MANUAL_CLOUD_LOW_SPEED
  -> AUTO_FOLLOW
  -> FAULT_LOCKOUT
  -> ESTOP_ACTIVE
```

| 模式 | 进入条件 | 退出条件 | 电机权限 |
|---|---|---|---|
| `BOOT_SELF_TEST` | 上电 | 自检通过/失败 | 无 |
| `SAFE_IDLE` | 自检通过但未解锁 | 人工选择模式 | 无，只读状态 |
| `MANUAL_RC` | DS600 在线且通道有效 | DS600 丢失/切模式/故障 | 有，低速限幅 |
| `MANUAL_H5_LOW_SPEED` | 本地 H5 解锁且 DS600 不接管 | H5 断线/DS600 接管/故障 | 有，更低速限幅 |
| `MANUAL_CLOUD_LOW_SPEED` | 云端 deadman 命令有效且 DS600/H5 不接管 | 云端命令超时/断网/DS600 接管/故障 | 有，最低速限幅 |
| `AUTO_FOLLOW` | 安装向导完成、UWB 有效、人工确认 | UWB 丢失/障碍/接管/故障 | 有，受避障限速 |
| `FAULT_LOCKOUT` | 严重故障锁定 | 人工复位 + 故障消除 | 无 |
| `ESTOP_ACTIVE` | 急停触发 | 物理恢复 + 人工复位 | 无 |

固定优先级：

```text
急停 > 故障锁定 > DS600 手动接管 > 避障限速/停车 > 自动跟随 > 本地 H5 低速点动 > 云端低速点动
```

### 6.1 模式 × 故障 × 动作真值表

| 事件/故障 | SAFE_IDLE | MANUAL_RC | MANUAL_H5_LOW_SPEED | MANUAL_CLOUD_LOW_SPEED | AUTO_FOLLOW | 是否锁定 | 恢复条件 |
|---|---|---|---|---|---|---|---|
| 物理急停 GPIO21 触发 | 进入 ESTOP | 立即停车 + ESTOP | 立即停车 + ESTOP | 立即停车 + ESTOP | 立即停车 + ESTOP | 是 | 急停物理恢复 + 人工复位 |
| 控制器故障输入 | FAULT_LOCKOUT | 立即停车 + FAULT | 立即停车 + FAULT | 立即停车 + FAULT | 立即停车 + FAULT | 是 | 故障消除 + 人工复位 |
| 低电压严重 | FAULT_LOCKOUT | 限速/停车，严重时 FAULT | 限速/停车，严重时 FAULT | 限速/停车，严重时 FAULT | 限速/停车，严重时 FAULT | 严重时是 | 电压恢复 + 人工复位 |
| DS600 丢失 | 记录离线 | 立即停车 + RC_LOST | H5 仍可低速但不得自动升级 | 云端仍可最低速但不得自动升级 | AUTO 中记录，若 DS600 是接管通道则停车 | MANUAL_RC 中锁定 | DS600 恢复 + 油门回中 + 人工确认 |
| H5/WebSocket 丢失 | 无动作 | 无动作 | 立即停车，退出 H5 模式 | 无动作 | 无动作，仅状态离线 | 否 | H5 重连 + 重新请求低速点动 |
| 云端命令/WiFi 丢失 | 无动作 | 无动作 | 无动作 | 立即停车，退出云控模式 | 无动作，仅状态离线 | 否 | 云端重连 + 重新按住 deadman |
| UWB 丢失 | 无动作 | 无动作 | 无动作 | 无动作 | 立即停车，退出 AUTO_FOLLOW | 否/可配置锁定 | UWB 恢复 + 人工重新确认 AUTO |
| 前方障碍 < stop_distance | 保持停止 | 立即停车/刹车 | 立即停车/刹车 | 立即停车/刹车 | 立即停车/刹车 | 否，持续存在时禁止动 | 障碍离开 + 命令重新下发 |
| 障碍进入 slow_distance | 无动作 | 限速 | 限速 | 限速 | 限速 | 否 | 障碍离开 |
| sensor_task/uwb_task 心跳 >200ms | FAULT_LOCKOUT | 立即停车 + FAULT | 立即停车 + FAULT | 立即停车 + FAULT | 立即停车 + FAULT | 是 | 任务恢复 + 人工复位 |
| 安装向导未完成 | 保持 SAFE_IDLE | 仅允许架空/低速测试 | 仅允许低速点动 | 仅允许最低速点动 | 禁止进入 AUTO | 否 | 完成安装向导 |

原则：安全事件先由 `safety_manager` 统一裁决；`mode_manager` 只能在安全允许范围内选择控制来源；`command_pipeline` 不得自行忽略上述动作。

## 7. FreeRTOS 任务和频率

第一版只开 4 类任务，避免过度复杂：

| 任务 | 频率/方式 | 优先级 | 内容 |
|---|---:|---:|---|
| `control_task` | 50Hz 固定周期 | 最高 | safety、mode、pipeline、mixer、drive 输出 |
| `sensor_task` | 20-50Hz | 高 | TOF/超声/IMU/电池快照更新 |
| `uwb_task` | 串口事件 + 超时检查 | 高 | UWB 帧解析、距离/方位更新 |
| `comm_task` | 5-10Hz | 低 | H5、遥测、日志、摄像头在线状态 |

硬规则：

- 电机输出只能在 `control_task` 中更新。
- H5/WebSocket 不允许阻塞 `control_task`。
- 串口解析失败必须丢帧并记录错误码，不能卡死任务。
- Watchdog 覆盖所有任务，任务超时进入故障锁定。
- 传感器读数用快照，控制任务不等待慢速 I2C/网页/串口阻塞。


### 7.1 双核固定与线程安全

建议采用 ESP32-S3 双核分工，但不能只靠“多核”保证安全：

| 任务 | Core 绑定 | 说明 |
|---|---:|---|
| `control_task` | Core 1 | 运动与安全闭环，50Hz，最高优先级，唯一更新电机输出 |
| `sensor_task` | Core 1 或 Core 0 | 只更新传感器快照；如果 I2C/超声读数可能阻塞，优先放 Core 0 并用双缓冲交给控制环 |
| `uwb_task` | Core 0 | 串口解析和超时检测，不阻塞控制环 |
| `comm_task` | Core 0 | WebServer/WebSocket/遥测/摄像头在线状态，低优先级 |

线程安全硬规则：

- `SystemState` 不能被多个任务无保护地直接读写。
- 传感器任务写入 `SensorSnapshot` 双缓冲，`control_task` 每 20ms 只读取已经提交的完整快照。
- 如果使用 Mutex，锁范围必须极短，只保护结构体拷贝，禁止在持锁期间做 I2C、串口、WebSocket、日志输出。
- `control_task` 不等待网络、网页、慢速串口和阻塞式传感器读取。

### 7.2 软件任务心跳看门狗

硬件 WDT 只能作为最后兜底。第一层保护必须是软件任务心跳：

- `sensor_task`、`uwb_task`、`comm_task` 每次完成循环后更新各自 `heartbeat_ms`。
- `control_task` 每轮检查心跳。
- `sensor_task` 或 `uwb_task` 心跳超过 200ms：进入 `FAULT_LOCKOUT`，`StopReason::WATCHDOG_TIMEOUT`，立即 `MotorCommand.enable=false`、PWM=0、刹车有效。
- `comm_task` 心跳超时只降低通信状态，不允许影响急停/手动遥控停车能力。
- 只有故障现场已经安全停机后，硬件 WDT 才作为最后复位兜底。

## 8. 引脚常量

引脚只允许定义在 `include/config/board_pins.h`。

> P0 修订：ESP32-S3-DevKitC-1 N8R8 / 带 PSRAM 核心板上，不再使用 GPIO35、GPIO36、GPIO37、GPIO47、GPIO48 作为外部输出。旧方案中这些脚用于刹车/倒车/使能/油门 PWM，现全部作废。实际 PCB 下单前还要按到货板卡丝印和原厂资料复核。

```cpp
#pragma once

// DS600 PWM input. CH6 首版不强制接入，优先释放 GPIO 给安全/驱动。
constexpr int PIN_RC_CH1_STEERING = 4;
constexpr int PIN_RC_CH2_THROTTLE = 5;
constexpr int PIN_RC_CH3_SPEED    = 6;
constexpr int PIN_RC_CH4_MODE     = 7;
constexpr int PIN_RC_CH5_STOP     = 8;
// constexpr int PIN_RC_CH6_AUX   = -1;  // P1 optional

// Ultrasonic: two HC-SR04 share one TRIG to save safe GPIO.
constexpr int PIN_US_SHARED_TRIG  = 9;

// I2C: TCA9548A + VL53L1X, optional external ADC/PWM monitor.
constexpr int PIN_I2C_SDA = 10;
constexpr int PIN_I2C_SCL = 11;

// Drive outputs: safe remap, replacing old GPIO35/36/37/47/48 mapping.
constexpr int PIN_LEFT_THROTTLE_PWM   = 12;
constexpr int PIN_RIGHT_THROTTLE_PWM  = 13;
constexpr int PIN_BRAKE_OUT           = 14;
constexpr int PIN_LEFT_REVERSE_OUT    = 15;
constexpr int PIN_RIGHT_REVERSE_OUT   = 16;
constexpr int PIN_DRIVE_ENABLE_OUT    = 39;

// UWB UART.
constexpr int PIN_UWB_TX = 17;
constexpr int PIN_UWB_RX = 18;

// P0 safety feedback and ADC.
constexpr int PIN_ESTOP_STATUS     = 21;
constexpr int PIN_BATTERY_ADC      = 1;
constexpr int PIN_CONTROLLER_FAULT = 2;

// Remaining low-speed sensors / status.
constexpr int PIN_US_LEFT_ECHO  = 40;
constexpr int PIN_US_RIGHT_ECHO = 41;
constexpr int PIN_LIDAR_RX      = 3;   // Fitted OEM lidar DATA/TX -> ESP32 RX.
constexpr int PIN_LIDAR_TX      = 43;  // ESP32 TX -> lidar CTL/RX; sends A5 60 before scan.
constexpr int PIN_IMU_RX        = 42;  // JY61P TX -> ESP32 RX; IMU TX/config line is P1 optional.
// constexpr int PIN_BUZZER     = -1;  // P1 optional or external I/O expander.
// ESP32-S3-CAM UART is P1 optional; first version uses WiFi video/status instead.
```

禁用/保留：

- 禁止外部输出：GPIO35、GPIO36、GPIO37、GPIO47、GPIO48。
- 禁止占用启动/USB/下载敏感脚：GPIO0、GPIO19、GPIO20、GPIO44、GPIO45、GPIO46；GPIO3 仅作 P1 雷达 DATA 输入，GPIO43 仅作 P1 雷达 CTL/TX，不作通用输出。
- GPIO2 当前只作为控制器故障输入候选，必须高阻/光耦/分压输入，不能被外部电路在上电/下载阶段强拉到异常电平；实物确认前不能作为输出。
- GPIO33、GPIO34 在带 OPI Flash/PSRAM 模组上也可能有风险，未按具体模组确认前不作为 P0 输出脚。
- GPIO38 板载 RGB 先保留。



### 8.1 上电物理安全、急停闭环、预充和油门限幅

这些是 P0，不是优化项：

1. **急停状态反馈必须接入 GPIO21。** 首版不允许省略急停反馈。物理急停被拍下时，主控必须同步进入 `ESTOP_ACTIVE/FAULT_LOCKOUT`，内部油门命令清零；急停旋开后也不能自动恢复，必须人工复位。
2. **所有驱动输出必须外部 10kΩ 下拉到 GND。** 包括左/右油门 PWM、刹车、左右倒车、软件使能。这样 ESP32 上电、复位、下载、崩溃时，PWM 转 0-5V 输入和控制器使能不会悬空。
3. **动力主电源需要防火花/预充设计。** 36V 电池接入两个无刷控制器时，控制器输入电容会产生浪涌；正式线束应使用 XT90S/防火花插头，或预充电阻 + 预充开关/继电器，再闭合主回路。
4. **左右倒车线必须物理拆分。** 左倒车由 GPIO15 控制，右倒车由 GPIO16 控制；不能并联到同一个 GPIO，否则无法一正一反原地转向。
5. **PWM→0-5V 模块必须做死区和限幅。** 实测每个控制器的转把起转电压、最大安全电压、回零延迟；软件中配置 `throttle_deadband_mv`、`throttle_min_active_mv`、`throttle_max_mv`、`throttle_slew_rise_ms`、`throttle_slew_fall_ms`。
6. **考虑 RC 滤波延迟。** PWM 转模拟模块通常有 50-150ms 响应延迟；避障停车距离和减速度限制必须预留额外滑行距离，不能假设 PWM=0 后模拟电压瞬间为 0。
7. **电池 ADC 分压统一按 36V-60V 通用设计。** GPIO1 电池采样必须使用 220k/10k 分压，60V 输入约 2.61V；旧 130k/10k 在 48V 满电 54.6V 时约 3.90V，禁止作为当前通用方案。ADC 反算必须读取 Profile 的 `battery_adc.divider_top_ohm` / `divider_bottom_ohm`，不能硬编码旧比例。
8. **油门反馈建议走 ADC。** 若 GPIO 不足，优先用 I2C 外置 ADC（如 ADS1115/ADS1015）读取左/右 0-5V 模块 VOUT 的分压值；没有油门反馈时，自动跟随速度上限必须更保守。
9. **PWM 参数必须固定并可校准。** 默认 `pwm_frequency_hz=1000`、`pwm_resolution_bits=12`；占空比到毫伏按 `PWM-OUTPUT-CALIBRATION.md` 的公式计算。校准字段必须保存到 NVS/Profile，未校准前禁止 AUTO_FOLLOW。

## 9. 模块接口规范

### 9.1 `safety_manager`

输入：`SystemState` 快照。  
输出：`SafetyDecision`。

必须检查：

- 急停状态。
- DS600 丢失。
- H5 丢失。
- UWB 丢失。
- TOF/超声/IMU/电池超时。
- 前方障碍停车距离。
- 低电压。
- 控制器故障输入。
- 安装向导是否完成。
- Watchdog/任务心跳。

规则：故障锁定后不能自动恢复，必须人工复位。

必须提供最终门控函数，例如：

```cpp
MotorCommand applyFinalGate(const MotorCommand& proposed, const SafetyDecision& safety);
```

`motion_mixer` 生成的任何 `MotorCommand` 在进入 `drive_adapter_analog_bldc` 前必须再过一次最终门控。最终门控必须**按当前模式裁决**，不能把某个输入源丢失写成全局停车：

- 急停、故障锁定、sensor/uwb 关键任务心跳超时、安装向导未完成但请求 AUTO：强制 `enable=false`、`left_target=0`、`right_target=0`、`brake=true`。
- `MANUAL_RC` 中 DS600 丢失：停车/锁定；但 `MANUAL_H5_LOW_SPEED` 或 `AUTO_FOLLOW` 中 DS600 仅作为接管通道离线时，不得无条件全局停车，按真值表处理。
- `MANUAL_H5_LOW_SPEED` 中 H5/WebSocket 丢失：停车并退出 H5 模式；但 `MANUAL_RC` / `AUTO_FOLLOW` 中 H5 丢失只标记状态离线，不影响当前运动来源。
- `AUTO_FOLLOW` 中 UWB 丢失：停车并退出 AUTO；但 `MANUAL_RC` / `MANUAL_H5_LOW_SPEED` 中 UWB 丢失只标记自动跟随不可用。

禁止只在链路前半段做一次安全预检查；禁止把“非当前控制源”的 lost-link 误写成全局停车。

最终门控规则必须按当前模式裁决：只有当前控制源丢失才触发该模式停车，非当前控制源丢失只更新在线状态或禁用对应模式。

### 9.2 `mode_manager`

输入：`SystemState` + `SafetyDecision`。  
输出：`RunMode`。

必须遵守：

- 急停/故障优先。
- DS600 手动接管优先于 AUTO_FOLLOW。
- H5 只能低速点动。
- AUTO_FOLLOW 必须安装向导完成 + UWB 有效 + 人工确认。

### 9.3 `command_pipeline`

输入：当前模式 + `RcInput` + `H5ControlInput` + `UwbTarget`。  
输出：`MotionIntent`。

职责：只生成“想怎么动”，不直接控制电机。

### 9.4 `motion_mixer`

输入：`MotionIntent` + 速度限制。  
输出：`MotorCommand`。

差速混控规则：

```text
left  = forward + turn
right = forward - turn
normalize 到 -1.0..1.0
乘以 speed_limit
应用加速度/减速度限制
left_reverse  = left_target < 0
right_reverse = right_target < 0
油门 PWM 只使用 abs(target) 映射到 0-5V 模拟量
```

要求：`MotorCommand.left_reverse` 和 `MotorCommand.right_reverse` 必须能独立表达左右轮方向。原地转向时允许一侧正转、一侧反转；禁止退回单一 `reverse` 布尔值。

### 9.4.1 极性语义

刹车、左右倒车、软件使能必须按 `POLARITY-DEFINITIONS.md` 实现。Profile 里的 `active_high` 指的是 **ESP32 GPIO 对 MOS/光耦输入的有效电平**，不是控制器线本身高电平有效。

当前默认：

```yaml
polarity:
  mcu_brake_out_active_level: high
  mcu_left_reverse_out_active_level: high
  mcu_right_reverse_out_active_level: high
  mcu_drive_enable_out_active_level: high
  controller_brake_line_active: pull_to_gnd
  controller_reverse_line_active: pull_to_gnd
  controller_enable_line_active: isolated_switch_or_relay
  estop_active_level: high
```

即 ESP32 GPIO 高电平只表示“驱动 MOS/光耦导通”，再由 MOS/光耦把控制器低刹/倒车线拉到 GND。代码禁止把 GPIO 直接接到控制器开关量线。

### 9.5 `drive_adapter_analog_bldc`

输入：`MotorCommand`。  
输出：GPIO12/GPIO13 油门 PWM、GPIO14 刹车、GPIO15/GPIO16 左右独立倒车、GPIO39 软件使能。旧 GPIO35/36/37/47/48 输出方案作废。

硬要求：

- 上电默认 PWM = 0。
- 左右倒车必须独立控制：`PIN_LEFT_REVERSE_OUT` 与 `PIN_RIGHT_REVERSE_OUT` 不能并联，否则无法原地差速转向。
- 禁止输出层只有单一 `reverse` 字段；`drive_adapter_analog_bldc` 必须最终独立写 GPIO15/GPIO16。
- `enable=false` 时 PWM = 0。
- `brake=true` 时 PWM = 0 并输出刹车控制。
- 未校准 PWM→0-5V 模块前，只允许低占空比架空测试。
- PWM 输出必须经过死区、最大电压限幅、上升/下降斜率限制；禁止直接把 -1..1 命令线性打满 0..5V。
- 不允许在此模块里判断 UWB/避障/模式；它只执行上游安全裁决后的命令。

### 9.6 `sensors/*`

I2C 总线硬要求：TCA9548A + VL53L1X ×3 的 SDA/SCL 必须外接 4.7kΩ 上拉到 3.3V，不能依赖 ESP32 内部弱上拉。`hal/i2c_bus.cpp` 必须提供 Bus Clear/软复位：检测 SDA 被拉低或传输超时时，释放 I2C 外设，手动输出约 9 个 SCL 脉冲并产生 STOP，然后重初始化 TCA9548A 和各 TOF 通道。省去 XSHUT 引脚后，Bus Clear 是 P0 防死锁要求。

每个传感器模块必须提供：

```cpp
bool begin();
void update(uint32_t now_ms);
Snapshot getSnapshot() const;
bool isOnline(uint32_t now_ms) const;
```

传感器错误不能卡死系统；只能标记无效、过期或错误码。

VL53L1X ×3 必须用非阻塞状态机读取：启动测距 -> 轮询 dataReady/中断标志 -> 读取已完成结果 -> 清中断/启动下一轮。禁止在 `sensor_task` 中对 3 个 TOF 依次调用阻塞式等待或 `delay()`；一次 sensor tick 内只做小步状态推进。I2C 任何异常只能标记传感器无效/过期并触发 Bus Clear，不能卡死控制任务。

### 9.7 `web/*`

H5 只能：

- 显示 `SystemState`。
- 提交低速点动请求。
- 提交模式请求。
- 提交人工复位请求。

H5 不能：

- 直接设置 PWM。
- 直接清除急停。
- 跳过安装向导。
- 修改安全红线参数。

## 10. 配置和 Profile

默认配置放 `include/config/profile_defaults.h`，运行时校准和安装向导结果放 NVS/文件系统。

第一版默认值：

```yaml
chassis:
  name: followbox_ds600_dual_bldc_v1
  chassis_type: differential_wheel
  battery_nominal_voltage: 36
  battery_full_voltage: 42
  supported_battery_max_voltage: 60
  max_speed_first_run_percent: 20
  max_speed_after_install_percent: 30
  accel_limit: 0.25
  decel_limit: 0.5

drive_adapter:
  adapter_type: ANALOG_BLDC
  throttle_output: pwm_to_0_5v
  watchdog_timeout_ms: 300
  invert_left_motor: false
  invert_right_motor: false

battery_adc:
  divider_top_ohm: 220000
  divider_bottom_ohm: 10000
  adc_max_input_mv: 3300
  supported_pack_max_mv: 60000
  old_130k_10k_forbidden: true

sensors:
  uwb_stale_stop_ms: 1000
  obstacle_stale_timeout_ms: 300
  slow_distance_mm: 1000
  stop_distance_mm: 500
  target_distance_mm: 1500
  tof_nonblocking_required: true
  i2c_external_pullup_ohm: 4700
  i2c_bus_clear_required: true
  imu_static_boot_hold_ms: 3000
  uwb_min_distance_from_dcdc_mm: 50

throttle:
  left_pwm_pin: 12
  right_pwm_pin: 13
  left_reverse_pin: 15
  right_reverse_pin: 16
  drive_enable_pin: 39
  external_pulldown_10k_required: true
  throttle_deadband_mv: 800
  throttle_min_active_mv: 1000
  throttle_max_mv: 3600
  throttle_rc_delay_ms: 150
  adc_feedback_recommended: true

remote:
  physical_lost_stop_ms: 500
  h5_lost_stop_ms: 500

alert:
  buzzer_type: passive_5v
  buzzer_gpio: -1
  buzzer_driver: transistor_or_mos_low_side
```

## 10.1 Profile Schema v1.0

Profile 字段名、单位和权限必须稳定，H5 只能修改标记为可调的非安全红线字段。

| 字段 | 单位/类型 | 默认 | 范围 | H5 可改 | 说明 |
|---|---|---:|---|---|---|
| `drive_adapter.adapter_type` | enum | `ANALOG_BLDC` | 固定 | 否 | 双 BLDC 控制器 + 模拟转把 |
| `drive_adapter.throttle_output` | enum | `pwm_to_0_5v` | 固定 | 否 | PWM 转 0-5V 模块 |
| `drive_adapter.feedback_source` | enum | `none` | `none/optional_external_adc` | 否 | 首版无控制器闭环反馈 |
| `throttle.throttle_deadband_mv` | mV | 800 | 实测后写入 | 安装向导内 | 转把死区 |
| `throttle.throttle_max_mv` | mV | 3600 | <= 实测安全上限 | 安装向导内 | 最大油门电压限幅 |
| `throttle.throttle_rc_delay_ms` | ms | 150 | 50-300 | 否 | PWM 转模拟 RC 延迟冗余 |
| `sensors.stop_distance_mm` | mm | 500 | >= 500 | 否 | 安全停车距离红线 |
| `remote.h5_lost_stop_ms` | ms | 500 | 300-500 | 否 | H5 点动断线停车 |
| `pins.left_throttle_pwm` | GPIO | 12 | 固定 | 否 | 左油门 PWM |
| `pins.right_throttle_pwm` | GPIO | 13 | 固定 | 否 | 右油门 PWM |
| `pins.brake_out` | GPIO | 14 | 固定 | 否 | MOS/光耦输入，不直连控制器线 |
| `pins.left_reverse_out` | GPIO | 15 | 固定 | 否 | 左倒车 MOS/光耦输入 |
| `pins.right_reverse_out` | GPIO | 16 | 固定 | 否 | 右倒车 MOS/光耦输入 |
| `pins.drive_enable_out` | GPIO | 39 | 固定 | 否 | 使能 MOS/继电器输入 |
| `pins.estop_status` | GPIO | 21 | 固定 | 否 | 急停隔离反馈 |
| `pins.ultrasonic_shared_trig` | GPIO | 9 | 固定 | 否 | 超声共享 TRIG |
| `pins.ultrasonic_left_echo` | GPIO | 40 | 固定 | 否 | 左 Echo 分压后输入 |
| `pins.ultrasonic_right_echo` | GPIO | 41 | 固定 | 否 | 右 Echo 分压后输入 |
| `pins.lidar_rx` | GPIO | 3 | 固定 | 否 | 实物拆机雷达 DATA/TX 输入 |
| `pins.lidar_tx` | GPIO | 43 | 固定 | 否 | 实物拆机雷达 CTL/RX 输出 |
| `pins.imu_rx` | GPIO | 42 | 固定 | 否 | JY61P TX 输入 |
| `polarity.mcu_brake_out_active_level` | enum | `high` | high/low | 否 | GPIO 对 MOS/光耦输入有效电平 |
| `polarity.controller_brake_line_active` | enum | `pull_to_gnd` | 固定/实测 | 否 | 控制器低刹有效方式 |
| `polarity.estop_active_level` | enum | `high` | high/low | 否 | GPIO21 急停有效电平 |
| `safety.estop_feedback_required` | bool | true | 固定 | 否 | P0 必接 |
| `safety.estop_feedback_type` | enum | `second_nc_contact` | second_nc_contact/optocoupler_elock_detect | 否 | 急停反馈硬件类型 |
| `safety.estop_fault_on_open_wire` | bool | true | 固定 | 否 | 反馈断线按急停处理 |
| `throttle.pwm_frequency_hz` | Hz | 1000 | 500-5000 实测 | 否 | PWM→0-5V 输入频率 |
| `throttle.pwm_resolution_bits` | bit | 12 | 10-14 | 否 | PWM 分辨率 |
| `throttle.measured_module_full_scale_mv` | mV | 5000 | 实测 | 安装向导 | 模块满量程输出 |
| `throttle.throttle_slew_rise_mv_per_s` | mV/s | 800 | 实测 | 安装向导 | 油门上升斜率 |
| `throttle.throttle_slew_fall_mv_per_s` | mV/s | 1600 | 实测 | 安装向导 | 油门下降斜率 |
| `remote.physical_lost_stop_ms` | ms | 500 | 100-1000 | 否 | DS600 丢失停车 |
| `video.video_required_for_motion` | bool | false | 固定 | 否 | 视频断流不影响安全主控 |
| `battery_adc.divider_top_ohm` | ohm | 220000 | 固定 | 否 | 电池 ADC 上拉分压，36V-60V 通用 |
| `battery_adc.divider_bottom_ohm` | ohm | 10000 | 固定 | 否 | 电池 ADC 下拉分压；60V 输出约 2.61V |
| `battery_adc.supported_pack_max_mv` | mV | 60000 | 固定 | 否 | 预留 48V 满电/60V 上限，不允许旧 130k/10k |
| `sensors.i2c_external_pullup_ohm` | ohm | 4700 | 固定 | 否 | SDA/SCL 外接上拉到 3.3V |
| `sensors.i2c_bus_clear_required` | bool | true | 固定 | 否 | 无 XSHUT 时必须实现 Bus Clear/软复位 |
| `sensors.imu_static_boot_hold_ms` | ms | 3000 | 固定 | 否 | JY61P 上电静止零偏窗口 |
| `sensors.uwb_min_distance_from_dcdc_mm` | mm | 50 | 固定 | 否 | UWB 与 DC-DC/Buck 电感最小距离 |
| `alert.buzzer_type` | enum | `passive_5v` | passive_5v/none | 否 | 默认无源蜂鸣器，LEDC 频率驱动 |

## 10.2 协议定义文件

协议不再作为口头占位。正式写驱动前必须读取并遵守：

| 协议文件 | 状态 | 要求 |
|---|---|---|
| `protocols/DS600-PWM.md` | 已冻结首版默认值 | CH1-CH5、脉宽范围、回中死区、丢失判定、5V 分压 |
| `protocols/H5-API.md` | 已冻结 v1.0 | 状态 JSON、低速点动、模式请求、人工复位限制 |
| `protocols/JY61P.md` | 半冻结 | 接线、电平、常见帧格式；到货后确认波特率/样例帧 |
| `protocols/UWB-GC-P2304.md` | 待厂家协议/抓包冻结 | 禁止 AI 编造帧头、校验、字段；协议未冻结前只允许 raw log/mock parser |

`uwb_gc_p2304.cpp` 在缺少原厂协议或串口样例前，不得实现猜测性 parser，更不得让 AUTO_FOLLOW 依赖未验证字段。

## 11. 安装向导锁

IMU 上电静止要求：JY60/JY61P 在上电最初 2-3 秒会估计静态零偏。BOOT_SELF_TEST 阶段必须保留 IMU 静止窗口；若检测到明显 yaw_rate/加速度扰动，应保持安全锁定并提示“上电后静止 3 秒/请重启或重新标定”。控制盒外观也必须贴静止标识，防止 yaw 灾难性漂移进入 AUTO_FOLLOW。

蜂鸣器要求：默认 5V 无源蜂鸣器 + 三极管/MOS 低边驱动，使用 LEDC/PWM 产生不同频率提示音；若未分配 GPIO 则 `BUZZER_GPIO=-1`，不能占用 P0 安全引脚。代码必须区分 active buzzer/passive buzzer，不得把有源蜂鸣器按频率旋律驱动。

未完成安装向导时：

允许：

- 查看状态。
- 传感器只读测试。
- DS600/H5 低速点动。
- 云端日志读取与架空低速远程点动。
- 架空单轮测试。

禁止：

- 自动跟随。
- 绕障前进。
- 高速输出。
- 远程公网驾驶、长距离连续路径、云端直接 PWM 控制。

必过项：

1. 架空车轮确认。
2. 急停测试。
3. 左轮方向测试。
4. 右轮方向测试。
5. 油门 PWM→0-5V 输出校准。
6. 电池 ADC 分压核对：确认 220k/10k，按万用表输入电压校准；禁止 130k/10k 用于 48V 底盘。
7. I2C 物理上拉和 Bus Clear 测试：拔/短暂扰动某一路 TOF 后系统不能卡死。
8. IMU 上电静止 3 秒测试：晃动车体时不得进入 AUTO_FOLLOW。
9. UWB 1m/2m 距离测试，并确认 DC-DC 工作时无明显跳变/丢包。
10. UWB 左右方向测试。
11. 前方 1m 限速、0.5m 停车测试。
9. DS600 遥控丢失停车测试。
10. H5 断线停车测试。
11. 保存 Profile。
12. 解锁自动跟随。

## 12. 文件大小和编码规则

| 文件类型 | 建议上限 | 超过后处理 |
|---|---:|---|
| `main.cpp` | 120 行 | 必须拆到 `app/` |
| 单个 `.cpp` | 250-350 行 | 拆子模块或提取工具函数 |
| 单个 `.h` | 150 行 | 只保留接口、结构体、常量 |
| `web/index.html` | 独立文件 | 通过工具生成 `web_assets.h` |
| 配置常量 | 集中配置文件 | 不散落在业务文件 |

编码规则：

- C++ 使用明确类型，时间用 `uint32_t now_ms`。
- 所有超时判断使用 `elapsedMs(now, last)`，避免 `millis()` 溢出问题。
- 业务模块不直接调用 `delay()`。
- 控制循环不做动态内存分配。
- 不在中断里做复杂解析或日志输出。
- 日志要短，控制循环日志限频。
- 新增模块必须有 `.h/.cpp`，并在本规范职责表里能找到归属。

## 13. PlatformIO / VS Code 约定

推荐使用 PlatformIO 项目：

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=1
  -D FOLLOWBOX_HW_V1=1
```

说明：

- ESP32-S3 原生 USB CDC 可能出现“烧录口”和“Serial 输出口”两个 COM 口。
- Windows 烧录/串口监视以实际设备管理器 COM 口为准。
- 如果 Serial Monitor 没输出，优先检查是否连到 CDC 输出口。

## 14. 首批编码顺序

必须按顺序来，不要一上来接所有硬件：

1. 建立 PlatformIO 项目骨架和目录结构。
2. 新建 `board_pins.h`、`types.h`、`system_state.h`、`error_codes.h`、`time_utils.h`。
3. 实现纯逻辑：`safety_manager`、`mode_manager`、`motion_mixer`。
4. 给纯逻辑写本地单元测试或最小 host test。
5. 实现 HAL：GPIO、PWM 输入、PWM 输出、ADC、UART、I2C、Watchdog。
6. 实现 DS600 PWM 输入，只打印通道值，不接电机。
7. 实现 `drive_adapter_analog_bldc`，只做架空低速输出。
8. 接入 UWB，只读距离/方位和超时状态。
9. 接入 TOF/超声/IMU，只读和超时保护。
10. 接入 H5 状态页和低速点动。
11. 接入安装向导锁。
12. 最后开启 AUTO_FOLLOW，且只允许低速参数。

## 15. 验收标准

每次写代码后至少验证：

- 编译通过。
- `main.cpp` 没有业务逻辑膨胀。
- 没有模块直接写油门 PWM 输出，除了 `drive_adapter_analog_bldc`；旧 GPIO47/GPIO48 油门方案作废。
- 传感器模块没有直接控制电机。
- H5 模块没有直接解除安全锁。
- 急停/故障/断线时 `MotorCommand.enable=false` 且 PWM=0。
- 上电、复位、看门狗恢复时电机不动。
- DS600 断联停车。
- UWB 丢失停车。
- 障碍过近停车。

## 16. 给任何开发者/AI 的硬性要求

无论是 VS Code 手写、Claude、Copilot，还是后续其他人维护，都必须遵守：

1. 代码框架以 `FIRMWARE-SPEC.md` 为准；开始任务必须先读本文件和 `CURRENT-WIRING-AI.md`。
1a. 禁止引用 `CURRENT-WIRING.html` 里的旧引脚；HTML 只能由当前 Markdown 重新生成，不作为手改源。
2. 不允许把多个模块塞进 `main.cpp`。
3. 不允许为了省事跨层调用硬件输出。
4. 不允许新增全局状态绕开 `SystemState`。
5. 不允许把网页大段 HTML 写进 `main.cpp`。
6. 不允许未完成安装向导就开启自动跟随。
7. 不允许传感器异常时默认“安全可通行”。
8. 不允许故障自动恢复继续行驶。
9. 改动代码必须说明修改了哪些文件、为什么改、怎么验证。

## 17. 当前结论

这份文件就是后续固件代码框架的总规范。以后写代码、审查代码、让 AI 改代码，都先看它；不符合它的实现，哪怕能编译，也要退回重构。

最终硬边界再次明确：

- main.cpp 只做启动入口，不写业务逻辑。
- 每个模块单独 `.h/.cpp`，不能把多个模块混在一个大文件里。
- 总调配代码必须分开：`app/app.cpp`、`app/scheduler.cpp`、`app/mode_manager.cpp`、`app/command_pipeline.cpp` 分别负责初始化、调度、模式、命令管线。
- drive_adapter_analog_bldc 是唯一能写安全重映射后油门 PWM 输出的模块；旧 GPIO47/GPIO48 方案作废。
- safety_manager 是所有运动命令的安全门。
- 传感器模块只能读传感器，不能控制电机。
- H5 不能直接设置 PWM，不能直接解除安全锁，不能绕过安装向导。
- GPIO35/36/37/47/48 禁止作为电机驱动输出；旧方案必须重构。
- Core 1 跑运动安全闭环，Core 0 跑通信/网络；跨任务共享状态必须双缓冲或短锁保护。
- VL53L1X 阵列必须非阻塞读取，禁止 3 路 TOF 阻塞轮询。
- 急停状态反馈、输出下拉、预充、防火花、油门死区/限幅属于 P0。
- FIRMWARE-SPEC.md 是固件代码框架总规范。
