# Follow the Box 极性定义 v1.0

> 本文件定义 Profile 中 `active_high/active_low` 的语义，防止代码把 MOS/光耦输入极性和控制器线有效极性混为一谈。

## 两层极性必须分开

1. **MCU 输出极性**：ESP32 GPIO 对 MOS/光耦输入的含义。
2. **控制器线有效极性**：无刷控制器刹车/倒车/使能线被拉到什么电平时有效。

当前首版 `example_bldc_analog_36v.yaml` 中：

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
  controller_fault_active_level: low_or_unknown_until_measured
```

含义：

| 字段 | 含义 |
|---|---|
| `mcu_*_active_level: high` | ESP32 GPIO 输出高电平时，MOS/光耦输入导通 |
| `controller_brake_line_active: pull_to_gnd` | 控制器低刹线被 MOS/光耦拉到 GND 时刹车有效 |
| `controller_reverse_line_active: pull_to_gnd` | 控制器倒车线被 MOS/光耦拉到 GND 时倒车有效 |
| `controller_enable_line_active` | 首版不能直接用 ESP32 拉 36V；必须是隔离开关/继电器/MOS 方案 |
| `estop_active_level: high` | GPIO21 高电平表示急停按下或反馈断线 |

## 输出默认态

所有驱动输出 GPIO 必须外部 10k 下拉到 GND：

| GPIO | 默认电平 | 默认结果 |
|---:|---:|---|
| 12/13 油门 PWM | 0 | 油门 0 |
| 14 刹车 MOS 输入 | 0 | 刹车输出不主动动作；故障时软件主动置高触发刹车 |
| 15/16 倒车 MOS 输入 | 0 | 不倒车 |
| 39 软件使能 MOS/继电器输入 | 0 | 不使能 |

## 代码要求

`drive_adapter_analog_bldc` 不允许直接假设控制器线电平。它只能根据 Profile 的 `polarity` 字段调用 HAL：

```cpp
writeMcuActive(PIN_BRAKE_OUT, command.brake, polarity.mcu_brake_out_active_level);
writeMcuActive(PIN_LEFT_REVERSE_OUT, command.left_reverse, polarity.mcu_left_reverse_out_active_level);
writeMcuActive(PIN_RIGHT_REVERSE_OUT, command.right_reverse, polarity.mcu_right_reverse_out_active_level);
writeMcuActive(PIN_DRIVE_ENABLE_OUT, command.enable, polarity.mcu_drive_enable_out_active_level);
```
