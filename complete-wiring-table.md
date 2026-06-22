# Follow the Box 完整接线表（当前版）

> 依据：`PIN-MAP-V1.md`、`CURRENT-WIRING-AI.md`、`CURRENT-PARTS-LIST.md`。本表替代旧占位表；旧 `from_pin/to_pin` 占位内容作废。
> 安全红线：电机主电流、控制器 BAT+/BAT-、电机相线不走洞洞板、排针、JST、杜邦线。

## 1. ESP32-S3 主控引脚总表

| ESP32-S3 引脚 | 方向 | 接到哪里 | 中间电路/器件 | 备注 |
|---|---|---|---|---|
| 5V | 电源输入 | 5V 低压母线 | DC-DC VOUT+，输出端并 470uF + 0.1uF | 给 ESP32 开发板供电 |
| 3V3 | 电源输出 | UWB、TCA9548A、VL53L1X、I2C 上拉、急停反馈上拉 | 不给 5V 模块供电 | 注意 3V3 电流余量 |
| GND | 地 | 低压逻辑地母线 | 只作信号参考；经 DC-DC 输出侧回到动力星型地 | 禁止承载控制器主电流 |
| GPIO1 | 输入 | 电池电压采样节点 | BAT+ -> 220k -> GPIO1 -> 10k -> GND，GPIO1 -> 0.1uF -> GND | 36V-60V 通用分压 |
| GPIO2 | 输入 | 控制器故障输入（可选） | 光耦/高阻分压后再接 | 未确认线性前不接，不影响下载 |
| GPIO4 | 输入 | DS600 CH1 Signal | CH1 S -> 10k -> GPIO4 -> 20k -> GND | 转向，5V PWM 降到约 3.3V |
| GPIO5 | 输入 | DS600 CH2 Signal | CH2 S -> 10k -> GPIO5 -> 20k -> GND | 油门 |
| GPIO6 | 输入 | DS600 CH3 Signal | CH3 S -> 10k -> GPIO6 -> 20k -> GND | 限速 |
| GPIO7 | 输入 | DS600 CH4 Signal | CH4 S -> 10k -> GPIO7 -> 20k -> GND | 模式 |
| GPIO8 | 输入 | DS600 CH5 Signal | CH5 S -> 10k -> GPIO8 -> 20k -> GND | STOP/刹车 |
| GPIO9 | 输出 | 左/右 HC-SR04 TRIG | 直接到两个 TRIG | DS600 CH6 首版不接 |
| GPIO10 | 双向 | TCA9548A SDA | 4.7k 上拉到 3V3 | I2C 主干 SDA |
| GPIO11 | 输出 | TCA9548A SCL | 4.7k 上拉到 3V3 | I2C 主干 SCL |
| GPIO12 | 输出 | 左 PWM->0-5V 模块 PWM IN | GPIO12 -> PWM IN，PWM IN -> 10k -> GND | 左油门，模块 VOUT 到左控制器转把信号 |
| GPIO13 | 输出 | 右 PWM->0-5V 模块 PWM IN | GPIO13 -> PWM IN，PWM IN -> 10k -> GND | 右油门，模块 VOUT 到右控制器转把信号 |
| GPIO14 | 输出 | MOS/光耦通道 1 输入 | GPIO14 -> IN，IN -> 10k -> GND | 控制左右控制器低刹线，禁止直连控制器线 |
| GPIO15 | 输出 | MOS/光耦通道 2 输入 | GPIO15 -> IN，IN -> 10k -> GND | 左倒车线，只控制左控制器 |
| GPIO16 | 输出 | MOS/光耦通道 3 输入 | GPIO16 -> IN，IN -> 10k -> GND | 右倒车线，只控制右控制器 |
| GPIO17 | 输出 | UWB RX | ESP32 TX -> UWB RX | UART 交叉 |
| GPIO18 | 输入 | UWB TX | UWB TX -> ESP32 RX | UART 交叉 |
| GPIO3 | 输入 | EAI S2 激光雷达 DATA/TX | 雷达 DATA -> GPIO3 | UART2 RX，150000 8N1 |
| GPIO21 | 输入 | 急停反馈低压侧 | 3V3 -> 10k -> GPIO21 -> 急停第二 NC -> GND，或光耦低压侧 | GPIO21=1 表示急停/断线故障 |
| GPIO39 | 输出 | MOS/光耦/继电器通道 4 输入 | GPIO39 -> IN，IN -> 10k -> GND | 软件使能，不能直连 36V 电门锁 |
| GPIO40 | 输入 | 左 HC-SR04 ECHO | Echo L -> 10k -> GPIO40 -> 20k -> GND | Echo 降压 |
| GPIO41 | 输入 | 右 HC-SR04 ECHO | Echo R -> 10k -> GPIO41 -> 20k -> GND | Echo 降压 |
| GPIO42 | 输入 | JY61P TX | JY61P TX -> GPIO42；若 TX 是 5V：TX -> 10k -> GPIO42 -> 20k -> GND | 首版只读 IMU TX |
| GPIO43 | 输出 | EAI S2 激光雷达 CTL/RX | ESP32 TX -> 雷达 CTL | 固件发送 `A5 60` 启动 |

