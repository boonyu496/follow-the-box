# FollowBox 固件代码审查报告

> **日期**: 2026-06-14  
> **方法**: Agent 逐文件阅读 + grep 数据流验证 + 硬件文档交叉引用 → 确认事实 → fusion 纯逻辑推理  
> **审查范围**: `firmware/src/` + `include/` + 全部硬件文档（PIN-MAP / 接线指南 / 极性定义 / 急停反馈）  
> **事实准确率**: 所有发现均经过至少两个独立来源确认

---

## 🔴 致命（1 项）

### 1. canClearLatchedFault 的逻辑死锁 — 通过 H5 永远无法清除故障

**文件**: `src/safety/safety_manager.cpp:176-179` + `:32-36`

```
// 清除故障的条件：
canClearLatchedFault() = !hasActiveLatchedFault() && !hasMotionRequest()

// hasMotionRequest 的定义：
hasMotionRequest() 包含 state.h5.unlock_request  ← 任何 H5 解锁即视为"有运动请求"
```

**死锁**: 低电压 → FAULT_LOCKOUT → 用户 H5 发解锁 → unlock_request=true → canClear=false → 永久锁死

**修复**: reset_fault_request 到达时应绕过运动请求检查

---

## 🟠 严重（4 项）

### 2. H5 Web 层直接修改 App/Drive 状态
**文件**: `src/web/h5_web_server.cpp:360-367`  
**问题**: pollInput 内直接调用 drive.setCalibration() / app.setInstallWizardComplete()，Web 层绕过安全锁  
**修复**: 校准/wizard 数据从 pollInput 返回，在 App::tick() 中由 safety_manager 验证后应用

### 3. 低电压检测无迟滞
**文件**: `src/sensors/power_monitor.cpp:26-27` + `src/safety/safety_manager.cpp:168-170`  
**问题**: 每 50Hz 判断一次，33V 以下直接 FAULT_LOCKOUT，加速时电压跌落误触发  
**修复**: 分两级：一级限速（≤33V 持续 2s）、二级锁存（≤30V 持续 5s）

### 4. OTA 安全标记 g_ota_in_progress 无原子保护
**文件**: `main.cpp:42, 57, 133`  
**问题**: volatile bool 在 ESP32 双核上不保证原子性  
**修复**: std::atomic<bool> 或 FreeRTOS 临界区

### 5. safe_idle_request 每周期清零 → 模式震荡
**文件**: `src/app/app.cpp:54-56`  
**问题**: 传感器心跳有 3s 宽限期，safe_idle 零宽容——逻辑不对称  
**修复**: H5CommandHandler/CloudClient 在下一次运动命令时清除

---

## 🟡 中等（3 项）

### 6. obstacle_manager 只限制 forward，不限制 turn
**文件**: `src/control/obstacle_manager.cpp:49-51`  
**二次风险**: 旋转 → 障碍物出 FOV → 恢复 forward → "旋转-前冲"震荡

### 7. GPIO2（控制器故障输入）浮空
**文件**: `src/sensors/power_monitor.cpp:11-12`  
**问题**: FLOATING pull，未接线但 isValid()=true，随机触发 motor_fault  
**修复**: PIN_CONTROLLER_FAULT 设为 -1 禁用

### 8. 启动心跳竞争
**文件**: `src/safety/safety_manager.cpp:188-202`  
**问题**: grace period 从 boot 开始算，传感器阻塞 >3s 则误触发 WATCHDOG_TIMEOUT

---

## 🔵 建议（4 项）

9. **motion_mixer 归一化死代码** — `src/control/motion_mixer.cpp:27-33` — clampUnit 在 peak>1.0 之前
10. **applyFinalGate 重复推导 reverse** — `src/safety/safety_manager.cpp:130-131` — 与 mixer 逻辑重复
11. **canAutoFollow 不检查 UWB staleness** — `src/app/mode_manager.cpp:10-13` — 模式抖动
12. **双 evaluate() 缺注释** — `src/app/app.cpp:45-47` — 逻辑正确但意图不清

---

## ✅ 已验证为正确的设计

| 断言 | 验证方法 |
|------|----------|
| 所有 PWM/GPIO 输出只在 drive_adapter_analog_bldc | grep 全 src/ |
| ESTOP GPIO21 fail-safe 设计 | 代码 + 电路文档 |
| 运动链路不可绕过 | 完整数据流追踪 |
| 双核隔离正确 | Core0=sensor+comm, Core1=control, spinlock |
| SharedState 临界区正确 | portENTER_CRITICAL 仅保护拷贝 |
| 云端命令经过安全链 | CloudClient → App → 完整链路 |

---

## 硬件事实速查

| GPIO | 功能 | 物理接线 | 代码状态 |
|------|------|----------|----------|
| 1 | 电池 ADC | 220k/10k 分压 ✓ | 正常 |
| 2 | 控制器故障 | **未接**（文档标注"未确认前不接"） | ⚠ FLOATING |
| 4-8 | DS600 CH1-5 | 10k/20k 分压 ✓ | 正常 |
| 12-13 | 油门 PWM | 10k 下拉 ✓ | 正常 |
| 14 | 刹车 | MOS/光耦 → 控制器低刹线 ✓ | 正常 |
| 15-16 | 左右倒车 | MOS/光耦 → 各自控制器 ✓ | 正常 |
| 21 | 急停反馈 | 3V3 上拉 + NC 触点 → GND ✓ | fail-safe |
| 39 | 软件使能 | **SW-08 标注"未确认前禁止接"** | 代码驱动但不连 |
| 42 | IMU RX | JY61P TX ✓ | UART_NUM_IMU=-1 |
