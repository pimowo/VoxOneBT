#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AppConfig {

constexpr uint32_t USB_SERIAL_BAUD = 115200;
constexpr uint32_t UART_BAUD = 115200;
constexpr size_t UART_MAX_LINE_LENGTH = 64;
constexpr char BLUETOOTH_NAME_PREFIX[] = "VoxOneBT-";
constexpr size_t BLUETOOTH_AUTO_NAME_SIZE =
    sizeof(BLUETOOTH_NAME_PREFIX) + 6;

}  // namespace AppConfig
