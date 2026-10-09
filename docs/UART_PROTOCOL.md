# UART Protocol v2

Transport settings are 115200 baud, 8 data bits, no parity, and 1 stop bit.
Messages are text lines terminated by LF (`\n`). Keywords and numeric fields
are ASCII; peer names and metadata may contain UTF-8. CR characters are
ignored, so both LF and CRLF input work.

At boot VoxOneBT sends:

```text
READY
PROTO 2
FW_VERSION 0.6.2-dev
BT_NAME VoxOneBT-EFF35A
CAPS A2DP AVRCP ABSVOL I2S_TX DIAG VU_RAW FW_UPDATE
```

`BT_NAME` is the actual name owned by BluetoothService; the suffix shown above
is an example. `CAPS` is a space-separated list of supported features. Later
capabilities may be appended without changing the line format.

## GET_STATUS

`GET_STATUS` returns one Bluetooth snapshot copied under the existing state
lock before any status line is sent. When disconnected:

```text
STATUS_BEGIN
PROTO 2
FW_VERSION 0.6.2-dev
BT_NAME VoxOneBT-EFF35A
CAPS A2DP AVRCP ABSVOL I2S_TX DIAG VU_RAW FW_UPDATE
DISCONNECTED
STOPPED
STATUS_END
```

When connected it returns `CONNECTED`, then `DEVICE name` if known, followed
by the current playback state. If known, `SAMPLE_RATE n` and `VOLUME n` follow
in that order. Non-empty metadata lines follow as `ARTIST text`, `TITLE text`,
and `ALBUM text`. Every response starts with `STATUS_BEGIN` and ends with
`STATUS_END`. The firmware and Bluetooth name lines have the same source and
format as the startup announcement.

## PING and GET_DIAG

`PING` returns one line, `PONG`, without changing Bluetooth state.

`GET_DIAG` returns current device diagnostics:

```text
DIAG_BEGIN
RESET_REASON POWERON
UPTIME 4321
HEAP 123456
MIN_HEAP 120000
DIAG_END
```

`UPTIME` is milliseconds since boot. `HEAP` and `MIN_HEAP` are free bytes.
`RESET_REASON` is one of `POWERON`, `SOFTWARE`, `WATCHDOG`, `PANIC`,
`EXTERNAL`, `DEEPSLEEP`, `BROWNOUT`, or `UNKNOWN`. The values above are
examples. Continuous verbose diagnostics
remain on USB Serial and are not sent on the inter-board UART.

## Asynchronous Bluetooth events

Changed state is sent once as one of `CONNECTED`, `DISCONNECTED`, `PLAYING`,
`PAUSED`, or `STOPPED`. Changed non-empty metadata is sent as `ARTIST text`,
`TITLE text`, or `ALBUM text`. Duplicate callback values are suppressed.
The asynchronously resolved peer name is emitted once as `DEVICE name`.
Repeated identical names are suppressed.

`VU_RAW` adds asynchronous `VU leftPeak rightPeak` lines, with independent
0–32768 peaks from 16-bit stereo PCM before ESP32-A2DP volume control. During
playback the module sends at most one update every 50 ms. A transition to
PAUSED, STOPPED, or DISCONNECTED sends one `VU 0 0`. PCM from this callback
is used only for metering; the existing post-volume I2S output is unchanged.
Older protocol-v2 implementations may omit this capability and these lines.

On disconnect, playback becomes stopped and stored metadata is cleared.
`DISCONNECTED` and one `VU 0 0` are emitted; the next `GET_STATUS` reports the
clean disconnected snapshot.
The stored peer name is also cleared, so no empty or placeholder `DEVICE`
message is emitted. A reconnect may report the resolved name again.

An empty line is ignored. An unknown command returns `ERR UNKNOWN_COMMAND`. A
line longer than 64 characters returns `ERR LINE_TOO_LONG`; input is discarded
through its terminating LF, after which normal parsing resumes. The 64-byte
limit applies only to incoming commands; outgoing metadata can be longer.

## AVRCP transport commands

`PLAY`, `PAUSE`, `NEXT`, and `PREV` are forwarded to the connected phone. A
successfully forwarded request returns `OK`. If no phone is connected, it
returns `ERR NOT_CONNECTED` and no AVRCP method is called.

`OK` confirms submission to the AVRCP stack, not execution by the phone.
Playback state and metadata continue to be reported only from phone callbacks.

## Absolute Volume

