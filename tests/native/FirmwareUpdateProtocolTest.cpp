#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "protocol/FirmwareUpdateProtocol.h"

using namespace FirmwareUpdate;

namespace {

std::vector<uint8_t> frame(FrameType type, uint32_t sequence,
                           const std::vector<uint8_t>& payload = {}) {
  uint8_t bytes[MaxFrameSize]{};
  const size_t length = encodeFrame(static_cast<uint8_t>(type), sequence,
                                    payload.empty() ? nullptr : payload.data(),
                                    payload.size(), bytes, sizeof(bytes));
  assert(length == HeaderSize + payload.size() + TrailerSize);
  return std::vector<uint8_t>(bytes, bytes + length);
}

uint32_t crcOf(const std::vector<uint8_t>& bytes) {
  Crc32 crc;
  crc.add(bytes.data(), bytes.size());
  return crc.value();
}

Decision begin(Session& session, uint32_t size, uint32_t crc,
               uint32_t maxSize = 2048, uint32_t nowMs = 100) {
  char line[MaxBeginLine + 1]{};
  std::snprintf(line, sizeof(line), "FW_BEGIN %u %08X 0.7.0-dev", size, crc);
  return session.begin(line, maxSize, nowMs);
}

struct Sink {
  explicit Sink(Session* active = nullptr) : session(active) {}
  Session* session = nullptr;
  uint32_t nowMs = 101;
  std::vector<Decision> decisions;
  std::vector<ParseError> errors;

  static void receive(void* context, const FrameView* view, ParseError error) {
    Sink& sink = *static_cast<Sink*>(context);
    sink.errors.push_back(error);
    if (view != nullptr && sink.session != nullptr)
      sink.decisions.push_back(sink.session->accept(*view, sink.nowMs));
  }
};

void feed(FrameParser& parser, const std::vector<uint8_t>& bytes, Sink& sink,
          size_t stride = 0) {
  if (stride == 0) stride = bytes.size();
  for (size_t offset = 0; offset < bytes.size(); offset += stride) {
    const size_t length = bytes.size() - offset < stride
                              ? bytes.size() - offset : stride;
    parser.feed(bytes.data() + offset, length, Sink::receive, &sink);
  }
}

}  // namespace

