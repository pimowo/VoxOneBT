#include "protocol/UartProtocol.h"

#include <string.h>

#include "AppConfig.h"
#include "Pins.h"
#include "Version.h"
#include "diagnostics/Logger.h"

UartProtocol::UartProtocol(HardwareSerial& serial) : serial_(serial) {}

void UartProtocol::begin() {
  serial_.begin(AppConfig::UART_BAUD, SERIAL_8N1, Pins::UART_RX,
                Pins::UART_TX);
  sendLine("READY");
  serial_.print("PROTO ");
  serial_.println(Version::PROTOCOL);
}

void UartProtocol::loop() {
  while (serial_.available() > 0) {
    consume(static_cast<char>(serial_.read()));
  }
}

void UartProtocol::setAvrcCommandHandler(AvrcCommandHandler handler,
                                         void* context) {
  avrcCommandHandler_ = handler;
  avrcCommandContext_ = context;
}

void UartProtocol::setVolumeCommandHandler(VolumeCommandHandler handler,
                                           void* context) {
  volumeCommandHandler_ = handler;
  volumeCommandContext_ = context;
}

bool UartProtocol::takeStatusRequest() {
  if (pendingStatusRequests_ == 0) {
    return false;
  }

  --pendingStatusRequests_;
  return true;
}

void UartProtocol::sendStatus(const BluetoothSnapshot& snapshot) {
  serial_.print("PROTO ");
  serial_.println(Version::PROTOCOL);
  sendLine("READY");
  sendConnection(snapshot.connection);
  if (snapshot.connection == BtConnectionState::Connected &&
      snapshot.peerNameKnown && snapshot.peerName[0] != '\0') {
    sendDevice(snapshot.peerName);
  }
  sendPlayback(snapshot.playback);
  if (snapshot.sampleRateKnown) {
    sendSampleRate(snapshot.sampleRate);
  }
  if (snapshot.volumeKnown) {
    sendVolume(snapshot.volume);
  }

  if (snapshot.artist[0] != '\0') {
    sendMetadata("ARTIST", snapshot.artist);
  }
  if (snapshot.title[0] != '\0') {
    sendMetadata("TITLE", snapshot.title);
  }
  if (snapshot.album[0] != '\0') {
    sendMetadata("ALBUM", snapshot.album);
  }
}

void UartProtocol::sendBluetoothChanges(const BluetoothChanges& changes) {
  if (changes.connectionChanged) {
    sendConnection(changes.connection);
  }
  if (changes.peerNameChanged && changes.peerNameKnown &&
      changes.peerName[0] != '\0') {
    sendDevice(changes.peerName);
  }
  if (changes.artistChanged && changes.artist[0] != '\0') {
    sendMetadata("ARTIST", changes.artist);
  }
  if (changes.titleChanged && changes.title[0] != '\0') {
    sendMetadata("TITLE", changes.title);
  }
  if (changes.albumChanged && changes.album[0] != '\0') {
    sendMetadata("ALBUM", changes.album);
  }
  if (changes.playbackChanged) {
    sendPlayback(changes.playback);
  }
  if (changes.sampleRateChanged && changes.sampleRateKnown) {
    sendSampleRate(changes.sampleRate);
  }
  if (changes.volumeChanged && changes.volumeKnown) {
    sendVolume(changes.volume);
  }
}

void UartProtocol::consume(char character) {
  if (character == '\r') {
    return;
  }

  if (character == '\n') {
    if (discardingOverflow_) {
      discardingOverflow_ = false;
      lineLength_ = 0;
      return;
    }

    if (lineLength_ > 0) {
      lineBuffer_[lineLength_] = '\0';
      handleLine();
      lineLength_ = 0;
    }
    return;
  }

  if (discardingOverflow_) {
    return;
  }

  if (lineLength_ >= AppConfig::UART_MAX_LINE_LENGTH) {
    lineLength_ = 0;
    discardingOverflow_ = true;
    sendLine("ERR LINE_TOO_LONG");
    Logger::warn("UART line exceeded 64 characters");
    return;
  }

  lineBuffer_[lineLength_++] = character;
}

