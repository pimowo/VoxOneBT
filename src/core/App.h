#pragma once

#include "audio/I2sOutput.h"
#include "bluetooth/BluetoothService.h"
#include "protocol/UartProtocol.h"
#include "update/OtaBootHealth.h"
#include "update/FirmwareOtaWriter.h"
#include "update/FirmwareUpdateReceiver.h"

class App : private FirmwareUpdateHooks {
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
  bool quiesce() override;
  void restore() override;
  void reply(const char* line) override;
  void flushTx() override;

  I2sOutput i2sOutput_;
  BluetoothService bluetoothService_;
  UartProtocol uartProtocol_;
  FirmwareOta::ArduinoOtaBackend otaBackend_;
  FirmwareOta::FirmwareOtaWriter otaWriter_;
  FirmwareUpdateReceiver updateReceiver_;
  OtaBootHealth otaBootHealth_;
  uint32_t lastDiagnosticsMs_ = 0;
  uint32_t lastVuCheckMs_ = 0;
  bool i2sRateReady_ = false;
};
