# JY61P IMU 串口协议

## 接线

| JY61P | ESP32-S3 |
|---|---|
| VCC | 5V |
| GND | GND |
| TX | GPIO42 |
| RX | 首版不接 |

若 JY61P TX 是 5V 电平，GPIO42 前必须分压/电平转换。

## 默认串口参数

常见默认值：9600 或 115200 8N1。首版必须用串口工具确认实际波特率后写入 Profile。

## 帧格式

JY61P 常见 WitMotion/JY 系列格式为：

```text
0x55 0x51 ... 加速度帧
0x55 0x52 ... 角速度帧
0x55 0x53 ... 角度帧
```

但不同固件可能不同。正式驱动必须以到货模块资料或串口样例确认。

## 首版需要字段

```cpp
struct ImuSnapshot {
  bool valid;
  uint32_t last_update_ms;
  float yaw_deg;
  float yaw_rate_dps;
  float pitch_deg;
  float roll_deg;
};
```

## 实现规则

- 首版只读 TX，不配置模块。
- 串口解析失败丢帧，不能阻塞控制任务。
- IMU 超时只降低 yaw 阻尼能力，不能让系统默认高速通行。
- yaw 方向需要安装向导实测，写入 `imu.yaw_sign`。

## 上电静止要求

JY60/JY61P 类 IMU 通常在上电最初 2-3 秒估计静态零偏。控制盒外观必须贴“上电后静止 3 秒”标识；固件 BOOT_SELF_TEST 应等待静止窗口，若 yaw_rate/加速度扰动明显，应保持安全锁定并提示重新静置/重启。
