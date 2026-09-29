#include "bluetooth/BluetoothService.h"

#include <esp_mac.h>
#include <stdio.h>
#include <string.h>

#include "diagnostics/Logger.h"

BluetoothService* BluetoothService::instance_ = nullptr;

namespace {

void recordCallback(CallbackDiagnostics& diagnostics, bool audioStream = false) {
  const uint32_t count =
      __atomic_add_fetch(&diagnostics.count, 1U, __ATOMIC_RELAXED);
  if (audioStream && count != 1U && count % 5000U != 0U) {
    return;
  }

  __atomic_store_n(&diagnostics.taskId,
                   reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&diagnostics.core, static_cast<uint32_t>(xPortGetCoreID()),
                   __ATOMIC_RELAXED);
  const uint32_t remaining = uxTaskGetStackHighWaterMark(nullptr);
  uint32_t minimum =
      __atomic_load_n(&diagnostics.stackHighWaterBytes, __ATOMIC_RELAXED);
  while (remaining < minimum &&
         !__atomic_compare_exchange_n(&diagnostics.stackHighWaterBytes,
                                      &minimum, remaining, false,
                                      __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
  }
}

void copyCallbackDiagnostics(CallbackDiagnostics& destination,
                             const CallbackDiagnostics& source) {
  destination.count = __atomic_load_n(&source.count, __ATOMIC_RELAXED);
  destination.taskId = __atomic_load_n(&source.taskId, __ATOMIC_RELAXED);
  destination.core = __atomic_load_n(&source.core, __ATOMIC_RELAXED);
  destination.stackHighWaterBytes =
      __atomic_load_n(&source.stackHighWaterBytes, __ATOMIC_RELAXED);
}

void copyMetadata(char* destination, const uint8_t* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }

  const char* text = reinterpret_cast<const char*>(source);
  const size_t length = strnlen(text, BT_METADATA_MAX_LENGTH);
  memcpy(destination, text, length);
  destination[length] = '\0';

  for (size_t index = 0; index < length; ++index) {
    if (destination[index] == '\r' || destination[index] == '\n') {
      destination[index] = ' ';
    }
  }
}

void copyPeerName(char* destination, const char* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }

  size_t length = strnlen(source, BT_PEER_NAME_MAX_LENGTH);
  if (length == BT_PEER_NAME_MAX_LENGTH && source[length] != '\0') {
    size_t codePointStart = length - 1;
    while (codePointStart > 0 &&
           (static_cast<uint8_t>(source[codePointStart]) & 0xC0U) == 0x80U) {
      --codePointStart;
    }
    const uint8_t lead = static_cast<uint8_t>(source[codePointStart]);
    const size_t expectedLength =
        (lead & 0x80U) == 0 ? 1U
        : (lead & 0xE0U) == 0xC0U ? 2U
        : (lead & 0xF0U) == 0xE0U ? 3U
        : (lead & 0xF8U) == 0xF0U ? 4U
                                  : 1U;
    if (length - codePointStart < expectedLength) {
      length = codePointStart;
    }
  }

  memcpy(destination, source, length);
  destination[length] = '\0';
  for (size_t index = 0; index < length; ++index) {
    if (destination[index] == '\r' || destination[index] == '\n') {
      destination[index] = ' ';
    }
  }
}

}  // namespace

void BluetoothService::PeerNameSink::app_gap_callback(
    esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  BluetoothA2DPSink::app_gap_callback(event, param);
  if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT && param != nullptr &&
      param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS &&
      peerNameCallback_ != nullptr) {
    peerNameCallback_(get_peer_name());
  }
}

BluetoothService::BluetoothService(I2sOutput& audioOutput)
    : audioOutput_(audioOutput) {
  instance_ = this;
  // Prevent the library's unconditional audio-config update from touching its
  // default legacy I2S output while physical audio is intentionally disabled.
  a2dpSink_.set_output(discardOutput_);
}

