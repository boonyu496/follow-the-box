# Follow the Box 当前唯一接线方案（AI 实施版）

> 文件用途：给 AI/Copilot/Claude/固件实现代理读取。  
> 本文件是当前接线实施依据；按这里的架构、引脚、电源和安全顺序执行。

## 1. 当前硬件架构

```text
HOTRC DS600 接收机 PWM（首版只接 CH1-CH5）
  -> ESP32-S3-DevKitC-1 主控 GPIO4-GPIO8
  -> safety_manager / mixer / follow_controller
  -> GPIO12 / GPIO13 输出左右油门 PWM（旧 GPIO47/48 作废）
  -> PWM 转 0-5V 模拟量模块 ×2
  -> 左/右 36V 350W 无刷控制器转把信号
  -> 左/右轮毂电机
```

辅助模块：

```text
GC-P2304-GS-2 UWB  -> UART GPIO17/18，3.3V
实物拆机激光雷达   -> 规范线序：DATA/TX 接 GPIO3(RX)，CTL/RX 接 GPIO43(TX)，5V/GND，串口电平需确认；固件诊断会临时轮询反接候选
JY61P IMU          -> TX 接 GPIO42，5V 供电，串口电平需确认/分压
VL53L1X ×3         -> TCA9548A -> I2C GPIO10/11，3.3V
HC-SR04 ×2         -> 共享 TRIG GPIO9，Echo 分别进 GPIO40/41 且必须分压
ESP32-S3-CAM       -> 独立视频板，5V 供电，首版用 WiFi 视频/状态，不占主控 UART
电池 ADC           -> GPIO1，经 220k/10k 分压（36V-60V 通用，48V 满电不超 3.3V）
急停状态反馈       -> GPIO21，P0 必接，不能省略
```

---

## 2. 禁止事项（实现和接线都必须遵守）

1. ESP32 GPIO 不直接接 36V/42V。
2. ESP32 GPIO 不直接吃 5V PWM/Echo，必须分压或电平转换。
3. ESP32-S3 没有真 DAC，不直接把 GPIO 当 0-5V 油门；必须用 PWM→0-5V 模块。
4. 急停不能只接 ESP32 GPIO；必须进入硬件电门锁/使能链。
5. 当前 Schneider XB5AS542C 1NC 急停不能串两个 350W 控制器 BAT+ 主电流。
6. 电机主电流不走洞洞板、面包板、JST 小插头、排针、杜邦线。
7. ESP32-S3-CAM 不做安全主控，只做视频。
8. 学习线只在架空单电机自学习时临时短接，学完断开并绝缘。

---

## 3. ESP32-S3 引脚分配

> P0 修订：旧方案中 GPIO35/36/37/47/48 用于刹车/倒车/使能/油门输出；对 ESP32-S3-DevKitC-1 N8R8 / 带 PSRAM 板风险过高，现全部作废。下面是当前安全重映射方案，PCB/线束制作前还要按到货实物复核。

