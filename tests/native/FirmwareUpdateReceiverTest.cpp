#include <Arduino.h>
#include <esp_system.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol/UartProtocol.h"
#include "update/FirmwareUpdateReceiver.h"

FakeESP ESP;
static uint32_t clockMs = 100;
uint32_t millis() { return clockMs; }
esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

namespace {

class Backend final : public FirmwareOta::Backend {
 public:
  FirmwareOta::Partitions partitions() override {
    FirmwareOta::Partitions p;
    p.runningPresent = p.nextPresent = p.nextIsOtaApp = slot;
    p.runningAddress = 0x10000;
    p.nextAddress = 0x150000;
    p.nextSize = 1310720;
    return p;
  }
  bool begin(uint32_t) override { ++begins; return !beginFail; }
  size_t write(const uint8_t*, size_t n) override {
    ++writes;
    return writeFail ? n - 1 : n;
  }
  bool end() override { ++ends; return !endFail; }
  void abort() override { ++aborts; }
  uint8_t rawError() const override { return 0; }

  int begins = 0, writes = 0, ends = 0, aborts = 0;
  bool slot = true, beginFail = false, writeFail = false, endFail = false;
};

struct Rig final : FirmwareUpdateHooks {
  HardwareSerial serial;
  UartProtocol uart{serial};
  Backend backend;
  FirmwareOta::FirmwareOtaWriter writer{backend};
  FirmwareUpdateReceiver receiver{writer, *this};
  int quiesces = 0, restores = 0, flushes = 0;
  bool quiesceOk = true;

  Rig() {
    uart.setFirmwareUpdateChannel(&receiver);
    uart.begin("BT");
    serial.clearOutput();
  }
  bool quiesce() override { ++quiesces; return quiesceOk; }
  void restore() override { ++restores; }
  void reply(const char* line) override { uart.sendFirmwareLine(line); }
  void flushTx() override { ++flushes; uart.flushFirmwareTx(); }
  void input(const std::string& bytes) { serial.feed(bytes); uart.loop(); }
  std::string take() {
    const std::string result = serial.output();
    serial.clearOutput();
    return result;
  }
  void begin(const std::vector<uint8_t>& image) {
    FirmwareUpdate::Crc32 crc;
    crc.add(image.data(), image.size());
    char command[64];
    std::snprintf(command, sizeof(command), "FW_BEGIN %u %08X 0.7.0-dev\n",
                  static_cast<unsigned>(image.size()), crc.value());
    input(command);
    assert(take() == "FW_READY 1024\n");
    assert(receiver.exclusive());
  }
};

std::string frame(FirmwareUpdate::FrameType type, uint32_t sequence,
                  const std::vector<uint8_t>& payload = {}) {
  uint8_t data[FirmwareUpdate::MaxFrameSize];
  const size_t n = FirmwareUpdate::encodeFrame(
      static_cast<uint8_t>(type), sequence,
      payload.empty() ? nullptr : payload.data(), payload.size(), data,
      sizeof(data));
  assert(n != 0);
  return std::string(reinterpret_cast<const char*>(data), n);
}

void expect(const std::string& actual, const char* expected) {
  if (actual != expected) {
    std::fprintf(stderr, "Expected %s, got %s\n", expected, actual.c_str());
    std::abort();
  }
}

}  // namespace

