#pragma once

#include <ESPAsyncWebServer.h>

#include "ota/cloud_ota_manager.h"

namespace followbox {

void registerLocalOtaRoutes(AsyncWebServer& server, CloudOtaManager* ota_manager);

}  // namespace followbox
