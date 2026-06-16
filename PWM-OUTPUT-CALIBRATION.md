# PWM→0-5V 油门输出校准规范

> 目标：防止油门不平滑、超过控制器安全电压、回零慢导致继续滑行。

## 默认 PWM 参数

| 参数 | 默认值 | 说明 |
|---|---:|---|
| PWM 频率 | 1000 Hz | 常见 PWM→0-5V 模块兼容；到货后可实测调整 |
| PWM 分辨率 | 12 bit | duty 0..4095 |
| 输出范围 | 0..5V 模块输出 | 实际给控制器前由软件限幅 |
| 安全最大油门 | 3600 mV | 首版保守值，实测后写入 NVS |
| 起转死区 | 800..1000 mV | 实测每个控制器 |
| RC 回零延迟 | 50..150 ms | 避障距离和刹车逻辑必须考虑 |

## 映射公式

设：

```text
cmd_abs = clamp(abs(target), 0.0, 1.0)
V_min = throttle_min_active_mv
V_max = throttle_max_mv
V_dead = throttle_deadband_mv
```

当 `cmd_abs == 0` 或 `enable=false` 或 `brake=true`：

```text
V_target_mv = 0
```

当 `cmd_abs > 0`：

```text
V_target_mv = V_min + cmd_abs * (V_max - V_min)
V_target_mv = clamp(V_target_mv, 0, V_max)
```

PWM duty：

```text
duty = round(V_target_mv / measured_module_full_scale_mv * (2^pwm_resolution_bits - 1))
```

`measured_module_full_scale_mv` 必须实测，不能假设一定等于 5000mV。

## 斜率限制

Profile 字段：

```yaml
throttle:
  pwm_frequency_hz: 1000
  pwm_resolution_bits: 12
  measured_module_full_scale_mv: 5000
  throttle_deadband_mv: 800
  throttle_min_active_mv: 1000
  throttle_max_mv: 3600
  throttle_slew_rise_mv_per_s: 800
  throttle_slew_fall_mv_per_s: 1600
  throttle_rc_delay_ms: 150
```

上升和下降都必须限速，但急停/故障时例外：软件立即 duty=0，同时硬件急停切电门锁。

## 校准流程

1. 不接控制器转把线，只接 PWM→0-5V 模块和万用表。
2. 输出 0%、10%、20%、50%、80% duty，记录 VOUT。
3. 确认 GPIO 复位/下载/看门狗时 VOUT 回 0。
4. 接控制器但车轮架空，逐步寻找起转电压。
5. 设置 `throttle_min_active_mv` 为起转电压略上方。
6. 设置 `throttle_max_mv` 为首版安全上限，不追求满速。
7. 测从非零输出到 VOUT < 100mV 的回零时间，写入 `throttle_rc_delay_ms`。
8. 保存到 NVS/Profile，未校准前禁止 AUTO_FOLLOW。

## NVS 字段

```text
throttle.left.full_scale_mv
throttle.right.full_scale_mv
throttle.left.deadband_mv
throttle.right.deadband_mv
throttle.left.min_active_mv
throttle.right.min_active_mv
throttle.left.max_mv
throttle.right.max_mv
throttle.left.rc_delay_ms
throttle.right.rc_delay_ms
throttle.calibrated
```
