#include "update/FirmwareUpdateReceiver.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t SessionTimeoutMs = 15000;
constexpr uint32_t RestartGraceMs = 100;

const char* reason(FirmwareUpdate::Error error) {
  using FirmwareUpdate::Error;
  switch (error) {
    case Error::InvalidBegin: return "INVALID_BEGIN";
    case Error::InvalidSize: return "INVALID_SIZE";
    case Error::InvalidCrc: return "INVALID_CRC";
    case Error::InvalidVersion: return "INVALID_VERSION";
    case Error::FrameTooLarge: return "FRAME_TOO_LARGE";
    case Error::FrameCrc: return "FRAME_CRC";
    case Error::WrongSequence: return "WRONG_SEQUENCE";
    case Error::DuplicateMismatch: return "DUPLICATE_MISMATCH";
    case Error::SizeOverflow: return "SIZE_OVERFLOW";
    case Error::IncompleteImage: return "INCOMPLETE_IMAGE";
    case Error::ImageCrc: return "IMAGE_CRC";
    case Error::Timeout: return "TIMEOUT";
    case Error::InvalidFrameType: return "INVALID_FRAME_TYPE";
    case Error::InvalidLength: return "INVALID_LENGTH";
    case Error::SequenceOverflow: return "SEQUENCE_OVERFLOW";
    default: return "INVALID_SESSION";
  }
}

}  // namespace

void FirmwareUpdateReceiver::beginCommand(const char* line, uint32_t nowMs) {
  if (exclusive()) {
    hooks_.reply("FW_ERR BUSY");
    return;
  }
  if (!writer_.inspect()) {
    hooks_.reply("FW_ERR NO_OTA_SLOT");
    return;
  }
  FirmwareUpdate::Session candidate;
  const auto result = candidate.begin(line, writer_.maxImageSize(), nowMs);
  if (result.kind != FirmwareUpdate::DecisionKind::Ready) {
    char message[64];
    snprintf(message, sizeof(message), "FW_ERR %s", reason(result.error));
    hooks_.reply(message);
    return;
  }
  if (!hooks_.quiesce()) {
    hooks_.restore();
    hooks_.reply("FW_ERR BT_QUIESCE");
    return;
  }
  if (!writer_.begin(candidate.expectedSize())) {
    writer_.abort();
    hooks_.restore();
    hooks_.reply("FW_ERR OTA_BEGIN");
    return;
  }
  session_ = candidate;
  parser_.reset();
  active_ = true;
  nowMs_ = nowMs;
  hooks_.reply("FW_READY 1024");
}

void FirmwareUpdateReceiver::sendSequence(const char* prefix,
                                           uint32_t sequence, uint32_t bytes) {
  char message[80];
  snprintf(message, sizeof(message), "%s %lu %lu", prefix,
           static_cast<unsigned long>(sequence),
           static_cast<unsigned long>(bytes));
  hooks_.reply(message);
}

void FirmwareUpdateReceiver::finishSession(bool restore) {
  active_ = false;
  parser_.reset();
  session_.reset();
  if (restore) hooks_.restore();
}

void FirmwareUpdateReceiver::fail(const char* error) {
  writer_.abort();
  finishSession(true);
  char message[80];
  snprintf(message, sizeof(message), "FW_ERR %s", error);
  hooks_.reply(message);
}

void FirmwareUpdateReceiver::frameCallback(
    void* context, const FirmwareUpdate::FrameView* frame,
    FirmwareUpdate::ParseError error) {
  static_cast<FirmwareUpdateReceiver*>(context)->onFrame(frame, error);
}

void FirmwareUpdateReceiver::onFrame(const FirmwareUpdate::FrameView* frame,
                                      FirmwareUpdate::ParseError error) {
  if (!active_ || frame == nullptr) return;
  if (error == FirmwareUpdate::ParseError::FrameTooLarge) {
    fail("FRAME_TOO_LARGE");
    return;
  }
  if (error == FirmwareUpdate::ParseError::FrameCrc) {
    char message[80];
    snprintf(message, sizeof(message), "FW_NACK %lu FRAME_CRC",
             static_cast<unsigned long>(frame->sequence));
    hooks_.reply(message);
    return;
  }

  FirmwareUpdate::Session candidate = session_;
  const auto decision = candidate.accept(*frame, nowMs_);
  using FirmwareUpdate::DecisionKind;
  if (decision.kind == DecisionKind::Ack) {
    if (decision.newData && !writer_.write(frame->payload, frame->length)) {
      fail("WRITE_FAILED");
      return;
    }
    session_ = candidate;
    sendSequence("FW_ACK", decision.sequence, decision.confirmedBytes);
    return;
  }
  if (decision.kind == DecisionKind::ImageValid) {
    // FW_VERIFY is informational; FW_OK alone means Update.end(false) passed.
    hooks_.reply("FW_VERIFY");
    if (!writer_.finish()) {
      fail("FINISH_FAILED");
      return;
    }
    session_ = candidate;
    active_ = false;
    restartPending_ = true;
    restartAtMs_ = nowMs_ + RestartGraceMs;
    hooks_.reply("FW_OK");
    hooks_.flushTx();
    return;
  }
  if (decision.kind == DecisionKind::Aborted) {
    writer_.abort();
    finishSession(true);
    hooks_.reply("FW_ABORTED");
    return;
  }
  if (decision.kind == DecisionKind::Nack) {
    if (candidate.state() == FirmwareUpdate::State::Error ||
        decision.error == FirmwareUpdate::Error::SizeOverflow ||
        decision.error == FirmwareUpdate::Error::InvalidState) {
      fail(reason(decision.error));
      return;
    }
    char message[80];
    snprintf(message, sizeof(message), "FW_NACK %lu %s",
             static_cast<unsigned long>(decision.sequence),
             reason(decision.error));
    hooks_.reply(message);
  }
}

void FirmwareUpdateReceiver::feedByte(uint8_t byte, uint32_t nowMs) {
  if (!active_) return;
  nowMs_ = nowMs;
  parser_.feed(&byte, 1, frameCallback, this);
}

void FirmwareUpdateReceiver::tick(uint32_t nowMs) {
  if (!active_) return;
  const auto result = session_.tick(nowMs, SessionTimeoutMs);
  if (result.kind == FirmwareUpdate::DecisionKind::Aborted)
    fail("TIMEOUT");
}