| 功能 | GPIO | 方向 | 接线/说明 |
|---|---:|---|---|
| DS600 CH1 转向 | GPIO4 | 输入 | 接收机 CH1 Signal，若 5V PWM 需 10k/20k 分压 |
| DS600 CH2 油门 | GPIO5 | 输入 | 接收机 CH2 Signal，若 5V PWM 需分压 |
| DS600 CH3 限速 | GPIO6 | 输入 | 接收机 CH3 Signal，若 5V PWM 需分压 |
| DS600 CH4 模式 | GPIO7 | 输入 | 接收机 CH4 Signal，若 5V PWM 需分压 |
| DS600 CH5 STOP/刹车 | GPIO8 | 输入 | 接收机 CH5 Signal，若 5V PWM 需分压 |
| 超声共享 TRIG | GPIO9 | 输出 | 左右 HC-SR04 可共用 TRIG；CH6 暂不接 |
| TOF I2C SDA | GPIO10 | 双向 | 接 TCA9548A SDA；也可挂外置 ADC |
| TOF I2C SCL | GPIO11 | 输出 | 接 TCA9548A SCL |
| 左油门 PWM | GPIO12 | 输出 | 接左 PWM→0-5V 模块 PWM IN；外部 10k 下拉 |
| 右油门 PWM | GPIO13 | 输出 | 接右 PWM→0-5V 模块 PWM IN；外部 10k 下拉 |
| 电机刹车输出 | GPIO14 | 输出 | 经 MOS/光耦控制两个控制器低刹线；外部 10k 下拉 |
| 左倒车输出 | GPIO15 | 输出 | 经 MOS/光耦控制左控制器倒车线；外部 10k 下拉 |
| 右倒车输出 | GPIO16 | 输出 | 经 MOS/光耦控制右控制器倒车线；外部 10k 下拉 |
| UWB TX | GPIO17 | 输出 | 接 UWB RX |
| UWB RX | GPIO18 | 输入 | 接 UWB TX |
| 急停状态输入 | GPIO21 | 输入 | P0 必接；急停按下必须让主控进入 ESTOP/故障锁定 |
| 电池电压 ADC | GPIO1 | 输入 | 36V/60V 通用：经 220k/10k 分压后输入，60V 约 2.61V |
| 控制器故障输入 | GPIO2 | 输入 | 可选，需光耦/分压；必须高阻，不能影响上电/下载 |
| 控制器软件使能 | GPIO39 | 输出 | 只驱动继电器/MOS，不直连高压电门锁；外部 10k 下拉 |
| 左超声 ECHO | GPIO40 | 输入 | 左 HC-SR04 ECHO 分压后输入 |
| 右超声 ECHO | GPIO41 | 输入 | 右 HC-SR04 ECHO 分压后输入 |
| 激光雷达 DATA/TX | GPIO3 | 输入 | 规范线序：实物 DATA/TX -> GPIO3；固件当前默认 UART2 115200 8N1，诊断版会轮询 150000 等候选线序和波特率 |
| 激光雷达 CTL/RX | GPIO43 | 输出 | 规范线序：ESP32 TX -> 雷达 CTL/RX；固件发送 `A5 60`，未识别帧头时只记录 raw，不生成有效障碍物 |
| IMU RX | GPIO42 | 输入 | 接 JY61P TX；若 5V 电平必须分压/电平转换 |

保留/禁用：GPIO35、GPIO36、GPIO37、GPIO47、GPIO48 禁止作为电机驱动输出；GPIO0、GPIO19、GPIO20、GPIO44、GPIO45、GPIO46 不占用；GPIO3/GPIO43 仅用于雷达 UART，固件诊断可在 `spec(DATA/TX->RX CTL/RX<-TX)` 与 `swap(CTL/TX->RX DATA/RX<-TX)` 间临时切换；GPIO33/34 未按具体模组确认前不作 P0 输出；GPIO38 板载 RGB 先保留。

---

## 4. 电源接线

### 4.1 动力电源：36V 电池到两个无刷控制器

```text
36V 电池 BAT+
  -> 主保险 F1（靠近电池正极，非 5A 小保险）
  -> 总电源开关
  -> 分两路到左/右无刷控制器 BAT+

36V 电池 BAT-
  -> 分两路到左/右无刷控制器 BAT-
```

要求：

- 36V 双 350W 额定总电流约 19.4A；F1 首台可按 30A 级别准备，最终按 BMS、线径、控制器铭牌调整。
- 动力线使用粗线、大电流接头/端子；不能经过控制板/洞洞板。
- 当前急停不在 BAT+ 主电流路径中。
- 动力主电源应加入防火花/预充：XT90S/防火花插头，或预充电阻 + 预充开关/继电器，避免控制器输入电容上电打火。

### 4.2 电门锁/急停硬件链

```text
36V 电池 BAT+
  -> F3 小保险（1A-3A）
  -> 总电源开关
  -> 急停 NC（Schneider XB5AS542C 1NC）
  -> 分两路到左/右控制器“电门锁”线
```

说明：

- 急停按下后，两个控制器失能。
- ESP32 不能绕过急停恢复动力。
- 如果实测电门锁线电流大，改为急停控制继电器/接触器线圈。
- 不允许：`36V BAT+ 主电流 -> 急停 NC -> 两个控制器 BAT+`。

### 4.3 急停状态反馈 P0 必接

急停 NC 触点切电门锁/使能线的同时，必须给 ESP32 一个急停状态反馈。急停按下后：

```text
GPIO21 = 急停触发状态
-> safety_manager 立即进入 ESTOP_ACTIVE / FAULT_LOCKOUT
-> 左右油门 PWM 清零
-> 软件使能关闭
-> 急停旋开后也不自动恢复，必须人工复位
```

不允许首版省略 GPIO21；否则物理急停释放时可能出现内部高油门残留导致暴冲。

