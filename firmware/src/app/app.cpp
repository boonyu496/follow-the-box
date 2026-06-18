#include "app/app.h"

namespace followbox {

void App::begin() {
  state_ = SystemState{};
  state_.mode = RunMode::BOOT_SELF_TEST;
  state_.estop_active = true;
}

void App::ingestSensorInputs(const UwbTarget& uwb,
                             const ObstacleSnapshot& obstacle,
                             const PowerStatus& power, const ImuSnapshot& imu,
                             const TofSnapshot& tof,
                             const SensorDiagnostics& sensor_diagnostics,
                             const UltrasonicSnapshot& ultrasonic,
                             const CameraStatus& camera,
                             bool estop_active,
                             uint32_t sensor_heartbeat_ms,
                             uint32_t uwb_heartbeat_ms) {
  state_.uwb = uwb;
  state_.obstacle = obstacle;
  state_.power = power;
  state_.imu = imu;
  state_.tof = tof;
  state_.sensor_diagnostics = sensor_diagnostics;
  state_.ultrasonic = ultrasonic;
  state_.camera = camera;
  state_.estop_active = estop_active;
  state_.heartbeat.sensor_task_ms = sensor_heartbeat_ms;
  state_.heartbeat.uwb_task_ms = uwb_heartbeat_ms;
}

void App::ingestRcInput(const RcInput& rc) { state_.rc = rc; }

void App::ingestH5Input(const H5ControlInput& h5) { state_.h5 = h5; }

void App::ingestCloudInput(const CloudControlInput& cloud) {
  state_.cloud = cloud;
}

void App::tick(uint32_t now_ms) {
  state_.now_ms = now_ms;
  if (state_.h5.reset_fault_request) {
    safety_manager_.requestFaultReset();
  }
  state_.safety = safety_manager_.evaluate(state_);
  state_.mode = mode_manager_.selectMode(state_, state_.safety);
  state_.safety = safety_manager_.evaluate(state_);
  state_.intent = command_pipeline_.buildIntent(state_);
  const ObstacleDecision obstacle = obstacle_manager_.apply(state_.intent, state_.obstacle);
  state_.intent = obstacle.intent;
  MotorCommand proposed =
      motion_mixer_.mix(state_.intent, state_.safety.max_speed_scale, now_ms);
  state_.motor_command = safety_manager_.applyFinalGate(proposed, state_);
  // Only clear reset_fault_request if the fault was actually cleared
  // (CODE-REVIEW-H5-2026-06-15 P0-3). Otherwise the request persists
  // for the next tick, avoiding the "consumed but rejected" race.
  if (!state_.safety.fault_latched) {
    state_.h5.reset_fault_request = false;
  }
  state_.h5.safe_idle_request = false;
  state_.cloud.safe_idle_request = false;
}

}  // namespace followbox
