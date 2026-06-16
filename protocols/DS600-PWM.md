# DS600 PWM 输入协议

## P0 接线

| 通道 | GPIO | 用途 |
|---|---:|---|
| CH1 | 4 | 转向 |
| CH2 | 5 | 油门 |
| CH3 | 6 | 限速 |
| CH4 | 7 | 模式 |
| CH5 | 8 | STOP/刹车 |
| CH6 | -1 | 首版不接 |

## 电平

DS600 PWM 高电平若为 5V，必须先经过分压/电平转换。禁止 5V 直接进 ESP32。

推荐首版分压：

```text
CHx Signal -> 10k -> ESP32 GPIOx -> 20k -> GND
```

## 脉宽判定

初始默认，实物校准后可调整：

| 项目 | 默认值 |
|---|---:|
| 有效最小脉宽 | 900 us |
| 有效最大脉宽 | 2100 us |
| 中位 | 1500 us |
| 回中死区 | ±50 us |
| 丢帧超时 | 100 ms |
| 遥控丢失停车 | 500 ms |

## 通道映射

```text
CH1: steering = normalize(ch1, 1000, 1500, 2000)
CH2: throttle = normalize(ch2, 1000, 1500, 2000)
CH3: speed_limit = clamp(map(ch3, 1000..2000 -> 0.1..1.0))
CH4: mode switch: SAFE_IDLE / MANUAL_RC / AUTO_REQUEST
CH5: stop_switch: active when above/below calibrated threshold
```

## 失败处理

- 任一 P0 通道脉宽越界：该帧无效。
- CH2 上电不在回中死区：禁止使能。
- 接收机断联或脉宽冻结超过阈值：`RC_LOST`。
- `MANUAL_RC` 中 RC_LOST 必须停车并锁定/需人工确认恢复。