#### GPIO21 急停反馈隔离接法

GPIO21 只能读 3.3V 逻辑电平，禁止直接连接 36V 电门锁线。

首选方案：给 Schneider XB5 急停增加第二触点模块，作为低压干接点：

```text
ESP32 3V3 -> 10k 上拉 -> GPIO21 -> 急停第二 NC 触点 -> GND
```

默认极性：

| 状态 | GPIO21 | 处理 |
|---|---:|---|
| 急停未按下，第二 NC 闭合 | 0 | ESTOP_RELEASED |
| 急停按下，第二 NC 断开 | 1 | ESTOP_ACTIVE |
| 反馈线断开/插头脱落 | 1 | ESTOP_ACTIVE / FAULT_LOCKOUT |

无法加第二触点时，必须使用光耦隔离检测电门锁链状态；光耦低压侧再接 GPIO21。具体接线见 `ESTOP-FEEDBACK-CIRCUIT.md`。


### 4.4 低压电源

```text
36V 电池 BAT+
  -> F2 低压支路保险 5A
  -> DC-DC VIN+

36V 电池 BAT-
  -> DC-DC VIN-

DC-DC 先空载调到 5.0V：
  VOUT+ -> 5V 低压母线
  VOUT- -> GND 低压母线
```

5V 供：ESP32-S3-DevKitC-1、DS600 接收机、ESP32-S3-CAM、HC-SR04、JY61P、蜂鸣器/状态灯。  
3.3V 供：UWB、TCA9548A、VL53L1X×3、分压/电平转换输出侧。

### 4.5 共地/地线环路规则（P0 安全）

必须共参考地，但不能让动力电流借道信号地：

1. 电池 BAT-、左控制器 BAT-、右控制器 BAT-、DC-DC VIN- 必须在**动力星型地/主负极汇流点**汇合，主回路使用粗线和可靠端子。
2. ESP32 GND、传感器 GND、DS600 GND、DC-DC VOUT- 组成**低压逻辑地**，只从 DC-DC 输出侧回到主负极，不允许串在任一控制器 BAT- 主电流路径中。
3. 左/右控制器转把 GND 只允许作为小电流信号参考地接入低压逻辑地；禁止它成为控制器 BAT- 断线后的备用回流路径。
4. 刹车、倒车、软件使能等控制器开关量优先用光耦/继电器隔离；若用 MOS 开漏拉低，必须确认控制器信号地和 ESP32 低压地之间只有小电流参考连接。
5. 线束验收时必须做“控制器 BAT- 接触不良风险检查”：任何单个控制器 BAT- 主线松脱时，不得存在经转把 GND、ESP32 GND、DC-DC 细线回电池的高电流通路。

不允许：电机主电流通过 ESP32 GND、DC-DC 细线、洞洞板 GND、JST/杜邦/排针回流。若无法确认主负极可靠和信号地不承载主电流，禁止接动力电。

---

## 5. DS600 接收机接线

| DS600 接收机 | ESP32-S3 | 说明 |
|---|---|---|
| CH1 S | GPIO4 | 转向，若 5V PWM 需分压 |
| CH2 S | GPIO5 | 油门，若 5V PWM 需分压 |
| CH3 S | GPIO6 | 限速，若 5V PWM 需分压 |
| CH4 S | GPIO7 | 模式，若 5V PWM 需分压 |
| CH5 S | GPIO8 | STOP/刹车，若 5V PWM 需分压 |
| CH6 S | 首版不接 | GPIO9 已分配给超声共享 TRIG；CH6 属 P1 可选 |
| 任意 + | 5V | 以接收机规格为准 |
| 任意 - | GND | 与 ESP32 共地 |

若 DS600 PWM 为 5V 高电平，每路：

```text
CHx Signal -> 10k -> ESP32 GPIOx -> 20k -> GND
```

验收：遥控器关闭/断联时，ESP32 必须油门归零并刹车。

---

## 6. 两个无刷控制器接线

### 左控制器

