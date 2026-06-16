#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Diagnostics for the forward TOF array. Telemetry only.
struct TofStats {
  uint32_t init_ok_mask = 0;       // bit c set when channel c initialised
  uint32_t read_count = 0;         // successful ranging reads (all channels)
  uint32_t timeout_count = 0;      // VL53L1X read timeouts
  uint32_t bus_clear_count = 0;    // I2C bus-clear recoveries attempted
  uint32_t last_read_ms = 0;
};

// Forward obstacle TOF array: TCA9548A I2C multiplexer + 3x VL53L1X.
//
// Wiring (CURRENT-WIRING-AI.md / ASSEMBLY-WIRING-MINDMAP): the three VL53L1X all
// share the 3.3V I2C bus (GPIO10/11, 4.7k pull-ups) behind a TCA9548A; channels
// map SD0 -> front-center, SD1 -> front-left, SD2 -> front-right. The first
// prototype has no XSHUT, so the bus is cleared on init / recovery instead of
// per-sensor reset.
//
// Ranging runs in non-blocking continuous mode: begin() initialises each sensor
// on its mux channel; update() round-robins one channel per call, reading only
// when data is ready so the sensor task never blocks. A channel that fails to
// init or times out keeps its *_valid flag low - a dead sensor must never read
// as "clear". Snapshot only: no GPIO/motion. Mounting geometry and any cross-talk
// between adjacent sensors MUST be confirmed on the bench before the data is
// trusted for motion.
class TofVl53l1xArray {
 public:
  void begin();

  // Service one channel (round-robin) and refresh the snapshot/timeouts.
  void update(uint32_t now_ms);

  const TofSnapshot& snapshot() const { return snapshot_; }
  const TofStats& stats() const { return stats_; }

 private:
  void applyChannelReading(uint8_t channel, int distance_mm, uint32_t now_ms);

  TofSnapshot snapshot_;
  TofStats stats_;
  uint8_t next_channel_ = 0;
  bool initialised_ = false;
};

}  // namespace followbox
