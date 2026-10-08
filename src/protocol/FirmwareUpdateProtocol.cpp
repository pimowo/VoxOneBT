#include "protocol/FirmwareUpdateProtocol.h"

#include <string.h>

namespace FirmwareUpdate {
namespace {

uint16_t read16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void write16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* data, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i)
    data[i] = static_cast<uint8_t>(value >> (8U * i));
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool versionChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
}

}  // namespace

void Crc32::add(const uint8_t* data, size_t length) {
  if (data == nullptr) return;
  for (size_t i = 0; i < length; ++i) {
    value_ ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      value_ = (value_ >> 1) ^ ((value_ & 1U) ? 0xEDB88320U : 0U);
  }
}

size_t encodeFrame(uint8_t type, uint32_t sequence, const uint8_t* payload,
                   size_t length, uint8_t* output, size_t capacity) {
  if (output == nullptr || length > MaxPayload ||
      (length != 0 && payload == nullptr) || capacity < HeaderSize + length + TrailerSize)
    return 0;
  output[0] = Magic0;
  output[1] = Magic1;
  output[2] = type;
  write32(output + 3, sequence);
  write16(output + 7, static_cast<uint16_t>(length));
  if (length != 0) memcpy(output + HeaderSize, payload, length);
  Crc32 crc;
  crc.add(output + 2, HeaderSize - 2 + length);
  write32(output + HeaderSize + length, crc.value());
  return HeaderSize + length + TrailerSize;
}

void FrameParser::resyncHeader() {
  for (size_t i = 1; i < used_; ++i) {
    if (buffer_[i] == Magic0) {
      const size_t remaining = used_ - i;
      memmove(buffer_, buffer_ + i, remaining);
      used_ = remaining;
      return;
    }
  }
  used_ = 0;
}

void FrameParser::process(Handler handler, void* context) {
  while (used_ != 0) {
    if (buffer_[0] != Magic0) {
      resyncHeader();
      continue;
    }
    if (used_ < 2) return;
    if (buffer_[1] != Magic1) {
      resyncHeader();
      continue;
    }
    if (used_ < HeaderSize) return;
    const uint16_t length = read16(buffer_ + 7);
    if (length > MaxPayload) {
      FrameView frame;
      frame.type = buffer_[2];
      frame.sequence = read32(buffer_ + 3);
      frame.length = length;
      if (handler) handler(context, &frame, ParseError::FrameTooLarge);
      resyncHeader();
      continue;
    }
    const size_t frameSize = HeaderSize + static_cast<size_t>(length) + TrailerSize;
    if (used_ < frameSize) return;
    FrameView frame;
    frame.type = buffer_[2];
    frame.sequence = read32(buffer_ + 3);
    frame.length = length;
    frame.payload = buffer_ + HeaderSize;
    frame.frameCrc = read32(buffer_ + HeaderSize + length);
    Crc32 crc;
    crc.add(buffer_ + 2, HeaderSize - 2 + length);
    if (handler)
      handler(context, &frame, crc.value() == frame.frameCrc
                                   ? ParseError::None : ParseError::FrameCrc);
    used_ = 0;
  }
}

void FrameParser::feed(const uint8_t* data, size_t length, Handler handler,
                       void* context) {
  if (data == nullptr) return;
  for (size_t i = 0; i < length; ++i) {
    if (used_ < MaxFrameSize) buffer_[used_++] = data[i];
    process(handler, context);
  }
}

void Session::clearTransient() {
  expectedSize_ = 0;
  expectedCrc_ = 0;
  confirmedBytes_ = 0;
  expectedSequence_ = 0;
  lastActivityMs_ = 0;
  lastSequence_ = 0;
  lastFrameCrc_ = 0;
  lastLength_ = 0;
  hasLastData_ = false;
  version_[0] = '\0';
  imageCrc_.reset();
}

void Session::reset() {
  clearTransient();
  state_ = State::Idle;
}

Decision Session::reply(DecisionKind kind, Error error, uint32_t sequence,
                        bool newData) const {
  Decision result;
  result.kind = kind;
  result.error = error;
  result.sequence = sequence;
  result.confirmedBytes = confirmedBytes_;
  result.newData = newData;
  return result;
}

Decision Session::fatal(Error error, uint32_t sequence) {
  state_ = State::Error;
  return reply(DecisionKind::Nack, error, sequence);
}

Decision Session::begin(const char* line, uint32_t maxImageSize,
                        uint32_t nowMs) {
  if (state_ == State::Receiving || state_ == State::ReadyForCommit)
    return reply(DecisionKind::Nack, Error::InvalidState);
  if (line == nullptr) return reply(DecisionKind::Nack, Error::InvalidBegin);
  size_t lineLength = 0;
  while (lineLength <= MaxBeginLine && line[lineLength] != '\0') ++lineLength;
  if (lineLength > MaxBeginLine || lineLength < 10 ||
      strncmp(line, "FW_BEGIN ", 9) != 0)
    return reply(DecisionKind::Nack, Error::InvalidBegin);

  const char* p = line + 9;
  if (*p < '0' || *p > '9')
    return reply(DecisionKind::Nack, Error::InvalidSize);
  uint32_t size = 0;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = static_cast<uint32_t>(*p++ - '0');
    if (size > (UINT32_MAX - digit) / 10U)
      return reply(DecisionKind::Nack, Error::InvalidSize);
    size = size * 10U + digit;
  }
  if (size == 0 || size > maxImageSize)
    return reply(DecisionKind::Nack, Error::InvalidSize);
  if (*p++ != ' ') return reply(DecisionKind::Nack, Error::InvalidBegin);
  uint32_t expectedCrc = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (*p == '\0') return reply(DecisionKind::Nack, Error::InvalidCrc);
    const int digit = hexDigit(*p++);
    if (digit < 0) return reply(DecisionKind::Nack, Error::InvalidCrc);
    expectedCrc = (expectedCrc << 4) | static_cast<uint32_t>(digit);
  }
  if (*p++ != ' ') return reply(DecisionKind::Nack, Error::InvalidCrc);
  size_t versionLength = 0;
  while (*p != '\0' && versionLength <= MaxVersion && versionChar(*p)) {
    ++p;
    ++versionLength;
  }
  if (versionLength == 0 || versionLength > MaxVersion || *p != '\0')
    return reply(DecisionKind::Nack, Error::InvalidVersion);

  clearTransient();
  expectedSize_ = size;
  expectedCrc_ = expectedCrc;
  memcpy(version_, p - versionLength, versionLength);
  version_[versionLength] = '\0';
  lastActivityMs_ = nowMs;
  state_ = State::Receiving;
  return reply(DecisionKind::Ready);
}

