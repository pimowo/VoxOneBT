#include <Arduino.h>

#include "AppConfig.h"
#include "core/App.h"

namespace {
App app;
}

void setup() {
  Serial.begin(AppConfig::USB_SERIAL_BAUD);
  app.begin();
}

void loop() {
  app.loop();
}
