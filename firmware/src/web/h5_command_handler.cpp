#include "web/h5_command_handler.h"

#include <algorithm>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {

float H5CommandHandler::clampUnit(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

void H5CommandHandler::reset() {
  input_ = H5ControlInput{};
  last_seq_ = 0;
  have_seq_ = false;
  pending_calibrate_ = false;
  pending_wizard_ = false;
}

void H5CommandHandler::stopMotion() {
  input_.throttle = 0.0f;
  input_.steering = 0.0f;
  input_.unlock_request = false;
}

void H5CommandHandler::onConnect(uint32_t now_ms) {
  input_ = H5ControlInput{};
  input_.connected = true;
  input_.last_update_ms = now_ms;
  // A fresh connection never starts moving: a deadman-held jog must arrive first.
  last_seq_ = 0;
  have_seq_ = false;
}

void H5CommandHandler::onDisconnect() {
  input_ = H5ControlInput{};
  last_seq_ = 0;
  have_seq_ = false;
}

void H5CommandHandler::onModeRequest(H5ModeRequest request, uint32_t now_ms) {
  if (!input_.connected) {
    return;
  }
  input_.last_update_ms = now_ms;
  switch (request) {
    case H5ModeRequest::AUTO_FOLLOW_REQUEST:
      // Only a request; mode_manager validates wizard/UWB. Leaving manual jog.
      input_.auto_request = true;
      input_.safe_idle_request = false;
      stopMotion();
      break;
    case H5ModeRequest::MANUAL_H5_LOW_SPEED:
      input_.auto_request = false;
      input_.safe_idle_request = false;
      break;
    case H5ModeRequest::SAFE_IDLE:
      input_.auto_request = false;
      input_.safe_idle_request = true;
      stopMotion();
      break;
    case H5ModeRequest::NONE:
    default:
      break;
  }
}

bool H5CommandHandler::onJog(uint32_t seq, float forward, float turn,
                             bool deadman, uint32_t now_ms) {
  if (!input_.connected) {
    return false;
  }
  // Replay / out-of-order protection: only strictly newer sequences move us.
  if (have_seq_ && seq <= last_seq_) {
    return false;
  }
  last_seq_ = seq;
  have_seq_ = true;
  input_.last_update_ms = now_ms;

  if (!deadman) {
    stopMotion();
    return true;
  }

  input_.auto_request = false;
  input_.safe_idle_request = false;
  input_.unlock_request = true;
  input_.throttle = clampUnit(forward);
  input_.steering = clampUnit(turn);
  return true;
}

bool H5CommandHandler::onResetFault(bool confirm, uint32_t now_ms) {
  if (!input_.connected || !confirm) {
    return false;
  }
  input_.last_update_ms = now_ms;
  stopMotion();
  input_.auto_request = false;
  input_.reset_fault_request = true;
  return true;
}

void H5CommandHandler::update(uint32_t now_ms) {
  if (!input_.connected) {
    return;
  }
  if (isStale(now_ms, input_.last_update_ms, profile::H5_LOST_STOP_MS)) {
    stopMotion();
  }
}

bool H5CommandHandler::onCalibrate(const ThrottleCalibration& cal, uint32_t now_ms) {
  if (!input_.connected) {
    return false;
  }
  input_.last_update_ms = now_ms;
  pending_cal_data_ = cal;
  pending_calibrate_ = true;
  return true;
}

bool H5CommandHandler::onWizardComplete(bool complete, uint32_t now_ms) {
  if (!input_.connected) {
    return false;
  }
  input_.last_update_ms = now_ms;
  pending_wizard_data_ = complete;
  pending_wizard_ = true;
  return true;
}

void H5CommandHandler::clearOneShotRequests() {
  input_.reset_fault_request = false;
  input_.safe_idle_request = false;
}

}  // namespace followbox
