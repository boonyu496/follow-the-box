#pragma once

#include <cstdint>

namespace followbox::cloud_config {

#ifndef FOLLOWBOX_FIELD_BUILD
#define FOLLOWBOX_FIELD_BUILD 0
#endif

// Cloud telemetry + low-speed remote command link. Enabled by default: the
// link only becomes active once the STA leg is provisioned via the H5 panel
// (POST /api/wifi) and actually connected, so an unprovisioned box simply
// keeps the cloud client idle. Real deployment credentials must be injected
// with -D build flags, not committed here.
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
// Empty by default so tracked firmware does not contain a reusable token.
#ifndef FOLLOWBOX_CLOUD_DEVICE_TOKEN
#define FOLLOWBOX_CLOUD_DEVICE_TOKEN ""
#define FOLLOWBOX_CLOUD_DEVICE_TOKEN_IS_DEFAULT 1
#else
#define FOLLOWBOX_CLOUD_DEVICE_TOKEN_IS_DEFAULT 0
#endif

#if FOLLOWBOX_FIELD_BUILD && (FOLLOWBOX_CLOUD_ENABLED != 0) && FOLLOWBOX_CLOUD_DEVICE_TOKEN_IS_DEFAULT
#error "Field cloud builds must set FOLLOWBOX_CLOUD_DEVICE_TOKEN via build flags"
#endif
constexpr char DEVICE_TOKEN[] = FOLLOWBOX_CLOUD_DEVICE_TOKEN;
constexpr bool DEVICE_TOKEN_CONFIGURED = (sizeof(DEVICE_TOKEN) > 1);

// The cloud H5 otherwise shows TOF changes up to one second late. 4 Hz keeps
// telemetry visibly responsive while the local AP/LAN H5 remains 10 Hz. Failed
// telemetry retries back off a little so public-network stalls do not saturate
// the single comm loop.
constexpr uint32_t UPLOAD_INTERVAL_MS = 250;
constexpr uint32_t COMMAND_POLL_INTERVAL_MS = 150;
constexpr uint32_t HTTP_TIMEOUT_MS = 1000;
constexpr uint32_t TELEMETRY_RETRY_MIN_MS = 750;
constexpr uint32_t TELEMETRY_RETRY_MAX_MS = 3000;

// Optional low-FPS public video relay. Keep it disabled by default on the main
// controller: pulling JPEG frames from the camera over the softAP while the STA
// leg uploads to the cloud shares the same WiFi/LwIP stack as local H5 and was
// observed to make AP/LAN HTTP stop responding after sustained runtime. Direct
// AP/LAN camera viewing can still use the camera board's own stream.
#ifndef FOLLOWBOX_CLOUD_VIDEO_ENABLED
#define FOLLOWBOX_CLOUD_VIDEO_ENABLED 0
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
