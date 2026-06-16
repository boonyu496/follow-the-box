#pragma once

#include <cstddef>

#include "core/system_state.h"

namespace followbox {

// Pure-logic helpers backing the H5 transport layer. They never touch sockets,
// WiFi, GPIO or the motion path: the comm layer calls these to turn a
// SystemState snapshot into the frozen /ws/state JSON (H5-API.md), and to map
// the project enums to the exact strings the panel expects.
const char* modeToString(RunMode mode);
const char* stopReasonToString(StopReason reason);

// Serialise the frozen H5 state JSON into the caller-provided buffer. Returns
// the number of characters written (excluding the NUL), or 0 if the buffer is
// too small. No heap allocation, safe to call from comm_task with a stack
// buffer. Recommended buffer size: >= 1280 bytes.
size_t buildStateJson(const SystemState& state, char* out, size_t out_size);

}  // namespace followbox
