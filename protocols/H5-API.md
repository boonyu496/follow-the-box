# H5 本地控制 API v1.0

> H5 只允许状态显示、低速点动、模式请求、人工复位请求。H5 不能直接设置 PWM，不能清除物理急停，不能绕过安装向导。

## WebSocket 状态下发

本节冻结 H5 状态 JSON 字段。

路径：`/ws/state`

```json
{
  "now_ms": 123456,
  "mode": "SAFE_IDLE",
  "safety": {
    "motion_allowed": false,
    "fault_latched": false,
    "stop_reason": "NONE",
    "max_speed_scale": 0.0
  },
  "rc": {"online": true, "last_update_ms": 123400},
  "cloud": {"connected": false, "last_update_ms": 0, "last_seq": 0},
  "imu": {"valid": false, "last_update_ms": 0, "yaw_deg": 0.0, "yaw_rate_dps": 0.0, "pitch_deg": 0.0, "roll_deg": 0.0},
  "uwb": {"valid": false, "distance_mm": 0, "bearing_deg": 0, "confidence": 0, "last_update_ms": 0},
  "obstacle": {"valid": false, "last_update_ms": 0, "front_left_mm": 0, "front_center_mm": 0, "front_right_mm": 0, "side_left_mm": 0, "side_right_mm": 0},
  "lidar": {"valid": false, "last_update_ms": 0, "front_left_mm": 0, "front_center_mm": 0, "front_right_mm": 0, "side_left_mm": 0, "side_right_mm": 0, "rx_bytes": 0, "packets": 0, "checksum_errors": 0, "framing_errors": 0, "scans": 0},
  "tof": {"valid": false, "last_update_ms": 0, "front_left_mm": 0, "front_center_mm": 0, "front_right_mm": 0, "front_left_valid": false, "front_center_valid": false, "front_right_valid": false, "init_ok_mask": 0, "init_attempt_count": 0, "init_failure_count": 0, "read_count": 0, "timeout_count": 0, "mux_nack_count": 0, "bus_clear_count": 0, "reinit_count": 0, "last_recovery_ms": 0},
  "ultrasonic": {"valid": false, "last_update_ms": 0, "left_mm": 0, "right_mm": 0, "left_valid": false, "right_valid": false},
  "camera": {"online": false, "stream_url": "http://192.168.4.2:81/stream"},
  "power": {"battery_voltage": 0.0, "low_battery": false},
  "motor": {"enable": false, "left_target": 0.0, "right_target": 0.0, "brake": true},
  "firmware": {"version": "2026.06.19-ota-h5.2"},
  "install_wizard_complete": false,
  "throttle_calibrated": false
}
```

字段说明（避障相关）：

- `obstacle`：**融合后**的避障快照（LiDAR + 前向 TOF + 侧向超声，按扇区取最近有效读数），是安全门控与限速实际使用的数据；`0` 表示该扇区无有效读数。
- `lidar`：EAI S2 的原始分区距离和只读诊断。`rx_bytes>0 && packets==0` 通常表示波特率/帧格式不匹配；只有 `valid=true` 的完整扫描才参与 `obstacle` 融合。
- `tof`：前向 TCA9548A + 3×VL53L1X 的原始分路读数，仅用于显示/诊断；每路以 `front_*_valid` 为准，`valid=true` 仅表示至少一路当前有效。
  `init_ok_mask` 的 bit0/1/2 对应前中/左前/右前；`init_attempt_count` 是启动与运行期初始化尝试总数，`init_failure_count` 表示 MUX 已选通但 VL53L1X 未响应。连续 MUX NACK/读取超时达到阈值后固件执行 Bus Clear，并每秒最多重试一路。
- `ultrasonic`：左右 HC-SR04 的原始侧向读数，仅用于显示/诊断（侧向避障 P0 暂不触发停车）；每路以 `left_valid` / `right_valid` 为准。
- `camera`：ESP32-S3-CAM 视频链路在线状态和 MJPEG 地址，仅用于显示；**视频断流不影响运动安全门控**。默认地址假定摄像头加入 FollowBox AP 后使用静态 IP `192.168.4.2`，流路径为 `:81/stream`。
- `imu`：JY62/JY61P 类 WitMotion IMU 只读姿态遥测；`valid=false` 表示固件未收到有效角度帧或已超时。当前默认 `UART_NUM_IMU=-1` 时该字段会持续无效，只用于 H5/云端诊断，不绕过安全链。
- `cloud`：云端低速点动链路状态，仅用于显示和诊断；云端命令仍由本地安全链最终裁决。
- `firmware.version`：当前正在运行的固件版本。云端只能在设备实际重新上报该版本后标记 OTA 完成。

## 固件 OTA（显式授权）

OTA 检查与安装严格分离：检查只读取云端 manifest，不写 Flash；只有用户点击“安装”并提交检查所得的精确版本后，设备才允许写入。`X-FollowBox-Key` 鉴权规则与其他本地写接口一致。

- `GET /api/ota/status`：读取当前版本、可用版本、检查/安装状态和失败原因。
- `POST /api/ota/check`：请求通信任务立即检查一次；返回 `202` 只表示已受理，H5 应继续轮询 status。
- `POST /api/ota/install`：body 为 `{"version":"<检查所得版本>"}`。版本变化、未先检查、OTA 忙或鉴权失败均拒绝。

“暂不安装”不调用安装接口，也不产生设备侧待执行请求。安装开始后，控制任务必须持续执行安全停车；下载、长度、MD5 或写入任一步失败后保持安全停机，等待受控重启或 USB 恢复。

兼容迁移：尚未包含本协议的旧固件仍轮询 `/firmware/version`。云端在没有 operator 安装授权时不得向该旧接口返回下载 URL；创建精确版本的安装请求后才返回 URL，使首个迁移版本同样受用户点击控制。

## 低速点动请求

路径：`POST /api/jog`

```json
{
  "seq": 1,
  "forward": 0.0,
  "turn": 0.0,
  "deadman": true,
  "client_time_ms": 123456
}
```

限制：

- `forward` / `turn` 范围 -1..1，但服务器强制限幅到 `h5_max_speed_percent`。
- 超过 `remote.h5_lost_stop_ms` 未收到新请求，退出 H5 点动并停车。
- `deadman=false` 必须停车。

## 模式请求

路径：`POST /api/mode-request`

```json
{"requested_mode": "MANUAL_H5_LOW_SPEED"}
```

允许值：`SAFE_IDLE`、`MANUAL_H5_LOW_SPEED`、`AUTO_FOLLOW_REQUEST`。

`AUTO_FOLLOW_REQUEST` 只表示请求，必须由 `mode_manager` 检查安装向导、UWB 有效、DS600 接管状态后决定。

## 人工复位请求

路径：`POST /api/reset-fault`

```json
{"confirm": true}
```

限制：

- 只能复位软件故障锁定。
- 不能复位物理急停；GPIO21 仍为急停状态时必须拒绝。
- 复位后仍要求油门回中/无运动请求。

## 安装向导完成请求

路径：`POST /api/wizard-complete`

```json
{
  "complete": true,
  "estop_checked": true,
  "wheels_lifted": true,
  "direction_checked": true,
  "throttle_checked": true
}
```

限制：

- `complete=true` 时四个确认字段必须都为 `true`，缺失或为 `false` 必须拒绝。
- 这些确认只代表人工完成了首烧/架空检查，不允许绕过急停、标定、UWB 或 `applyFinalGate()`。
- 油门标定状态由 `/api/calibrate` 和 NVS schema 决定，不能仅靠安装向导按钮伪造。