| 控制器/模块 | 接线 |
|---|---|
| BAT+ / BAT- | 36V 动力电源左分支 |
| 三根粗相线 | 左轮毂电机三相线 |
| 霍尔 5线 | 左轮毂电机霍尔线 |
| 转把 +5V | 左 PWM→0-5V 模块 VCC |
| 转把 GND | 左 PWM→0-5V 模块 GND，并与 ESP32 GND 共地 |
| 转把信号 | 左 PWM→0-5V 模块 VOUT |
| PWM→0-5V 模块 PWM IN | ESP32 GPIO12；外部 10k 下拉 |
| 低刹线 | GPIO14 只驱动 MOS/光耦输入，再由 MOS/光耦把控制器低刹线拉到 GND；未装 MOS/光耦前禁止连接 |
| 倒车线 | 左控制器倒车线由 GPIO15 驱动 MOS/光耦后拉到 GND；未装 MOS/光耦前禁止连接 |
| 电门锁线 | 接电门锁/急停硬件链输出；GPIO21 只能通过第二触点干接点或光耦隔离读取急停反馈，禁止直接接 36V 电门锁线 |
| 三速线 | 首版不接，默认低速 |
| 巡航线 | 不接 |
| 学习线 | 只在架空自学习时临时短接，学完断开绝缘 |
| 助力/仪表线 | 首版不接 |

### 右控制器

| 控制器/模块 | 接线 |
|---|---|
| BAT+ / BAT- | 36V 动力电源右分支 |
| 三根粗相线 | 右轮毂电机三相线 |
| 霍尔 5线 | 右轮毂电机霍尔线 |
| 转把 +5V | 右 PWM→0-5V 模块 VCC |
| 转把 GND | 右 PWM→0-5V 模块 GND，并与 ESP32 GND 共地 |
| 转把信号 | 右 PWM→0-5V 模块 VOUT |
| PWM→0-5V 模块 PWM IN | ESP32 GPIO13；外部 10k 下拉 |
| 低刹线 | GPIO14 只驱动 MOS/光耦输入，再由 MOS/光耦把控制器低刹线拉到 GND；未装 MOS/光耦前禁止连接 |
| 倒车线 | 右控制器倒车线由 GPIO16 驱动 MOS/光耦后拉到 GND；未装 MOS/光耦前禁止连接 |
| 电门锁线 | 接电门锁/急停硬件链输出；GPIO21 只能通过第二触点干接点或光耦隔离读取急停反馈，禁止直接接 36V 电门锁线 |
| 三速线 | 首版不接，默认低速 |
| 巡航线 | 不接 |
| 学习线 | 只在架空自学习时临时短接，学完断开绝缘 |
| 助力/仪表线 | 首版不接 |

注意：左右控制器的转把 +5V 不互相短接，也不拿来给 ESP32 供电；只共信号地。左右倒车线必须分开控制，不能并联到同一 GPIO。PWM→0-5V 模块 VOUT 需要实测起转死区、最大安全电压和回零延迟；建议通过外置 ADC 分压监测左右 VOUT。

---

## 7. 传感器/视频接线

### UWB GC-P2304-GS-2

布置要求：UWB 天线和模块必须远离 DC-DC Buck、电池主线、控制器相线和大电流端子；与 DC-DC 模块/电感至少保持 50mm，优先放控制盒顶部或外置天线。DC-DC 5V 输出端并联低 ESR 电解电容（建议 470uF 起步，耐压 ≥10V）+ 0.1uF 陶瓷电容，降低 Buck 纹波和射频测距跳变。

| UWB | ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| TX | GPIO18 |
| RX | GPIO17 |
| 天线 | 外置 SMA/IPEX，不放铝盒内 |

### 实物拆机激光雷达

EaiLidarTest 配置曾显示目标可按 `S2-YJ`、UART 150000 8N1、`intensity=8`、启动命令 `A5 60` 处理；官方 YDLidar SDK 的常见三角协议扫描帧头是 `AA 55`。但 2026-06-22/24 现场日志显示车端在规范线序 `DATA/TX -> GPIO3, CTL/RX <- GPIO43` 下，`115200` 时出现可解释的重复 `55 AA 03 08 ...` 帧，角度字段与距离优先样本更符合实物输出；150000 下多为角度不可能的 raw 字节。因此固件当前默认 115200，并保留 150000 等自动探测。固件保留 `AA55` 主解析：`PH(2) + CT(1) + LSN(1) + FSA(2) + LSA(2) + CS(2) + LSN × 3`，每点 `quality(1) + distance_lsb + distance_msb`；同时接受现场实测 `55 AA 03 LSN + FSA + LSA + LSN × (distance_lsb + distance_msb + quality)` 的无校验帧，但必须通过 CT/count/角度/距离合理性门槛。不得再按 `10 + LSN × 2` 截包，也不要直接套用 LD06/LD19 的 `54 2C / 230400` 协议。

