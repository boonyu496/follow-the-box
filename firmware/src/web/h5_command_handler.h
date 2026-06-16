#pragma once

#include <cstdint>

#include "core/types.h"
#include "drive/drive_adapter_analog_bldc.h"

namespace followbox {

// Mode requests the H5 panel may issue (H5-API.md POST /api/mode-request).
// AUTO_FOLLOW is only a *request*; mode_manager still checks install wizard,
// UWB validity and DS600 takeover before honouring it.
enum class H5ModeRequest {
  NONE,
  SAFE_IDLE,
  MANUAL_H5_LOW_SPEED,
  AUTO_FOLLOW_REQUEST,
};

// Pure-logic H5 command handler: turns decoded panel events into the
// H5ControlInput snapshot consumed by mode_manager / command_pipeline.
//
// Transport-free by design: the Arduino WebServer/WebSocket/JSON layer decodes
// frames and calls these methods; this class holds no sockets and never touches
// GPIO or the motion path. Per H5-API.md it can only request low-speed jog and
// modes - it can never set PWM, clear the physical e-stop, or skip the wizard.
// Speed is clamped to -1..1 here; H5_MAX_SPEED_SCALE is applied downstream.
class H5CommandHandler {
 public:
  void reset();

  // WebSocket lifecycle from the comm layer.
  void onConnect(uint32_t now_ms);
  void onDisconnect();

  // POST /api/mode-request.
  void onModeRequest(H5ModeRequest request, uint32_t now_ms);

  // POST /api/reset-fault. This only raises a one-shot software reset request;
  // SafetyManager decides whether the latched fault may actually clear.
  bool onResetFault(bool confirm, uint32_t now_ms);

  // POST /api/jog. Returns false when the command is ignored (stale/replayed
  // seq or no active connection); deadman=false forces a stop.
  bool onJog(uint32_t seq, float forward, float turn, bool deadman,
             uint32_t now_ms);

  // POST /api/calibrate and /api/wizard-complete
  bool onCalibrate(const ThrottleCalibration& cal, uint32_t now_ms);
  bool onWizardComplete(bool complete, uint32_t now_ms);

  bool hasPendingCalibrate() const { return pending_calibrate_; }
  const ThrottleCalibration& getPendingCalibrate() const { return pending_cal_data_; }
  void clearPendingCalibrate() { pending_calibrate_ = false; }

  bool hasPendingWizard() const { return pending_wizard_; }
  bool getPendingWizard() const { return pending_wizard_data_; }
  void clearPendingWizard() { pending_wizard_ = false; }

  // Apply the staleness timeout (no fresh jog/mode within H5_LOST_STOP_MS
  // zeroes motion); safety_manager still gates on connection + staleness.
  void update(uint32_t now_ms);
  void clearOneShotRequests();

  const H5ControlInput& input() const { return input_; }

 private:
  static float clampUnit(float value);
  void stopMotion();

  H5ControlInput input_;
  uint32_t last_seq_ = 0;
  bool have_seq_ = false;

  bool pending_calibrate_ = false;
  ThrottleCalibration pending_cal_data_;
  bool pending_wizard_ = false;
  bool pending_wizard_data_ = false;
};

}  // namespace followbox
