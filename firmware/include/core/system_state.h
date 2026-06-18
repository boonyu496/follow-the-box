#pragma once

#include "core/types.h"

namespace followbox {

struct SystemState {
  RunMode mode = RunMode::BOOT_SELF_TEST;
  RcInput rc;
  H5ControlInput h5;
  CloudControlInput cloud;
  UwbTarget uwb;
  ObstacleSnapshot obstacle;
  TofSnapshot tof;
  SensorDiagnostics sensor_diagnostics;
  UltrasonicSnapshot ultrasonic;
  CameraStatus camera;
  ImuSnapshot imu;
  PowerStatus power;
  SafetyDecision safety;
  MotionIntent intent;
  MotorCommand motor_command;
  TaskHeartbeat heartbeat;
  bool estop_active = true;
  bool install_wizard_complete = false;
  bool throttle_calibrated = false;
  uint32_t now_ms = 0;
};

}  // namespace followbox
