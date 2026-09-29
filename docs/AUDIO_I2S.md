# Audio I2S contract

The audio link is one-way. VoxOneBT is the I2S master transmitter and VoxOne
MAIN is the I2S slave receiver.

| Signal | VoxOneBT | Direction |
|---|---|---:|
| BCLK | GPIO4 | -> |
| WS / LRCLK | GPIO25 | -> |
| DATA | GPIO27 | -> |
| GND | GND | <-> |

Receiver pins depend on the SALON or DIN installation. See `HARDWARE.md` for
both wiring maps. A common ground is required.

The wire format is Philips I2S, MSB first, signed 16-bit stereo PCM with
interleaved left/right frames. VoxOneBT forwards the native A2DP rate (normally
16000, 32000, 44100, or 48000 Hz) exactly as reported by the negotiated stream.
There is no resampling.

Application-owned legacy I2S0 is installed after the first valid rate callback
and uses 8 DMA buffers of 256 stereo frames each (8192 bytes total). This gives
about 46 ms of DMA capacity at 44.1 kHz, chosen to absorb callback scheduling
jitter without adding an application buffer or task. PCM is dropped before
the rate is known or while playback is inactive. Pause, stop, and disconnect
stop I2S and clear DMA so stale audio is not retained.
