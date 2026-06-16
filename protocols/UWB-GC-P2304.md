# GC-P2304-GS-2 UWB 串口协议冻结文件

## 当前已知

- 模块：GC-P2304-GS-2。
- 接口：UART，不是 SPI。
- 供电：当前样机按 3.3V。
- ESP32 引脚：TX GPIO17 -> UWB RX；RX GPIO18 <- UWB TX。
- 默认波特率：115200（规格书写明出厂 115200、8N1）。
- 规格书证据已在旧项目资料中找到：`/mnt/d/autofloowcar/zhilliao/_tmp_gc_p2304_base_spec_utf8.txt` 第 261-301 行，来源为 `GC-P2304-GS-2基站规格书.pdf` 的文本提取。

## 已冻结的二进制测距帧

规格书“5.4 测距数据解析”给出的帧格式：

```text
F0 06 ID_L ID_H DIST_L DIST_H ANG_L ANG_H RSSI AA
```

| 字段 | 长度 | 说明 |
|---|---:|---|
| `0xF0` | 1 byte | 帧头 |
| `0x06` | 1 byte | 有效数据长度/类型 |
| `ID_L ID_H` | 2 bytes | 测距模块 ID，小端 |
| `DIST_L DIST_H` | 2 bytes | 距离，小端，单位 cm |
| `ANG_L ANG_H` | 2 bytes | 角度，小端，单位 degree；负角度二进制编码需实测确认 |
| `RSSI` | 1 byte | 信号强度，`dBm = RSSI - 256` |
| `0xAA` | 1 byte | 帧尾 |

规格书样例：

```text
F0 06 03 00 73 00 14 00 BC AA
```

含义：ID `0x0003`，距离 `0x0073 = 115cm`，角度 `20°`，RSSI `0xBC - 256 = -68dBm`。

## 实现注意

正式实现 `uwb_gc_p2304.cpp` 时允许按上述二进制帧写 parser，但仍必须保留以下保护：

1. 串口 raw log / parse error / frame count / last update age 统计；
2. 超时后 `UwbTarget.valid=false`，不能沿用旧距离/旧角度；
3. 负角度编码、正负方向和安装朝向必须通过左右移动抓包/架空测试确认；
4. `AUTO_FOLLOW` 不能仅因 parser 存在就自动启用，仍需安装向导、人工确认、safety gate；
5. parser 不得阻塞 control loop，不得直接控制电机。

## 目标解析字段

最终驱动必须输出：

```cpp
struct UwbTarget {
  bool valid;
  uint32_t last_update_ms;
  int distance_mm;
  float bearing_deg;
  uint8_t confidence;
};
```

## 临时实现规则

在协议未冻结前，只允许写：

- UART 初始化；
- 原始串口日志记录（raw log）；
- 超时检测；
- mock parser 单元测试；

禁止写“看起来像”的帧解析逻辑；不得实现猜测性 parser，更禁止让 AUTO_FOLLOW 依赖未验证字段。

## 布局与 EMI 要求

UWB 模块/天线应远离 DC-DC Buck、电池主线、控制器相线和大电流端子；与 DC-DC 模块/电感至少保持 50mm。DC-DC 5V 输出端建议并联低 ESR 电解电容（470uF 起步，耐压 ≥10V）和 0.1uF 陶瓷电容。若测距出现跳变/丢包，优先检查 Buck 纹波、天线附近金属/电源线、模块供电稳定性。
