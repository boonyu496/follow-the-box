# FollowBox ESP32-S3-CAM Firmware

Standalone firmware for the ESP32-S3-N16R8 CAM carrier with an OV5640 camera.
It joins the FollowBox controller softAP and serves an MJPEG stream directly to
the H5 browser.

## Network

- SSID: `FollowBox`
- Password: `followbox123`
- Static IP: `192.168.4.2`
- Stream: `http://192.168.4.2:81/stream`
- Status: `http://192.168.4.2/status`
- Snapshot: `http://192.168.4.2/capture`

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
  -D CAM_FRAME_SIZE=FRAMESIZE_SVGA
  -D CAM_JPEG_QUALITY=12
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
keeps the stream at SVGA JPEG quality 12 so the AP/LAN H5 is responsive and the
cloud relay frames stay inside the controller upload limit.

Keep the camera on a separate 5 V rail with enough current margin. A weak supply
often looks like random WiFi disconnects or blank frames.
