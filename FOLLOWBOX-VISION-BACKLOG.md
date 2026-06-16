# FollowBox 三大远景待办

> 当前 P0 仍是本地安全闭环（遥控/UWB 跟随/避障/急停），这些远景排在 P0 之后逐步展开。

---

## 远景一：云端训练 + 远程调试

### 目标
开发阶段实现日志实时上网页、远程查看调试；发给客户组装后也能远程诊断调试。

### 技术路线
```
ESP32-S3 WiFi → WebSocket/JSON → 浏览器(H5) / 公网MQTT → 远程开发机
```

### 待办

| 优先级 | 任务 | 说明 | 阶段 |
|:---:|---|---|---|
| P1 | telemetry_logger 模块骨架 | 定时(5-10Hz)输出 SystemState JSON，不阻塞控制循环 | MVP0 |
| P1 | WebSocket 推送通道 | ESP32 端 /ws/telemetry 端点，H5 端 new WebSocket() 订阅 | MVP0 |
| P1 | H5 telemetry 面板 | 实时显示：模式、速度、UWB距离/角度、TOF障碍、IMU yaw、电池电压、急停状态 | MVP0 |
| P2 | 云端日志服务器 | 公网 MQTT broker 或 WebSocket 中继，接收 ESP32 日志流 | MVP1 |
| P2 | OTA 固件升级 | 支持远程推送固件，升级后自动重连 | MVP1 |
| P2 | 远程调试指令 | 云端下发安全调试指令：`SET_LOG_LEVEL`、`DUMP_STATE`、`PING_SENSOR`，限制在安全模式下执行 | MVP1 |
| P3 | 日志批量采集 | 记录 30 分钟跟踪日志，云端回放/分析跟随参数、UWB 信号特征、避障时机 | MVP2 |
| P3 | 云端训练流水线 | 收集大量场景数据（UWB+TOF+IMU+PWM）→ 云端训练改进参数/模型 → OTA 下发 | MVP3 |

### 硬件需求
- 已有：ESP32-S3 WiFi + ESP32-S3-CAM
- 需补：无（全用现有硬件）

---

## 远景二：云端轨迹记录 → 自动驾驶

### 目标
跟随过程中云端记录轨迹，后续能沿记录路径自动行走（示教-回放），最终实现自动驾驶。

### 技术路线（三阶段）

```
Phase 1: UWB相对轨迹          Phase 2: 加入扫描LiDAR建图    Phase 3: 示教-回放
UWB距离+方位                    2D点云+姿态                   记录路径→自动重走
IMU航向                        SLAM建图+定位                障碍打断/恢复
PWM速度估算                    全局路径规划                  多段路线切换
```

### 待办

| 优先级 | 任务 | 说明 | 阶段 |
|:---:|---|---|---|
| P2 | UWB 轨迹数据定义 | 定义云端轨迹记录格式：`{t_ms, uwb_dist, uwb_bearing, yaw_deg, left_pwm, right_pwm, obstacle_states}` | P1 |
| P2 | 轨迹上传 | ESP32 跟随过程中每 100ms 记录轨迹点，打包上传到云端 | P1 |
| P2 | 云端轨迹回放 | Web 浏览器绘制轨迹线 + 标注模式切换 / 避障事件 / 急停事件 | P1 |
| P3 | 轨迹回放模式 | ESP32 增加 REPLAY 模式：沿记录路径自动低速走，UWB 实时纠偏 | P2 |
| P4 | 扫描 LiDAR 融合 | **需要确认当前 TOF LiDAR 型号**：若是扫描式 LiDAR（LD06/TFmini），建立 2D SLAM 流水线 | P3 |
| P4 | 示教-回放上线 | 人在前走（UWB 跟随）→ 同时记录路径 → 回放时沿路径走 + 障碍打断/恢复 | P3 |
| P5 | 多路线存储 | 云端存储多条路线，下车时选择路线自动执行 | P4 |

