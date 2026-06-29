#include "safety/safety_manager.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

float clampUnit(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

constexpr float kForwardObstacleDeadband = 0.04f;

MotorCommand stoppedCommand(bool brake) {
  MotorCommand command;
  command.enable = false;
  command.brake = brake;
  command.left_target = 0.0f;
  command.right_target = 0.0f;
  command.left_reverse = false;
  command.right_reverse = false;
  return command;
}

bool currentModeRequiresMotionSource(RunMode mode) {
  return mode == RunMode::MANUAL_RC || mode == RunMode::MANUAL_H5_LOW_SPEED ||
         mode == RunMode::MANUAL_CLOUD_LOW_SPEED || mode == RunMode::AUTO_FOLLOW;
}

bool hasMotionRequest(const SystemState& state) {
  return std::fabs(state.rc.throttle) > 0.04f || std::fabs(state.rc.steering) > 0.04f ||
         state.h5.unlock_request || state.cloud.unlock_request ||
         state.h5.auto_request || state.rc.auto_request;
}

bool hasFrontObstacleReading(const ObstacleSnapshot& obstacle) {
  return obstacle.front_left_mm > 0 || obstacle.front_center_mm > 0 ||
         obstacle.front_right_mm > 0;
}

}  // namespace

SafetyDecision SafetyManager::evaluate(const SystemState& state) {
  SafetyDecision decision;
  decision.profile = safetyProfileForMode(state.mode);
  decision.max_speed_scale = speedScaleForMode(state.mode);

  if (applyHardGate(state, decision)) {
    return decision;
  }

  if (applyModeGate(state, decision)) {
    return decision;
  }

  decision.motion_allowed = currentModeRequiresMotionSource(state.mode);
  if (!decision.motion_allowed) {
    decision.max_speed_scale = 0.0f;
  }
  return decision;
}

void SafetyManager::requestFaultReset() { reset_requested_ = true; }

MotorCommand SafetyManager::applyFinalGate(const MotorCommand& proposed,
                                           const SystemState& state) const {
  const SafetyDecision& safety = state.safety;
  if (!safety.motion_allowed || safety.fault_latched ||
      safety.stop_reason != StopReason::NONE) {
    return stoppedCommand(true);
  }

  MotorCommand gated = proposed;
  if (!gated.enable || gated.brake) {
    return stoppedCommand(gated.brake);
  }

  gated.left_target = clampUnit(gated.left_target);
  gated.right_target = clampUnit(gated.right_target);
  gated.left_reverse = gated.left_target < 0.0f;
  gated.right_reverse = gated.right_target < 0.0f;
  return gated;
}

bool SafetyManager::applyHardGate(const SystemState& state,
                                  SafetyDecision& decision) {
  StopReason active_latched_reason = StopReason::NONE;
  if (hasActiveLatchedFault(state, active_latched_reason)) {
    fault_latched_ = true;
    latched_reason_ = active_latched_reason;
  } else if (fault_latched_ && reset_requested_ && canClearLatchedFault(state)) {
    fault_latched_ = false;
    latched_reason_ = StopReason::NONE;
  }
  reset_requested_ = false;

  if (fault_latched_) {
    decision.fault_latched = true;
    decision.stop_reason = latched_reason_;
    return true;
  }
  return false;
}

bool SafetyManager::applyModeGate(const SystemState& state,
                                  SafetyDecision& decision) const {
  if (state.mode == RunMode::AUTO_FOLLOW &&
      (!state.install_wizard_complete || !state.throttle_calibrated)) {
    decision.stop_reason = StopReason::INSTALL_WIZARD_NOT_DONE;
    return true;
  }

  if (hasAutoObstacleTimeout(state)) {
    decision.stop_reason = StopReason::SENSOR_TIMEOUT;
    return true;
  }

  if (frontObstacleBlocksCurrentCommand(state)) {
    decision.stop_reason = StopReason::OBSTACLE_STOP;
    return true;
  }

  if (state.mode == RunMode::MANUAL_RC &&
      (!state.rc.online ||
       isStale(state.now_ms, state.rc.last_update_ms, profile::PHYSICAL_REMOTE_LOST_STOP_MS) ||
       state.rc.stop_switch)) {
    decision.stop_reason = StopReason::RC_LOST;
    return true;
  }

  if (state.mode == RunMode::MANUAL_H5_LOW_SPEED &&
      (!state.h5.connected ||
       isStale(state.now_ms, state.h5.last_update_ms, profile::H5_LOST_STOP_MS))) {
    decision.stop_reason = StopReason::H5_LOST;
    return true;
  }

  if (state.mode == RunMode::MANUAL_CLOUD_LOW_SPEED &&
      (!state.cloud.connected ||
       isStale(state.now_ms, state.cloud.last_update_ms,
               profile::CLOUD_LOST_STOP_MS))) {
    decision.stop_reason = StopReason::CLOUD_LOST;
    return true;
  }

  if (state.mode == RunMode::AUTO_FOLLOW &&
      (!state.uwb.valid ||
       isStale(state.now_ms, state.uwb.last_update_ms, profile::UWB_STALE_STOP_MS))) {
    decision.stop_reason = StopReason::UWB_LOST;
    return true;
  }
  return false;
}

