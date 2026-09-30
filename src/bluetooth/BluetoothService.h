#pragma once

#include <BluetoothA2DPSink.h>
#include <stddef.h>
#include <stdint.h>

#include "AppConfig.h"
#include "audio/I2sOutput.h"
#include "bluetooth/RawVuMeter.h"

constexpr size_t BT_METADATA_MAX_LENGTH = 192;
constexpr size_t BT_PEER_NAME_MAX_LENGTH = 96;

enum class BtConnectionState : uint8_t {
  Disconnected,
  Connected,
};

enum class BtPlaybackState : uint8_t {
  Stopped,
  Playing,
  Paused,
};

enum class AvrcCommand : uint8_t {
  Play,
  Pause,
  Next,
  Previous,
};

struct BluetoothSnapshot {
  BtConnectionState connection = BtConnectionState::Disconnected;
  BtPlaybackState playback = BtPlaybackState::Stopped;
  uint8_t volume = 0;
  bool volumeKnown = false;
  uint32_t sampleRate = 0;
  bool sampleRateKnown = false;
  bool peerNameKnown = false;
  char peerName[BT_PEER_NAME_MAX_LENGTH + 1]{};
  char artist[BT_METADATA_MAX_LENGTH + 1]{};
  char title[BT_METADATA_MAX_LENGTH + 1]{};
  char album[BT_METADATA_MAX_LENGTH + 1]{};
};

struct BluetoothChanges : BluetoothSnapshot {
  bool connectionChanged = false;
  bool playbackChanged = false;
  bool artistChanged = false;
  bool titleChanged = false;
  bool albumChanged = false;
  bool volumeChanged = false;
  bool sampleRateChanged = false;
  bool peerNameChanged = false;
  bool unsupportedPlaybackStatus = false;
  uint8_t unsupportedPlaybackValue = 0;
};

struct CallbackDiagnostics {
  uint32_t count = 0;
  uintptr_t taskId = 0;
  uint32_t core = UINT32_MAX;
  uint32_t stackHighWaterBytes = UINT32_MAX;
};

struct BluetoothDiagnostics {
  CallbackDiagnostics connection;
  CallbackDiagnostics peerName;
  CallbackDiagnostics metadata;
  CallbackDiagnostics volume;
  CallbackDiagnostics playback;
  CallbackDiagnostics sampleRate;
  CallbackDiagnostics stream;
};

class BluetoothService {
 public:
  explicit BluetoothService(I2sOutput& audioOutput);

  void begin();
  const char* name() const { return deviceName_; }
  bool sendAvrcCommand(AvrcCommand command);
  bool setVolume(uint8_t volume);
  bool takeChanges(BluetoothChanges& changes);
  void getSnapshot(BluetoothSnapshot& snapshot) const;
  void getDiagnostics(BluetoothDiagnostics& diagnostics) const;
  bool takeRawVu(uint32_t nowMs, RawVuPeaks& peaks);
  void clearRawVu();

 private:
  class PeerNameSink final : public BluetoothA2DPSink {
   public:
    using PeerNameCallback = void (*)(const char* name);
    void setPeerNameCallback(PeerNameCallback callback) {
      peerNameCallback_ = callback;
    }

   protected:
    void app_gap_callback(esp_bt_gap_cb_event_t event,
                          esp_bt_gap_cb_param_t* param) override;

   private:
    PeerNameCallback peerNameCallback_ = nullptr;
  };

  class DiscardOutput final : public BluetoothA2DPOutput {
   public:
    bool begin() override { return true; }
    size_t write(const uint8_t* data, size_t length) override {
      (void)data;
      return length;
    }
    void end() override {}
    void set_sample_rate(int sampleRate) override { (void)sampleRate; }
    void set_output_active(bool active) override { (void)active; }
  };

  enum PendingFlag : uint16_t {
    ConnectionPending = 1U << 0,
    PlaybackPending = 1U << 1,
    ArtistPending = 1U << 2,
    TitlePending = 1U << 3,
    AlbumPending = 1U << 4,
    UnsupportedPlaybackPending = 1U << 5,
    VolumePending = 1U << 6,
    SampleRatePending = 1U << 7,
    PeerNamePending = 1U << 8,
  };

  static void connectionCallback(esp_a2d_connection_state_t state,
                                 void* context);
  static void playbackCallback(esp_avrc_playback_stat_t state);
  static void metadataCallback(uint8_t attributeId, const uint8_t* text);
  static void volumeCallback(int volume);
  static void sampleRateCallback(uint16_t sampleRate);
  static void peerNameCallback(const char* name);
  static void streamAudio(const uint8_t* data, uint32_t length);
  static void measureRawAudio(const uint8_t* data, uint32_t length);

  void updateConnection(esp_a2d_connection_state_t state);
  void updatePlayback(esp_avrc_playback_stat_t state);
  void updateMetadata(uint8_t attributeId, const uint8_t* text);
  void updateVolume(int volume);
  void updateSampleRate(uint16_t sampleRate);
  void updatePeerName(const char* name);
  void clearSessionStateLocked();
  bool prepareAutoName();

  static BluetoothService* instance_;

  DiscardOutput discardOutput_;
  PeerNameSink a2dpSink_;
  char deviceName_[AppConfig::BLUETOOTH_AUTO_NAME_SIZE]{};
  I2sOutput& audioOutput_;
  mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
  BluetoothSnapshot state_{};
  uint16_t pendingFlags_ = 0;
  uint8_t unsupportedPlaybackValue_ = 0;
  BluetoothDiagnostics diagnostics_{};
  RawVuMeter rawVu_{};
};
