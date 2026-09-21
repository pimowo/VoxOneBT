# Hardware

Use a classic ESP32 such as ESP32-WROOM-32, ESP32-WROOM-32U, or a compatible
D1 Mini ESP32. Do not use ESP32-S3, ESP32-C3, or an ESP32-WROVER variant that
uses GPIO16/17 for PSRAM.

## UART connection

Both sides use 3.3 V logic. Power off the boards while making connections.

| VoxOneBT | Direction | VoxOne MAIN |
|---|---:|---|
| GPIO17 (TX) | -> | GPIO16 (RX) |
| GPIO16 (RX) | <- | GPIO17 (TX) |
| GND | <-> | GND |

USB Serial is a separate diagnostic channel at 115200 baud. The inter-board
UART is hardware UART2 at 115200 baud, 8N1.

## I2S connection

With both boards powered off, connect VoxOneBT BCLK GPIO26 to MAIN GPIO21,
VoxOneBT WS GPIO25 to MAIN GPIO22, VoxOneBT DATA GPIO27 to MAIN GPIO34, and
GND to GND. VoxOneBT drives all three signals as I2S master TX; MAIN receives
them as I2S slave RX. See `AUDIO_I2S.md` for the format contract.
