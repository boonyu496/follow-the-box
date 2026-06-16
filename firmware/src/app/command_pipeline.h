#pragma once

#include "control/follow_controller_uwb.h"
#include "core/system_state.h"

namespace followbox {

class CommandPipeline {
 public:
  MotionIntent buildIntent(const SystemState& state);

 private:
  FollowControllerUwb follow_controller_;
};

}  // namespace followbox

