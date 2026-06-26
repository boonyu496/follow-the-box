#pragma once

#include <cstdint>

namespace followbox {

enum class RunMode {
  BOOT_SELF_TEST,
  SAFE_IDLE,
  MANUAL_RC,
  MANUAL_H5_LOW_SPEED,
  MANUAL_CLOUD_LOW_SPEED,
  AUTO_FOLLOW,
  FAULT_LOCKOUT,
  ESTOP_ACTIVE
};

enum class StopReason {
  NONE,
  ESTOP,
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

enum class ControlSource {
  NONE,
  DS600_RC,
  H5_LOCAL,
  CLOUD_REMOTE,
  UWB_FOLLOW
};

struct RcInput {
  bool online = false;
  uint32_t last_update_ms = 0;
  uint16_t ch_us[6] = {0, 0, 0, 0, 0, 0};
  uint32_t ch_age_ms[6] = {0, 0, 0, 0, 0, 0};
  float throttle = 0.0f;
  float steering = 0.0f;
  float speed_limit = 0.0f;
  bool stop_switch = false;
  bool auto_request = false;
};

struct H5ControlInput {
  bool connected = false;
  uint32_t last_update_ms = 0;
  bool unlock_request = false;
  bool auto_request = false;
  bool safe_idle_request = false;
  bool reset_fault_request = false;
  float throttle = 0.0f;
  float steering = 0.0f;

  // Deferred operations — set by transport layer, executed by main.cpp
  // in the control-loop context (CODE-REVIEW-H5-2026-06-15 P0-1).
  bool pending_calibrate = false;
  int cal_deadband_mv = 0;
  int cal_min_active_mv = 0;
  int cal_max_mv = 0;
  int cal_module_full_scale_mv = 0;
  int cal_rise_mv_per_s = 0;
  int cal_fall_mv_per_s = 0;
  bool pending_wizard_complete = false;
};

struct CloudControlInput {
  bool connected = false;
  uint32_t last_update_ms = 0;
  uint32_t last_seq = 0;
  bool unlock_request = false;
  bool safe_idle_request = false;
  float throttle = 0.0f;
  float steering = 0.0f;
};

struct UwbTarget {
  bool valid = false;
  uint32_t last_update_ms = 0;
  int distance_mm = 0;
  float bearing_deg = 0.0f;
  uint8_t confidence = 0;
};

struct ObstacleSnapshot {
  bool valid = false;
  uint32_t last_update_ms = 0;
  int front_left_mm = 0;
  int front_center_mm = 0;
  int front_right_mm = 0;
  int side_left_mm = 0;
  int side_right_mm = 0;
};

struct ImuSnapshot {
  bool valid = false;
  uint32_t last_update_ms = 0;
  float yaw_deg = 0.0f;
  float yaw_rate_dps = 0.0f;
  float pitch_deg = 0.0f;
  float roll_deg = 0.0f;
};

// Forward TOF array (TCA9548A + 3x VL53L1X). Read-only ranging snapshot consumed
// by telemetry and (in a later safety-reviewed step) the obstacle limiter. A
// channel that fails to range keeps its *_valid flag low instead of faking a
// distance, so a dead sensor never reads as "clear".
struct TofSnapshot {
  bool valid = false;  // true once at least one channel ranged this cycle
  uint32_t last_update_ms = 0;
  int front_left_mm = 0;
  int front_center_mm = 0;
  int front_right_mm = 0;
  bool front_left_valid = false;
  bool front_center_valid = false;
  bool front_right_valid = false;
};

// Read-only bring-up evidence. These counters are deliberately separate from
// the fused obstacle snapshot so the H5 panel can distinguish a dead sensor
// from a genuinely clear sector without affecting motion decisions.
struct SensorDiagnostics {
  bool lidar_valid = false;
  uint32_t lidar_last_update_ms = 0;
  int lidar_front_left_mm = 0;
  int lidar_front_center_mm = 0;
  int lidar_front_right_mm = 0;
  int lidar_side_left_mm = 0;
  int lidar_side_right_mm = 0;
  uint32_t lidar_rx_bytes = 0;
  uint32_t lidar_packets = 0;
  uint32_t lidar_checksum_errors = 0;
  uint32_t lidar_framing_errors = 0;
  uint32_t lidar_scans = 0;

  uint32_t tof_init_ok_mask = 0;
  uint32_t tof_init_attempt_count = 0;
  uint32_t tof_init_failure_count = 0;
  uint32_t tof_read_count = 0;
  uint32_t tof_timeout_count = 0;
  uint32_t tof_mux_nack_count = 0;
  uint32_t tof_bus_clear_count = 0;
  uint32_t tof_reinit_count = 0;
  uint32_t tof_last_recovery_ms = 0;
};

// Side ultrasonic pair (two HC-SR04 sharing one TRIG). Auxiliary side-clearance
// snapshot only; a missing echo keeps the matching *_valid flag low.
struct UltrasonicSnapshot {
  bool valid = false;
  uint32_t last_update_ms = 0;
  int left_mm = 0;
  int right_mm = 0;
  bool left_valid = false;
  bool right_valid = false;
};

// ESP32-S3-CAM video link status. Display/telemetry only - per FIRMWARE-SPEC the
// camera is never a safety input, so video loss must NOT affect the motion gate.
struct CameraStatus {
  bool online = false;
  uint32_t last_update_ms = 0;
};

struct PowerStatus {
  bool valid = false;
  uint32_t last_update_ms = 0;
  float battery_voltage = 0.0f;
  bool low_battery = false;
  bool motor_fault_left = false;
  bool motor_fault_right = false;
};

struct SafetyDecision {
  bool motion_allowed = false;
  bool fault_latched = false;
  float max_speed_scale = 0.0f;
  StopReason stop_reason = StopReason::NONE;
};

struct MotionIntent {
  ControlSource source = ControlSource::NONE;
  bool request_motion = false;
  float forward = 0.0f;
  float turn = 0.0f;
};

struct MotorCommand {
  bool enable = false;
  bool brake = true;
  float left_target = 0.0f;
  float right_target = 0.0f;
  bool left_reverse = false;
  bool right_reverse = false;
};

struct TaskHeartbeat {
  uint32_t sensor_task_ms = 0;
  uint32_t uwb_task_ms = 0;
  uint32_t comm_task_ms = 0;
};

}  // namespace followbox