void BluetoothService::begin() {
  if (!prepareAutoName()) {
    Logger::error("Bluetooth MAC read failed");
    return;
  }

  a2dpSink_.setPeerNameCallback(peerNameCallback);
  a2dpSink_.set_on_connection_state_changed(connectionCallback, this);
  a2dpSink_.set_avrc_rn_playstatus_callback(playbackCallback);
  a2dpSink_.set_avrc_metadata_attribute_mask(
      ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
      ESP_AVRC_MD_ATTR_ALBUM);
  a2dpSink_.set_avrc_metadata_callback(metadataCallback);
  a2dpSink_.set_avrc_rn_volumechange(volumeCallback);
  a2dpSink_.set_sample_rate_callback(sampleRateCallback);

  // Application-owned I2S is the only physical output. The false argument
  // prevents ESP32-A2DP from also writing through its legacy I2S backend.
  a2dpSink_.set_stream_reader(streamAudio, false);
  a2dpSink_.start(deviceName_, false);

  Logger::info("Bluetooth initialized");
  Logger::info("A2DP Sink started as", deviceName_);
}

bool BluetoothService::prepareAutoName() {
  uint8_t btMac[6];
  if (esp_read_mac(btMac, ESP_MAC_BT) != ESP_OK) {
    return false;
  }

  snprintf(deviceName_, sizeof(deviceName_), "%s%02X%02X%02X",
           AppConfig::BLUETOOTH_NAME_PREFIX,
           static_cast<unsigned int>(btMac[3]),
           static_cast<unsigned int>(btMac[4]),
           static_cast<unsigned int>(btMac[5]));
  return true;
}

bool BluetoothService::sendAvrcCommand(AvrcCommand command) {
  portENTER_CRITICAL(&stateMux_);
  const bool connected = state_.connection == BtConnectionState::Connected;
  portEXIT_CRITICAL(&stateMux_);

  if (!connected) {
    Logger::warn("AVRCP command rejected: not connected");
    return false;
  }

  switch (command) {
    case AvrcCommand::Play:
      Logger::info("AVRCP command: PLAY");
      a2dpSink_.play();
      break;
    case AvrcCommand::Pause:
      Logger::info("AVRCP command: PAUSE");
      a2dpSink_.pause();
      break;
    case AvrcCommand::Next:
      Logger::info("AVRCP command: NEXT");
      a2dpSink_.next();
      break;
    case AvrcCommand::Previous:
      Logger::info("AVRCP command: PREV");
      a2dpSink_.previous();
      break;
  }

  return true;
}

bool BluetoothService::setVolume(uint8_t volume) {
  portENTER_CRITICAL(&stateMux_);
  const bool connected = state_.connection == BtConnectionState::Connected;
  portEXIT_CRITICAL(&stateMux_);

  if (!connected) {
    Logger::warn("AVRCP volume rejected: not connected");
    return false;
  }

  Logger::info("AVRCP volume command:", volume);
  a2dpSink_.set_volume(volume);
  return true;
}

bool BluetoothService::takeChanges(BluetoothChanges& changes) {
  portENTER_CRITICAL(&stateMux_);
  const uint16_t flags = pendingFlags_;
  if (flags == 0) {
    portEXIT_CRITICAL(&stateMux_);
    return false;
  }

  changes.connection = state_.connection;
  changes.playback = state_.playback;
  changes.volume = state_.volume;
  changes.volumeKnown = state_.volumeKnown;
  changes.sampleRate = state_.sampleRate;
  changes.sampleRateKnown = state_.sampleRateKnown;
  changes.peerNameKnown = state_.peerNameKnown;
  memcpy(changes.peerName, state_.peerName, sizeof(changes.peerName));
  memcpy(changes.artist, state_.artist, sizeof(changes.artist));
  memcpy(changes.title, state_.title, sizeof(changes.title));
  memcpy(changes.album, state_.album, sizeof(changes.album));
  changes.connectionChanged = (flags & ConnectionPending) != 0;
  changes.playbackChanged = (flags & PlaybackPending) != 0;
  changes.artistChanged = (flags & ArtistPending) != 0;
  changes.titleChanged = (flags & TitlePending) != 0;
  changes.albumChanged = (flags & AlbumPending) != 0;
  changes.volumeChanged = (flags & VolumePending) != 0;
  changes.sampleRateChanged = (flags & SampleRatePending) != 0;
  changes.peerNameChanged = (flags & PeerNamePending) != 0;
  changes.unsupportedPlaybackStatus =
      (flags & UnsupportedPlaybackPending) != 0;
  changes.unsupportedPlaybackValue = unsupportedPlaybackValue_;
  pendingFlags_ = 0;
  portEXIT_CRITICAL(&stateMux_);
  return true;
}

