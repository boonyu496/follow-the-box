#include "app/shared_state.h"

#include <Arduino.h>

namespace followbox {
namespace {

// Separate spinlocks so a sensor publish (Core 0) and a state publish (Core 1)
// never block each other. Each guards only a single struct copy.
portMUX_TYPE g_sensor_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;

SensorBundle g_sensors;
SystemState g_state;

}  // namespace

void SharedState::publishSensors(const SensorBundle& bundle) {
  portENTER_CRITICAL(&g_sensor_mux);
  g_sensors = bundle;
  portEXIT_CRITICAL(&g_sensor_mux);
}

SensorBundle SharedState::latestSensors() const {
  SensorBundle copy;
  portENTER_CRITICAL(&g_sensor_mux);
  copy = g_sensors;
  portEXIT_CRITICAL(&g_sensor_mux);
  return copy;
}

void SharedState::publishState(const SystemState& state) {
  portENTER_CRITICAL(&g_state_mux);
  g_state = state;
  portEXIT_CRITICAL(&g_state_mux);
}

SystemState SharedState::latestState() const {
  SystemState copy;
  portENTER_CRITICAL(&g_state_mux);
  copy = g_state;
  portEXIT_CRITICAL(&g_state_mux);
  return copy;
}

}  // namespace followbox
