# Follow the Box Pin Map v1.0

> 当前唯一 Pin Map：本文件是当前样机唯一引脚表。`FIRMWARE-SPEC.md`、`CURRENT-WIRING-AI.md`、Profile、代码 `board_pins.h` 必须引用本表。若有差异，以本文件 + `CURRENT-WIRING-AI.md` 为准，并同步修正所有文档。

## 当前唯一引脚分配

| 功能 | GPIO | 方向 | P0/P1 | 说明 |
|---|---:|---|---|---|
| DS600 CH1 转向 | 4 | 输入 | P0 | 若 PWM 高电平为 5V，必须分压/电平转换 |
| DS600 CH2 油门 | 5 | 输入 | P0 | 若 PWM 高电平为 5V，必须分压/电平转换 |
| DS600 CH3 限速 | 6 | 输入 | P0 | 若 PWM 高电平为 5V，必须分压/电平转换 |
| DS600 CH4 模式 | 7 | 输入 | P0 | 若 PWM 高电平为 5V，必须分压/电平转换 |
| DS600 CH5 STOP/刹车 | 8 | 输入 | P0 | 若 PWM 高电平为 5V，必须分压/电平转换 |
| DS600 CH6 | -1 | 不接 | P1 | 首版不接；GPIO9 留给超声 TRIG |
| 超声共享 TRIG | 9 | 输出 | P0 | 左右 HC-SR04 共用 TRIG |
| TOF I2C SDA | 10 | 双向 | P0 | TCA9548A + VL53L1X ×3 |
| TOF I2C SCL | 11 | 输出 | P0 | TCA9548A + VL53L1X ×3 |
| 左油门 PWM | 12 | 输出 | P0 | 到左 PWM→0-5V 模块 PWM IN；外部 10k 下拉 |
| 右油门 PWM | 13 | 输出 | P0 | 到右 PWM→0-5V 模块 PWM IN；外部 10k 下拉 |
| 刹车输出 | 14 | 输出 | P0 | 只驱动 MOS/光耦输入；不能直连控制器线 |
| 左倒车输出 | 15 | 输出 | P0 | 只驱动 MOS/光耦输入；不能与右倒车并联 |
| 右倒车输出 | 16 | 输出 | P0 | 只驱动 MOS/光耦输入；不能与左倒车并联 |
| UWB TX | 17 | 输出 | P0 | ESP32 TX -> UWB RX |
| UWB RX | 18 | 输入 | P0 | UWB TX -> ESP32 RX |
| 激光雷达 DATA/RX | 3 | 输入 | P1 | EAI S2 DATA/TX → GPIO3；3.3V 直连，若高于 3.3V 必须转换 |
| 激光雷达 CTL/TX | 43 | 输出 | P1 | ESP32 TX → 雷达 CTL/RX；固件上电发送 `A5 60` 启动 |
| 急停状态反馈 | 21 | 输入 | P0 | 必须来自第二触点干接点或隔离检测，禁止直接接 36V 电门锁线 |
| 电池电压 ADC | 1 | 输入 | P0 | 220k/10k 分压，36V-60V 通用；60V 约 2.61V，旧 130k/10k 禁用 |
| 控制器故障输入 | 2 | 输入 | P1/P0可选 | 必须高阻/光耦/分压；不能影响上电/下载；未确认前不作为输出 |
| 软件使能 | 39 | 输出 | P0 | 只驱动 MOS/光耦/继电器输入；不直连 36V 电门锁 |
| 左超声 ECHO | 40 | 输入 | P0 | HC-SR04 Echo 必须分压/电平转换 |
| 右超声 ECHO | 41 | 输入 | P0 | HC-SR04 Echo 必须分压/电平转换 |
| IMU RX | 42 | 输入 | P0 | 接 JY61P TX；若 5V 电平必须分压/电平转换 |

## 明确作废

```text
GPIO35 / GPIO36 / GPIO37 作为刹车/倒车/使能输出：作废
GPIO47 / GPIO48 作为左右油门 PWM：作废
DS600 CH6 占 GPIO9：作废
HC-SR04 占 GPIO12/13/14/15：作废
JY61P 占 GPIO40/41：作废
单 bool reverse 倒车结构：作废
```
