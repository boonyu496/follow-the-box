# FollowBox ESP32-S3-CAM Firmware

Standalone firmware for the ESP32-S3-N16R8 CAM carrier with an OV5640 camera.
It joins the FollowBox controller softAP and serves an MJPEG stream directly to
the H5 browser.

## Network

- SSID: `FollowBox`
- Password: `followbox-dev-only`
- Static IP: `192.168.4.10`
- Stream: `http://192.168.4.10/stream`
- Legacy stream: `http://192.168.4.10:81/stream`
- Status: `http://192.168.4.10/status`
- Snapshot: `http://192.168.4.10/capture`

The main FollowBox controller does not receive or relay video. It only publishes
the stream URL in telemetry, and video loss must not affect motion safety gates.

## Build And Flash

```powershell
pio run -d vision_cam
pio run -d vision_cam -t upload
pio device monitor -d vision_cam
```

If the carrier board pinout differs, override pins in `platformio.ini`:

```ini
build_flags =
  ${env:esp32-s3-n16r8-cam.build_flags}
  -D CAM_PIN_XCLK=15
  -D CAM_PIN_SIOD=4
  -D CAM_PIN_SIOC=5
  -D CAM_FRAME_SIZE=FRAMESIZE_VGA
  -D CAM_JPEG_QUALITY=18
  -D CAM_STREAM_TARGET_FPS=12
```

The default pin map is:

| Signal | GPIO |
|---|---:|
| XCLK | 15 |
| SIOD/SDA | 4 |
| SIOC/SCL | 5 |
| D0..D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |
| PWDN/RESET | -1 / -1 |

OV5640 uses the SCCB control bus (`SIOD`/`SIOC`) plus the parallel DVP signals
(`D0..D7`, `VSYNC`, `HREF`, `PCLK`) and an external `XCLK`. The default build
keeps the stream at VGA JPEG quality 18 with a 12 fps target so the AP/LAN H5
stays responsive and the cloud relay frames stay inside the controller upload
limit. If the picture still advances only every few seconds, check `/status`
while the H5 is open: `stream_frames` should keep increasing, `last_frame_bytes`
should be well below the cloud upload cap, and `last_stream_frame_interval_ms`
should normally sit near the configured frame interval rather than multi-second
gaps.

Keep the camera on a separate 5 V rail with enough current margin. A weak supply
often looks like random WiFi disconnects or blank frames.

The fixed `.10` address is intentionally outside the first softAP DHCP leases.
Do not use `.2` for the camera: phones and laptops commonly receive that address
when they are the first client on the FollowBox hotspot, causing IP collisions
and browser `ERR_CONNECTION_REFUSED` symptoms.