void UartProtocol::handleLine() {
  if (strcmp(lineBuffer_, "GET_STATUS") == 0) {
    if (pendingStatusRequests_ < UINT8_MAX) {
      ++pendingStatusRequests_;
    }
    return;
  }

  if (strcmp(lineBuffer_, "PLAY") == 0) {
    handleAvrcCommand(AvrcCommand::Play);
    return;
  }
  if (strcmp(lineBuffer_, "PAUSE") == 0) {
    handleAvrcCommand(AvrcCommand::Pause);
    return;
  }
  if (strcmp(lineBuffer_, "NEXT") == 0) {
    handleAvrcCommand(AvrcCommand::Next);
    return;
  }
  if (strcmp(lineBuffer_, "PREV") == 0) {
    handleAvrcCommand(AvrcCommand::Previous);
    return;
  }

  constexpr char SET_VOLUME_COMMAND[] = "SET_VOLUME";
  if (strncmp(lineBuffer_, SET_VOLUME_COMMAND,
              sizeof(SET_VOLUME_COMMAND) - 1) == 0 &&
      (lineBuffer_[sizeof(SET_VOLUME_COMMAND) - 1] == '\0' ||
       lineBuffer_[sizeof(SET_VOLUME_COMMAND) - 1] == ' ')) {
    handleSetVolume(lineBuffer_ + sizeof(SET_VOLUME_COMMAND) - 1);
    return;
  }

  sendLine("ERR UNKNOWN_COMMAND");
}

void UartProtocol::sendLine(const char* line) {
  serial_.print(line);
  serial_.write('\n');
}

void UartProtocol::sendConnection(BtConnectionState state) {
  sendLine(state == BtConnectionState::Connected ? "CONNECTED"
                                                 : "DISCONNECTED");
}

void UartProtocol::sendDevice(const char* name) {
  sendMetadata("DEVICE", name);
}

void UartProtocol::sendPlayback(BtPlaybackState state) {
  switch (state) {
    case BtPlaybackState::Playing:
      sendLine("PLAYING");
      break;
    case BtPlaybackState::Paused:
      sendLine("PAUSED");
      break;
    case BtPlaybackState::Stopped:
    default:
      sendLine("STOPPED");
      break;
  }
}

void UartProtocol::sendMetadata(const char* key, const char* value) {
  serial_.print(key);
  serial_.write(' ');
  serial_.print(value);
  serial_.write('\n');
}

void UartProtocol::sendVolume(uint8_t volume) {
  serial_.print("VOLUME ");
  serial_.println(volume);
}

void UartProtocol::sendSampleRate(uint32_t sampleRate) {
  serial_.print("SAMPLE_RATE ");
  serial_.println(sampleRate);
}

void UartProtocol::handleAvrcCommand(AvrcCommand command) {
  if (avrcCommandHandler_ == nullptr) {
    sendLine("ERR NOT_IMPLEMENTED");
    return;
  }

  sendLine(avrcCommandHandler_(command, avrcCommandContext_)
               ? "OK"
               : "ERR NOT_CONNECTED");
}

void UartProtocol::handleSetVolume(const char* command) {
  uint8_t volume = 0;
  if (!parseVolume(command, volume)) {
    Logger::warn("SET_VOLUME invalid value");
    sendLine("ERR INVALID_VALUE");
    return;
  }

  if (volumeCommandHandler_ == nullptr) {
    sendLine("ERR NOT_IMPLEMENTED");
    return;
  }

  sendLine(volumeCommandHandler_(volume, volumeCommandContext_)
               ? "OK"
               : "ERR NOT_CONNECTED");
}

bool UartProtocol::parseVolume(const char* text, uint8_t& volume) const {
  while (*text == ' ') {
    ++text;
  }

  if (*text < '0' || *text > '9') {
    return false;
  }

  uint16_t value = 0;
  while (*text >= '0' && *text <= '9') {
    value = static_cast<uint16_t>(value * 10U + (*text - '0'));
    if (value > 127U) {
      return false;
    }
    ++text;
  }

  while (*text == ' ') {
    ++text;
  }

  if (*text != '\0') {
    return false;
  }

  volume = static_cast<uint8_t>(value);
  return true;
}
