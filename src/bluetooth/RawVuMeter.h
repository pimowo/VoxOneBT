#pragma once

#include <stddef.h>
#include <stdint.h>

struct RawVuPeaks {
  uint16_t left = 0;
  uint16_t right = 0;
};

// ESP32-A2DP supplies interleaved little-endian signed 16-bit stereo frames.
// The raw callback runs before its VolumeControl touches these samples.
inline RawVuPeaks measureRawVu(const uint8_t* data, size_t bytes) {
  RawVuPeaks peaks;
  if (data == nullptr) return peaks;
  for (size_t index = 0; index + 3 < bytes; index += 4) {
    const int16_t left = static_cast<int16_t>(
        static_cast<uint16_t>(data[index]) |
        (static_cast<uint16_t>(data[index + 1]) << 8));
    const int16_t right = static_cast<int16_t>(
        static_cast<uint16_t>(data[index + 2]) |
        (static_cast<uint16_t>(data[index + 3]) << 8));
    const uint16_t leftPeak = static_cast<uint16_t>(
        left < 0 ? -static_cast<int32_t>(left) : left);
    const uint16_t rightPeak = static_cast<uint16_t>(
        right < 0 ? -static_cast<int32_t>(right) : right);
    if (leftPeak > peaks.left) peaks.left = leftPeak;
    if (rightPeak > peaks.right) peaks.right = rightPeak;
  }
  return peaks;
}

class RawVuMeter {
 public:
  static constexpr uint32_t IntervalMs = 50;

  void add(const RawVuPeaks& peaks) {
    if (peaks.left > peaks_.left) peaks_.left = peaks.left;
    if (peaks.right > peaks_.right) peaks_.right = peaks.right;
  }

  bool takeIfDue(uint32_t nowMs, RawVuPeaks& peaks) {
    if (static_cast<uint32_t>(nowMs - lastSentMs_) < IntervalMs)
      return false;
    lastSentMs_ = nowMs;
    peaks = peaks_;
    peaks_ = RawVuPeaks{};
    return true;
  }

  void clear() { peaks_ = RawVuPeaks{}; }

 private:
  RawVuPeaks peaks_{};
  uint32_t lastSentMs_ = 0;
};

inline bool rawVuShouldSendZero(bool connectionChanged, bool connected,
                                bool playbackChanged, bool playing) {
  return (connectionChanged && !connected) ||
         (playbackChanged && !playing);
}
