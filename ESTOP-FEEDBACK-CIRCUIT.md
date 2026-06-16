# GPIO21 急停反馈隔离接线

> 目标：让 ESP32 知道急停是否按下，但绝不能把 36V 电门锁链直接接到 GPIO21。

## 硬规则

1. GPIO21 只允许读取 3.3V 逻辑电平。
2. 禁止：`36V 电门锁线 -> 电阻 -> GPIO21` 这种直接分压方案，除非经过专门隔离设计并验证故障安全；首版优先不用。
3. 推荐优先级：**第二触点干接点方案 > 光耦隔离检测方案**。
4. 急停反馈断线必须按“急停/故障”处理，不能默认安全。

## 方案 A：第二触点干接点（首选）

如果 Schneider XB5AS542C 已用 1NC 触点切电门锁，则给急停加第二触点模块，专门给 ESP32 低压侧读取。

推荐接法（低压 3.3V 侧）：

```text
ESP32 3V3 -> 10k 上拉 -> GPIO21 -> 急停第二 NC 触点 -> GND
```

逻辑定义：

| 状态 | GPIO21 | 含义 |
|---|---:|---|
| 急停未按下，第二 NC 闭合 | 0 | ESTOP_RELEASED |
| 急停按下，第二 NC 断开 | 1 | ESTOP_ACTIVE |
| 线断/插头脱落 | 1 | ESTOP_ACTIVE / FAULT_LOCKOUT |

说明：这里 GPIO21 的 `1` 表示危险/急停，便于断线故障安全。也可反向设计，但必须在 `POLARITY-DEFINITIONS.md` 和 Profile 里同步。

## 方案 B：光耦隔离检测电门锁链

当无法加第二触点时，用光耦检测急停后电门锁输出是否存在。36V 侧只驱动光耦 LED，ESP32 只读取光耦晶体管低压侧。

```text
36V 电门锁链输出 -> 限流电阻/恒流输入 -> 光耦 LED -> 电门锁 GND/回路
光耦晶体管侧：GPIO21 + 10k 上拉到 3V3，光耦导通时拉低 GPIO21
```

必须由实测确定：急停未按下时光耦导通还是断开，并写入 Profile：

```yaml
safety:
  estop_feedback_type: second_nc_contact | optocoupler_elock_detect
  estop_active_level: high
  estop_fault_on_open_wire: true
```

## 禁止给 AI 的误解

- 不能写“电门锁线同时 GPIO21 读取”而不写隔离。
- 不能把 GPIO21 直接接控制器电门锁 36V 线。
- 不能省略 GPIO21；否则急停释放后可能因内部命令残留导致暴冲。