bool SafetyManager::hasStopObstacle(const ObstacleSnapshot& obstacle) const {
  if (!obstacle.valid) {
    return false;
  }

  const int distances[] = {
      obstacle.front_left_mm,
      obstacle.front_center_mm,
      obstacle.front_right_mm,
  };

  for (int distance : distances) {
    if (distance > 0 && distance < profile::OBSTACLE_STOP_DISTANCE_MM) {
      return true;
    }
  }
  return false;
}

bool SafetyManager::frontObstacleBlocksCurrentCommand(const SystemState& state) const {
  if (!hasStopObstacle(state.obstacle)) {
    return false;
  }

  switch (state.mode) {
    case RunMode::MANUAL_RC:
      return state.rc.throttle > kForwardObstacleDeadband;
    case RunMode::MANUAL_H5_LOW_SPEED:
      return state.h5.throttle > kForwardObstacleDeadband;
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      return state.cloud.throttle > kForwardObstacleDeadband;
    case RunMode::AUTO_FOLLOW:
      return true;
    default:
      return false;
  }
}

bool SafetyManager::hasActiveLatchedFault(const SystemState& state,
                                          StopReason& reason) const {
  if (state.estop_active) {
    reason = StopReason::ESTOP;
    return true;
  }
  if (state.power.motor_fault_left || state.power.motor_fault_right) {
    reason = StopReason::MOTOR_FAULT;
    return true;
  }
  if (hasCriticalHeartbeatTimeout(state)) {
    reason = StopReason::WATCHDOG_TIMEOUT;
    return true;
  }
  if (state.power.low_battery) {
    reason = StopReason::LOW_BATTERY;
    return true;
  }
  reason = StopReason::NONE;
  return false;
}

bool SafetyManager::canClearLatchedFault(const SystemState& state) const {
  StopReason reason = StopReason::NONE;
  return !hasActiveLatchedFault(state, reason) && !hasMotionRequest(state);
}

bool SafetyManager::hasAutoObstacleTimeout(const SystemState& state) const {
  return state.mode == RunMode::AUTO_FOLLOW &&
         (!state.obstacle.valid ||
          !hasFrontObstacleReading(state.obstacle) ||
          isStale(state.now_ms, state.obstacle.last_update_ms,
                  profile::OBSTACLE_STALE_TIMEOUT_MS));
}

bool SafetyManager::hasCriticalHeartbeatTimeout(const SystemState& state) const {
  const uint32_t timeout = profile::TASK_HEARTBEAT_TIMEOUT_MS;
  const uint32_t boot_grace = profile::TASK_HEARTBEAT_BOOT_GRACE_MS;
  if (state.now_ms <= boot_grace) {
    return false;
  }

  const bool sensor_started = state.heartbeat.sensor_task_ms != 0;
  const bool sensor_timeout = sensor_started
      ? (elapsedMs(state.now_ms, state.heartbeat.sensor_task_ms) > timeout)
      : true;

  const bool uwb_started = state.heartbeat.uwb_task_ms != 0;
  const bool uwb_timeout = uwb_started
      ? (elapsedMs(state.now_ms, state.heartbeat.uwb_task_ms) > timeout)
      : true;

  return sensor_timeout || uwb_timeout;
}

SafetyProfile SafetyManager::safetyProfileForMode(RunMode mode) const {
  switch (mode) {
    case RunMode::MANUAL_RC:
      return SafetyProfile::LocalManual;
    case RunMode::MANUAL_H5_LOW_SPEED:
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      return SafetyProfile::RemoteManual;
    case RunMode::AUTO_FOLLOW:
      return SafetyProfile::Autonomous;
    default:
      return SafetyProfile::RemoteManual;
  }
}

float SafetyManager::speedScaleForMode(RunMode mode) const {
  switch (mode) {
    case RunMode::MANUAL_RC:
      return profile::REMOTE_MAX_SPEED_SCALE;
    case RunMode::MANUAL_H5_LOW_SPEED:
      return profile::H5_MAX_SPEED_SCALE;
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      return profile::CLOUD_MAX_SPEED_SCALE;
    case RunMode::AUTO_FOLLOW:
      return profile::AUTO_FOLLOW_MAX_SPEED_SCALE;
    default:
      return 0.0f;
  }
}

}  // namespace followbox
