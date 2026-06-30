# FollowBox Cloud Telemetry and Remote Jog v0.1

> 目标：让 ESP32-S3 在 STA WiFi 下主动连接云端，在不同网络中上传状态/日志，并允许受限的远程低速点动调试。云端不是安全主控。

## Safety Boundary

- 云端不能直接设置 PWM、电压、GPIO、急停、安装向导或安全红线参数。
- 云端远程遥控只允许 `MANUAL_CLOUD_LOW_SPEED` 低速点动。
- 每条远程运动命令必须带 `seq`、`deadman=true`、`forward`、`turn`，并在 `CLOUD_LOST_STOP_MS` 内持续刷新。
- 命令超时、服务器断开、WiFi 断开、急停、低电压、心跳超时、障碍停车都会由本地 `safety_manager` 停车。
- DS600 手动输入仍高于 H5/云端；本地安全链最终否决所有运动。

## Firmware Configuration

Enable cloud mode only after local USB bring-up succeeds:

```ini
build_flags =
  -D FOLLOWBOX_WIFI_STA=1
  -D FOLLOWBOX_CLOUD_ENABLED=1
```

Then set:

- STA WiFi credentials via H5 `/api/wifi`, or bench-only build flags
  `FOLLOWBOX_WIFI_STA_SSID` / `FOLLOWBOX_WIFI_STA_PASSWORD`.
- Cloud values via build flags, not by editing tracked headers:
  `FOLLOWBOX_CLOUD_BASE_URL`, `FOLLOWBOX_CLOUD_DEVICE_ID`, and
  `FOLLOWBOX_CLOUD_DEVICE_TOKEN`.

## Device -> Cloud

`POST /api/device/:deviceId/ingest`

```json
{
  "device_id": "followbox-001",
  "token": "<FOLLOWBOX_CLOUD_DEVICE_TOKEN>",
  "seq": 1,
  "state": {
    "now_ms": 123456,
    "mode": "SAFE_IDLE",
    "safety": {"motion_allowed": false, "stop_reason": "NONE"},
    "cloud": {"connected": false, "last_update_ms": 0, "last_seq": 0}
  },
  "logs": ["[123][I] TLM mode=IDLE ..."]
}
```

The `state` object is the same schema as `protocols/H5-API.md`, extended with `cloud`.

## Cloud -> Device

`GET /api/device/:deviceId/command?token=...&last_seq=...`

Stop / idle:

```json
{"ok": true, "seq": 2, "deadman": false, "forward": 0, "turn": 0}
```

Low-speed jog:

```json
{"ok": true, "seq": 3, "deadman": true, "forward": 0.2, "turn": -0.1}
```

Safe idle:

```json
{"ok": true, "seq": 4, "safe_idle": true, "deadman": false, "forward": 0, "turn": 0}
```

## Operator -> Cloud

`POST /api/device/:deviceId/command`

Headers:

- `Authorization: Bearer <FOLLOWBOX_OPERATOR_TOKEN>` when the server env var is set.

Body is the same command object, except the server owns and increments `seq`.

## Deployment

Start the included reference service:

```powershell
node cloud/server.js
```

Environment variables:

- `PORT=8080`
- `FOLLOWBOX_DEVICE_TOKEN=...` (must match firmware build flag)
- `FOLLOWBOX_OPERATOR_TOKEN=...` (used by cloud H5/operator APIs)

If those tokens are missing, protected APIs reject requests by default. For a
local-only bench server, `FOLLOWBOX_ALLOW_INSECURE_DEV_AUTH=1` can temporarily
disable auth; never use that flag on an internet-facing service.

For internet use, put the service behind HTTPS reverse proxy and set strict tokens.
