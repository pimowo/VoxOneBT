# VoxOneBT

VoxOneBT is standalone firmware for a classic ESP32-WROOM-32-class board.
Version 0.6.1-dev implements stage 6: Bluetooth Classic A2DP Sink state,
AVRCP reporting and commands, Absolute Volume, negotiated sample-rate reporting,
and native-rate I2S transmission over UART protocol v1.

Implemented now:

- diagnostic logging over USB Serial at 115200 baud,
- communication with VoxOne MAIN over hardware UART2 at 115200 8N1,
- bounded UART line parsing and `GET_STATUS`,
- A2DP Sink advertised as `VoxOneBT`,
- connection, playback, artist, title, and album reporting,
- connected peer name reporting as `DEVICE name`,
- `PLAY`, `PAUSE`, `NEXT`, and `PREV` forwarding to the connected phone,
- `SET_VOLUME n` and callback-driven `VOLUME n` in the range 0..127,
- callback-driven `SAMPLE_RATE n` from the negotiated A2DP stream.
- physical I2S master TX on BCLK26, WS25, and DOUT27 using signed 16-bit
  interleaved stereo PCM in Philips I2S format.

Decoded PCM is forwarded at its negotiated native rate without resampling.
There is no MAIN command for sample rate and no additional audio task or
application ring buffer.

Build with:

```text
pio run
```

See `docs/HARDWARE.md` and `docs/TEST_PLAN.md` before connecting the boards.
