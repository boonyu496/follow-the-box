# FollowBox AI 多智能体团队协作规则

> 本文档定义 FollowBox 项目的多 AI Agent 协作架构。所有参与开发的 AI 必须先读此文件。

## 架构图

```
Coordinator (hermes default, deepseek-v4-flash)
  │
  ├── architect (deepseek-reasoner)
  │    职责：架构设计、任务分解、模块边界、引脚规划、风险评估
  │    项目技能：01-firmware-architecture-guardian + 02-firmware-implementation-planner
  │
  ├── safety-reviewer (deepseek-chat)
  │    职责：安全门控、真值表验证、运动链路审查、故障/恢复逻辑
  │    项目技能：03-safety-control-reviewer
  │
  ├── sensor-fusion (deepseek-chat)
  │    职责：UWB/TOF/IMU/超声/DS600 驱动、数据融合、SensorSnapshot
  │    项目技能：04-sensor-protocol-integrator
  │
  ├── motion (deepseek-chat)
  │    职责：PWM 输出、motion_mixer、PID 控制、电池 ADC、驱动适配器
  │    项目技能：05-drive-power-calibration-engineer
  │
  ├── vision-h5 (deepseek-chat)
  │    职责：H5 Web 控制面板、实时遥测、WiFi 配置、安装向导
  │    项目技能：06-h5-telemetry-ui-engineer
  │
  ├── tester (deepseek-chat)
  │    职责：代码审查、bug 排查、构建验证、单元测试、handoff 日志
  │    项目技能：07-code-review-debugger
  │
  └── safety-officer (deepseek-chat)
       职责：上电许可、测试分级审批、安全操作规范
       项目技能：08-bringup-test-safety-officer
```

## 标准工作流

```
用户需求
  → architect (架构审查 + 任务分解)
  → 各角色并行/串行开发
  → safety-reviewer (安全审查)
  → tester (代码审查 + 构建验证)
  → safety-officer (测试许可审批)
  → 真机验证
```

## 协作原则

### 依赖顺序
1. architect 先行（定义接口、分解任务）
2. sensor-fusion + motion + vision-h5 可并行
3. 驱动类开发完成后 → safety-reviewer 审查
4. 所有代码合并后 → tester 统一审查 + 构建
5. 需要物理测试时 → safety-officer 审批

### 任务粒度
- 每个子任务 ≤ 3 个文件修改
- 涉及电机输出/安全链路 → 单独任务，标注 `safety-critical`
- 跨模块接口变更 → architect 先审批

### 上下文传递
每个子代理只能看到传入的 context —— 传足够的文件路径和约束。

### 硬约束（所有角色遵守）
1. 项目路径：`D:\car\Follow the box` → WSL `/mnt/d/car/Follow the box`
2. 权威文档：FIRMWARE-SPEC.md > CURRENT-WIRING-AI.md > PIN-MAP-V1.md
3. main.cpp 只做入口，禁止业务逻辑
4. GPIO 常量只在 board_pins.h
5. 所有运动经过 safety_manager → applyFinalGate() → drive_adapter
6. drive_adapter_analog_bldc 是唯一 PWM 出口
7. 禁止旧 GPIO35/36/37/47/48
8. UWB 协议未抓包前禁止编造 parser
9. H5 不能设 PWM / 清安全锁 / 绕过安装向导

### 子代理限制
- leaf 角色：不能 delegate_task、不能 clarify、不能 memory、不能 send_message
- 所有角色：通过结构化输出汇报结果

### 失败处理
- 子代理返回 FAIL → coordinator 分析原因，重新分配或降级为手动
- 安全相关失败 → 立即停止，等待用户决策

## 委派命令模板

```bash
# 使用 architect profile
delegate_task(
    acp_command='hermes',
    acp_args=['--profile', 'architect', '--acp', '--stdio'],
    goal='审查 FollowBox 固件架构',
    context='项目路径: /mnt/d/car/Follow the box。读取 FIRMWARE-SPEC.md, CURRENT-WIRING-AI.md, PIN-MAP-V1.md',
    toolsets=['terminal', 'file', 'search']
)

# 使用 motion profile
delegate_task(
    acp_command='hermes',
    acp_args=['--profile', 'motion', '--acp', '--stdio'],
    goal='实现 motion_mixer 差速解算',
    context='项目路径: /mnt/d/car/Follow the box。MotionIntent 结构定义在 core/types.h...',
    toolsets=['terminal', 'file', 'search']
)
```

## 项目技能矩阵

| 编号 | 技能名称 | Hermes Profile | 职责 |
|------|----------|----------------|------|
| 00 | dispatcher | default (coordinator) | 任务分发 |
| 01 | firmware-architecture-guardian | architect | 架构设计 |
| 02 | firmware-implementation-planner | architect | 实施规划 |
| 03 | safety-control-reviewer | safety-reviewer | 安全审查 |
| 04 | sensor-protocol-integrator | sensor-fusion | 传感器/协议 |
| 05 | drive-power-calibration-engineer | motion | 驱动/电源 |
| 06 | h5-telemetry-ui-engineer | vision-h5 | H5/遥测 |
| 07 | code-review-debugger | tester | 审查/测试 |
| 08 | bringup-test-safety-officer | safety-officer | 测试安全 |
