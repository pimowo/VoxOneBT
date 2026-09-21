# Stage 6 test plan

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

## Build and boot

1. Run `pio run` and confirm a successful build.
2. Flash the VoxOneBT board only after identifying its correct serial port.
3. Open its USB Serial monitor at 115200 baud.
4. Confirm the diagnostic boot output includes version `0.6.1-dev`, `BOOT`,
   `UART READY`, `Bluetooth initialized`, and `A2DP Sink started as VoxOneBT`.
   `I2S initialized` must appear only after a valid stream rate is negotiated.

## Inter-board UART

With power off, connect UART as documented. I2S may remain disconnected for
UART-only tests. Power the boards and confirm that MAIN sees `READY` and
`PROTO 1`.

Send each test as a line ending in LF unless otherwise noted:

| Input | Expected UART response |
|---|---|
| `GET_STATUS` | `PROTO 1`, then `READY` |
| empty line | no response |
| `GET_STATUS\r\n` | `PROTO 1`, then `READY` |
| `PLAY` while disconnected | `ERR NOT_CONNECTED` |
| `SOMETHING_ELSE` | `ERR UNKNOWN_COMMAND` |
| more than 64 characters, then LF | `ERR LINE_TOO_LONG` once |
| valid command after an overlong line | normal valid response |

Finally, send several commands back-to-back, each terminated with LF, and
verify one complete response per non-empty command without resets or stalls.

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

## Completed Bluetooth hardware checks

1. Pair a phone with `VoxOneBT` and expect `CONNECTED` once.
2. Start media and verify artist, title, album, and playback events.
3. Repeat identical metadata and confirm it is not resent.
4. Pause, resume, and stop, checking `PAUSED`, `PLAYING`, and `STOPPED`.
5. Run `GET_STATUS` while connected and compare the complete snapshot.
6. Disconnect and expect only `DISCONNECTED`; then verify `GET_STATUS` returns
   `DISCONNECTED` and `STOPPED` without old metadata.
7. With both boards powered off, connect BCLK26 -> MAIN GPIO21, WS25 -> MAIN
   GPIO22, DATA27 -> MAIN GPIO34, and common GND.
8. On playback, verify with a logic analyzer that VoxOneBT is master, BCLK and
   WS use Philips timing, and WS rate equals the negotiated `SAMPLE_RATE`.
9. Check 16-bit signed interleaved L/R audio at 16/32/44.1/48 kHz where the
   phone offers those rates; verify there is no resampling.
10. Pause, stop, and disconnect; verify I2S stops and no stale audio tail is
    emitted after resuming.

The checks above were completed successfully on a Wemos D1 R32, including
reconnect, UTF-8 metadata, I2S audio, and peer-name reporting (`Redmi Note 14`).
No crash or reset occurred during the test session.
