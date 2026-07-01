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

}  // namespace followbox
