#include <Arduino.h>
#include <esp_system.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "Version.h"
#include "protocol/UartProtocol.h"

FakeESP ESP;

namespace {

esp_reset_reason_t resetReason = ESP_RST_POWERON;
uint32_t uptimeMs = 4321;

void expect(const std::string& actual, const std::string& expected) {
  if (actual != expected) {
    std::cerr << "Expected:\n" << expected << "Actual:\n" << actual;
    std::exit(1);
  }
}

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

bool avrcHandler(AvrcCommand command, void* context) {
  *static_cast<AvrcCommand*>(context) = command;
  return true;
}

bool volumeHandler(uint8_t volume, void* context) {
  *static_cast<uint8_t*>(context) = volume;
  return true;
}

}  // namespace

uint32_t millis() { return uptimeMs; }
esp_reset_reason_t esp_reset_reason() { return resetReason; }

int main() {
  static_assert(AppConfig::UART_BAUD == 921600, "inter-board UART baud changed");
  static_assert(AppConfig::USB_SERIAL_BAUD == 115200, "USB debug baud changed");
  const std::string versionLine = std::string("FW_VERSION ") + Version::FIRMWARE + "\n";
  HardwareSerial serial;
  UartProtocol protocol(serial);
  protocol.begin("VoxOneBT-EFF35A");
  expect(serial.output(),
         std::string("READY\nPROTO 2\n") + versionLine +
         "BT_NAME VoxOneBT-EFF35A\nCAPS A2DP AVRCP ABSVOL I2S_TX DIAG VU_RAW FW_UPDATE\n");

  serial.clearOutput();
  serial.feed("PING\nGET_STATUS\r\nGET_DIAG\n");
  protocol.loop();
  expect(serial.output(),
         "PONG\nDIAG_BEGIN\nRESET_REASON POWERON\nUPTIME 4321\n"
         "HEAP 123456\nMIN_HEAP 120000\nDIAG_END\n");
  expect(protocol.takeStatusRequest(), "GET_STATUS request was not queued");
  expect(!protocol.takeStatusRequest(), "Unexpected second status request");

  serial.clearOutput();
  BluetoothSnapshot snapshot;
  protocol.sendStatus(snapshot, OtaStatus::NotPending);
  expect(serial.output(),
         std::string("STATUS_BEGIN\nPROTO 2\n") + versionLine +
         "BT_NAME VoxOneBT-EFF35A\nCAPS A2DP AVRCP ABSVOL I2S_TX DIAG VU_RAW FW_UPDATE\n"
         "OTA_STATE NOT_PENDING\n"
         "DISCONNECTED\nSTOPPED\nSTATUS_END\n");

  snapshot.connection = BtConnectionState::Connected;
  snapshot.peerNameKnown = true;
  std::strcpy(snapshot.peerName, "Redmi Note 14");
  snapshot.playback = BtPlaybackState::Playing;
  snapshot.sampleRateKnown = true;
  snapshot.sampleRate = 44100;
  snapshot.volumeKnown = true;
  snapshot.volume = 51;
  std::strcpy(snapshot.artist, "Artist");
  std::strcpy(snapshot.title, "Title");
  std::strcpy(snapshot.album, "Album");
  serial.clearOutput();
  protocol.sendStatus(snapshot, OtaStatus::PendingVerify);
  expect(serial.output(),
         std::string("STATUS_BEGIN\nPROTO 2\n") + versionLine +
         "BT_NAME VoxOneBT-EFF35A\nCAPS A2DP AVRCP ABSVOL I2S_TX DIAG VU_RAW FW_UPDATE\n"
         "OTA_STATE PENDING_VERIFY\n"
         "CONNECTED\nDEVICE Redmi Note 14\nPLAYING\nSAMPLE_RATE 44100\n"
         "VOLUME 51\nARTIST Artist\nTITLE Title\nALBUM Album\nSTATUS_END\n");
  expect(serial.output().find("OTA_STATE PENDING_VERIFY\n") ==
             serial.output().rfind("OTA_STATE PENDING_VERIFY\n"),
         "GET_STATUS must contain exactly one OTA_STATE line");

  BluetoothChanges changes;
  changes.connection = BtConnectionState::Connected;
  changes.connectionChanged = true;
  changes.peerNameKnown = true;
  changes.peerNameChanged = true;
  std::strcpy(changes.peerName, "Redmi Note 14");
  changes.playback = BtPlaybackState::Playing;
  changes.playbackChanged = true;
  changes.sampleRateKnown = true;
  changes.sampleRate = 44100;
  changes.sampleRateChanged = true;
  changes.volumeKnown = true;
  changes.volume = 51;
  changes.volumeChanged = true;
  changes.artistChanged = true;
  std::strcpy(changes.artist, "Artist");
  serial.clearOutput();
  protocol.sendBluetoothChanges(changes);
  expect(serial.output(),
         "CONNECTED\nDEVICE Redmi Note 14\nARTIST Artist\nPLAYING\n"
         "SAMPLE_RATE 44100\nVOLUME 51\n");

  serial.clearOutput();
  protocol.sendVu(12000, 32768);
  protocol.sendVu(0, 0);
  protocol.sendVu(65535, 1);
  expect(serial.output(), "VU 12000 32768\nVU 0 0\nVU 32768 1\n");

  AvrcCommand lastCommand = AvrcCommand::Pause;
  uint8_t lastVolume = 0;
  protocol.setAvrcCommandHandler(avrcHandler, &lastCommand);
  protocol.setVolumeCommandHandler(volumeHandler, &lastVolume);
  serial.clearOutput();
  serial.feed("PLAY\nPAUSE\nNEXT\nPREV\nSET_VOLUME 73\nBOGUS\n");
  protocol.loop();
  expect(serial.output(),
         "OK\nOK\nOK\nOK\nOK\nERR UNKNOWN_COMMAND\n");
  expect(lastCommand == AvrcCommand::Previous, "PREV was not forwarded");
  expect(lastVolume == 73, "SET_VOLUME was not forwarded");

  serial.clearOutput();
  serial.feed("SET_VOLUME 128\n");
  protocol.loop();
  expect(serial.output(), "ERR INVALID_VALUE\n");

  resetReason = ESP_RST_PANIC;
  serial.clearOutput();
  serial.feed("GET_DIAG\n");
  protocol.loop();
  expect(serial.output().find("RESET_REASON PANIC\n") != std::string::npos,
         "PANIC reset reason was not mapped");

  resetReason = ESP_RST_TASK_WDT;
  serial.clearOutput();
  serial.feed("GET_DIAG\n");
  protocol.loop();
  expect(serial.output().find("RESET_REASON WATCHDOG\n") != std::string::npos,
         "WATCHDOG reset reason was not mapped");

  resetReason = ESP_RST_BROWNOUT;
  serial.clearOutput();
  serial.feed("GET_DIAG\n");
  protocol.loop();
  expect(serial.output().find("RESET_REASON BROWNOUT\n") != std::string::npos,
         "BROWNOUT reset reason was not mapped");

  serial.clearOutput();
  serial.feed(std::string(65, 'X') + "\nPING\n");
  protocol.loop();
  expect(serial.output(), "ERR LINE_TOO_LONG\nPONG\n");

  std::cout << "UartProtocol native tests PASS\n";
}
