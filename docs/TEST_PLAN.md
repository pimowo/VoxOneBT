# UART Protocol v2 test plan

## Current no-hardware verification

1. Run `pio run` for `esp32dev`.
2. Confirm Arduino-ESP32 resolves to 2.0.17.
3. Confirm ESP32-A2DP resolves from pinned tag `v.1.8.2` and no AudioTools
   dependency is installed.
4. Confirm the build has no compiler warnings or errors.
5. Inspect `set_stream_reader(..., false)` and verify ESP32-A2DP does not own a
   physical output while application `I2sOutput` exclusively owns I2S0.
6. Verify no default sample rate, resampler, application audio task, or extra
   PCM ring buffer is present.
7. Run the native UART protocol test with `g++ -std=c++11 -Wall -Wextra
   -Itests/native/stubs -Iinclude -Isrc tests/native/UartProtocolTest.cpp
   src/protocol/UartProtocol.cpp -o .pio/build/uart_protocol_native.exe`, then
   run `.pio/build/uart_protocol_native.exe`.

## Build and boot

1. Run `pio run` and confirm a successful build.
2. Flash the VoxOneBT board only after identifying its correct serial port.
3. Open its USB Serial monitor at 115200 baud.
4. Confirm the diagnostic boot output includes version `0.6.1-dev`, `BOOT`,
   `UART READY`, `Bluetooth initialized`, and
   `A2DP Sink started as VoxOneBT-XXXXXX`.
   `I2S initialized` must appear only after a valid stream rate is negotiated.

## Bluetooth name

1. Read this module's Bluetooth MAC and note its last three bytes. Use the BT
   MAC, not a Wi-Fi MAC or a MAIN board address.
2. Confirm the advertised and logged name matches `VoxOneBT-[0-9A-F]{6}`.
3. Confirm the six suffix characters equal those three BT MAC bytes in order,
   uppercase and without separators. For example, BT MAC
   `AA:BB:CC:A1:B2:C3` gives `VoxOneBT-A1B2C3`.
4. Reboot and confirm the name is unchanged.

## Inter-board UART

With power off, connect UART as documented. I2S may remain disconnected for
UART-only tests. Power the boards and confirm that MAIN sees `READY` and
`PROTO 2`, followed by `FW_VERSION`, `BT_NAME`, and
`CAPS A2DP AVRCP ABSVOL I2S_TX DIAG`.

Send each test as a line ending in LF unless otherwise noted:

| Input | Expected UART response |
|---|---|
| `GET_STATUS` | `STATUS_BEGIN`, identity fields, state, `STATUS_END` |
| empty line | no response |
| `GET_STATUS\r\n` | same framed status response |
| `PING` | `PONG` |
| `GET_DIAG` | `DIAG_BEGIN`, reset reason, uptime, heap, minimum heap, `DIAG_END` |
| `PLAY` while disconnected | `ERR NOT_CONNECTED` |
| `SOMETHING_ELSE` | `ERR UNKNOWN_COMMAND` |
| more than 64 characters, then LF | `ERR LINE_TOO_LONG` once |
| valid command after an overlong line | normal valid response |

Finally, send several commands back-to-back, each terminated with LF, and
verify one complete response per non-empty command without resets or stalls.
Check that every `GET_STATUS` contains `PROTO 2`, `FW_VERSION 0.6.1-dev`, the
same `BT_NAME` used by A2DP, and the capability line. Check the status fields
appear between `STATUS_BEGIN` and `STATUS_END` in documented order. Check that
`GET_DIAG` uses a short reset token and that repeated `PING` requests have no
effect on Bluetooth state. The 64-byte limit applies to incoming commands;
longer outgoing metadata is allowed.

## Logical command paths

| State and input | Expected response and action |
|---|---|
| disconnected + `PLAY` | `ERR NOT_CONNECTED`; do not call A2DP |
| disconnected + `NEXT` | `ERR NOT_CONNECTED`; do not call A2DP |
| connected + `PLAY` | call `play()`, then `OK` |
| connected + `PAUSE` | call `pause()`, then `OK` |
| connected + `NEXT` | call `next()`, then `OK` |
| connected + `PREV` | call `previous()`, then `OK` |
| disconnected + `SET_VOLUME 50` | `ERR NOT_CONNECTED` |
| `FOO` | `ERR UNKNOWN_COMMAND` |

No successful transport command may directly alter playback state or metadata.
Expected `PLAYING`, `PAUSED`, `STOPPED`, and metadata changes must arrive later
through Bluetooth callbacks.

## Logical Absolute Volume paths

| State and input | Expected response and action |
|---|---|
| disconnected + `SET_VOLUME 50` | `ERR NOT_CONNECTED`; do not call A2DP |
| connected + `SET_VOLUME 50` | call `set_volume(50)`, then `OK` |
| callback `50` for the first time | emit `VOLUME 50` |
| callback `50` again | no UART event |
| callback `51` | emit `VOLUME 51` |
| `SET_VOLUME -1` | `ERR INVALID_VALUE` |
| `SET_VOLUME 128` | `ERR INVALID_VALUE` |
| `SET_VOLUME abc` | `ERR INVALID_VALUE` |
| `SET_VOLUME` | `ERR INVALID_VALUE` |
| `SET_VOLUME 12abc` | `ERR INVALID_VALUE` |