### 硬件需求
- Phase 1-2：已有（UWB + IMU + 里程计）
- Phase 3-4：需扫描式 TOF LiDAR（LD06/TFmini/或其他）或加轻量上位机（RPi Zero 2W / ESP32-P4）

### 参考项目
- [QVPR/teach-repeat](https://github.com/QVPR/teach-repeat)：低算力 monocular vision + wheel odometry 示教-回放
- [jremington/UWB-Indoor-Localization_Arduino](https://github.com/jremington/UWB-Indoor-Localization_Arduino)：UWB 2D/3D 定位参考

---

## 远景三：自主交互（识别门 | 找人帮忙 | 买东西）

### 目标
小车能自动识别电梯门/单元门，遇到需要帮助的场景时自动对话请人开门，极端场景能自动买东西。

### 技术路线

```
Phase 1: 门检测+语音请求帮助    Phase 2: 双向语音对讲         Phase 3: 商品交互(远期)
TOF发现前方障碍持续>5秒          麦克风采集路人回答            货仓+机械臂
CAM拍照→VLM判断场景             云端语音识别+LLM理解          支付API对接
播放预设语音请求开门             生成动态回应
```

### 待办

| 优先级 | 任务 | 说明 | 阶段 |
|:---:|---|---|---|
| P2 | TOF 卡住检测逻辑 | 前方 TOF 持续 >3 秒距离不变且小于 1m → 触发 `STUCK_BY_DOOR` 事件 | P1 |
| P2 | CAM 拍照上传 | `STUCK` 事件触发时自动拍照 JPEG，上传云端辅助判断 | P1 |
| P2 | 云端 VLM 场景判断 | 上传照片后云端 LLM/VLM 判断：电梯门 / 单元门 / 障碍物 / 人 / 不可通行 | P1 |
| P2 | 预设语音请求帮助 | 买 MAX98357 功放($10) + 小喇叭($5) → 预录 3-5 句请求语音，根据场景播放 | P1 |
| P2 | 等待超时逻辑 | 请求帮助后等待 15-30 秒，TOF 变远则继续，否则重试或停车报警 | P1 |
| P3 | 双向语音 | 加 INMP441 麦克风($15) → 路人回答 → 云端语音→文字→LLM 理解 → 决定下一步 | P2 |
| P3 | 动态 TTS 回应 | 根据场景实时生成语音回应，不再只放预录音 | P2 |
| P4 | 电梯场景增强 | 检测电梯门 → 等人进电梯后跟进去 → 检测楼层 → 出电梯 | P3 |
| P5 | 自动买东西 | 货仓门 + 扫码 + 支付 API（远期愿景，当前不投入资源） | P4 |

### 硬件需求
- P1：已有（TOF + CAM）
- P2：需补 MAX98357 + 喇叭（~¥25）
- P3：需补 MAX98357 + INMP441 麦克风（~¥40）
- P4：需补扫描 LiDAR 或视觉 SLAM 辅助
- P5：需补货仓/机械臂/支付模块（长期）

### 行业参考
- [Axios: Robots learn to ask humans for help](https://www.axios.com/2026/04/01/robots-delivery-serve-tmobile) — 2026 年 4 月报道，配送机器人用 AI 向路人请求开门帮助

---

## 阶段总览

```text
P0 ─── 本地安全闭环（遥控/UWB/避障/急停/H5/日志）          ← 当前在做
         │
P1 ─── telemetry_logger + WebSocket → H5 实时状态显示      ← 远景一
         │
P2 ─── 远程日志服务器 + OTA + 轨迹记录 + 门检测+语音请求帮助  ← 三个远景并行
         │
P3 ─── 示教-回放模式 + 双向语音交互 + 云端轨迹训练           ← 远景二+三
         │
P4 ─── 扫描 LiDAR SLAM + 电梯自主乘降 + 多路线存储          ← 远期
         │
P5 ─── 自动买东西 / 配送                                      ← 远期愿景
```

---

## 更新记录

| 日期 | 更新人 | 内容 |
|---|---|---|
| 2026-05-30 | Hermes | 首次创建：三个远景待办文档 |
