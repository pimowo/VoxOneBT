#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "update/FirmwareOtaWriter.h"

using namespace FirmwareOta;

namespace {

class FakeBackend final : public Backend {
 public:
  FakeBackend() {
    layout.runningPresent = true;
    layout.nextPresent = true;
    layout.nextIsOtaApp = true;
    layout.runningAddress = 0x10000;
    layout.nextAddress = 0x150000;
    layout.nextSize = 1310720;
  }

  Partitions partitions() override { return layout; }
  bool begin(uint32_t size) override {
    ++beginCalls;
    lastBeginSize = size;
    if (beginFails) {
      error = 5;
      return false;
    }
    running = true;
    error = 0;
    return true;
  }
  size_t write(const uint8_t*, size_t length) override {
    ++writeCalls;
    if (partialWrite) {
      error = 1;
      return length - 1;
    }
    return length;
  }
  bool end() override {
    ++endCalls;
    if (endFails) {
      error = 9;
      return false;
    }
    running = false;
    return true;
  }
  void abort() override {
    ++abortCalls;
    running = false;
  }
  uint8_t rawError() const override { return error; }

  Partitions layout{};
  uint32_t lastBeginSize = 0;
  int beginCalls = 0;
  int writeCalls = 0;
  int endCalls = 0;
  int abortCalls = 0;
  bool running = false;
  bool beginFails = false;
  bool partialWrite = false;
  bool endFails = false;
  uint8_t error = 0;
};

}  // namespace

int main() {
  FakeBackend backend;
  FirmwareOtaWriter writer(backend);
  const uint8_t bytes[] = {0xE9, 1, 2, 3};

  assert(!writer.write(bytes, 1) && writer.lastError() == Error::NotActive);
  assert(writer.inspect() && writer.maxImageSize() == 1310720);
  assert(!writer.begin(0) && writer.lastError() == Error::InvalidSize);
  assert(!writer.begin(1310721) && writer.lastError() == Error::InvalidSize);
  assert(writer.begin(1310720));
  assert(writer.active() && backend.lastBeginSize == 1310720);
  assert(!writer.begin(1) && writer.lastError() == Error::AlreadyActive);
  writer.abort();
  assert(!writer.active() && backend.abortCalls == 1);
  writer.abort();
  assert(backend.abortCalls == 1);

  assert(writer.begin(4));
  assert(!writer.finish() && writer.lastError() == Error::IncompleteImage);
  assert(writer.active());
  assert(!writer.write(nullptr, 1) && writer.needsAbort());
  assert(!writer.write(bytes, 1) && writer.lastError() == Error::NeedsAbort);
  writer.abort();
  assert(writer.begin(4));
  assert(!writer.write(bytes, 0) && writer.needsAbort());
  writer.abort();

  assert(writer.begin(3));
  assert(!writer.write(bytes, 4) && writer.lastError() == Error::Overflow);
  assert(backend.writeCalls == 0);
  writer.abort();
  assert(writer.begin(3));
  assert(!writer.write(bytes, std::numeric_limits<size_t>::max()) &&
         writer.lastError() == Error::Overflow);
  assert(backend.writeCalls == 0);
  writer.abort();

  backend.partialWrite = true;
  assert(writer.begin(4));
  assert(!writer.write(bytes, 4));
  assert(writer.lastError() == Error::WriteFailed && writer.rawUpdateError() == 1);
  assert(writer.bytesWritten() == 0 && writer.needsAbort());
  assert(!writer.finish() && writer.lastError() == Error::NeedsAbort);
  writer.abort();
  backend.partialWrite = false;

  backend.endFails = true;
  assert(writer.begin(4) && writer.write(bytes, 4));
  assert(!writer.finish() && writer.lastError() == Error::EndFailed);
  assert(writer.rawUpdateError() == 9 && writer.needsAbort());
  writer.abort();
  backend.endFails = false;
  backend.error = 0;

  assert(writer.begin(4));
  assert(writer.write(bytes, 2) && writer.write(bytes + 2, 2));
  assert(writer.bytesWritten() == 4);
  assert(writer.finish());
  assert(!writer.active() && writer.bytesWritten() == 4);
  assert(writer.lastError() == Error::None && backend.endCalls == 2);
  writer.abort();
  assert(backend.abortCalls == 7);
  assert(writer.begin(4)); // Fresh session after abort.
  writer.abort();

  backend.beginFails = true;
  assert(!writer.begin(4) && writer.lastError() == Error::BeginFailed);
  assert(writer.rawUpdateError() == 5 && !writer.active());
  backend.beginFails = false;
  backend.layout.nextPresent = false;
  assert(!writer.inspect() && writer.lastError() == Error::NoOtaPartition);
  backend.layout.nextPresent = true;
  backend.layout.runningPresent = false;
  assert(!writer.inspect() && writer.lastError() == Error::InvalidPartition);
  backend.layout.runningPresent = true;
  backend.layout.nextIsOtaApp = false;
  assert(!writer.inspect() && writer.lastError() == Error::InvalidPartition);
  backend.layout.nextIsOtaApp = true;
  backend.layout.nextAddress = backend.layout.runningAddress;
  assert(!writer.begin(4) && writer.lastError() == Error::InvalidPartition);
  backend.layout.nextAddress = 0x150000;
  backend.layout.nextSize = 0;
  assert(!writer.begin(4) && writer.lastError() == Error::InvalidPartition);
  backend.layout.nextSize = UINT32_MAX;
  assert(!writer.begin(UINT32_MAX) && writer.lastError() == Error::InvalidSize);

  std::puts("FirmwareOtaWriter native tests PASS");
}