`SET_VOLUME n` accepts one complete decimal value from 0 through 127. When a
phone is connected, the value is forwarded to ESP32-A2DP and `OK` is returned.
Without a connection the response is `ERR NOT_CONNECTED`.

The command itself never emits `VOLUME n` and does not make volume known. A
real Absolute Volume callback from the phone updates the snapshot and emits:

```text
VOLUME 73
```

Duplicate callback values are suppressed. Disconnect clears the known flag,
so disconnected status never contains volume.

## Sample rate

When the A2DP audio configuration supplies a sample rate, the firmware emits:

```text
SAMPLE_RATE 44100
```

The value is stored exactly as supplied by ESP32-A2DP. Repeated identical
callbacks are suppressed. Before the first callback and after disconnect,
`GET_STATUS` omits this line. There is no MAIN-to-BT sample-rate command.

## Errors

- `ERR NOT_CONNECTED`: transport command without a connected phone
- `ERR UNKNOWN_COMMAND`: command is not recognized
- `ERR NOT_IMPLEMENTED`: recognized but reserved command
- `ERR LINE_TOO_LONG`: input exceeds 64 characters
- `ERR INVALID_VALUE`: missing, malformed, or out-of-range volume

## UART firmware update (`FW_UPDATE`)

Only VoxOneBT firmware is updated. Protocol version remains `PROTO 2` and
transport remains 115200 8N1. MAIN starts with one LF-terminated ASCII line:

```text
FW_BEGIN <size-decimal> <crc32-8-hex-digits> <version>
```

`size` is the exact image length, from 1 through the inactive OTA slot size.
The CRC is CRC-32/ISO-HDLC of the complete firmware image. `version` is 1–24
ASCII letters, digits, `.`, `_`, or `-`. The complete input line must fit the
64-byte UART command limit. Missing/extra fields, zero/overflow/oversize size,
bad CRC, and invalid version are rejected as `FW_ERR <reason>`. Lack of an
inactive OTA slot gives `FW_ERR NO_OTA_SLOT`; failure to prepare Bluetooth or
start the OTA writer gives `FW_ERR BT_QUIESCE` or `FW_ERR OTA_BEGIN`.

After successful preparation VoxOneBT sends `FW_READY 1024`. From that point,
MAIN sends **binary frames only**; ASCII commands are not parsed until abort,
error, or timeout. The UART is exclusive to update replies: asynchronous BT
events, VU, and status are suppressed. USB Serial diagnostics remain available.
The A2DP peer is disconnected, Bluetooth is made non-connectable and
non-discoverable, application PCM/I2S is stopped, and VU state is cleared.

Each frame is `B7 4F | type:u8 | sequence:u32 LE | length:u16 LE |
payload:length bytes | crc32:u32 LE`. The frame CRC-32/ISO-HDLC covers `type`
through the final payload byte, excluding magic and CRC trailer. Type 1 is
DATA (1–1024 payload bytes), type 2 is END (zero payload), and type 3 is ABORT
(zero payload). Sequences start at zero and advance once per accepted DATA.
END/ABORT carry the next expected sequence.

For a newly accepted DATA, `FW_ACK <sequence> <confirmedBytes>` is sent only
after the entire payload has been accepted by `Update.write()`. An identical
retry of the latest DATA receives the same ACK without another flash write.
CRC or sequence errors are retryable as `FW_NACK <sequence> FRAME_CRC` or
`FW_NACK <sequence> WRONG_SEQUENCE`; an incomplete END is retryable as
`FW_NACK <sequence> INCOMPLETE_IMAGE`. A changed duplicate, image CRC mismatch,
size overflow, invalid session, flash write/finalization failure, or timeout
is fatal: `FW_ERR <reason>` aborts the OTA writer and returns to text mode.
Inactivity for more than 15 seconds after the last valid DATA/END aborts the
session with `FW_ERR TIMEOUT`.

For a valid END, the session verifies exact image length and whole-image CRC;
VoxOneBT sends `FW_VERIFY`, then performs `Update.end(false)`. `FW_OK` is sent
only if finalization succeeds. UART TX is flushed (TX only), then VoxOneBT
restarts after a 100 ms grace period. OTA boot-health validation of the new
image happens on the subsequent boot. A valid ABORT returns `FW_ABORTED`,
aborts the writer, restores the normal UART/BT policy and does not restart.
No reconnect or AVRCP PLAY is forced after abort/error. Success does not
re-enable Bluetooth before restart.

## Future extensions

This version does not implement `SET_BT_NAME` or custom-name NVS storage.
Capability tokens may be added when further features are implemented.
