#include "app/mode_manager.h"

#include <cmath>

namespace followbox {
namespace {

constexpr float kManualCommandDeadband = 0.04f;

bool canAutoFollow(const SystemState& state) {
  return state.install_wizard_complete && state.throttle_calibrated &&
         state.uwb.valid;
}

}  // namespace

RunMode ModeManager::selectMode(const SystemState& state,
                                const SafetyDecision& safety) const {
  if (safety.stop_reason == StopReason::ESTOP || state.estop_active) {
    return RunMode::ESTOP_ACTIVE;
  }

  if (safety.fault_latched) {
    return RunMode::FAULT_LOCKOUT;
  }

  if (state.mode == RunMode::BOOT_SELF_TEST) {
    return RunMode::SAFE_IDLE;
  }

  if (state.rc.online && !state.rc.stop_switch && rcHasManualCommand(state.rc)) {
    return RunMode::MANUAL_RC;
  }

  if (state.h5.safe_idle_request) {
    return RunMode::SAFE_IDLE;
  }

  if (state.cloud.safe_idle_request) {
    return RunMode::SAFE_IDLE;
  }

  if (state.mode == RunMode::AUTO_FOLLOW && canAutoFollow(state)) {
    return RunMode::AUTO_FOLLOW;
  }

  if (state.rc.auto_request && canAutoFollow(state)) {
    return RunMode::AUTO_FOLLOW;
  }

  if (state.h5.connected && state.h5.auto_request && canAutoFollow(state)) {
    return RunMode::AUTO_FOLLOW;
  }

  if (state.h5.connected && state.h5.unlock_request) {
    return RunMode::MANUAL_H5_LOW_SPEED;
  }

  if (state.cloud.connected && state.cloud.unlock_request) {
    return RunMode::MANUAL_CLOUD_LOW_SPEED;
  }

  return RunMode::SAFE_IDLE;
}

bool ModeManager::rcHasManualCommand(const RcInput& rc) const {
  return std::fabs(rc.throttle) > kManualCommandDeadband ||
         std::fabs(rc.steering) > kManualCommandDeadband;
}

}  // namespace followbox
