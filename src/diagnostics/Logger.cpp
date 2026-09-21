#include "diagnostics/Logger.h"

#include <Arduino.h>

namespace {

void logMessage(const char* level, const char* message) {
  Serial.print('[');
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);
}

}  // namespace

namespace Logger {

void info(const char* message) {
  logMessage("INFO", message);
}

void info(const char* message, const char* value) {
  Serial.print("[INFO] ");
  Serial.print(message);
  Serial.print(' ');
  Serial.println(value);
}

void info(const char* message, int value) {
  Serial.print("[INFO] ");
  Serial.print(message);
  Serial.print(' ');
  Serial.println(value);
}

void info(const char* message, uint32_t value, const char* unit) {
  Serial.print("[INFO] ");
  Serial.print(message);
  Serial.print(' ');
  Serial.print(value);
  Serial.print(' ');
  Serial.println(unit);
}

void warn(const char* message) {
  logMessage("WARN", message);
}

void error(const char* message) {
  logMessage("ERROR", message);
}

}  // namespace Logger