Decision Session::accept(const FrameView& frame, uint32_t nowMs) {
  if (frame.length > MaxPayload)
    return reply(DecisionKind::Nack, Error::FrameTooLarge, frame.sequence);
  if (frame.length != 0 && frame.payload == nullptr)
    return reply(DecisionKind::Nack, Error::InvalidLength, frame.sequence);
  if (state_ == State::Receiving &&
      frame.type == static_cast<uint8_t>(FrameType::Data) && hasLastData_ &&
      frame.sequence == lastSequence_ &&
      (frame.length != lastLength_ || frame.frameCrc != lastFrameCrc_ ||
       memcmp(frame.payload, lastPayload_, frame.length) != 0))
    return fatal(Error::DuplicateMismatch, frame.sequence);
  uint8_t header[HeaderSize - 2];
  header[0] = frame.type;
  write32(header + 1, frame.sequence);
  write16(header + 5, frame.length);
  Crc32 frameCrc;
  frameCrc.add(header, sizeof(header));
  frameCrc.add(frame.payload, frame.length);
  if (frameCrc.value() != frame.frameCrc)
    return reply(DecisionKind::Nack, Error::FrameCrc, frame.sequence);

  if (state_ == State::ReadyForCommit &&
      frame.type == static_cast<uint8_t>(FrameType::End) &&
      frame.sequence == expectedSequence_ && frame.length == 0)
    return reply(DecisionKind::ImageValid, Error::None, frame.sequence);
  if (state_ != State::Receiving)
    return reply(DecisionKind::Nack, Error::InvalidState, frame.sequence);

  if (frame.type == static_cast<uint8_t>(FrameType::Abort)) {
    if (frame.length != 0)
      return reply(DecisionKind::Nack, Error::InvalidLength, frame.sequence);
    if (frame.sequence != expectedSequence_)
      return reply(DecisionKind::Nack, Error::WrongSequence, frame.sequence);
    Decision result = abort();
    result.sequence = frame.sequence;
    return result;
  }
  if (frame.type == static_cast<uint8_t>(FrameType::End)) {
    if (frame.length != 0)
      return reply(DecisionKind::Nack, Error::InvalidLength, frame.sequence);
    if (frame.sequence != expectedSequence_)
      return reply(DecisionKind::Nack, Error::WrongSequence, frame.sequence);
    if (confirmedBytes_ != expectedSize_)
      return reply(DecisionKind::Nack, Error::IncompleteImage, frame.sequence);
    if (imageCrc_.value() != expectedCrc_)
      return fatal(Error::ImageCrc, frame.sequence);
    lastActivityMs_ = nowMs;
    state_ = State::ReadyForCommit;
    return reply(DecisionKind::ImageValid, Error::None, frame.sequence);
  }
  if (frame.type != static_cast<uint8_t>(FrameType::Data))
    return reply(DecisionKind::Nack, Error::InvalidFrameType, frame.sequence);
  if (frame.length == 0)
    return reply(DecisionKind::Nack, Error::InvalidLength, frame.sequence);

  if (hasLastData_ && frame.sequence == lastSequence_) {
    lastActivityMs_ = nowMs;
    return reply(DecisionKind::Ack, Error::None, frame.sequence);
  }
  if (frame.sequence != expectedSequence_)
    return reply(DecisionKind::Nack, Error::WrongSequence, frame.sequence);
  if (frame.length > expectedSize_ - confirmedBytes_)
    return reply(DecisionKind::Nack, Error::SizeOverflow, frame.sequence);
  if (expectedSequence_ == UINT32_MAX)
    return fatal(Error::SequenceOverflow, frame.sequence);

  imageCrc_.add(frame.payload, frame.length);
  confirmedBytes_ += frame.length;
  lastSequence_ = frame.sequence;
  lastLength_ = frame.length;
  lastFrameCrc_ = frame.frameCrc;
  memcpy(lastPayload_, frame.payload, frame.length);
  hasLastData_ = true;
  ++expectedSequence_;
  lastActivityMs_ = nowMs;
  return reply(DecisionKind::Ack, Error::None, frame.sequence, true);
}

Decision Session::abort() {
  const uint32_t confirmed = confirmedBytes_;
  clearTransient();
  state_ = State::Aborted;
  Decision result = reply(DecisionKind::Aborted);
  result.confirmedBytes = confirmed;
  return result;
}

Decision Session::tick(uint32_t nowMs, uint32_t timeoutMs) {
  if (state_ != State::Receiving ||
      static_cast<uint32_t>(nowMs - lastActivityMs_) <= timeoutMs)
    return reply(DecisionKind::None);
  Decision result = abort();
  result.error = Error::Timeout;
  return result;
}

}  // namespace FirmwareUpdate
