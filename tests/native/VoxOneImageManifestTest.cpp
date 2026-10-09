#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Version.h"
#include "update/VoxOneImageManifest.h"

using namespace VoxOneImageManifest;

namespace {
void refreshCrc(std::vector<uint8_t>& bytes) {
  const uint32_t value = crc32(bytes.data(), 56);
  for (unsigned i = 0; i < 4; ++i) bytes[56 + i] = value >> (8 * i);
}

void expectMutation(size_t offset, uint8_t value, Error expected) {
  std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t*>(&kCurrent),
                             reinterpret_cast<const uint8_t*>(&kCurrent) + kSize);
  bytes[offset] = value;
  if (expected != Error::Crc && expected != Error::Magic &&
      expected != Error::FirmwareVersion)
    refreshCrc(bytes);
  assert(validate(bytes.data(), bytes.size()) == expected);
}
}  // namespace

int main() {
  const uint8_t* current = reinterpret_cast<const uint8_t*>(&kCurrent);
  assert(kSize == 60 && current[16] == 1 && current[18] == 60);
  assert(validate(current, kSize) == Error::None);
  assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) ==
         0xcbf43926u);
  assert(std::strcmp(kCurrent.firmwareVersion, Version::FIRMWARE) == 0);
  assert(kCurrent.uartProtocolVersion == Version::PROTOCOL);
  expectMutation(0, 0, Error::Magic);
  expectMutation(16, 2, Error::Format);
  expectMutation(18, 59, Error::Size);
  expectMutation(20, 0, Error::Product);
  expectMutation(24, 'A', Error::Family);
  expectMutation(25, 1, Error::Revision);
  expectMutation(26, 2, Error::Chip);
  expectMutation(28, 3, Error::Protocol);
  expectMutation(29, 1, Error::Reserved);
  expectMutation(32, 0, Error::FirmwareVersion);
  expectMutation(56, current[56] ^ 1, Error::Crc);

  std::vector<uint8_t> bytes(current, current + kSize);
  for (size_t i = 32; i < 55; ++i) bytes[i] = 'a';
  bytes[55] = 0;
  refreshCrc(bytes);
  assert(validate(bytes.data(), bytes.size()) == Error::None);
  bytes[55] = 'a';
  refreshCrc(bytes);
  assert(validate(bytes.data(), bytes.size()) == Error::FirmwareVersion);
  bytes.assign(current, current + kSize);
  bytes[54] = 'x';
  refreshCrc(bytes);
  assert(validate(bytes.data(), bytes.size()) == Error::FirmwareVersion);
  assert(validate(nullptr, 0) == Error::Truncated);
  assert(validate(current, kSize - 1) == Error::Truncated);

  std::vector<uint8_t> image(17, 0x7f);
  image.insert(image.end(), current, current + kSize);
  image.insert(image.end(), 5, 0x42);
  size_t offset = 0;
  assert(findExactlyOne(image.data(), image.size(), &offset) == Error::None);
  assert(offset == 17);
  image.insert(image.end(), current, current + kSize);
  assert(findExactlyOne(image.data(), image.size()) == Error::Count);
  image.clear();
  assert(findExactlyOne(image.data(), image.size()) == Error::Count);
  image.assign(current, current + kSize - 1);
  assert(findExactlyOne(image.data(), image.size()) == Error::Count);
  std::puts("VoxOneImageManifest native tests passed");
}
