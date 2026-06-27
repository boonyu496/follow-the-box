#pragma once

#include "core/system_state.h"

namespace followbox {

class SafetyManager {
 public:
  SafetyDecision evaluate(const SystemState& state);
  MotorCommand applyFinalGate(const MotorCommand& proposed,
                              const SystemState& state) const;
  void requestFaultReset();

 private:
  bool applyHardGate(const SystemState& state, SafetyDecision& decision);
  bool applyModeGate(const SystemState& state, SafetyDecision& decision) const;
  bool hasActiveLatchedFault(const SystemState& state, StopReason& reason) const;
  bool canClearLatchedFault(const SystemState& state) const;
  bool hasStopObstacle(const ObstacleSnapshot& obstacle) const;
  bool frontObstacleBlocksCurrentCommand(const SystemState& state) const;
  bool hasAutoObstacleTimeout(const SystemState& state) const;
  bool hasCriticalHeartbeatTimeout(const SystemState& state) const;
  SafetyProfile safetyProfileForMode(RunMode mode) const;
  float speedScaleForMode(RunMode mode) const;

  bool fault_latched_ = false;
  StopReason latched_reason_ = StopReason::NONE;
  bool reset_requested_ = false;
};

}  // namespace followbox
