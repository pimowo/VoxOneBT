#include "core/App.h"

#include <Arduino.h>

#include "Version.h"
#include "diagnostics/Logger.h"

namespace {

void logCallbackDiagnostics(const char* name,
                            const CallbackDiagnostics& diagnostics) {
  if (diagnostics.count == 0) {
    return;
  }
  Serial.printf("[DIAG] %s count=%lu task=%p core=%lu stackHwm=%lu B\n",
                name, static_cast<unsigned long>(diagnostics.count),
                reinterpret_cast<void*>(diagnostics.taskId),
                static_cast<unsigned long>(diagnostics.core),
                static_cast<unsigned long>(diagnostics.stackHighWaterBytes));
}

}  // namespace

App::App() : i2sOutput_(), bluetoothService_(i2sOutput_), uartProtocol_(Serial2) {}

void App::begin() {
  Logger::info("VoxOneBT", Version::FIRMWARE);
  Logger::info("BOOT");
  uartProtocol_.setAvrcCommandHandler(handleAvrcCommand, this);
  uartProtocol_.setVolumeCommandHandler(handleVolumeCommand, this);
  i2sOutput_.begin();
  bluetoothService_.begin();
  uartProtocol_.begin(bluetoothService_.name());
  Logger::info("UART READY");
  logDiagnostics("boot");
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
  const uint32_t now = millis();
  if (now - lastDiagnosticsMs_ >= 30000U) {
    lastDiagnosticsMs_ = now;
    logDiagnostics("periodic");
  }
}

void App::logDiagnostics(const char* event) {
  Serial.printf("[DIAG] %s heap=%lu minHeap=%lu appStackHwm=%lu B\n",
                event, static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  BluetoothDiagnostics diagnostics{};
  bluetoothService_.getDiagnostics(diagnostics);
  logCallbackDiagnostics("connection", diagnostics.connection);
  logCallbackDiagnostics("peerName", diagnostics.peerName);
  logCallbackDiagnostics("metadata", diagnostics.metadata);
  logCallbackDiagnostics("volume", diagnostics.volume);
  logCallbackDiagnostics("playback", diagnostics.playback);
  logCallbackDiagnostics("sampleRate", diagnostics.sampleRate);
  logCallbackDiagnostics("stream", diagnostics.stream);
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
  if (changes.connectionChanged) {
    logDiagnostics(changes.connection == BtConnectionState::Connected
                       ? "connected" : "disconnected");
  } else if (changes.sampleRateChanged) {
    logDiagnostics("sampleRate");
  }
}

void App::processStatusRequests() {
  while (uartProtocol_.takeStatusRequest()) {
    BluetoothSnapshot snapshot{};
    bluetoothService_.getSnapshot(snapshot);
    uartProtocol_.sendStatus(snapshot);
  }
}
