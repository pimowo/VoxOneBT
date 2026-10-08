#include "update/FirmwareOtaWriter.h"

#include <limits.h>

namespace FirmwareOta {

bool FirmwareOtaWriter::fail(Error error, bool fatal) {
  error_ = error;
  if (fatal) failed_ = true;
  return false;
}

bool FirmwareOtaWriter::inspect() {
  if (active_) return fail(Error::AlreadyActive);
  maxImageSize_ = 0;
  const Partitions partitions = backend_.partitions();
  if (!partitions.nextPresent) return fail(Error::NoOtaPartition);
  if (!partitions.runningPresent || !partitions.nextIsOtaApp ||
      partitions.nextSize == 0 ||
      partitions.nextAddress == partitions.runningAddress)
    return fail(Error::InvalidPartition);
  maxImageSize_ = partitions.nextSize;
  error_ = Error::None;
  return true;
}

bool FirmwareOtaWriter::begin(uint32_t expectedSize) {
  if (active_) return fail(Error::AlreadyActive);
  rawUpdateError_ = 0;
  if (!inspect()) return false;
  if (expectedSize == 0 || expectedSize > maxImageSize_ ||
      expectedSize == UINT32_MAX) // Arduino uses this value for unknown size.
    return fail(Error::InvalidSize);
  expectedSize_ = 0;
  bytesWritten_ = 0;
  failed_ = false;
  if (!backend_.begin(expectedSize)) {
    rawUpdateError_ = backend_.rawError();
    return fail(Error::BeginFailed);
  }
  expectedSize_ = expectedSize;
  active_ = true;
  error_ = Error::None;
  return true;
}

bool FirmwareOtaWriter::write(const uint8_t* data, size_t length) {
  if (!active_) return fail(Error::NotActive);
  if (failed_) return fail(Error::NeedsAbort);
  if (data == nullptr || length == 0)
    return fail(Error::InvalidData, true);
  if (length > static_cast<size_t>(expectedSize_ - bytesWritten_))
    return fail(Error::Overflow, true);
  if (backend_.write(data, length) != length) {
    rawUpdateError_ = backend_.rawError();
    return fail(Error::WriteFailed, true);
  }
  bytesWritten_ += static_cast<uint32_t>(length);
  error_ = Error::None;
  return true;
}

bool FirmwareOtaWriter::finish() {
  if (!active_) return fail(Error::NotActive);
  if (failed_) return fail(Error::NeedsAbort);
  if (bytesWritten_ != expectedSize_)
    return fail(Error::IncompleteImage);
  if (!backend_.end() || backend_.rawError() != 0) {
    rawUpdateError_ = backend_.rawError();
    return fail(Error::EndFailed, true);
  }
  active_ = false;
  error_ = Error::None;
  rawUpdateError_ = 0;
  return true;
}

void FirmwareOtaWriter::abort() {
  if (active_) backend_.abort();
  active_ = false;
  failed_ = false;
  expectedSize_ = 0;
  bytesWritten_ = 0;
  rawUpdateError_ = 0;
  error_ = Error::None;
}

}  // namespace FirmwareOta