| 雷达 | ESP32-S3 |
|---|---|
| 5V | 5V |
| GND | GND |
| DATA/TX | GPIO3 |
| CTL/RX | GPIO43 |

GPIO3/GPIO43 电平均按 3.3V 处理；若实测 DATA/CTL 高于 3.3V，必须加电平转换后再接 ESP32。`SensorTask` 在 `rx=0` 或 `rx=1(+0)` 停滞时会自动轮询规范线序、反接线序和候选波特率；以日志里的 `wiring=... rx_pin=... tx_pin=... baud=...` 判断哪组真正有持续数据。

### JY61P IMU

JY60/JY61P 上电初始 2-3 秒会估计静态零偏；控制盒外壳必须贴“上电后静止 3 秒”标识。固件 BOOT_SELF_TEST 阶段应等待 IMU 静止窗口完成，若检测到明显 yaw_rate/加速度扰动，则提示重新静置/重启，禁止直接进入 AUTO_FOLLOW。

| JY61P | ESP32-S3 |
|---|---|
| VCC | 5V |
| GND | GND |
| TX | GPIO42 |
| RX | 首版不接 |

首版只读 JY61P TX -> ESP32 GPIO42；若 JY61P TX 是 5V 电平，GPIO42 前加分压或电平转换。JY61P RX 配置线属于 P1 可选，不能占 GPIO40/41。

### TOF：TCA9548A + VL53L1X ×3

I2C 物理要求：SDA、SCL 必须外接 4.7kΩ 上拉到 3.3V（靠近 TCA9548A/主控均可，线长较长时优先靠近总线主干），不能依赖 ESP32 内部弱上拉。首版省去 XSHUT 时，固件必须实现 I2C Bus Clear/软复位：SDA 被拉低时临时释放 I2C、手动脉冲 SCL 约 9 次、产生 STOP、重初始化 TCA9548A 和各通道 TOF；否则单个传感器死锁可能拖死整条总线。

| TCA9548A | ESP32-S3 |
|---|---|
| VCC/VIN | 3V3 |
| GND | GND |
| SDA | GPIO10 |
| SCL | GPIO11 |

| TCA9548A 通道 | TOF 安装位 |
|---|---|
| CH0 | 前中 VL53L1X |
| CH1 | 左前 VL53L1X |
| CH2 | 右前 VL53L1X |

### 超声波 HC-SR04 ×2

| 模块 | VCC | GND | TRIG | ECHO |
|---|---|---|---|---|
| 左 HC-SR04 | 5V | GND | GPIO9 | 分压后 GPIO40 |
| 右 HC-SR04 | 5V | GND | GPIO9 | 分压后 GPIO41 |

Echo 分压：`ECHO -> 10k -> ESP32 GPIO -> 20k -> GND`。

### 蜂鸣器/状态音（P1，但先冻结选型）

若安装蜂鸣器，默认选 5V **无源蜂鸣器**，由 ESP32 通过三极管/MOS 管低边驱动，GPIO 使用 LEDC/PWM 输出不同频率提示音；不要把“有源蜂鸣器”和“无源蜂鸣器”混写进固件任务。建议语义：低速/倒车为低频间歇音，急停/故障为高频急促音。未分配专用 GPIO 前，`BUZZER_GPIO = -1`，不能占用 P0 安全引脚。

### ESP32-S3-CAM

| ESP32-S3-CAM | 接线 |
|---|---|
| 5V | DC-DC 5V |
| GND | GND |
| 视频/状态 | 首版走 WiFi/H5 页面，不占主控 UART |

视频断流不能影响安全控制。GPIO16 已用于右倒车，GPIO42 已用于 IMU RX，GPIO43 已用于雷达 CTL，首版不能再给摄像头 UART。

---

## 8. 电池采样（可选）

首版虽是 36V 电池（10 串满电约 42V），但项目预留 48V/13 串底盘（满电约 54.6V）。电池采样分压必须统一按 36V-60V 通用设计，避免换 48V 底盘时烧毁 ESP32-S3 ADC。

```text
BAT+ -> 220k -> GPIO1 -> 10k -> GND
GPIO1 -> 0.1uF -> GND
```

