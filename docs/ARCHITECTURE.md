# Architecture

VoxOneBT is a separate classic ESP32 running separate firmware. It does not
share a project or firmware image with VoxOne MAIN.

The MAIN-to-BT control path is a cross-connected hardware UART. VoxOneBT also
runs a Bluetooth Classic A2DP Sink named `VoxOneBT`. AVRCP supplies playback
status and metadata. A one-way I2S path sends decoded PCM to MAIN.

The firmware is deliberately small:

- `main.cpp` performs bootstrap only.
- `App` coordinates Bluetooth changes, UART commands, events, and snapshots.
- `BluetoothService` owns A2DP/AVRCP callbacks and bounded state storage.
- `UartProtocol` owns bounded line reception and UART formatting.
- `Logger` writes diagnostics to USB Serial.
- `I2sOutput` exclusively owns legacy I2S0, its GPIO routing, DMA, native-rate
  clock changes, PCM writes, and stop/clear lifecycle.

Control callbacks update protected state and dirty flags. This includes the
peer name resolved asynchronously by the Bluetooth GAP remote-name event.
`App.loop()` later
consumes those changes, configures/stops I2S, logs, and sends UART messages.
The A2DP PCM stream callback writes directly to `I2sOutput` with a bounded
wait. No additional application FreeRTOS task, ring buffer, or event bus is
used.

ESP32-A2DP v1.8.2 exposes `get_peer_name()` but no public peer-name callback.
A small sink subclass observes its protected GAP callback, lets the base class
store a successful remote name, then passes the public getter result into
BluetoothService state. The fixed buffer, known flag, and dirty flag follow
the same callback-to-App-to-UART path as other Bluetooth state.

Transport commands follow `UART -> UartProtocol -> App -> BluetoothService ->
ESP32-A2DP -> phone`. `UartProtocol` parses the request without calling the
Bluetooth library directly. BluetoothService checks the connection and uses
the library's public `play()`, `pause()`, `next()`, or `previous()` method.
Commands never predict or modify playback state or metadata; phone callbacks
remain the sole source of truth.

Absolute Volume follows the same path. `SET_VOLUME n` calls the public A2DP
sink API only after validation and a connection check. It does not mark volume
as known. Only the remote Absolute Volume callback updates the stored value,
sets its dirty flag, and causes `VOLUME n` to be sent by `App.loop()`.

The public ESP32-A2DP sample-rate callback reports the SBC configuration chosen
for the stream. BluetoothService stores it with a known flag and dirty bit;
`App.loop()` later logs and sends `SAMPLE_RATE n`. No sample rate is assumed
before that callback, and disconnect clears the known flag.

ESP32-A2DP is pinned to tag `v.1.8.2`. Its stream reader is configured with the
library output flag disabled and forwards PCM to application-owned I2S0. The
retained no-op output backend only absorbs the library's unconditional output
configuration call; it owns no peripheral and writes no PCM. This prevents a
second legacy-I2S owner.

No default rate is assumed. I2S0 is installed only after the negotiated rate
callback, stopped until playback is active, dynamically retimed with
`i2s_set_clk`, and stopped plus DMA-cleared on pause, stop, or disconnect.
