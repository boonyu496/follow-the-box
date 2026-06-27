#pragma once

#include <cstdint>

namespace followbox::net {

// H5 control panel transport configuration.
//
// The box always runs AP+STA: the softAP below is the provisioning/local
// control channel and is never turned off, while the STA leg joins the
// user's WiFi using credentials saved via POST /api/wifi (NVS, see
// storage/wifi_store.h). STA_SSID below is only a compile-time bench
// fallback used when nothing has been provisioned yet.
#ifndef FOLLOWBOX_WIFI_STA
#define FOLLOWBOX_WIFI_STA 0
#endif

constexpr bool USE_SOFT_AP = (FOLLOWBOX_WIFI_STA == 0);  // legacy; AP is always on now.

// --- softAP (box hotspot) ---
constexpr char SOFT_AP_SSID[] = "FollowBox";
constexpr char SOFT_AP_PASSWORD[] = "followbox123";  // >= 8 chars; CHANGE BEFORE FIELD USE.
constexpr uint8_t SOFT_AP_CHANNEL = 6;
constexpr uint8_t SOFT_AP_MAX_CONN = 2;  // panel + spare; keep small for latency.

// --- STA (join existing WiFi) ---
// Bench builds may inject these without committing secrets:
//   -D FOLLOWBOX_WIFI_STA_SSID=\"ssid\"
//   -D FOLLOWBOX_WIFI_STA_PASSWORD=\"password\"
#ifndef FOLLOWBOX_WIFI_STA_SSID
#define FOLLOWBOX_WIFI_STA_SSID ""
#endif

#ifndef FOLLOWBOX_WIFI_STA_PASSWORD
#define FOLLOWBOX_WIFI_STA_PASSWORD ""
#endif

constexpr char STA_SSID[] = FOLLOWBOX_WIFI_STA_SSID;
constexpr char STA_PASSWORD[] = FOLLOWBOX_WIFI_STA_PASSWORD;

// HTTP/WebSocket server port.
constexpr uint16_t HTTP_PORT = 80;

// --- local H5/API authorization ---
// Production/field builds must override both values, for example:
//   -D FOLLOWBOX_LOCAL_API_AUTH_REQUIRED=1
//   -D FOLLOWBOX_LOCAL_API_KEY=\"site-specific-random-key\"
#ifndef FOLLOWBOX_LOCAL_API_AUTH_REQUIRED
#define FOLLOWBOX_LOCAL_API_AUTH_REQUIRED 0
#endif

#ifndef FOLLOWBOX_LOCAL_API_KEY
#define FOLLOWBOX_LOCAL_API_KEY ""
#endif

constexpr bool LOCAL_API_AUTH_REQUIRED =
    (FOLLOWBOX_LOCAL_API_AUTH_REQUIRED == 1);
constexpr char LOCAL_API_KEY[] = FOLLOWBOX_LOCAL_API_KEY;
constexpr char LOCAL_API_KEY_HEADER[] = "X-FollowBox-Key";

// Minimum interval between /ws/state pushes (ms). The control loop runs at
// 50 Hz; the panel does not need every frame and flooding the socket steals
// CPU from the motion path.
constexpr uint32_t STATE_PUSH_INTERVAL_MS = 100;

// --- OTA upload after the first USB flash ---
// First flash still has to be done over USB so this OTA service exists on the
// device. After that, PlatformIO can upload over WiFi with upload_protocol=espota.
#ifndef FOLLOWBOX_OTA_ENABLED
#define FOLLOWBOX_OTA_ENABLED 1
#endif

constexpr bool OTA_ENABLED = (FOLLOWBOX_OTA_ENABLED == 1);
constexpr char OTA_HOSTNAME[] = "followbox";
constexpr uint16_t OTA_PORT = 3232;
constexpr char OTA_PASSWORD[] = "followbox-ota";  // CHANGE BEFORE FIELD USE.

}  // namespace followbox::net