换算：42V -> 约 1.83V；54.6V -> 约 2.37V；60V -> 约 2.61V。旧 130k/10k 只能覆盖 36V/42V，54.6V 会到约 3.90V，禁止继续作为当前通用方案。ADC 代码必须用 Profile 中的 `battery_adc.divider_top_ohm=220000`、`divider_bottom_ohm=10000` 反算电池电压。

---

## 9. 固件实现要求

AI/代码代理必须按这些接口抽象：

```cpp
// RC input: CH1-CH5 only in P0
CH1_STEER_GPIO = 4
CH2_THROTTLE_GPIO = 5
CH3_SPEED_LIMIT_GPIO = 6
CH4_MODE_GPIO = 7
CH5_STOP_GPIO = 8
CH6_AUX_GPIO = -1  // P1 optional; GPIO9 is ultrasonic shared TRIG

// Sensors
I2C_SDA_GPIO = 10
I2C_SCL_GPIO = 11
US_SHARED_TRIG_GPIO = 9
US_LEFT_ECHO_GPIO = 40
US_RIGHT_ECHO_GPIO = 41
UWB_TX_GPIO = 17
UWB_RX_GPIO = 18
LIDAR_RX_GPIO = 3
LIDAR_TX_GPIO = 43
IMU_RX_GPIO = 42
IMU_TX_GPIO = -1  // P1 optional

// Safety / ADC
ESTOP_STATUS_GPIO = 21
BATTERY_ADC_GPIO = 1
CONTROLLER_FAULT_GPIO = 2

// Motor control
LEFT_THROTTLE_PWM_GPIO = 12
RIGHT_THROTTLE_PWM_GPIO = 13
BRAKE_GPIO = 14
LEFT_REVERSE_GPIO = 15
RIGHT_REVERSE_GPIO = 16
SOFT_ENABLE_GPIO = 39
BUZZER_GPIO = -1  // P1 optional or external I/O expander
```

状态机底线：

- 所有运动输出在进入 `drive_adapter_analog_bldc` 前必须经过 `safety_manager.applyFinalGate()` 最终门控；禁止只做前置安全判断。
- boot 默认 STOP。
- DS600 无有效脉宽 -> 油门 0 + 刹车。
- UWB stale -> 自动跟随模式停车。
- H5/WebSocket 超时 -> 停车。
- 油门上电不为 0 -> 禁止使能。
- 急停状态 GPIO21 是 P0 必接；急停触发 -> 锁定 FAULT，需人工复位。

---

## 10. 首次上电/调试顺序

1. 不接 ESP32，单独给 DC-DC 上 36V，空载调 VOUT=5.0V。
2. 只接 ESP32 + DS600，串口确认 CH1-CH5 脉宽、断联 failsafe；CH6 首版不接。
3. 只接 TCA9548A + 三个 TOF，确认 CH0/CH1/CH2 都能读数。
4. 只接 UWB，确认 distance/bearing 数据。
5. 只接 EAI S2 雷达，确认 H5 雷达 RX/包/圈增长。
6. 只接 IMU，确认 yaw/yaw_rate 方向。
7. 车轮架空，只接一个控制器和一个电机，电门锁线经急停上电。
8. 临时短接该控制器学习线，完成自学习；方向正确后断开学习线并绝缘。
9. 用手动转把或 PWM→0-5V 模块测试最低油门。
10. 再接另一个控制器和电机，重复自学习。
11. 最后启用 ESP32 左右油门输出、刹车、倒车。
12. 地面低速测试前，验证急停能通过电门锁线硬件停两个电机。

---

## 11. 还缺资料/不可最终定死的点

1. DS600 接收机正反面照片：确认 6 路 PWM、S/+/- 顺序、供电范围、信号电平。
2. 控制器实物线标近照：确认电门锁、转把、刹车、倒车、三速、巡航、学习线的颜色和插头。
3. 轮毂电机线束照片：确认三相线和霍尔插头线序。
4. 电池铭牌：确认容量、BMS 持续/峰值放电能力。
5. 急停背面端子编号：确认 NC 端子，是否能加第二触点给 ESP32 状态输入。
6. PWM→0-5V 模块到货后：实测输出范围、响应、默认下拉状态。

---

## 12. 文档使用规则

- AI 实施、固件、接线、采购复核，优先读本文件和 `PIN-MAP-V1.md`。
- GPIO21 急停反馈按 `ESTOP-FEEDBACK-CIRCUIT.md`，极性按 `POLARITY-DEFINITIONS.md`，PWM 油门校准按 `PWM-OUTPUT-CALIBRATION.md`。
