#include "core/App.h"

#include <Arduino.h>

#include "Version.h"
#include "diagnostics/Logger.h"

App::App() : i2sOutput_(), bluetoothService_(i2sOutput_), uartProtocol_(Serial2) {}

void App::begin() {
  Logger::info("VoxOneBT", Version::FIRMWARE);
  Logger::info("BOOT");
  uartProtocol_.setAvrcCommandHandler(handleAvrcCommand, this);
  uartProtocol_.setVolumeCommandHandler(handleVolumeCommand, this);
  uartProtocol_.begin();
  Logger::info("UART READY");
  bluetoothService_.begin();
}

bool App::handleAvrcCommand(AvrcCommand command, void* context) {
  App* app = static_cast<App*>(context);
  return app != nullptr && app->bluetoothService_.sendAvrcCommand(command);
}

bool App::handleVolumeCommand(uint8_t volume, void* context) {
  App* app = static_cast<App*>(context);
  return app != nullptr && app->bluetoothService_.setVolume(volume);
}

void App::loop() {
  uartProtocol_.loop();
  processBluetoothChanges();
  processStatusRequests();
  i2sOutput_.loop();
}

void App::processBluetoothChanges() {
  BluetoothChanges changes{};
  if (!bluetoothService_.takeChanges(changes)) {
    return;
  }

  if (changes.connectionChanged) {
    Logger::info(changes.connection == BtConnectionState::Connected
                     ? "BT connected"
                     : "BT disconnected");
    if (changes.connection == BtConnectionState::Disconnected) {
      i2sOutput_.setActive(false);
    }
  }

  if (changes.artistChanged && changes.artist[0] != '\0') {
    Logger::info("BT artist:", changes.artist);
  }
  if (changes.titleChanged && changes.title[0] != '\0') {
    Logger::info("BT title:", changes.title);
  }
  if (changes.albumChanged && changes.album[0] != '\0') {
    Logger::info("BT album:", changes.album);
  }
  if (changes.peerNameChanged && changes.peerNameKnown &&
      changes.peerName[0] != '\0') {
    Logger::info("BT device:", changes.peerName);
  }

  if (changes.sampleRateChanged && changes.sampleRateKnown) {
    i2sOutput_.setSampleRate(changes.sampleRate);
  }

  if (changes.playbackChanged) {
    i2sOutput_.setActive(changes.playback == BtPlaybackState::Playing);
    switch (changes.playback) {
      case BtPlaybackState::Playing:
        Logger::info("BT playback: PLAYING");
        break;
      case BtPlaybackState::Paused:
        Logger::info("BT playback: PAUSED");
        break;
      case BtPlaybackState::Stopped:
        Logger::info("BT playback: STOPPED");
        break;
    }
  }

  if (changes.unsupportedPlaybackStatus) {
    Logger::warn("BT playback status not supported; state unchanged");
  }

  if (changes.volumeChanged && changes.volumeKnown) {
    Logger::info("BT volume:", changes.volume);
  }
  if (changes.sampleRateChanged && changes.sampleRateKnown) {
    Logger::info("BT sample rate:", changes.sampleRate, "Hz");
  }

  uartProtocol_.sendBluetoothChanges(changes);
}

void App::processStatusRequests() {
  while (uartProtocol_.takeStatusRequest()) {
    BluetoothSnapshot snapshot{};
    bluetoothService_.getSnapshot(snapshot);
    uartProtocol_.sendStatus(snapshot);
  }
}
