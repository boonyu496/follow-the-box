#pragma once

#include "app/command_pipeline.h"
#include "app/mode_manager.h"
#include "control/motion_mixer.h"
#include "control/obstacle_manager.h"
#include "core/system_state.h"
#include "safety/safety_manager.h"

namespace followbox {

class App {
 public:
  void begin();

  // Ingest the latest sensor-task snapshots into SystemState before tick().
  // Keeps App hardware-free: the sensor task owns UART/parsers and only hands
  // over parsed snapshots plus the heartbeats the safety watchdog consumes.
  // The fused obstacle snapshot drives the motion path; tof/ultrasonic/camera
  // are carried for telemetry only (camera is never a safety input).
  void ingestSensorInputs(const UwbTarget& uwb, const ObstacleSnapshot& obstacle,
                          const PowerStatus& power, const ImuSnapshot& imu,
                          const TofSnapshot& tof,
                          const SensorDiagnostics& sensor_diagnostics,
                          const UltrasonicSnapshot& ultrasonic,
                          const CameraStatus& camera,
                          bool estop_active,
                          uint32_t sensor_heartbeat_ms,
                          uint32_t uwb_heartbeat_ms);

  // Ingest the latest DS600 RC snapshot. Kept separate from sensor inputs
  // because the RC channels are a control source, not an environment sensor.
  void ingestRcInput(const RcInput& rc);

  // Ingest the latest H5 panel control snapshot (low-speed jog / mode request).
  void ingestH5Input(const H5ControlInput& h5);

  // Ingest the latest cloud control snapshot. Cloud control is treated as a
  // separate low-speed source so local H5 state and remote state never blur.
  void ingestCloudInput(const CloudControlInput& cloud);

  void tick(uint32_t now_ms);
  const SystemState& state() const { return state_; }

  // Seed the persisted install-wizard gate at boot (from ProfileStore). The
  // safety manager reads SystemState.install_wizard_complete to allow
  // AUTO_FOLLOW; call once after begin(), before the control task starts.
  void setInstallWizardComplete(bool complete) {
    state_.install_wizard_complete = complete;
  }

  void setThrottleCalibrated(bool calibrated) {
    state_.throttle_calibrated = calibrated;
  }

 private:
  SystemState state_;
  SafetyManager safety_manager_;
  ModeManager mode_manager_;
  CommandPipeline command_pipeline_;
  ObstacleManager obstacle_manager_;
  MotionMixer motion_mixer_;
};

}  // namespace followbox