int main() {
  const uint8_t digits[] = {'1','2','3','4','5','6','7','8','9'};
  Crc32 known;
  known.add(digits, sizeof(digits));
  assert(known.value() == 0xCBF43926U); // CRC-32/ISO-HDLC known vector

  Session session;
  assert(begin(session, 1, crcOf({0x2A})).kind == DecisionKind::Ready);
  assert(session.state() == State::Receiving);
  assert(std::strcmp(session.version(), "0.7.0-dev") == 0);
  session.reset();
  assert(begin(session, 0, 0).error == Error::InvalidSize);
  assert(begin(session, 2049, 0).error == Error::InvalidSize);
  assert(session.begin("FW_BEGIN 1 GG000000 v", 2048, 100).error == Error::InvalidCrc);
  assert(session.begin("FW_BEGIN 1 00000000", 2048, 100).error == Error::InvalidCrc);
  assert(session.begin("FW_BEGIN 1 00000000 ", 2048, 100).error == Error::InvalidVersion);
  assert(session.begin("FW_BEGIN 4294967296 00000000 v", UINT32_MAX, 100).error == Error::InvalidSize);
  assert(session.begin("FW_BEGIN 1 00000000 version with spaces", 2048, 100).error == Error::InvalidVersion);
  assert(begin(session, 2048, 0, 2048).kind == DecisionKind::Ready);
  assert(session.expectedSize() == 2048);
  assert(begin(session, 1, 0).error == Error::InvalidState);

  // Explicit little-endian serialization, byte-at-a-time and a one-byte chunk.
  const std::vector<uint8_t> one = {0x2A};
  const std::vector<uint8_t> encoded = frame(FrameType::Data, 0x12345678U, one);
  assert(encoded[0] == Magic0 && encoded[1] == Magic1);
  assert(encoded[2] == 1 && encoded[3] == 0x78 && encoded[4] == 0x56);
  assert(encoded[5] == 0x34 && encoded[6] == 0x12);
  assert(encoded[7] == 1 && encoded[8] == 0);
  assert(encodeFrame(1, 0, one.data(), MaxPayload + 1,
                     nullptr, 0) == 0);
  session.reset();
  assert(begin(session, 1, crcOf(one)).kind == DecisionKind::Ready);
  FrameParser parser;
  Sink sink{&session};
  feed(parser, frame(FrameType::Data, 0, one), sink, 1);
  assert(sink.decisions.size() == 1);
  assert(sink.decisions[0].kind == DecisionKind::Ack && sink.decisions[0].newData);
  assert(sink.decisions[0].confirmedBytes == 1 && session.expectedSequence() == 1);
  feed(parser, frame(FrameType::End, 1), sink, 7);
  assert(sink.decisions.back().kind == DecisionKind::ImageValid);
  assert(session.state() == State::ReadyForCommit);
  feed(parser, frame(FrameType::End, 1), sink);
  assert(sink.decisions.back().kind == DecisionKind::ImageValid);

  // Two DATA frames in one feed, with a full 1024-byte chunk.
  std::vector<uint8_t> full(MaxPayload);
  for (size_t i = 0; i < full.size(); ++i) full[i] = static_cast<uint8_t>(i);
  std::vector<uint8_t> image = full;
  image.push_back(0xA5);
  session.reset();
  assert(begin(session, static_cast<uint32_t>(image.size()), crcOf(image)).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  std::vector<uint8_t> combined = frame(FrameType::Data, 0, full);
  const std::vector<uint8_t> second = frame(FrameType::Data, 1, {0xA5});
  combined.insert(combined.end(), second.begin(), second.end());
  feed(parser, combined, sink); // Both complete frames in one feed() call.
  assert(sink.decisions.size() == 2);
  assert(sink.decisions[0].newData && sink.decisions[1].newData);
  assert(session.confirmedBytes() == 1025 && session.imageCrc() == crcOf(image));
  feed(parser, frame(FrameType::End, 2), sink);
  assert(sink.decisions.back().kind == DecisionKind::ImageValid);
  parser.reset();
  Sink fragmented;
  feed(parser, combined, fragmented, 200);
  assert(fragmented.errors.size() == 2);

  // CRC failure does not advance the image and parsing resumes at the next frame.
  session.reset();
  assert(begin(session, 1, crcOf(one)).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  std::vector<uint8_t> damaged = frame(FrameType::Data, 0, one);
  damaged.back() ^= 0x80;
  const std::vector<uint8_t> good = frame(FrameType::Data, 0, one);
  damaged.insert(damaged.end(), good.begin(), good.end());
  feed(parser, damaged, sink);
  assert(sink.errors[0] == ParseError::FrameCrc);
  assert(sink.decisions[0].error == Error::FrameCrc);
  assert(sink.decisions[1].kind == DecisionKind::Ack);
  assert(session.confirmedBytes() == 1);

  // Oversized length, garbage, and partial frames recover around magic.
  parser.reset();
  sink = Sink{};
  const uint8_t garbage[] = {0x00, 0xB7, 0x00, 0x42};
  parser.feed(garbage, sizeof(garbage), Sink::receive, &sink);
  uint8_t tooLarge[HeaderSize] = {Magic0, Magic1, 1, 0, 0, 0, 0, 0x01, 0x04};
  parser.feed(tooLarge, sizeof(tooLarge), Sink::receive, &sink); // 1025
  assert(sink.errors.size() == 1 && sink.errors[0] == ParseError::FrameTooLarge);
  assert(sink.decisions.empty());
  feed(parser, good, sink, 1);
  assert(sink.errors.back() == ParseError::None);
  parser.reset();
  sink.errors.clear();
  parser.feed(good.data(), 5, Sink::receive, &sink);
  assert(sink.errors.empty());
  parser.reset();
  feed(parser, good, sink, 1);
  assert(sink.errors.size() == 1);

  // Sequence, duplicate ACK, mismatch, overflow, and premature END.
  session.reset();
  assert(begin(session, 2, crcOf({0x2A, 0x2B})).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  feed(parser, frame(FrameType::Data, 2, one), sink);
  assert(sink.decisions.back().error == Error::WrongSequence);
  feed(parser, frame(FrameType::Data, 0), sink);
  assert(sink.decisions.back().error == Error::InvalidLength);
  feed(parser, frame(FrameType::End, 0, one), sink);
  assert(sink.decisions.back().error == Error::InvalidLength);
  feed(parser, frame(FrameType::Data, UINT32_MAX, one), sink);
  assert(sink.decisions.back().error == Error::WrongSequence);
  feed(parser, good, sink);
  assert(sink.decisions.back().kind == DecisionKind::Ack);
  feed(parser, good, sink);
  assert(sink.decisions.back().kind == DecisionKind::Ack && !sink.decisions.back().newData);
  assert(session.confirmedBytes() == 1 && session.expectedSequence() == 1);
  feed(parser, frame(FrameType::End, 1), sink);
  assert(sink.decisions.back().error == Error::IncompleteImage);
  feed(parser, frame(FrameType::Data, 1, {0x2B, 0x2C}), sink);
  assert(sink.decisions.back().error == Error::SizeOverflow);
  feed(parser, frame(FrameType::Data, 0, {0x2B}), sink);
  assert(sink.decisions.back().error == Error::DuplicateMismatch);
  assert(session.state() == State::Error);

  session.reset();
  assert(begin(session, 2, crcOf({0x2A, 0x2B})).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  feed(parser, good, sink);
  std::vector<uint8_t> changedCrc = good;
  changedCrc.back() ^= 1;
  feed(parser, changedCrc, sink);
  assert(sink.decisions.back().error == Error::DuplicateMismatch);
  assert(session.state() == State::Error);

  // Final image CRC, invalid type, ABORT, timeout and clean new session.
  session.reset();
  assert(begin(session, 1, 0).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  feed(parser, frame(FrameType::Data, 0, one), sink);
  feed(parser, frame(FrameType::End, 1), sink);
  assert(sink.decisions.back().error == Error::ImageCrc);
  assert(session.state() == State::Error);
  session.reset();
  assert(begin(session, 1, crcOf(one)).kind == DecisionKind::Ready);
  parser.reset();
  sink = Sink{&session};
  uint8_t invalid[MaxFrameSize]{};
  const size_t invalidSize = encodeFrame(99, 0, nullptr, 0, invalid, sizeof(invalid));
  parser.feed(invalid, invalidSize, Sink::receive, &sink);
  assert(sink.decisions.back().error == Error::InvalidFrameType);
  feed(parser, frame(FrameType::Abort, 0), sink);
  assert(sink.decisions.back().kind == DecisionKind::Aborted);
  assert(session.state() == State::Aborted && session.confirmedBytes() == 0);
  assert(begin(session, 1, crcOf(one), 2048, 200).kind == DecisionKind::Ready);
  assert(session.expectedSequence() == 0 && session.imageCrc() == 0);
  assert(session.tick(15200, 15000).kind == DecisionKind::None);
  const Decision timeout = session.tick(15201, 15000);
  assert(timeout.kind == DecisionKind::Aborted && timeout.error == Error::Timeout);
  assert(session.state() == State::Aborted);
  assert(begin(session, 1, crcOf(one)).kind == DecisionKind::Ready);
  assert(session.expectedSequence() == 0 && session.confirmedBytes() == 0);

  std::puts("FirmwareUpdateProtocol native tests PASS");
}
