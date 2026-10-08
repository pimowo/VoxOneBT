#include <cassert>

#include "bluetooth/A2dpAudioGate.h"

int main() {
  A2dpAudioGate gate;
  assert(!gate.desiredActive());

  // Configuration before STARTED.
  gate.onConnection(true);
  gate.onSampleRate();
  // AVRCP PLAYING can arrive while the A2DP datapath is still stopped.
  bool avrcPlaying = true;
  assert(avrcPlaying && !gate.desiredActive());
  gate.onAudioState(A2dpAudioState::Started);
  assert(gate.desiredActive());

  // AVRCP PAUSED does not enter this gate or stop an active A2DP datapath.
  avrcPlaying = false;
  assert(!avrcPlaying && gate.desiredActive());
  gate.onAudioState(A2dpAudioState::Suspended);
  assert(!gate.desiredActive());
  gate.onAudioState(A2dpAudioState::Started);
  assert(gate.desiredActive());
  gate.onAudioState(A2dpAudioState::Stopped);
  assert(!gate.desiredActive());

  // STARTED before sample-rate configuration.
  gate.onConnection(false);
  gate.onConnection(true);
  gate.onAudioState(A2dpAudioState::Started);
  assert(!gate.desiredActive());
  gate.onSampleRate();
  assert(gate.desiredActive());

  // Disconnect and the next session cannot reuse the old active state/rate.
  gate.onConnection(false);
  assert(gate.state() == A2dpAudioState::Stopped);
  assert(!gate.desiredActive());
  gate.onConnection(true);
  assert(!gate.desiredActive());
  gate.onSampleRate();
  assert(!gate.desiredActive());
}
