# Hardware

The target board is the Wemos D1 mini ESP32 with a classic ESP32. The Wemos
D1 R32 was used only for testing. Both boards use the same VoxOneBT GPIO
assignments, chosen for pins available on the D1 mini ESP32 inner rows. Do
not use ESP32-S3, ESP32-C3, or an ESP32-WROVER variant that uses GPIO16/17
for PSRAM.

## VoxOneBT pins

| Interface | Signal | VoxOneBT GPIO |
|---|---|---:|
| UART2 | TX | 17 |
| UART2 | RX | 16 |
| I2S0 | BCLK | 4 |
| I2S0 | WS / LRCLK | 25 |
| I2S0 | DATA | 27 |

Both sides use 3.3 V logic. Power off the boards while making connections.
USB Serial is a separate diagnostic channel at 115200 baud. The inter-board
UART is hardware UART2 at 115200 baud, 8N1.

## SALON connection

| VoxOneBT | Direction | SALON MAIN |
|---|---:|---|
| BCLK GPIO4 | -> | GPIO41 |
| WS GPIO25 | -> | GPIO40 |
| DATA GPIO27 | -> | GPIO39 |
| TX GPIO17 | -> | RX GPIO15 |
| RX GPIO16 | <- | TX GPIO16 |
| GND | <-> | GND |

## DIN connection

| VoxOneBT | Direction | DIN |
|---|---:|---|
| BCLK GPIO4 | -> | GPIO4 |
| WS GPIO25 | -> | GPIO5 |
| DATA GPIO27 | -> | GPIO6 |
| TX GPIO17 | -> | RX GPIO7 |
| RX GPIO16 | <- | TX GPIO8 |
| GND | <-> | GND |

The common ground is mandatory for both installations. VoxOneBT drives the
three I2S signals as master TX; the receiver uses I2S slave RX. See
`AUDIO_I2S.md` for the wire format.
