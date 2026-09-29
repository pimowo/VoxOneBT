#pragma once

#include <stdint.h>

enum class BtConnectionState : uint8_t { Disconnected, Connected };
enum class BtPlaybackState : uint8_t { Stopped, Playing, Paused };
enum class AvrcCommand : uint8_t { Play, Pause, Next, Previous };

struct BluetoothSnapshot {
  BtConnectionState connection = BtConnectionState::Disconnected;
  BtPlaybackState playback = BtPlaybackState::Stopped;
  uint8_t volume = 0;
  bool volumeKnown = false;
  uint32_t sampleRate = 0;
  bool sampleRateKnown = false;
  bool peerNameKnown = false;
  char peerName[97]{};
  char artist[193]{};
  char title[193]{};
  char album[193]{};
};

struct BluetoothChanges : BluetoothSnapshot {
  bool connectionChanged = false;
  bool playbackChanged = false;
  bool artistChanged = false;
  bool titleChanged = false;
  bool albumChanged = false;
  bool volumeChanged = false;
  bool sampleRateChanged = false;
  bool peerNameChanged = false;
};
