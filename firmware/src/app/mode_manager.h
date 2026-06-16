#pragma once

#include "core/system_state.h"

namespace followbox {

class ModeManager {
 public:
  RunMode selectMode(const SystemState& state, const SafetyDecision& safety) const;

 private:
  bool rcHasManualCommand(const RcInput& rc) const;
};

}  // namespace followbox

