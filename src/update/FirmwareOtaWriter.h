#pragma once

#include <stddef.h>
#include <stdint.h>

namespace FirmwareOta {

struct Partitions {
  bool runningPresent = false;
  bool nextPresent = false;
  bool nextIsOtaApp = false;
  uint32_t runningAddress = 0;
  uint32_t nextAddress = 0;
  uint32_t nextSize = 0;
};

// Thin boundary around Arduino Update and ESP-IDF partition queries. A native
// fake can implement this interface without including either framework.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual Partitions partitions() = 0;
  virtual bool begin(uint32_t size) = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual bool end() = 0;
  virtual void abort() = 0;
  virtual uint8_t rawError() const = 0;
};

enum class Error : uint8_t {
  None,
  NoOtaPartition,
  InvalidPartition,
  InvalidSize,
  AlreadyActive,
  NotActive,
  InvalidData,
  Overflow,
  BeginFailed,
  WriteFailed,
  IncompleteImage,
  EndFailed,
  NeedsAbort,
};

class FirmwareOtaWriter {
 public:
  explicit FirmwareOtaWriter(Backend& backend) : backend_(backend) {}

  bool inspect();
  uint32_t maxImageSize() const { return maxImageSize_; }
  bool begin(uint32_t expectedSize);
  bool write(const uint8_t* data, size_t length);
  bool finish();
  void abort();

  bool active() const { return active_; }
  bool needsAbort() const { return failed_; }
  uint32_t bytesWritten() const { return bytesWritten_; }
  Error lastError() const { return error_; }
  uint8_t rawUpdateError() const { return rawUpdateError_; }

 private:
  bool fail(Error error, bool fatal = false);

  Backend& backend_;
  uint32_t maxImageSize_ = 0;
  uint32_t expectedSize_ = 0;
  uint32_t bytesWritten_ = 0;
  Error error_ = Error::None;
  uint8_t rawUpdateError_ = 0;
  bool active_ = false;
  bool failed_ = false;
};

// Production adapter. Construction and inspection do not alter flash.
class ArduinoOtaBackend final : public Backend {
 public:
  Partitions partitions() override;
  bool begin(uint32_t size) override;
  size_t write(const uint8_t* data, size_t length) override;
  bool end() override;
  void abort() override;
  uint8_t rawError() const override;
};

}  // namespace FirmwareOta
