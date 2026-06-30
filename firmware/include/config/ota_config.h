#pragma once

#include <cstdint>

namespace followbox::ota_config {

#ifndef FOLLOWBOX_FIRMWARE_VERSION
#define FOLLOWBOX_FIRMWARE_VERSION "2026.06.30-runtime-split.1"
#endif

#ifndef FOLLOWBOX_CLOUD_OTA_ENABLED
#define FOLLOWBOX_CLOUD_OTA_ENABLED 1
#endif

constexpr char CURRENT_VERSION[] = FOLLOWBOX_FIRMWARE_VERSION;
constexpr bool CLOUD_OTA_ENABLED = (FOLLOWBOX_CLOUD_OTA_ENABLED == 1);
constexpr uint32_t CHECK_INTERVAL_MS = 60000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

}  // namespace followbox::ota_config
