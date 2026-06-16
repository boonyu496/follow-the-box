# Follow the Box 固件代码架构规划

> 架构草案已升级为正式 `FIRMWARE-SPEC.md`。后续实际写代码以 `FIRMWARE-SPEC.md` 为准；本文件只保留为架构规划参考。
>
> **AI/开发者注意：若本文件与 `FIRMWARE-SPEC.md` 或 `CURRENT-WIRING-AI.md` 有任何差异，必须以 `FIRMWARE-SPEC.md` + `CURRENT-WIRING-AI.md` 为准。当前唯一 Pin Map v1.0 和左右独立倒车规则见下方“当前样机硬件边界”。**

## 0. 当前样机硬件边界（必须与正式规范一致）

```text
DS600 CH1-CH5 -> GPIO4-GPIO8
DS600 CH6     -> 首版不接，P1 可选
超声 TRIG     -> GPIO9
TOF I2C       -> SDA GPIO10 / SCL GPIO11
左/右油门 PWM -> GPIO12 / GPIO13，经 PWM→0-5V 模块
刹车输出       -> GPIO14
左/右倒车输出  -> GPIO15 / GPIO16，必须独立，不能并联
UWB UART      -> TX GPIO17 / RX GPIO18
急停状态反馈   -> GPIO21，P0 必接
电池 ADC      -> GPIO1
控制器故障输入  -> GPIO2
软件使能       -> GPIO39
左/右超声 Echo -> GPIO40 / GPIO41，必须分压
IMU RX        -> GPIO42，接 JY61P TX
```

旧 GPIO35/36/37/47/48 电机输出方案作废；旧单 `bool reverse` 倒车表达作废。`MotorCommand` 必须包含 `left_target`、`right_target`、`left_reverse`、`right_reverse`，油门 PWM 只使用 `abs(target)` 映射到 0-5V。

## 1. 总原则

1. `main.cpp` 只做启动入口，不写业务逻辑，目标控制在 80-120 行以内。
2. 每个硬件驱动、功能模块、调度模块单独 `.h/.cpp`。
3. 所有模块通过统一数据结构交换状态，不允许互相直接改内部变量。
4. 安全链路独立成 `safety_manager`，任何运动命令都必须先过安全门。
5. H5 页面、配置、调试日志与运动控制代码分开，避免一个文件越来越大。
6. 第一版只做低速安全闭环，不做复杂导航框架。

## 2. 推荐目录结构

```text
firmware/
  platformio.ini
  include/
    config/
      board_pins.h              # GPIO 分配，只在这里定义引脚
      build_flags.h             # 编译开关：是否启用 UWB/TOF/CAM/H5
      profile_defaults.h        # 首台样机默认参数
    core/
      types.h                   # 全局数据结构：传感器、模式、运动命令
      system_state.h            # SystemState 总状态快照
      error_codes.h             # 错误码/故障码
      time_utils.h              # millis 时间差工具，避免溢出问题
  src/
    main.cpp                    # 只调用 App::begin() / App::tick()

    app/
      app.h / app.cpp           # 总入口：初始化、创建任务、主循环
      scheduler.h / scheduler.cpp
      mode_manager.h / mode_manager.cpp
      command_pipeline.h / command_pipeline.cpp

    safety/
      safety_manager.h / safety_manager.cpp
      failsafe_rules.h / failsafe_rules.cpp
      self_test.h / self_test.cpp

    control/
      rc_input_ds600.h / rc_input_ds600.cpp
      h5_control_input.h / h5_control_input.cpp
      follow_controller_uwb.h / follow_controller_uwb.cpp
      obstacle_manager.h / obstacle_manager.cpp
      motion_mixer.h / motion_mixer.cpp
      speed_limiter.h / speed_limiter.cpp

    drive/
      drive_adapter.h           # 抽象接口
      drive_adapter_analog_bldc.h / drive_adapter_analog_bldc.cpp
      motor_output.h / motor_output.cpp
      brake_output.h / brake_output.cpp

    sensors/
      uwb_gc_p2304.h / uwb_gc_p2304.cpp
      tof_vl53l1x_array.h / tof_vl53l1x_array.cpp
      ultrasonic_array.h / ultrasonic_array.cpp
      imu_jy61p.h / imu_jy61p.cpp
      power_monitor.h / power_monitor.cpp
      camera_link.h / camera_link.cpp

    hal/
      gpio_in.h / gpio_in.cpp
      gpio_out.h / gpio_out.cpp
      pwm_input.h / pwm_input.cpp
      pwm_output.h / pwm_output.cpp
      adc_reader.h / adc_reader.cpp
      i2c_bus.h / i2c_bus.cpp
      uart_bus.h / uart_bus.cpp
      watchdog.h / watchdog.cpp

    web/
      h5_web_server.h / h5_web_server.cpp
      telemetry_api.h / telemetry_api.cpp
      web_assets.h              # 由 tools 生成，不手写大段 HTML

    storage/
      profile_store.h / profile_store.cpp
      calibration_store.h / calibration_store.cpp

    telemetry/
      telemetry_logger.h / telemetry_logger.cpp
      debug_console.h / debug_console.cpp

  web/
    index.html                  # H5 页面源码，不塞进 main.cpp
    app.js
    style.css

  tools/
    build_web_assets.py         # 把 web/ 打包生成 src/web/web_assets.h
    serial_log_parser.py        # 后续分析串口日志

  test/
    test_safety_manager/
    test_motion_mixer/
    test_mode_manager/
    test_follow_controller/
```