## 2. 电源与安全链

| 编号 | 从 | 引脚/端子 | 到 | 引脚/端子 | 线径/器件 | 备注 |
|---|---|---|---|---|---|---|
| PWR-01 | 36V 电池 | BAT+ | F1 主保险 | IN | 12-14AWG，30A 级别按实物确认 | 主动力短路保护，靠近电池 |
| PWR-02 | F1 主保险 | OUT | 总电源开关 | IN | 12-14AWG | 主动力开关 |
| PWR-03 | 总电源开关 | OUT | 左控制器 | BAT+ | 12-14AWG | 不走洞洞板 |
| PWR-04 | 总电源开关 | OUT | 右控制器 | BAT+ | 12-14AWG | 不走洞洞板 |
| PWR-05 | 36V 电池 | BAT- | 动力星型地 | 主负极汇流点 | 12-14AWG | 与左右控制器 BAT- 汇合 |
| PWR-06 | 动力星型地 | 主负极汇流点 | 左控制器 | BAT- | 12-14AWG | 禁止经 ESP32 GND 回流 |
| PWR-07 | 动力星型地 | 主负极汇流点 | 右控制器 | BAT- | 12-14AWG | 禁止经 ESP32 GND 回流 |
| PWR-08 | 36V 电池 | BAT+ | F2 低压保险 | IN | 18-22AWG，5A | DC-DC 输入保护 |
| PWR-09 | F2 低压保险 | OUT | DC-DC | VIN+ | 18-22AWG | 先空载调 5.00V |
| PWR-10 | 动力星型地 | 主负极汇流点 | DC-DC | VIN- | 18-22AWG | 低压源头参考地 |
| PWR-11 | DC-DC | VOUT+ | 5V 低压母线 | 5V | 22-24AWG | 并 470uF + 0.1uF 到 GND |
| PWR-12 | DC-DC | VOUT- | GND 低压母线 | GND | 22-24AWG | 逻辑地母线 |
| ESTOP-01 | 36V 电池/F1 后 | BAT+ | F3 小保险 | IN | 1A-3A | 电门锁/急停支路 |
| ESTOP-02 | F3 小保险 | OUT | 总电源开关后/急停链 | IN | 20-22AWG | 按实物开关位置确认 |
| ESTOP-03 | 急停 NC | OUT | 左/右控制器 | 电门锁线 | 20-22AWG | 按下急停后两个控制器失能 |
| ESTOP-04 | ESP32 3V3 | 3V3 | GPIO21 节点 | 上拉端 | 10k | 急停反馈低压侧 |
| ESTOP-05 | GPIO21 节点 | GPIO21 | 急停第二 NC | 一端 | 24-26AWG | 禁止接 36V 电门锁线 |
| ESTOP-06 | 急停第二 NC | 另一端 | GND 低压母线 | GND | 24-26AWG | 断线等同急停 |

## 3. 遥控、传感器、视频

