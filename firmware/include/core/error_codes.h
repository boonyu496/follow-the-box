#pragma once

namespace followbox {

enum class ErrorCode {
  NONE,
  ESTOP_ACTIVE,
  RC_LOST,
  H5_LOST,
  CLOUD_LOST,
  UWB_LOST,
  OBSTACLE_STOP,
  LOW_BATTERY,
  SENSOR_TIMEOUT,
  MOTOR_FAULT,
  INSTALL_WIZARD_NOT_DONE,
  WATCHDOG_TIMEOUT
};

}  // namespace followbox
