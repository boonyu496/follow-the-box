#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

class DriveAdapter {
 public:
  virtual ~DriveAdapter() = default;
  virtual bool begin() = 0;
  virtual void writeCommand(const MotorCommand& command, uint32_t now_ms) = 0;
  virtual void stopNow() = 0;
};

}  // namespace followbox

