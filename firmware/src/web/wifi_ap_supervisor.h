#pragma once

#include <cstdint>

#include <ESPAsyncWebServer.h>

#include "storage/wifi_store.h"

namespace followbox {

using LocalApiAuthCallback = bool (*)(AsyncWebServerRequest* request);

void beginWifiApSupervisor(WifiStore* wifi_store);
void registerWifiApSupervisorRoutes(AsyncWebServer& server,
                                    LocalApiAuthCallback require_auth);
void loopWifiApSupervisor(uint32_t now_ms);

// Boot diagnostics surfaced in /api/wifi/status. `reset_reason` is the ESP-IDF
// reason label for THIS boot; `boot_count` is a reset counter kept in RTC RAM
// so it survives WDT/PANIC/BROWNOUT resets. A hotspot drop that reboots the
// box makes boot_count climb -> confirms a reboot (vs a phone merely leaving a
// still-running AP). Pure diagnostics: never touches the motion/safety path.
void setBootDiag(const char* reset_reason, uint32_t boot_count);

}  // namespace followbox
