# FollowBox H5 模块代码审查报告

> 审查日期：2026-06-15  
> 审查方法：Agent 事实查证 → Fusion 纯逻辑推理（3 模型）  
> 审查范围：`firmware/src/web/h5_*.{cpp,h}`（6 文件，~1040 行）

## 总体评分

**安全评分：2.5/10**（三模型共识：GLM 2/10, DeepSeek 2/10, Qwen 3/10）

> 当前 H5 模块存在架构级安全缺陷，不可上线用于有人值守以外的任何场景。

## 发现清单

### 🔴 P0 — 致命

| # | 问题 | 位置 | 严重性 |
|---|------|------|--------|
| 1 | **传输层越权直调 drive/app** | `h5_web_server.cpp:360-367` | 致命 |
| 2 | **一键绕过安装向导** | `h5_web_server.cpp:365-368` + `POST /api/wizard-complete` | 致命 |
| 3 | **故障复位逻辑自毁** | `h5_command_handler.cpp:101` + `safety_manager.cpp:178` | 致命 |

#### 问题 1 详情：传输层越权

```cpp
// h5_web_server.cpp:360-367 — 传输层直接操作驱动层
if (g_handler.hasPendingCalibrate()) {
    drive.setCalibration(...);        // ← 跨层调用，绕过所有安全校验
    app.setThrottleCalibrated(true);  // ← 跨层调用
}
if (g_handler.hasPendingWizard()) {
    app.setInstallWizardComplete(...); // ← 跨层调用
}
```

**攻击路径**：operator token 泄露 → POST /api/calibrate → 恶意 calibration 参数写入 NVS → pollInput() 无校验直接调 drive.setCalibration() → 下次 deadman 按下时以篡改参数暴走。

**跨核风险**（Qwen 发现）：pollInput() 在 Core 0 (AsyncTCP) 执行，主循环在 Core 1。drive.setCalibration() 无跨核同步保护，主循环可能读到半写的校准结构体（torn read），导致 PWM 输出瞬时突变。

#### 问题 2 详情：绕过安装向导

POST /api/wizard-complete 只需要 operator token，不需要 deadman 保持。攻击者可远程一键完成向导，设备在未完成安全自检的情况下进入可运行态。

#### 问题 3 详情：故障复位逻辑自毁

`onResetFault()` 调用 `stopMotion()` 清除了 `unlock_request`，使 `hasMotionRequest()` 返回 false。安全检查从"验证操作员停止了运动"退化为"验证复位动作清除了运动标志"。这在逻辑上自相矛盾——如果复位本身就能清除运动标志，那安全检查就形同虚设。

### 🟡 P1 — 严重

| # | 问题 | 位置 |
|---|------|------|
| 4 | **reset_fault_request 竞态：请求被消费但复位被拒绝** | `app.cpp:54` + `safety_manager.cpp:48` |
| 5 | **校准参数无输入范围校验** | `drive_adapter_analog_bldc.cpp`（待确认） |

#### 问题 4：竞态窗口

`app.tick():54` 无条件清零 `state_.h5.reset_fault_request`。若 `evaluate()` 在同一周期因 `hasMotionRequest=true` 拒绝复位，请求永久丢失，用户必须重新触发。

### 🟢 P2 — 建议

| # | 问题 |
|---|------|
| 6 | `onBody` 模板使用 `static` 缓冲区（257 字节），单核下安全但设计脆弱 |
| 7 | 缺少 calibrate/wizard 端点的速率限制 |

## 已确认正常的项

- ✅ H5 不直接操作 PWM/GPIO（grep 验证：`web/` 目录 0 匹配）
- ✅ NVS 写操作在 spinlock 外部执行（line 255）
- ✅ `onResetFault` 的 `stopMotion()` 已正确清除 unlock/throttle/steering/auto_request
- ✅ `clearOneShotRequests` 和 `app.tick()` 的双清在不同对象上，无冲突
- ✅ 存在 constant-time token 比较 + operator token 认证
- ✅ 存在 safety_manager 门禁机制

## 修复方案

### P0-1+2：将 drive/app 调用移出传输层

在 `H5ControlInput` 中增加 pending 标志，由 `App::tick()` 在安全上下文中执行：

```cpp
struct H5ControlInput {
    // ... existing fields ...
    bool pending_calibrate = false;
    ThrottleCalibration pending_cal_data{};
    bool pending_wizard_complete = false;
};
```

`pollInput()` 只设置标志，不调 drive/app。`App::tick()` 在处理 safety 之后执行 calibrate/wizard。

### P0-3：修复故障复位链路

将 `reset_fault_request` 的清零移到 `evaluate()` 确认复位成功后：

```cpp
// app.cpp — 只在复位成功后才清零
if (state_.h5.reset_fault_request) {
    safety_manager_.requestFaultReset();
}
state_.safety = safety_manager_.evaluate(state_);
if (state_.h5.reset_fault_request && !state_.safety.fault_latched) {
    state_.h5.reset_fault_request = false;  // 只在实际清除了故障时才清零
}
```

### P1-4：Calibrate/Wizard 端点加 deadman 要求

POST /api/calibrate 和 /api/wizard-complete 需要 deadman 保持（或双 confirm 机制）。

## 审查流程记录

| 阶段 | 方法 | 耗时 | 发现数 |
|------|------|------|--------|
| 1. 全量阅读 | Agent 读 6 文件 + 协议文档 | ~2 min | 上下文建立 |
| 2. grep 验证 | 4 次交叉搜索 | ~1 min | 确认越权 + 复位链路 |
| 3. Fusion 推理 | 3 模型 panel | ~210s | 3 共识 + 5 独到见解 |
| 4. 整合报告 | Agent | ~5 min | 7 发现 + 修复方案 |

## 参考

- `followbox-code-review` skill — FollowBox 代码审查标准工作流
- `hermes-fusion` skill — 融合推理工具使用指南
- 上次审查：`CODE-REVIEW-2026-06-14.md`