| 编号 | 模块 | 引脚 | 接到 | 引脚 | 中间电路 | 备注 |
|---|---|---|---|---|---|---|
| RC-01 | DS600 | CH1 S | ESP32 | GPIO4 | 10k/20k 分压 | 转向 |
| RC-02 | DS600 | CH2 S | ESP32 | GPIO5 | 10k/20k 分压 | 油门 |
| RC-03 | DS600 | CH3 S | ESP32 | GPIO6 | 10k/20k 分压 | 限速 |
| RC-04 | DS600 | CH4 S | ESP32 | GPIO7 | 10k/20k 分压 | 模式 |
| RC-05 | DS600 | CH5 S | ESP32 | GPIO8 | 10k/20k 分压 | STOP/刹车 |
| RC-06 | DS600 | CH6 S | 不接 | - | - | GPIO9 留给超声 TRIG |
| RC-07 | DS600 | + | 5V 低压母线 | 5V | - | 以接收机规格为准 |
| RC-08 | DS600 | - | GND 低压母线 | GND | - | 共地 |
| UWB-01 | UWB GC-P2304 | VCC | ESP32/3V3 母线 | 3V3 | - | 远离 DC-DC 至少 50mm |
| UWB-02 | UWB GC-P2304 | GND | GND 低压母线 | GND | - | 共地 |
| UWB-03 | UWB GC-P2304 | TX | ESP32 | GPIO18 | - | UWB TX -> ESP32 RX |
| UWB-04 | UWB GC-P2304 | RX | ESP32 | GPIO17 | - | ESP32 TX -> UWB RX |
| LIDAR-01 | EAI S2 激光雷达 | 5V | 5V 低压母线 | 5V | - | 与 ESP32 共地 |
| LIDAR-02 | EAI S2 激光雷达 | GND | GND 低压母线 | GND | - | 共地 |
| LIDAR-03 | EAI S2 激光雷达 | DATA | ESP32 | GPIO3 | 3.3V 串口电平 | 雷达 DATA/TX -> ESP32 RX |
| LIDAR-04 | EAI S2 激光雷达 | CTL | ESP32 | GPIO43 | 3.3V 串口电平 | ESP32 TX -> 雷达 CTL/RX |
| IMU-01 | JY61P | VCC | 5V 低压母线 | 5V | - | 上电后静止 3 秒 |
| IMU-02 | JY61P | GND | GND 低压母线 | GND | - | 模块保持水平 |
| IMU-03 | JY61P | TX | ESP32 | GPIO42 | 若 TX 5V，10k/20k 分压 | 首版只读 TX |
| IMU-04 | JY61P | RX | 不接 | - | - | P1 配置线，首版不占 GPIO40/41 |
| I2C-01 | TCA9548A | VIN/VCC | 3V3 母线 | 3V3 | - | 统一 3.3V |
| I2C-02 | TCA9548A | GND | GND 低压母线 | GND | - | 共地 |
| I2C-03 | TCA9548A | SDA | ESP32 | GPIO10 | 4.7k 上拉到 3V3 | I2C 主干 |
| I2C-04 | TCA9548A | SCL | ESP32 | GPIO11 | 4.7k 上拉到 3V3 | I2C 主干 |
| TOF-01 | TCA9548A | CH0 SDA/SCL | 前中 VL53L1X | SDA/SCL | - | 前中 |
| TOF-02 | TCA9548A | CH1 SDA/SCL | 左前 VL53L1X | SDA/SCL | - | 左前 |
| TOF-03 | TCA9548A | CH2 SDA/SCL | 右前 VL53L1X | SDA/SCL | - | 右前 |
| TOF-04 | 3V3/GND 母线 | 3V3/GND | 三个 VL53L1X | VCC/GND | - | 4P JST 建议：3V3/GND/SDA/SCL |
| US-01 | 左 HC-SR04 | VCC | 5V 低压母线 | 5V | - | 左侧超声 |
| US-02 | 左 HC-SR04 | GND | GND 低压母线 | GND | - | 共地 |
| US-03 | 左 HC-SR04 | TRIG | ESP32 | GPIO9 | 直接接 | 左右共用 TRIG |
| US-04 | 左 HC-SR04 | ECHO | ESP32 | GPIO40 | 10k/20k 分压 | Echo 降压 |
| US-05 | 右 HC-SR04 | VCC | 5V 低压母线 | 5V | - | 右侧超声 |
| US-06 | 右 HC-SR04 | GND | GND 低压母线 | GND | - | 共地 |
| US-07 | 右 HC-SR04 | TRIG | ESP32 | GPIO9 | 直接接 | 左右共用 TRIG |
| US-08 | 右 HC-SR04 | ECHO | ESP32 | GPIO41 | 10k/20k 分压 | Echo 降压 |
| CAM-01 | ESP32-S3-CAM | 5V | 5V 低压母线 | 5V | - | 只做视频 |
| CAM-02 | ESP32-S3-CAM | GND | GND 低压母线 | GND | - | 不接主控 UART |

