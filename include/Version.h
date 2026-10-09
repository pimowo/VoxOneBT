#pragma once

#include <stdint.h>

#define VOXONE_FIRMWARE_VERSION "0.6.3-dev"
#define VOXONE_UART_PROTOCOL_VERSION 2

namespace Version {

constexpr char FIRMWARE[] = VOXONE_FIRMWARE_VERSION;
constexpr uint8_t PROTOCOL = VOXONE_UART_PROTOCOL_VERSION;

}  // namespace Version
