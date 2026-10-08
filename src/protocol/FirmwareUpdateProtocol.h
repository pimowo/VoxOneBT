#pragma once

#include <stddef.h>
#include <stdint.h>

namespace FirmwareUpdate {

constexpr uint8_t Magic0 = 0xB7;
constexpr uint8_t Magic1 = 0x4F;
constexpr size_t MaxPayload = 1024;
constexpr size_t HeaderSize = 9;
constexpr size_t TrailerSize = 4;
constexpr size_t MaxFrameSize = HeaderSize + MaxPayload + TrailerSize;
constexpr size_t MaxBeginLine = 64;
constexpr size_t MaxVersion = 24;

enum class FrameType : uint8_t { Data = 1, End = 2, Abort = 3 };
enum class ParseError : uint8_t { None, FrameTooLarge, FrameCrc };

struct FrameView {
  uint8_t type = 0;
  uint32_t sequence = 0;
  uint16_t length = 0;
  const uint8_t* payload = nullptr;  // Valid only during the parser callback.
  uint32_t frameCrc = 0;
};

class Crc32 {
 public:
  void reset() { value_ = 0xFFFFFFFFU; }
  void add(const uint8_t* data, size_t length);
  uint32_t value() const { return value_ ^ 0xFFFFFFFFU; }

 private:
  uint32_t value_ = 0xFFFFFFFFU;
};

// All numeric fields are little-endian. CRC-32/ISO-HDLC covers bytes from
// type through payload, excluding magic and the CRC trailer.
size_t encodeFrame(uint8_t type, uint32_t sequence, const uint8_t* payload,
                   size_t length, uint8_t* output, size_t capacity);

class FrameParser {
 public:
  using Handler = void (*)(void* context, const FrameView* frame,
                           ParseError error);

  void feed(const uint8_t* data, size_t length, Handler handler, void* context);
  void reset() { used_ = 0; }

 private:
  void process(Handler handler, void* context);
  void resyncHeader();

  uint8_t buffer_[MaxFrameSize]{};
  size_t used_ = 0;
};

enum class State : uint8_t { Idle, Receiving, ReadyForCommit, Error, Aborted };
enum class DecisionKind : uint8_t { None, Ready, Ack, Nack, ImageValid, Aborted };
enum class Error : uint8_t {
  None,
  InvalidBegin,
  InvalidSize,
  InvalidCrc,
  InvalidVersion,
  FrameTooLarge,
  FrameCrc,
  WrongSequence,
  DuplicateMismatch,
  SizeOverflow,
  IncompleteImage,
  ImageCrc,
  Timeout,
  InvalidState,
  InvalidFrameType,
  InvalidLength,
  SequenceOverflow,
};

struct Decision {
  DecisionKind kind = DecisionKind::None;
  Error error = Error::None;
  uint32_t sequence = 0;
  uint32_t confirmedBytes = 0;
  bool newData = false;  // False for an ACK of an already accepted frame.
};

class Session {
 public:
  void reset();
  Decision begin(const char* line, uint32_t maxImageSize, uint32_t nowMs);
  Decision accept(const FrameView& frame, uint32_t nowMs);
  Decision tick(uint32_t nowMs, uint32_t timeoutMs);
  Decision abort();

  State state() const { return state_; }
  uint32_t expectedSize() const { return expectedSize_; }
  uint32_t confirmedBytes() const { return confirmedBytes_; }
  uint32_t expectedSequence() const { return expectedSequence_; }
  uint32_t imageCrc() const { return imageCrc_.value(); }
  const char* version() const { return version_; }

 private:
  Decision reply(DecisionKind kind, Error error = Error::None,
                 uint32_t sequence = 0, bool newData = false) const;
  Decision fatal(Error error, uint32_t sequence);
  void clearTransient();

  State state_ = State::Idle;
  uint32_t expectedSize_ = 0;
  uint32_t expectedCrc_ = 0;
  uint32_t confirmedBytes_ = 0;
  uint32_t expectedSequence_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastSequence_ = 0;
  uint32_t lastFrameCrc_ = 0;
  uint16_t lastLength_ = 0;
  bool hasLastData_ = false;
  uint8_t lastPayload_[MaxPayload]{};
  char version_[MaxVersion + 1]{};
  Crc32 imageCrc_{};
};

}  // namespace FirmwareUpdate