## 4. 油门输出、控制器开关量、电机

| 编号 | 从 | 引脚 | 到 | 引脚 | 中间电路/器件 | 备注 |
|---|---|---|---|---|---|---|
| THR-01 | ESP32 | GPIO12 | 左 PWM->0-5V 模块 | PWM IN | PWM IN -> 10k -> GND | 左油门输入 |
| THR-02 | 左控制器 | 转把 +5V | 左 PWM->0-5V 模块 | VCC | 不与右控制器 +5V 短接 | 推荐用控制器转把 5V 供本侧模块 |
| THR-03 | 左控制器 | 转把 GND | 左 PWM->0-5V 模块 | GND | 与低压 GND 作信号参考 | 禁止承载 BAT- 主电流 |
| THR-04 | 左 PWM->0-5V 模块 | VOUT | 左控制器 | 转把信号 | 先万用表校准 | 首版限幅，不追求满速 |
| THR-05 | ESP32 | GPIO13 | 右 PWM->0-5V 模块 | PWM IN | PWM IN -> 10k -> GND | 右油门输入 |
| THR-06 | 右控制器 | 转把 +5V | 右 PWM->0-5V 模块 | VCC | 不与左控制器 +5V 短接 | 推荐用控制器转把 5V 供本侧模块 |
| THR-07 | 右控制器 | 转把 GND | 右 PWM->0-5V 模块 | GND | 与低压 GND 作信号参考 | 禁止承载 BAT- 主电流 |
| THR-08 | 右 PWM->0-5V 模块 | VOUT | 右控制器 | 转把信号 | 先万用表校准 | 首版限幅，不追求满速 |
| SW-01 | ESP32 | GPIO14 | MOS/光耦通道 1 | IN | IN -> 10k -> GND | ESP32 高电平导通 |
| SW-02 | MOS/光耦通道 1 | OUT | 左/右控制器 | 低刹线 | 隔离或开漏拉到控制器信号 GND | 两控制器低刹可并联到同一刹车输出前需实测 |
| SW-03 | ESP32 | GPIO15 | MOS/光耦通道 2 | IN | IN -> 10k -> GND | 左倒车 |
| SW-04 | MOS/光耦通道 2 | OUT | 左控制器 | 倒车线 | 拉到左控制器信号 GND 有效 | 左右倒车不能并联 |
| SW-05 | ESP32 | GPIO16 | MOS/光耦通道 3 | IN | IN -> 10k -> GND | 右倒车 |
| SW-06 | MOS/光耦通道 3 | OUT | 右控制器 | 倒车线 | 拉到右控制器信号 GND 有效 | 左右倒车不能并联 |
| SW-07 | ESP32 | GPIO39 | MOS/光耦/继电器通道 4 | IN | IN -> 10k -> GND | 软件使能 |
| SW-08 | MOS/光耦/继电器通道 4 | OUT | 控制器使能/电门锁隔离侧 | 按实物确认 | 不能直连 36V 电门锁 | 未确认前禁止接 |
| MOT-01 | 左控制器 | 三根相线 | 左轮毂电机 | 三相线 | 粗线/原车插头 | 不进控制盒低压板 |
| MOT-02 | 左控制器 | 霍尔 5线 | 左轮毂电机 | 霍尔线 | 原车插头 | 线序按实物确认 |
| MOT-03 | 右控制器 | 三根相线 | 右轮毂电机 | 三相线 | 粗线/原车插头 | 不进控制盒低压板 |
| MOT-04 | 右控制器 | 霍尔 5线 | 右轮毂电机 | 霍尔线 | 原车插头 | 线序按实物确认 |
