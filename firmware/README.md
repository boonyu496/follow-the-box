# FollowBox Firmware

This PlatformIO firmware follows the root `FIRMWARE-SPEC.md`.

Current implementation stage:

- P0-1: project skeleton, pin map, core types.
- P0-2 partial: pure logic for safety, mode selection, and differential mixing.
- P0-A: UWB GC-P2304 binary-frame parser + filter (`src/sensors/uwb_gc_p2304.*`), no GPIO.
- P0-B: UWB follow controller (`src/control/follow_controller_uwb.*`) wired into
  `AUTO_FOLLOW`; outputs `MotionIntent` only, still gated by `SafetyManager`.
- LiDAR LD19/LD06 DTOF parser (`src/sensors/lidar_ld19.*`) folding the 360 deg
  scan into `ObstacleSnapshot` sectors, no GPIO.
- P0-D: front-sector obstacle limiter (`src/control/obstacle_manager.*`) wired
  into `App::tick` between `command_pipeline` and `motion_mixer`; slow/stop on
  forward only, no auto-reverse, no avoidance state machine yet.
- P1 cloud telemetry/remote jog skeleton: `src/cloud/cloud_client.*` can upload
  `/ws/state`-schema snapshots plus buffered logs to a cloud service and poll
  `MANUAL_CLOUD_LOW_SPEED` commands. It is disabled by default and still goes
  through `SafetyManager::applyFinalGate()` before any motor output.
- Forward TOF array (`src/sensors/tof_vl53l1x_array.*`, TCA9548A + 3x VL53L1X),
  side ultrasonic pair (`src/sensors/ultrasonic_array.*`, 2x HC-SR04 shared TRIG,
  interrupt echo capture), and ESP32-S3-CAM link status (`src/sensors/camera_link.*`,
  status only - never a safety input). All produce snapshots only, no motion.
- Obstacle fusion (`src/sensors/obstacle_fusion.*`): merges lidar + TOF + ultrasonic
  into the single `ObstacleSnapshot` the motion path reads (closest valid reading
  per sector). `SafetyManager`/`ObstacleManager` logic is unchanged - they just see
  a richer snapshot. Pure logic, in the g++ smoke test.

Motor GPIO/PWM output is owned only by `src/drive/drive_adapter_analog_bldc.*`
after `SafetyManager::applyFinalGate()`.

Sensor UART wiring (UWB on GPIO17/18, lidar UART, IMU on GPIO42) is still pending;
the parsers above are byte-stream only and must be fed from a sensor task without
blocking the control loop.

Quick checks from the project root:

```powershell
$env:PLATFORMIO_CORE_DIR = (Resolve-Path 'firmware\.pio-core').Path
$env:PLATFORMIO_HOME_DIR = $env:PLATFORMIO_CORE_DIR
pio run -d firmware
g++ -std=c++17 -Ifirmware/include -Ifirmware/src `
  firmware/tools/logic_smoke_test.cpp `
  firmware/src/safety/safety_manager.cpp `
  firmware/src/app/mode_manager.cpp `
  firmware/src/app/command_pipeline.cpp `
  firmware/src/control/motion_mixer.cpp `
  firmware/src/control/follow_controller_uwb.cpp `
  firmware/src/control/obstacle_manager.cpp `
  firmware/src/sensors/uwb_gc_p2304.cpp `
  firmware/src/sensors/lidar_ld19.cpp `
  firmware/src/sensors/jy61p_imu.cpp `
  firmware/src/sensors/obstacle_fusion.cpp `
  firmware/src/app/app.cpp `
  firmware/src/web/telemetry_api.cpp `
  firmware/src/web/h5_command_handler.cpp `
  firmware/src/web/h5_request_parser.cpp `
  -o firmware/tools/logic_smoke_test.exe
firmware\tools\logic_smoke_test.exe
```

If the MSYS2 g++ subprocess (`cc1plus`) exits with no output, prepend its runtime
to PATH first: `$env:Path = 'C:\msys64\mingw64\bin;' + $env:Path`.

If `firmware\.pio-core` does not exist yet, create it first with `New-Item -ItemType Directory -Force firmware\.pio-core`.

USB first flash, OTA afterwards:

```powershell
# Default target: ESP32-S3-DevKitC-1-N32R16V (32 MB OPI flash + 16 MB OPI PSRAM).
# If the board previously boot-looped with a wrong flash mode, erase once.
pio run -d firmware -e esp32-s3-devkitc-1 -t erase

# First time only: USB flash the OTA-capable firmware and LittleFS H5 assets.
pio run -d firmware -e esp32-s3-devkitc-1 -t upload
pio run -d firmware -e esp32-s3-devkitc-1 -t uploadfs

# Older 8 MB Quad-flash DevKitC-1 boards must use the explicit fallback env:
# pio run -d firmware -e esp32-s3-devkitc-1-n8 -t upload
# pio run -d firmware -e esp32-s3-devkitc-1-n8 -t uploadfs

# After that: connect the PC to FollowBox SoftAP and upload over OTA.
pio run -d firmware -e ota -t upload

# If FOLLOWBOX_WIFI_STA=1 is used, override with the car IP.
pio run -d firmware -e ota -t upload --upload-port 192.168.x.x
```

The OTA service uses `followbox` on port `3232` with the development password
from `include/config/network_config.h`. Change the password before field use.

Cloud telemetry reference service:

```powershell
node cloud/server.js
```

Cloud firmware is opt-in only. After local bring-up, set STA WiFi credentials
and cloud endpoint/token, then build with `-D FOLLOWBOX_WIFI_STA=1` and
`-D FOLLOWBOX_CLOUD_ENABLED=1`.
