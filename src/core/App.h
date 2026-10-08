#pragma once

#include "audio/I2sOutput.h"
#include "bluetooth/BluetoothService.h"
#include "protocol/UartProtocol.h"

class App {
 public:
  App();

  void begin();
  void loop();

 private:
  static bool handleAvrcCommand(AvrcCommand command, void* context);
  static bool handleVolumeCommand(uint8_t volume, void* context);
  void processBluetoothChanges();
  void processStatusRequests();
  void processVu();
  void logDiagnostics(const char* event);

  I2sOutput i2sOutput_;
  BluetoothService bluetoothService_;
  UartProtocol uartProtocol_;
  uint32_t lastDiagnosticsMs_ = 0;
  uint32_t lastVuCheckMs_ = 0;
  bool i2sRateReady_ = false;
};
