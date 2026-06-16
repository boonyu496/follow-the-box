#pragma once

#include <cstddef>

#include "web/h5_command_handler.h"

namespace followbox {

// Decoded POST /api/jog body (H5-API.md). Bounds/clamping stay the handler's
// job; this only reports what was present in the request.
struct JogRequest {
  bool valid = false;
  uint32_t seq = 0;
  float forward = 0.0f;
  float turn = 0.0f;
  bool deadman = false;  // absent or false -> stop
};

// Pure-logic, allocation-free decoders for the small fixed-schema H5 request
// bodies. They never touch sockets; the comm layer reads the HTTP body and
// passes it here, then forwards the result to H5CommandHandler. A missing or
// malformed field makes the whole request invalid (fail safe) rather than
// guessing a value. Only flat top-level keys are supported, matching the spec.
JogRequest parseJogRequest(const char* body, size_t length);

// Decode POST /api/mode-request "requested_mode". Unknown values -> NONE.
H5ModeRequest parseModeRequest(const char* body, size_t length);

// Decode POST /api/reset-fault "confirm". Missing/malformed confirm -> false.
bool parseResetFaultRequest(const char* body, size_t length);

struct CalibrateRequest {
  bool valid = false;
  uint32_t deadband_mv = 0;
  uint32_t min_active_mv = 0;
  uint32_t max_mv = 0;
  uint32_t module_full_scale_mv = 0;
  uint32_t rise_mv_per_s = 0;
  uint32_t fall_mv_per_s = 0;
};

struct WizardRequest {
  bool valid = false;
  bool complete = false;
  bool estop_checked = false;
  bool wheels_lifted = false;
  bool direction_checked = false;
  bool throttle_checked = false;
};

CalibrateRequest parseCalibrateRequest(const char* body, size_t length);
WizardRequest parseWizardRequest(const char* body, size_t length);

// Decode POST /api/wifi {"ssid":"...","password":"..."}. ssid is mandatory
// (1..32 bytes); password is optional (open network) up to 64 bytes. Escaped
// characters other than \" and \\ invalidate the request (fail safe).
struct WifiConfigRequest {
  bool valid = false;
  char ssid[33] = {0};
  char password[65] = {0};
};

WifiConfigRequest parseWifiConfigRequest(const char* body, size_t length);

}  // namespace followbox
