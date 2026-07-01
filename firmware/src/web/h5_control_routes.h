#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "storage/calibration_store.h"
#include "storage/profile_store.h"
#include "web/h5_command_handler.h"

namespace followbox {

void registerH5ControlRoutes(AsyncWebServer& server, H5CommandHandler& handler,
                             portMUX_TYPE& handler_mux,
                             ProfileStore* profile_store,
                             CalibrationStore* calibration_store);

}  // namespace followbox

