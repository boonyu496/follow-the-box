#pragma once

#include <cstdint>

namespace followbox::cloud_config {

// Cloud telemetry + low-speed remote command link. Enabled by default: the
// link only becomes active once the STA leg is provisioned via the H5 panel
// (POST /api/wifi) and actually connected, so an unprovisioned box simply
// keeps the cloud client idle. Override any of these with -D build flags.
#ifndef FOLLOWBOX_CLOUD_ENABLED
#define FOLLOWBOX_CLOUD_ENABLED 1
#endif

constexpr bool ENABLED = (FOLLOWBOX_CLOUD_ENABLED != 0);

// No trailing slash. Must match the deployed cloud/server.js (see
// cloud/followbox-nginx.conf: boonai.cn/fb -> 127.0.0.1:8080).
#ifndef FOLLOWBOX_CLOUD_BASE_URL
#define FOLLOWBOX_CLOUD_BASE_URL "http://www.boonai.cn/fb"
#endif
constexpr char API_BASE_URL[] = FOLLOWBOX_CLOUD_BASE_URL;

// Stable device id shown in the cloud dashboard.
#ifndef FOLLOWBOX_CLOUD_DEVICE_ID
#define FOLLOWBOX_CLOUD_DEVICE_ID "followbox-001"
#endif
constexpr char DEVICE_ID[] = FOLLOWBOX_CLOUD_DEVICE_ID;

// Shared device token. Must equal FOLLOWBOX_DEVICE_TOKEN on the server.
// Default matches cloud/server.js's default; override both for production.
#ifndef FOLLOWBOX_CLOUD_DEVICE_TOKEN
#define FOLLOWBOX_CLOUD_DEVICE_TOKEN "f892ef460de624143d7d65cb5a863f84"
#endif
constexpr char DEVICE_TOKEN[] = FOLLOWBOX_CLOUD_DEVICE_TOKEN;

// The cloud H5 otherwise shows TOF changes up to one second late. 4 Hz keeps
// telemetry visibly responsive while the local AP/LAN H5 remains 10 Hz. Failed
// telemetry retries back off a little so public-network stalls do not saturate
// the single comm loop.
constexpr uint32_t UPLOAD_INTERVAL_MS = 250;
constexpr uint32_t COMMAND_POLL_INTERVAL_MS = 150;
constexpr uint32_t HTTP_TIMEOUT_MS = 1000;
constexpr uint32_t TELEMETRY_RETRY_MIN_MS = 750;
constexpr uint32_t TELEMETRY_RETRY_MAX_MS = 3000;

// Optional low-FPS public video relay. The controller fetches a JPEG snapshot
// from the camera on the local FollowBox AP and uploads it to the cloud. This
// is display-only and never feeds the motion/safety path. Video is strictly
// lower priority than telemetry: failures back off, and video is skipped while
// telemetry has not recently succeeded.
#ifndef FOLLOWBOX_CLOUD_VIDEO_ENABLED
#define FOLLOWBOX_CLOUD_VIDEO_ENABLED 1
#endif
constexpr bool VIDEO_ENABLED = (FOLLOWBOX_CLOUD_VIDEO_ENABLED != 0);

#ifndef FOLLOWBOX_CAMERA_CAPTURE_URL
#define FOLLOWBOX_CAMERA_CAPTURE_URL "http://192.168.4.10/capture"
#endif
constexpr char CAMERA_CAPTURE_URL[] = FOLLOWBOX_CAMERA_CAPTURE_URL;

constexpr uint32_t VIDEO_UPLOAD_INTERVAL_MS = 2500;
constexpr uint32_t VIDEO_HTTP_TIMEOUT_MS = 1500;
constexpr uint32_t VIDEO_MAX_FRAME_BYTES = 220 * 1024;
constexpr uint32_t VIDEO_RETRY_MIN_MS = 10000;
constexpr uint32_t VIDEO_RETRY_MAX_MS = 30000;
constexpr uint32_t VIDEO_TELEMETRY_GRACE_MS = 2000;

}  // namespace followbox::cloud_config
