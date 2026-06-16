#include "app/command_pipeline.h"

#include "core/math_utils.h"

namespace followbox {

MotionIntent CommandPipeline::buildIntent(const SystemState& state) {
  MotionIntent intent;
  switch (state.mode) {
    case RunMode::MANUAL_RC:
      intent.source = ControlSource::DS600_RC;
      intent.request_motion = true;
      intent.forward = clampUnit(state.rc.throttle);
      intent.turn = clampUnit(state.rc.steering);
      break;
    case RunMode::MANUAL_H5_LOW_SPEED:
      intent.source = ControlSource::H5_LOCAL;
      intent.request_motion = state.h5.connected;
      intent.forward = clampUnit(state.h5.throttle);
      intent.turn = clampUnit(state.h5.steering);
      break;
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      intent.source = ControlSource::CLOUD_REMOTE;
      intent.request_motion = state.cloud.connected;
      intent.forward = clampUnit(state.cloud.throttle);
      intent.turn = clampUnit(state.cloud.steering);
      break;
    case RunMode::AUTO_FOLLOW:
      // Pure-logic UWB follow policy. The safety manager still gates this intent
      // (install wizard, UWB-lost, obstacle, e-stop) before any motor output.
      intent = follow_controller_.update(state.uwb, state.imu, state.now_ms);
      break;
    default:
      follow_controller_.reset();
      break;
  }
  return intent;
}

}  // namespace followbox
