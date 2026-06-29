# FollowBox Reset Probe

This is a standalone ESP32-S3 diagnostic sketch for reset-loop, brownout, panic,
watchdog, flash/PSRAM, and basic memory checks. It does not include FollowBox
motor, sensor, H5, OTA, or cloud code.

Current FollowBox main board: ESP32-S3-DevKitC-1-N32R16V /
ESP32-S3-WROOM-2-N32R16V, 32 MB Octal Flash + 16 MB Octal PSRAM, OPI/1.8 V.
Do not change the board, Flash, PSRAM, OPI, or voltage configuration for this
project.

Use it when the board keeps rebooting and you need to split the problem:

- If this probe also reboots, suspect power, USB cable, EN/reset line, board
  variant, flash mode, or PSRAM/flash hardware configuration.
- If this probe is stable but the main firmware reboots, suspect main firmware
  tasks, sensor buses, blocking I/O, watchdog starvation, or application code.

## Build And Upload

From the repository root:

```powershell
New-Item -ItemType Directory -Force firmware\.pio-core | Out-Null
$env:PLATFORMIO_CORE_DIR = (Resolve-Path 'firmware\.pio-core').Path
$env:PLATFORMIO_HOME_DIR = $env:PLATFORMIO_CORE_DIR
pio run -d firmware\diagnostics\reset_probe -e esp32-s3-devkitc-1 -t upload
pio device monitor -d firmware\diagnostics\reset_probe -b 115200
```

## Serial Commands

Send one character in the serial monitor:

- `s`: print a full diagnostic snapshot
- `r`: trigger `ESP.restart()` and verify `SW` reset reporting
- `p`: trigger `abort()` and verify `PANIC` reset reporting
- `d`: enter 3-second deep sleep and verify `DEEPSLEEP` reporting
- `h`: print this help

## Reading The Result

- `BROWNOUT`: voltage sag. Check USB cable, 5V input, 3V3 regulator, motor power
  coupling, and common ground. Test with motors/controllers disconnected.
- `POWERON` repeatedly: power is being removed or the board is not staying up.
- `EXT`: EN/reset line is being pulled or the reset button/circuit is noisy.
- `PANIC`: firmware crashed. Save the backtrace printed before reboot.
- `TASK_WDT`, `INT_WDT`, or `WDT`: code is blocking or starving a watchdog.
- Probe stable, main firmware unstable: compare main firmware logs around sensor
  task, I2C/TOF, UART parsers, cloud/HTTP calls, and telemetry logging.