void BluetoothService::getSnapshot(BluetoothSnapshot& snapshot) const {
  portENTER_CRITICAL(&stateMux_);
  snapshot.connection = state_.connection;
  snapshot.playback = state_.playback;
  snapshot.volume = state_.volume;
  snapshot.volumeKnown = state_.volumeKnown;
  snapshot.sampleRate = state_.sampleRate;
  snapshot.sampleRateKnown = state_.sampleRateKnown;
  snapshot.peerNameKnown = state_.peerNameKnown;
  memcpy(snapshot.peerName, state_.peerName, sizeof(snapshot.peerName));
  memcpy(snapshot.artist, state_.artist, sizeof(snapshot.artist));
  memcpy(snapshot.title, state_.title, sizeof(snapshot.title));
  memcpy(snapshot.album, state_.album, sizeof(snapshot.album));
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::getDiagnostics(BluetoothDiagnostics& diagnostics) const {
  copyCallbackDiagnostics(diagnostics.connection, diagnostics_.connection);
  copyCallbackDiagnostics(diagnostics.peerName, diagnostics_.peerName);
  copyCallbackDiagnostics(diagnostics.metadata, diagnostics_.metadata);
  copyCallbackDiagnostics(diagnostics.volume, diagnostics_.volume);
  copyCallbackDiagnostics(diagnostics.playback, diagnostics_.playback);
  copyCallbackDiagnostics(diagnostics.sampleRate, diagnostics_.sampleRate);
  copyCallbackDiagnostics(diagnostics.stream, diagnostics_.stream);
}

void BluetoothService::connectionCallback(esp_a2d_connection_state_t state,
                                          void* context) {
  BluetoothService* service = static_cast<BluetoothService*>(context);
  if (service != nullptr) {
    recordCallback(service->diagnostics_.connection);
    service->updateConnection(state);
  }
}

void BluetoothService::playbackCallback(esp_avrc_playback_stat_t state) {
  if (instance_ != nullptr) {
    recordCallback(instance_->diagnostics_.playback);
    instance_->updatePlayback(state);
  }
}

void BluetoothService::metadataCallback(uint8_t attributeId,
                                        const uint8_t* text) {
  if (instance_ != nullptr && text != nullptr) {
    recordCallback(instance_->diagnostics_.metadata);
    instance_->updateMetadata(attributeId, text);
  }
}

void BluetoothService::volumeCallback(int volume) {
  if (instance_ != nullptr) {
    recordCallback(instance_->diagnostics_.volume);
    instance_->updateVolume(volume);
  }
}

void BluetoothService::sampleRateCallback(uint16_t sampleRate) {
  if (instance_ != nullptr) {
    recordCallback(instance_->diagnostics_.sampleRate);
    instance_->updateSampleRate(sampleRate);
  }
}

void BluetoothService::peerNameCallback(const char* name) {
  if (instance_ != nullptr) {
    recordCallback(instance_->diagnostics_.peerName);
    instance_->updatePeerName(name);
  }
}

void BluetoothService::streamAudio(const uint8_t* data, uint32_t length) {
  if (instance_ != nullptr) {
    recordCallback(instance_->diagnostics_.stream, true);
    instance_->audioOutput_.write(data, length);
  }
}

void BluetoothService::updateConnection(esp_a2d_connection_state_t state) {
  if (state != ESP_A2D_CONNECTION_STATE_CONNECTED &&
      state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    return;
  }

  const BtConnectionState next =
      state == ESP_A2D_CONNECTION_STATE_CONNECTED
          ? BtConnectionState::Connected
          : BtConnectionState::Disconnected;

  portENTER_CRITICAL(&stateMux_);
  if (state_.connection != next) {
    state_.connection = next;
    pendingFlags_ |= ConnectionPending;
  }

  if (next == BtConnectionState::Disconnected) {
    clearSessionStateLocked();
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::updatePlayback(esp_avrc_playback_stat_t state) {
  BtPlaybackState next;
  switch (state) {
    case ESP_AVRC_PLAYBACK_STOPPED:
      next = BtPlaybackState::Stopped;
      break;
    case ESP_AVRC_PLAYBACK_PLAYING:
      next = BtPlaybackState::Playing;
      break;
    case ESP_AVRC_PLAYBACK_PAUSED:
      next = BtPlaybackState::Paused;
      break;
    default:
      portENTER_CRITICAL(&stateMux_);
      unsupportedPlaybackValue_ = static_cast<uint8_t>(state);
      pendingFlags_ |= UnsupportedPlaybackPending;
      portEXIT_CRITICAL(&stateMux_);
      return;
  }

  portENTER_CRITICAL(&stateMux_);
  if (state_.playback != next) {
    state_.playback = next;
    pendingFlags_ |= PlaybackPending;
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::updateMetadata(uint8_t attributeId,
                                      const uint8_t* text) {
  char value[BT_METADATA_MAX_LENGTH + 1]{};
  copyMetadata(value, text);

  char* destination = nullptr;
  PendingFlag flag = ArtistPending;
  switch (attributeId) {
    case ESP_AVRC_MD_ATTR_ARTIST:
      destination = state_.artist;
      flag = ArtistPending;
      break;
    case ESP_AVRC_MD_ATTR_TITLE:
      destination = state_.title;
      flag = TitlePending;
      break;
    case ESP_AVRC_MD_ATTR_ALBUM:
      destination = state_.album;
      flag = AlbumPending;
      break;
    default:
      return;
  }

  portENTER_CRITICAL(&stateMux_);
  if (strncmp(destination, value, BT_METADATA_MAX_LENGTH + 1) != 0) {
    memcpy(destination, value, sizeof(value));
    pendingFlags_ |= flag;
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::updateVolume(int volume) {
  if (volume < 0 || volume > 127) {
    return;
  }

  portENTER_CRITICAL(&stateMux_);
  const uint8_t value = static_cast<uint8_t>(volume);
  if (!state_.volumeKnown || state_.volume != value) {
    state_.volume = value;
    state_.volumeKnown = true;
    pendingFlags_ |= VolumePending;
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::updateSampleRate(uint16_t sampleRate) {
  if (sampleRate == 0) {
    return;
  }

  portENTER_CRITICAL(&stateMux_);
  if (!state_.sampleRateKnown || state_.sampleRate != sampleRate) {
    state_.sampleRate = sampleRate;
    state_.sampleRateKnown = true;
    pendingFlags_ |= SampleRatePending;
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::updatePeerName(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }

  char value[BT_PEER_NAME_MAX_LENGTH + 1]{};
  copyPeerName(value, name);
  if (value[0] == '\0') {
    return;
  }

  portENTER_CRITICAL(&stateMux_);
  if (state_.connection == BtConnectionState::Connected &&
      (!state_.peerNameKnown ||
       strncmp(state_.peerName, value, sizeof(state_.peerName)) != 0)) {
    memcpy(state_.peerName, value, sizeof(value));
    state_.peerNameKnown = true;
    pendingFlags_ |= PeerNamePending;
  }
  portEXIT_CRITICAL(&stateMux_);
}

void BluetoothService::clearSessionStateLocked() {
  state_.playback = BtPlaybackState::Stopped;
  state_.artist[0] = '\0';
  state_.title[0] = '\0';
  state_.album[0] = '\0';
  state_.volumeKnown = false;
  state_.sampleRateKnown = false;
  state_.peerNameKnown = false;
  state_.peerName[0] = '\0';
  pendingFlags_ &= static_cast<uint16_t>(
      ~(PlaybackPending | ArtistPending | TitlePending | AlbumPending |
        VolumePending | SampleRatePending | PeerNamePending));
}
