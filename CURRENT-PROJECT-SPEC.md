# Follow the Box 当前项目总纲（精简版）

> 当前项目总纲：给人和 AI 统一理解自动跟随车控制盒的架构、安全、电源和下一步。

## 1. 当前项目定位

Follow the Box 是自动跟随小车/载物车控制盒首台样机：

- 现场实体遥控：HOTRC DS600 六通道 PWM。
- 自动跟随：GC-P2304-GS-2 UWB，输出距离和方位。
- 避障：前方三路 VL53L1X TOF + 左右 HC-SR04 超声。
- 姿态：JY61P IMU。
- 视频：独立 ESP32-S3-CAM，只做视频，不做安全主控。
- 驱动：两个 36V/48V 350W 有霍尔无刷控制器，分别驱动左右轮毂电机。
- 主控：普通 ESP32-S3-DevKitC-1，负责遥控输入、传感器、安全状态机和油门输出。

## 2. 当前权威架构

```text
HOTRC DS600 接收机 PWM（P0 只接 CH1-CH5，CH6 首版不接）
  -> ESP32-S3-DevKitC-1 GPIO4-GPIO8；GPIO9 保留给超声共享 TRIG
  -> safety_manager / mixer / follow_controller
  -> GPIO12/GPIO13 输出左右油门 PWM（旧 GPIO47/48 作废）
  -> PWM 转 0-5V 模拟量模块 ×2
  -> 左/右 36V 350W 无刷控制器转把信号
  -> 左/右轮毂电机
```


## 3. Antigravity P0 安全修订

1. ESP32-S3-DevKitC-1 N8R8 / 带 PSRAM 板禁止把 GPIO35/36/37/47/48 作为电机驱动输出；旧引脚方案作废。
2. 左右倒车线必须独立：左倒车 GPIO15，右倒车 GPIO16，不能并联。
3. 急停状态反馈 GPIO21 是 P0 必接；急停释放后不允许自动恢复运动。
4. 动力主电源需要防火花/预充设计。
5. PWM→0-5V 油门输出必须做死区、最大电压限幅、斜率限制；避障距离要考虑 50-150ms 模拟输出延迟。
6. 代码采用双核分工：Core 1 运动安全闭环，Core 0 通信/网络；共享状态必须双缓冲或短锁。
7. VL53L1X ×3 必须非阻塞读取，不能在 sensor_task 里阻塞等待 3 路测距。

## 4. 当前必须遵守的安全结论

1. ESP32-S3 只接 5V/3.3V 和低压信号，不接 36V/42V 动力线。
2. ESP32-S3 没有真正 DAC，不能直接输出稳定 0-5V 油门；必须用 `PWM 转 0-5V 模拟量模块 ×2`。
3. Schneider XB5AS542C 1NC 急停只切“电门锁/使能线”，不串两个 350W 控制器 BAT+ 主电流。
4. 电机主电流不走洞洞板、排针、JST 小插头、杜邦线。
5. DS600 PWM、HC-SR04 Echo、JY61P TX 若是 5V 高电平，进 ESP32 前必须分压/电平转换；分压/电平转换物料属于 P0。
6. ESP32-S3-CAM 只做视频，不做主控，不进安全闭环。
7. 首次调试必须架空车轮。

## 5. 当前电源结构

```text
动力主回路：
36V BAT+ -> F1 主保险 -> 总电源开关 -> 左/右控制器 BAT+
36V BAT- -> 左/右控制器 BAT-

急停/电门锁链：
36V BAT+ -> F3 小保险(1A-3A) -> 总电源开关 -> 急停 NC -> 左/右控制器电门锁线

低压控制电源：
36V BAT+ -> F2 低压保险(5A) -> DC-DC VIN+
36V BAT- -> DC-DC VIN-
DC-DC 先空载调到 5.0V -> ESP32/DS600/传感器/视频板
```

## 6. 当前主文档

| 文件 | 用途 |
|---|---|
| `README.md` / `README.html` | 项目入口 |
| `FIRMWARE-SPEC.md` / `FIRMWARE-SPEC.html` | 固件代码框架总规范，后续写代码按此执行 |
| `CURRENT-PROJECT-SPEC.md` / `CURRENT-PROJECT-SPEC.html` | 项目总纲 |
| `CURRENT-PARTS-LIST.md` | 配件清单、待补物料、线束分组 |
| `PIN-MAP-V1.md` | 当前唯一 Pin Map v1.0 |
| `CURRENT-WIRING-AI.md` / `CURRENT-WIRING.html` | 接线方案 |
| `ESTOP-FEEDBACK-CIRCUIT.md` | GPIO21 急停反馈隔离接线 |
| `POLARITY-DEFINITIONS.md` | GPIO/MOS/控制器线极性定义 |
| `PWM-OUTPUT-CALIBRATION.md` | PWM→0-5V 油门校准规范 |
| `CURRENT-BOX-DESIGN.md` | 控制盒外观、分区、端口和装配规则 |
| `CURRENT-FIRMWARE-SKILL-PLAN.md` | 固件模块、Profile、安装向导、测试计划和 skill 规划 |
| `CURRENT-FIRMWARE-ARCHITECTURE.md` / `CURRENT-FIRMWARE-ARCHITECTURE.html` | 固件代码架构、模块拆分和任务调度 |


## 7. 固件代码框架冻结

后续不管用 VS Code 手写，还是用 Claude/Copilot 辅助，都以 `FIRMWARE-SPEC.md` 为代码框架总规范。

硬性边界：

1. `main.cpp` 只做启动入口。
2. 每个模块单独 `.h/.cpp`。
3. `drive_adapter_analog_bldc` 是唯一能写安全重映射后油门 PWM 输出的模块；旧 GPIO47/GPIO48 油门方案作废。
4. 所有运动命令必须先经过 `safety_manager`。
5. H5 页面源码放 `firmware/web/`，不塞进 `main.cpp`。

## 8. 下一步只做这几件事

1. 买/确认 `PWM 转 0-5V 模拟量模块 ×2`。
2. 补齐主保险 F1、F3 小保险、总开关、大电流端子、动力线、分压/电平转换物料、MOS/光耦小板。
3. 到货后拍 DS600 接收机、控制器线标、轮毂电机线束、电池铭牌、急停背面。
4. 按 `CURRENT-WIRING-AI.md` 的上电顺序，先低压、再传感器、再架空单电机。
