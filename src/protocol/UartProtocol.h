#pragma once

#include <Arduino.h>
#include <stddef.h>

#include "AppConfig.h"
#include "bluetooth/BluetoothService.h"

class UartProtocol {
 public:
  using AvrcCommandHandler = bool (*)(AvrcCommand command, void* context);
  using VolumeCommandHandler = bool (*)(uint8_t volume, void* context);

  explicit UartProtocol(HardwareSerial& serial);

  void begin(const char* bluetoothName);
  void loop();
  void setAvrcCommandHandler(AvrcCommandHandler handler, void* context);
  void setVolumeCommandHandler(VolumeCommandHandler handler, void* context);
  bool takeStatusRequest();
  void sendStatus(const BluetoothSnapshot& snapshot);
  void sendBluetoothChanges(const BluetoothChanges& changes);
  void sendVu(uint16_t leftPeak, uint16_t rightPeak);

 private:
  void consume(char character);
  void handleLine();
  void sendLine(const char* line);
  void sendIdentity();
  void sendDiagnostics();
  void sendConnection(BtConnectionState state);
  void sendDevice(const char* name);
  void sendPlayback(BtPlaybackState state);
  void sendMetadata(const char* key, const char* value);
  void sendVolume(uint8_t volume);
  void sendSampleRate(uint32_t sampleRate);
  void handleAvrcCommand(AvrcCommand command);
  void handleSetVolume(const char* command);
  bool parseVolume(const char* text, uint8_t& volume) const;

  HardwareSerial& serial_;
  const char* bluetoothName_ = nullptr;
  char lineBuffer_[AppConfig::UART_MAX_LINE_LENGTH + 1]{};
  size_t lineLength_ = 0;
  bool discardingOverflow_ = false;
  uint8_t pendingStatusRequests_ = 0;
  AvrcCommandHandler avrcCommandHandler_ = nullptr;
  VolumeCommandHandler volumeCommandHandler_ = nullptr;
  void* avrcCommandContext_ = nullptr;
  void* volumeCommandContext_ = nullptr;
};
