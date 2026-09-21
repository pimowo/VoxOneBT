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

  I2sOutput i2sOutput_;
  BluetoothService bluetoothService_;
  UartProtocol uartProtocol_;
};
