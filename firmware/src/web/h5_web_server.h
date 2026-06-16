#pragma once

#include "core/system_state.h"
#include "web/h5_command_handler.h"
#include "app/app.h"
#include "drive/drive_adapter_analog_bldc.h"
#include "storage/profile_store.h"
#include "storage/calibration_store.h"
#include "storage/wifi_store.h"
 
namespace followbox {
 
// Arduino transport for the H5 control panel. Thin glue only: WiFi (softAP or
// STA), an async HTTP server for the H5-API.md POST endpoints, and a WebSocket
// that pushes the frozen /ws/state JSON. All decision logic lives in the
// pure-logic H5CommandHandler / telemetry_api / h5_request_parser; this class
// just moves bytes and never touches GPIO, PWM or the motion path.
//
// Concurrency: ESPAsyncWebServer runs its callbacks in the AsyncTCP task while
// the control loop runs in loopTask. The shared H5CommandHandler is guarded by
// a portMUX spinlock with critical sections kept to a struct copy / trivial
// state update (FIRMWARE-SPEC 7.1). State JSON is built and pushed from the
// loop task only, where SystemState is owned, so it needs no extra lock.
class H5WebServer {
 public:
  // Bring up WiFi (AP always on; STA joined from NVS credentials when
  // provisioned) + HTTP/WS server. Safe to call once from setup().
  void begin(ProfileStore* profile_store, CalibrationStore* calibration_store,
             WifiStore* wifi_store);
 
  // Run staleness timeout and copy out the latest H5 control snapshot for
  // App::ingestH5Input. Acquires the spinlock briefly.
  H5ControlInput pollInput(uint32_t now_ms, App& app, DriveAdapterAnalogBldc& drive);
 
  // Push the current SystemState to connected panels (rate-limited) and reap
  // closed sockets. Call once per loop after tick(); reads state in the loop
  // task only.
  void pushState(const SystemState& state, uint32_t now_ms);
};
 
}  // namespace followbox