## 3. 核心运行链路

所有运动都走同一条流水线：

```text
传感器/输入采集
  -> SystemState 快照
  -> safety_manager 计算 SafetyDecision
  -> mode_manager 选择当前控制来源
  -> command_pipeline 生成 MotionIntent
  -> obstacle_manager / speed_limiter 限制速度
  -> motion_mixer 转成左右轮目标
  -> safety_manager.applyFinalGate() 最终门控
  -> drive_adapter_analog_bldc 输出左右油门 PWM/刹车/左右倒车/软件使能
```

重点：

- DS600 手动遥控、H5 低速遥控、UWB 自动跟随都不能直接控制电机。
- 它们只能输出 `MotionIntent`，最后统一由 `command_pipeline` 和 `safety_manager` 裁决。
- `drive_adapter_analog_bldc` 是唯一能写安全重映射后油门 PWM 输出的模块；旧 GPIO47/GPIO48 方案作废。

## 4. 分层职责

| 层 | 文件/模块 | 职责 | 禁止做什么 |
|---|---|---|---|
| 启动层 | `main.cpp`, `app.cpp` | 初始化、任务创建、循环调用 | 禁止写传感器算法/运动算法 |
| 硬件抽象层 | `hal/*` | GPIO、PWM、ADC、UART、I2C、Watchdog | 禁止写业务判断 |
| 传感器层 | `sensors/*` | 读取 UWB/TOF/超声/IMU/电池/摄像头状态 | 禁止直接控制电机 |
| 输入层 | `rc_input_ds600`, `h5_control_input` | 把遥控/网页指令转成标准输入 | 禁止绕过安全逻辑 |
| 安全层 | `safety/*` | 急停、断线、低电压、障碍、超时、故障锁定 | 禁止被其他模块跳过 |
| 决策层 | `mode_manager`, `command_pipeline` | 选择手动/自动/网页控制，合成运动意图 | 禁止直接写 PWM |
| 控制层 | `follow_controller`, `obstacle_manager`, `motion_mixer` | 跟随控制、避障限速、差速混控 | 禁止读写硬件 GPIO |
| 驱动层 | `drive/*` | 油门 PWM、刹车、倒车、使能输出 | 禁止决定是否安全 |
| 通信层 | `web/*`, `telemetry/*` | H5 状态、日志、调试接口 | 禁止高优先级阻塞控制循环 |
| 存储层 | `storage/*` | Profile、校准参数保存/读取 | 禁止在控制循环里频繁写 Flash |

## 5. 关键数据结构边界

后续代码应先定义这些结构，再写模块：

| 数据结构 | 来源 | 用途 |
|---|---|---|
| `RcInput` | DS600 | 六通道 PWM、是否在线、最后更新时间 |
| `H5ControlInput` | H5 | 网页低速点动/模式请求/解锁请求 |
| `UwbTarget` | UWB | 距离、方位、置信度、是否过期 |
| `ObstacleSnapshot` | TOF + 超声 | 前左/前中/前右/左侧/右侧距离和有效性 |
| `ImuSnapshot` | JY61P | yaw、yaw_rate、pitch、roll、是否有效 |
| `PowerStatus` | ADC/故障输入 | 电池电压、低电压、控制器故障 |
| `SafetyDecision` | safety_manager | 是否允许运动、限速、停车原因、故障锁定 |
| `MotionIntent` | mode/跟随/遥控 | 前进速度、转向、请求模式 |
| `MotorCommand` | motion_mixer | left/right signed 目标、刹车、使能、左右独立倒车输出 |
| `SystemState` | app/scheduler | 当前全局快照，只读传给 H5/日志 |

规则：模块之间优先传结构体快照，不传一堆零散全局变量。

## 6. 模式状态机

```text
BOOT_SELF_TEST
  -> SAFE_IDLE
  -> MANUAL_RC
  -> MANUAL_H5_LOW_SPEED
  -> AUTO_FOLLOW
  -> FAULT_LOCKOUT
  -> ESTOP_ACTIVE
```

| 模式 | 进入条件 | 退出条件 | 电机权限 |
|---|---|---|---|
| `BOOT_SELF_TEST` | 上电 | 自检通过/失败 | 无 |
| `SAFE_IDLE` | 自检通过但未解锁 | 人工选择模式 | 无，允许只读状态 |
| `MANUAL_RC` | DS600 在线且手动通道有效 | DS600 丢失/切模式/故障 | 有，低速限幅 |
| `MANUAL_H5_LOW_SPEED` | 本地 H5 解锁且 DS600 不接管 | H5 断线/DS600 接管/故障 | 有，更低速限幅 |
| `AUTO_FOLLOW` | 安装向导完成、UWB 有效、人工确认 | UWB 丢失/障碍/接管/故障 | 有，受避障限速 |

