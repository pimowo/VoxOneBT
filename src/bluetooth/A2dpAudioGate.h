#pragma once

#include <stdint.h>

enum class A2dpAudioState : uint8_t {
  Stopped,
  Started,
  Suspended,
};

// Tracks the A2DP datapath independently of AVRCP transport status.
class A2dpAudioGate {
 public:
  void onConnection(bool connected) {
    connected_ = connected;
    if (!connected) {
      state_ = A2dpAudioState::Stopped;
      sampleRateKnown_ = false;
    }
  }

  void onAudioState(A2dpAudioState state) { state_ = state; }
  void onSampleRate() { sampleRateKnown_ = true; }

  A2dpAudioState state() const { return state_; }
  bool desiredActive() const {
    return connected_ && sampleRateKnown_ &&
           state_ == A2dpAudioState::Started;
  }

 private:
  A2dpAudioState state_ = A2dpAudioState::Stopped;
  bool connected_ = false;
  bool sampleRateKnown_ = false;
};
