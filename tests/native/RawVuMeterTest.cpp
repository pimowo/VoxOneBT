#include <cassert>
#include <cstdint>

#include "bluetooth/RawVuMeter.h"

int main() {
  const uint8_t pcm[] = {
      0xe8, 0x03, 0x30, 0xf8, // L=1000, R=-2000
      0x00, 0x80, 0xff, 0x7f  // L=INT16_MIN, R=INT16_MAX
  };
  RawVuPeaks peaks = measureRawVu(pcm, sizeof(pcm));
  assert(peaks.left == 32768 && peaks.right == 32767);
  peaks = measureRawVu(pcm, 4);
  assert(peaks.left == 1000 && peaks.right == 2000);
  peaks = measureRawVu(pcm, 3);
  assert(peaks.left == 0 && peaks.right == 0);
  peaks = measureRawVu(nullptr, 4);
  assert(peaks.left == 0 && peaks.right == 0);
  const uint8_t silence[] = {0, 0, 0, 0};
  peaks = measureRawVu(silence, sizeof(silence));
  assert(peaks.left == 0 && peaks.right == 0);

  RawVuMeter meter;
  meter.add({1000, 2000});
  meter.add({4000, 1500});
  assert(!meter.takeIfDue(49, peaks));
  assert(meter.takeIfDue(50, peaks));
  assert(peaks.left == 4000 && peaks.right == 2000);
  assert(!meter.takeIfDue(99, peaks));
  assert(meter.takeIfDue(100, peaks));
  assert(peaks.left == 0 && peaks.right == 0);
  meter.add({500, 600});
  meter.clear();
  assert(meter.takeIfDue(150, peaks));
  assert(peaks.left == 0 && peaks.right == 0);

  assert(rawVuShouldSendZero(false, true, true, false)); // PAUSE/STOP
  assert(rawVuShouldSendZero(true, false, false, false)); // DISCONNECT
  assert(rawVuShouldSendZero(true, false, true, false));  // one event, one zero
  assert(!rawVuShouldSendZero(false, true, false, false)); // steady PAUSE
  assert(!rawVuShouldSendZero(false, true, true, true));  // PLAY
}
