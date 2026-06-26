#include "telemetry/telemetry_logger.h"

#include "core/time_utils.h"
#include "telemetry/debug_console.h"

namespace followbox {
namespace {

const char* modeName(RunMode mode) {
  switch (mode) {
    case RunMode::BOOT_SELF_TEST:
      return "BOOT";
    case RunMode::SAFE_IDLE:
      return "IDLE";
    case RunMode::MANUAL_RC:
      return "RC";
    case RunMode::MANUAL_H5_LOW_SPEED:
      return "H5";
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      return "CLOUD";
    case RunMode::AUTO_FOLLOW:
      return "AUTO";
    case RunMode::FAULT_LOCKOUT:
      return "FAULT";
    case RunMode::ESTOP_ACTIVE:
      return "ESTOP";
  }
  return "?";
}

const char* stopName(StopReason reason) {
  switch (reason) {
    case StopReason::NONE:
      return "NONE";
    case StopReason::ESTOP:
      return "ESTOP";
    case StopReason::RC_LOST:
      return "RC_LOST";
    case StopReason::H5_LOST:
      return "H5_LOST";
    case StopReason::CLOUD_LOST:
      return "CLOUD_LOST";
    case StopReason::UWB_LOST:
      return "UWB_LOST";
    case StopReason::OBSTACLE_STOP:
      return "OBSTACLE";
    case StopReason::LOW_BATTERY:
      return "LOW_BATT";
    case StopReason::SENSOR_TIMEOUT:
      return "SENSOR_TO";
    case StopReason::MOTOR_FAULT:
      return "MOTOR_FLT";
    case StopReason::INSTALL_WIZARD_NOT_DONE:
      return "NO_WIZARD";
    case StopReason::WATCHDOG_TIMEOUT:
      return "WDT";
  }
  return "?";
}

}  // namespace

void TelemetryLogger::begin(uint32_t period_ms) {
  period_ms_ = period_ms == 0 ? 1000 : period_ms;
  last_emit_ms_ = 0;
  primed_ = false;
}

void TelemetryLogger::update(const SystemState& state, uint32_t now_ms) {
  const bool transition =
      !primed_ || state.mode != last_mode_ || state.safety.stop_reason != last_stop_;
  const bool periodic =
      !primed_ || elapsedMs(now_ms, last_emit_ms_) >= period_ms_;

  if (!transition && !periodic) {
    return;
  }

  emit(state);
  last_emit_ms_ = now_ms;
  last_mode_ = state.mode;
  last_stop_ = state.safety.stop_reason;
  primed_ = true;
}

void TelemetryLogger::emit(const SystemState& state) {
  const MotorCommand& mc = state.motor_command;
  const RcInput& rc = state.rc;
  const uint32_t rc_age_ms =
      elapsedMsClamped(state.now_ms, rc.last_update_ms);
  FB_LOGI(
      "TLM mode=%s stop=%s en=%d brk=%d L=%.2f%c R=%.2f%c scale=%.2f "
      "batt=%.1f estop=%d wiz=%d uwb=%d/%dmm "
      "rc=%d age=%lu ch=%u/%u/%u/%u/%u ch_age=%lu/%lu/%lu/%lu/%lu "
      "thr=%.2f str=%.2f spd=%.2f stop=%d auto=%d "
      "lidar=%d rx=%lu pkt=%lu scan=%lu ce=%lu fe=%lu "
      "tof=0x%lx/%lu init=%lu/%lu nack=%lu to=%lu busclr=%lu reinit=%lu",
      modeName(state.mode), stopName(state.safety.stop_reason), mc.enable ? 1 : 0,
      mc.brake ? 1 : 0, static_cast<double>(mc.left_target),
      mc.left_reverse ? 'R' : 'F', static_cast<double>(mc.right_target),
      mc.right_reverse ? 'R' : 'F',
      static_cast<double>(state.safety.max_speed_scale),
      static_cast<double>(state.power.battery_voltage),
      state.estop_active ? 1 : 0, state.install_wizard_complete ? 1 : 0,
      state.uwb.valid ? 1 : 0, state.uwb.distance_mm,
      rc.online ? 1 : 0, static_cast<unsigned long>(rc_age_ms),
      static_cast<unsigned>(rc.ch_us[0]), static_cast<unsigned>(rc.ch_us[1]),
      static_cast<unsigned>(rc.ch_us[2]), static_cast<unsigned>(rc.ch_us[3]),
      static_cast<unsigned>(rc.ch_us[4]),
      static_cast<unsigned long>(rc.ch_age_ms[0]),
      static_cast<unsigned long>(rc.ch_age_ms[1]),
      static_cast<unsigned long>(rc.ch_age_ms[2]),
      static_cast<unsigned long>(rc.ch_age_ms[3]),
      static_cast<unsigned long>(rc.ch_age_ms[4]),
      static_cast<double>(rc.throttle),
      static_cast<double>(rc.steering), static_cast<double>(rc.speed_limit),
      rc.stop_switch ? 1 : 0, rc.auto_request ? 1 : 0,
      state.sensor_diagnostics.lidar_valid ? 1 : 0,
      static_cast<unsigned long>(state.sensor_diagnostics.lidar_rx_bytes),
      static_cast<unsigned long>(state.sensor_diagnostics.lidar_packets),
      static_cast<unsigned long>(state.sensor_diagnostics.lidar_scans),
      static_cast<unsigned long>(
          state.sensor_diagnostics.lidar_checksum_errors),
      static_cast<unsigned long>(
          state.sensor_diagnostics.lidar_framing_errors),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_init_ok_mask),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_read_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_init_attempt_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_init_failure_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_mux_nack_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_timeout_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_bus_clear_count),
      static_cast<unsigned long>(state.sensor_diagnostics.tof_reinit_count));
}

}  // namespace followbox