Verify that `SET_VOLUME` alone never emits `VOLUME`. `GET_STATUS` includes
volume only after a remote callback and omits it again after disconnect.

## Logical sample-rate paths

| Input state/change | Expected result |
|---|---|
| startup | sample rate unknown; no UART event |
| callback `44100` | emit `SAMPLE_RATE 44100` |
| callback `44100` again | no UART event |
| callback `48000` | emit `SAMPLE_RATE 48000` |
| `GET_STATUS` while known | include current `SAMPLE_RATE` |
| disconnect | clear known flag and pending event |
| `GET_STATUS` after disconnect | omit `SAMPLE_RATE` |
| reconnect, callback `44100` | emit `SAMPLE_RATE 44100` again |

Confirm that no default 44100 or zero value is reported before negotiation.

## Logical peer-name paths

| Input state/change | Expected result |
|---|---|
| connected, resolved `Galaxy S24` | emit `DEVICE Galaxy S24` and log once |
| same name resolved again | no second UART event or application log |
| `GET_STATUS` while known | include `DEVICE Galaxy S24` after `CONNECTED` |
| name not yet known | omit `DEVICE` |
| disconnect | clear name and pending event; emit no `DEVICE` |
| reconnect, name resolved again | emit `DEVICE Galaxy S24` again |

Also verify UTF-8 names remain valid and CR/LF bytes are replaced with spaces.

## Bluetooth hardware checks

1. Pair a phone with the `VoxOneBT-XXXXXX` name verified above and expect
   `CONNECTED` once.
2. Start media and verify artist, title, album, and playback events.
3. Repeat identical metadata and confirm it is not resent.
4. Pause, resume, and stop, checking `PAUSED`, `PLAYING`, and `STOPPED`.
5. Run `GET_STATUS` while connected and compare the complete snapshot.
6. Disconnect and expect only `DISCONNECTED`; then verify `GET_STATUS` returns
   `DISCONNECTED` and `STOPPED` without old metadata.
7. With both boards powered off, connect VoxOneBT BCLK GPIO18, WS GPIO19,
   DATA GPIO23, and UART TX17/RX16 to the SALON or DIN pins in `HARDWARE.md`.
   Connect a common GND.
8. On playback, verify with a logic analyzer that VoxOneBT is master, BCLK and
   WS use Philips timing, and WS rate equals the negotiated `SAMPLE_RATE`.
9. Check 16-bit signed interleaved L/R audio at 16/32/44.1/48 kHz where the
   phone offers those rates; verify there is no resampling.
10. Pause, stop, and disconnect; verify I2S stops and no stale audio tail is
    emitted after resuming.

Bluetooth and generated-name checks passed on a Wemos D1 mini
ESP32 classic in the hardware checkpoint, including repeated reconnects and
the name `VoxOneBT-EFF35A`. Protocol v2 UART responses require a new hardware
run after this firmware is flashed. Physical verification of the I2S
GPIO18/19/23 mapping is **PENDING**.

## Crash investigation retest

The diagnostic firmware prints `[DIAG]` on boot, on connection/disconnection or
sample-rate changes, and every 30 seconds. Each report includes current/minimum
free heap and the minimum free stack of the Arduino loop task in bytes. Callback
lines contain cumulative counts, task handle, core, and minimum free stack for
connection, peer name, metadata, volume, playback, sample rate, and PCM stream.
The PCM path only increments a counter; it samples task/core/stack on its first
callback and every 5000 callbacks. No PCM packet is printed from that callback.

1. With MAIN's UART runtime still inactive, record the USB Serial boot log and
   verify the advertised name is `VoxOneBT-EFF35A` for the current module.
2. Run the same phone and audio source as in the failing session for at least
   30 minutes. Capture the complete serial log, including each `[DIAG]` line,
   panic registers, and backtrace if a reset occurs.
3. During playback, repeat metadata changes, play/pause, volume changes, and
   reconnects. Compare heap, minimum heap, and stack high-water marks before
   and after each event. Check that `stream` counts rise without per-packet logs.
4. Repeat once with the SALON UART TX wire disconnected from VoxOneBT RX16, or
   hold RX16 at 3.3 V through a suitable pull-up while no transmitter drives it.
   Compare the rate of `UART line exceeded 64 characters` warnings. Restore
   the intended wiring afterward. Do not connect RX16 directly to 3.3 V if a
   transmitter may drive the line low.
5. If a panic repeats, preserve the exact ELF from the same build and decode
   its PC/backtrace addresses. Compare the last diagnostic counters and memory
   minima; do not infer the cause from the last volume log alone.
