#pragma once

#include <stdint.h>

#include "protocol/FirmwareUpdateProtocol.h"
#include "update/FirmwareOtaWriter.h"

// UART knows only this boundary; all session and flash state lives elsewhere.
class FirmwareUpdateChannel {
 public:
  virtual ~FirmwareUpdateChannel() = default;
  virtual void beginCommand(const char* line, uint32_t nowMs) = 0;
  virtual void feedByte(uint8_t byte, uint32_t nowMs) = 0;
  virtual void tick(uint32_t nowMs) = 0;
  virtual bool exclusive() const = 0;
  virtual bool restartDue(uint32_t nowMs) const = 0;
};

class FirmwareUpdateHooks {
 public:
  virtual ~FirmwareUpdateHooks() = default;
  virtual bool quiesce() = 0;
  virtual void restore() = 0;
  virtual void reply(const char* line) = 0;
  virtual void flushTx() = 0;
};

class FirmwareUpdateReceiver final : public FirmwareUpdateChannel {
 public:
  FirmwareUpdateReceiver(FirmwareOta::FirmwareOtaWriter& writer,
                         FirmwareUpdateHooks& hooks)
      : writer_(writer), hooks_(hooks) {}

  void beginCommand(const char* line, uint32_t nowMs) override;
  void feedByte(uint8_t byte, uint32_t nowMs) override;
  void tick(uint32_t nowMs) override;
  bool exclusive() const override { return active_ || restartPending_; }
  bool restartDue(uint32_t nowMs) const override {
    return restartPending_ && static_cast<int32_t>(nowMs - restartAtMs_) >= 0;
  }

 private:
  static void frameCallback(void* context, const FirmwareUpdate::FrameView* frame,
                            FirmwareUpdate::ParseError error);
  void onFrame(const FirmwareUpdate::FrameView* frame,
               FirmwareUpdate::ParseError error);
  void fail(const char* reason);
  void finishSession(bool restore);
  void sendSequence(const char* prefix, uint32_t sequence, uint32_t bytes);

  FirmwareOta::FirmwareOtaWriter& writer_;
  FirmwareUpdateHooks& hooks_;
  FirmwareUpdate::Session session_{};
  FirmwareUpdate::FrameParser parser_{};
  uint32_t nowMs_ = 0;
  uint32_t restartAtMs_ = 0;
  bool active_ = false;
  bool restartPending_ = false;
};
