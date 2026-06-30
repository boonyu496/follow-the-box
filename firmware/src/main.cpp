#include <Arduino.h>

#include "app/runtime.h"

namespace {

followbox::Runtime runtime;

}  // namespace

void setup() {
  runtime.begin();
}

void loop() {
  runtime.loopIdle();
}