int main() {
  clockMs = 100;
  {
    Rig r;
    r.input("PING\n");
    expect(r.take(), "PONG\n");
    r.input("FW_BEGIN\nFW_BEGIN 0 00000000 v\n"
            "FW_BEGIN 4294967296 00000000 v\n"
            "FW_BEGIN 1 BAD v\nFW_BEGIN 1 00000000 \n"
            "FW_BEGIN 1 00000000 1234567890123456789012345\n"
            "FW_BEGIN 1 00000000 v extra\n");
    const std::string errors = r.take();
    assert(errors.find("FW_ERR INVALID_BEGIN\n") != std::string::npos);
    assert(errors.find("FW_ERR INVALID_SIZE\n") != std::string::npos);
    assert(errors.find("FW_ERR INVALID_CRC\n") != std::string::npos);
    assert(errors.find("FW_ERR INVALID_VERSION\n") != std::string::npos);
    assert(r.backend.begins == 0);
    r.backend.slot = false;
    r.input("FW_BEGIN 1 00000000 v\n");
    expect(r.take(), "FW_ERR NO_OTA_SLOT\n");
    r.backend.slot = true;
    r.input("FW_BEGIN 1310721 00000000 v\n");
    expect(r.take(), "FW_ERR INVALID_SIZE\n");
  }
  {
    Rig r;
    r.quiesceOk = false;
    r.input("FW_BEGIN 1 00000000 v\n");
    expect(r.take(), "FW_ERR BT_QUIESCE\n");
    assert(r.restores == 1 && r.backend.begins == 0);
    r.quiesceOk = true;
    r.backend.beginFail = true;
    r.input("FW_BEGIN 1 00000000 v\n");
    expect(r.take(), "FW_ERR OTA_BEGIN\n");
    assert(r.restores == 2 && !r.receiver.exclusive());
  }
  {
    Rig r;
    r.begin({1, 2, 3});
    r.receiver.beginCommand("FW_BEGIN 1 00000000 v", clockMs);
    expect(r.take(), "FW_ERR BUSY\n");
    BluetoothChanges changes;
    changes.connectionChanged = true;
    r.uart.sendBluetoothChanges(changes);
    r.uart.sendVu(2, 3);
    r.uart.sendStatus(BluetoothSnapshot{}, OtaStatus::Unknown);
    assert(r.take().empty());
    // Binary bytes (including ASCII command text) never reach line parser.
    r.input("PING\n");
    assert(r.take().empty());
    const std::string first = frame(FirmwareUpdate::FrameType::Data, 0, {1, 2});
    for (char byte : first) r.input(std::string(1, byte));
    expect(r.take(), "FW_ACK 0 2\n");
    assert(r.backend.writes == 1);
    r.input(first);
    expect(r.take(), "FW_ACK 0 2\n");
    assert(r.backend.writes == 1);
    r.input(frame(FirmwareUpdate::FrameType::End, 1));
    expect(r.take(), "FW_NACK 1 INCOMPLETE_IMAGE\n");
    r.input(frame(FirmwareUpdate::FrameType::Data, 2, {3}));
    expect(r.take(), "FW_NACK 2 WRONG_SEQUENCE\n");
    r.input(frame(FirmwareUpdate::FrameType::Data, 1, {3}) +
            frame(FirmwareUpdate::FrameType::End, 2));
    expect(r.take(), "FW_ACK 1 3\nFW_VERIFY\nFW_OK\n");
    assert(r.backend.writes == 2 && r.backend.ends == 1 && r.flushes == 1);
    assert(!r.receiver.restartDue(clockMs));
    assert(r.receiver.restartDue(clockMs + 100));
  }
  {
    Rig r;
    r.begin({1});
    r.input("GET_STATUS\n");
    assert(!r.uart.takeStatusRequest());
    std::string bad = frame(FirmwareUpdate::FrameType::Data, 0, {1});
    bad.back() ^= 1;
    r.input(bad);
    expect(r.take(), "FW_NACK 0 FRAME_CRC\n");
    assert(r.backend.writes == 0 && r.receiver.exclusive());
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {1}));
    expect(r.take(), "FW_ACK 0 1\n");
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {2}));
    expect(r.take(), "FW_ERR DUPLICATE_MISMATCH\n");
    assert(r.backend.aborts == 1 && r.restores == 1);
    assert(!r.receiver.restartDue(clockMs + 1000));
    r.input("PING\n");
    expect(r.take(), "PONG\n");
  }
  {
    Rig r;
    r.backend.writeFail = true;
    r.begin({1});
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {1}));
    expect(r.take(), "FW_ERR WRITE_FAILED\n");
    assert(r.backend.aborts == 1 && !r.receiver.exclusive());
    assert(!r.receiver.restartDue(clockMs + 1000));
  }
  {
    Rig r;
    r.begin({1});
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {1, 2}));
    expect(r.take(), "FW_ERR SIZE_OVERFLOW\n");
    assert(r.backend.writes == 0 && r.backend.aborts == 1);
  }
  {
    Rig r;
    r.begin({1});
    uint8_t oversized[FirmwareUpdate::HeaderSize] = {
        FirmwareUpdate::Magic0, FirmwareUpdate::Magic1, 1, 0, 0, 0, 0,
        0x01, 0x04};  // 1025-byte payload declaration
    r.input(std::string(reinterpret_cast<const char*>(oversized),
                        sizeof(oversized)));
    expect(r.take(), "FW_ERR FRAME_TOO_LARGE\n");
    assert(r.backend.aborts == 1 && !r.receiver.exclusive());
  }
  {
    Rig r;
    r.begin({1});
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {2}));
    expect(r.take(), "FW_ACK 0 1\n");
    r.input(frame(FirmwareUpdate::FrameType::End, 1));
    expect(r.take(), "FW_ERR IMAGE_CRC\n");
    assert(r.backend.ends == 0 && r.backend.aborts == 1);
  }
  {
    Rig r;
    r.backend.endFail = true;
    r.begin({1});
    r.input(frame(FirmwareUpdate::FrameType::Data, 0, {1}) +
            frame(FirmwareUpdate::FrameType::End, 1));
    expect(r.take(), "FW_ACK 0 1\nFW_VERIFY\nFW_ERR FINISH_FAILED\n");
    assert(!r.receiver.restartDue(clockMs + 1000));
  }
  {
    Rig r;
    r.begin({1});
    r.input(frame(FirmwareUpdate::FrameType::Abort, 0));
    expect(r.take(), "FW_ABORTED\n");
    assert(r.backend.aborts == 1 && r.restores == 1 && !r.receiver.exclusive());
    r.input("PING\n");
    expect(r.take(), "PONG\n");
  }
  {
    Rig r;
    r.begin({1});
    clockMs += 15001;
    r.uart.loop();
    expect(r.take(), "FW_ERR TIMEOUT\n");
    assert(r.backend.aborts == 1 && r.restores == 1);
    assert(!r.receiver.restartDue(clockMs + 1000));
  }
  std::puts("FirmwareUpdateReceiver native tests PASS");
}
