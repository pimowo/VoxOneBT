# UART Protocol v1

Transport settings are 115200 baud, 8 data bits, no parity, and 1 stop bit.
Messages are ASCII text lines terminated by LF (`\n`). CR characters are
ignored, so both LF and CRLF input work.

At boot VoxOneBT sends:

```text
READY
PROTO 1
```

## Implemented command

`GET_STATUS` returns a current Bluetooth snapshot. When disconnected:

```text
PROTO 1
READY
DISCONNECTED
STOPPED
```

When connected it returns `CONNECTED`, then `DEVICE name` if the peer name is
known, followed by the current playback state and each non-empty metadata
field. If known, `SAMPLE_RATE n` and `VOLUME n` are emitted after playback
state and before metadata, in that order. Metadata lines use `ARTIST text`,
`TITLE text`, and `ALBUM text`.

## Asynchronous Bluetooth events

Changed state is sent once as one of `CONNECTED`, `DISCONNECTED`, `PLAYING`,
`PAUSED`, or `STOPPED`. Changed non-empty metadata is sent as `ARTIST text`,
`TITLE text`, or `ALBUM text`. Duplicate callback values are suppressed.
The asynchronously resolved peer name is emitted once as `DEVICE name`.
Repeated identical names are suppressed.

On disconnect, playback becomes stopped and stored metadata is cleared. Only
`DISCONNECTED` is emitted automatically; the next `GET_STATUS` reports the
clean disconnected snapshot.
The stored peer name is also cleared, so no empty or placeholder `DEVICE`
message is emitted. A reconnect may report the resolved name again.

An empty line is ignored. An unknown command returns `ERR UNKNOWN_COMMAND`. A
line longer than 64 characters returns `ERR LINE_TOO_LONG`; input is discarded
through its terminating LF, after which normal parsing resumes.

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
The message set and protocol version are unchanged by physical I2S output.

## Errors

- `ERR NOT_CONNECTED`: transport command without a connected phone
- `ERR UNKNOWN_COMMAND`: command is not recognized
- `ERR NOT_IMPLEMENTED`: recognized but reserved command
- `ERR LINE_TOO_LONG`: input exceeds 64 characters
- `ERR INVALID_VALUE`: missing, malformed, or out-of-range volume

## Reserved, not implemented

No additional commands are reserved in this stage.