Lost-link 裁决必须按当前模式执行：`MANUAL_RC` 只因 DS600 丢失停车，`MANUAL_H5_LOW_SPEED` 只因 H5 断线停车，`AUTO_FOLLOW` 只因 UWB 丢失停车；非当前控制源离线只更新在线状态或禁用对应模式，不得在 `applyFinalGate()` 中写成全局停车。

| `FAULT_LOCKOUT` | 任一严重故障 | 人工复位 + 故障消除 | 无 |
| `ESTOP_ACTIVE` | 急停触发/急停状态输入断开 | 物理恢复 + 人工复位 | 无 |

优先级固定：

```text
急停 > 故障锁定 > DS600 手动接管 > 避障限速/停车 > 自动跟随 > H5 低速点动
```

## 7. FreeRTOS / 循环频率建议

P0 修订：`control_task` 固定 Core 1 运行；`comm_task` 固定 Core 0 低优先级运行。跨任务共享 `SystemState` 必须双缓冲或短锁保护。`sensor_task`/`uwb_task` 必须更新软件心跳，超过 200ms 由 `safety_manager` 触发故障锁定。VL53L1X 阵列必须非阻塞读取。


第一版不要开太多复杂任务，建议 4 类任务即可：

| 任务 | 建议频率/方式 | 优先级 | 内容 |
|---|---:|---:|---|
| `control_task` | 50Hz 固定周期 | 最高 | safety、mode、pipeline、mixer、drive 输出 |
| `sensor_task` | 20-50Hz | 高 | TOF/超声/IMU/电池快照更新 |
| `uwb_task` | 串口事件 + 超时检查 | 高 | UWB 帧解析、距离/方位更新 |
| `comm_task` | 5-10Hz | 低 | H5、遥测、日志、摄像头在线状态 |

要求：

- 电机输出只能在 `control_task` 里更新。
- H5/WebSocket 不允许阻塞 `control_task`。
- 串口解析失败不能卡死任务，必须丢帧并记录错误码。
- Watchdog 覆盖所有任务，任务超时进入故障锁定。

## 8. 文件大小控制

| 文件类型 | 建议上限 | 超过后处理 |
|---|---:|---|
| `main.cpp` | 120 行 | 必须拆到 `app/` |
| 单个模块 `.cpp` | 250-350 行 | 拆子模块或提取工具函数 |
| 单个 `.h` | 150 行 | 只保留接口和结构体 |
| H5 `index.html` | 独立在 `web/` | 不直接写进 `main.cpp` |
| 配置常量 | 独立在 `include/config/` | 不散落在业务文件 |

## 9. 首批实现顺序

后续真正写代码时，建议按这个顺序拆给 Claude/Copilot：

1. 建立 PlatformIO 项目骨架和目录结构，不接硬件逻辑。
2. 定义 `board_pins.h`、`types.h`、`system_state.h`、`error_codes.h`。
3. 实现 `safety_manager`、`mode_manager`、`motion_mixer` 的纯逻辑，并做本地单元测试。
4. 实现 HAL：PWM 输入、PWM 输出、ADC、GPIO、UART、I2C。
5. 实现 DS600 输入读取和通道校准。
6. 实现 `drive_adapter_analog_bldc`，只做架空电机低速输出。
7. 接入 UWB、TOF、超声、IMU，各自先只读和超时保护。
8. 接入 H5 状态页和低速点动。
9. 接入安装向导，未完成向导前禁止自动跟随。
10. 最后开启 `AUTO_FOLLOW`，并只允许低速参数。

## 10. 后续代码任务的硬要求

无论是 VS Code 手写、Claude/Copilot 辅助，还是后续其他人维护，都必须写清：

- 只允许按本文件目录结构创建/修改文件。
- 禁止把多个模块塞进 `main.cpp`。
- 禁止新增未列出的全局状态；确实需要新增必须先改 `types.h/system_state.h`。
- 禁止在传感器模块里直接写电机输出。
- 禁止在 H5/Web 代码里直接解除安全锁。
- 每次实现后必须给出编译命令、测试命令、改动文件列表。

## 11. 当前结论

你的判断是对的：这个项目必须模块化。正确做法不是“一个大文件写完能跑”，而是先把安全、输入、决策、驱动、传感器、网页、存储分层。这样后面接硬件、查故障、让 AI 分工改代码，才不会越改越乱。

必须明确执行：

- main.cpp 只做启动入口，不写业务逻辑。
- drive_adapter_analog_bldc 是唯一能写安全重映射后油门 PWM 输出的模块；GPIO12/GPIO13 输出左右油门，GPIO15/GPIO16 输出左右独立倒车；旧 GPIO47/GPIO48 油门方案和单 `bool reverse` 方案作废。
- safety_manager 是所有运动命令的安全门，任何输入源都不能绕过；运动链路必须包含预裁决和最终 `MotorCommand` 门控两步。
