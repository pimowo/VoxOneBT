#pragma once

#include <stdint.h>

namespace Logger {

void info(const char* message);
void info(const char* message, const char* value);
void info(const char* message, int value);
void info(const char* message, uint32_t value, const char* unit);
void warn(const char* message);
void error(const char* message);

}  // namespace Logger
