#include "update/VoxOneImageManifest.h"

#include <string.h>

#include "Version.h"

namespace VoxOneImageManifest {

const uint8_t kMagic[16] = {
    0xcf, 0x38, 0x29, 0x2c, 0xc1, 0xa1, 0x48, 0x4c,
    0xb9, 0xb3, 0xf5, 0x57, 0xfd, 0xea, 0x56, 0x0a};

namespace {

static_assert(sizeof(VOXONE_FIRMWARE_VERSION) <= kVersionCapacity,
              "FW_VERSION does not fit the manifest");

// C++11 constexpr recursion keeps the CRC in flash as a build-time constant.
constexpr uint8_t magicByte(size_t i) {
  return i == 0 ? 0xcf : i == 1 ? 0x38 : i == 2 ? 0x29 : i == 3 ? 0x2c
       : i == 4 ? 0xc1 : i == 5 ? 0xa1 : i == 6 ? 0x48 : i == 7 ? 0x4c
       : i == 8 ? 0xb9 : i == 9 ? 0xb3 : i == 10 ? 0xf5 : i == 11 ? 0x57
       : i == 12 ? 0xfd : i == 13 ? 0xea : i == 14 ? 0x56 : 0x0a;
}

constexpr uint8_t manifestByte(size_t i) {
  return i < 16 ? magicByte(i)
       : i == 16 ? 1 : i == 18 ? 60
       : i == 20 ? 0x01 : i == 21 ? 0x54 : i == 22 ? 0x42 : i == 23 ? 0x56
       : i == 24 ? 'V' : i == 26 ? 1
       : i == 28 ? Version::PROTOCOL
       : i >= kVersionOffset && i < kVersionOffset + sizeof(Version::FIRMWARE)
           ? Version::FIRMWARE[i - kVersionOffset]
       : 0;
}

constexpr uint32_t crcBit(uint32_t crc, unsigned bits) {
  return bits == 0 ? crc
       : crcBit((crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u), bits - 1);
}

constexpr uint32_t crcRange(size_t i, uint32_t crc) {
  return i == 56 ? crc ^ 0xffffffffu
       : crcRange(i + 1, crcBit(crc ^ manifestByte(i), 8));
}

uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}

uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

const Data kCurrent = {
    {0xcf, 0x38, 0x29, 0x2c, 0xc1, 0xa1, 0x48, 0x4c,
     0xb9, 0xb3, 0xf5, 0x57, 0xfd, 0xea, 0x56, 0x0a},
    kFormatVersion, kSize, kProductVoxOneBt, 'V', 0, kChipEsp32,
    Version::PROTOCOL, {0, 0, 0}, VOXONE_FIRMWARE_VERSION,
    crcRange(0, 0xffffffffu)};

uint32_t crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

Error validate(const uint8_t* bytes, size_t length) {
  if (bytes == nullptr || length < kSize) return Error::Truncated;
  if (memcmp(bytes, kMagic, 16) != 0) return Error::Magic;
  if (read16(bytes + 16) != kFormatVersion) return Error::Format;
  if (read16(bytes + 18) != kSize) return Error::Size;
  if (read32(bytes + 20) != kProductVoxOneBt) return Error::Product;
  if (bytes[24] != 'V') return Error::Family;
  if (bytes[25] != 0) return Error::Revision;
  if (read16(bytes + 26) != kChipEsp32) return Error::Chip;
  if (bytes[28] != Version::PROTOCOL) return Error::Protocol;
  if (bytes[29] || bytes[30] || bytes[31]) return Error::Reserved;
  bool terminated = false;
  for (size_t i = 0; i < kVersionCapacity; ++i) {
    const uint8_t c = bytes[kVersionOffset + i];
    if (terminated) {
      if (c != 0) return Error::FirmwareVersion;
    } else if (c == 0) {
      if (i == 0) return Error::FirmwareVersion;
      terminated = true;
    } else if (!((c >= '0' && c <= '9') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= 'a' && c <= 'z') ||
                 c == '.' || c == '_' || c == '-')) {
      return Error::FirmwareVersion;
    }
  }
  if (!terminated) return Error::FirmwareVersion;
  if (read32(bytes + 56) != crc32(bytes, 56)) return Error::Crc;
  return Error::None;
}

Error findExactlyOne(const uint8_t* image, size_t length, size_t* offset) {
  if (image == nullptr || length < kSize) return Error::Count;
  size_t found = 0;
  size_t foundOffset = 0;
  for (size_t i = 0; i <= length - kSize; ++i) {
    if (image[i] != kMagic[0] || memcmp(image + i, kMagic, 16) != 0)
      continue;
    if (validate(image + i, kSize) == Error::None) {
      ++found;
      foundOffset = i;
      if (found > 1) return Error::Count;
    }
  }
  if (found != 1) return Error::Count;
  if (offset != nullptr) *offset = foundOffset;
  return Error::None;
}

}  // namespace VoxOneImageManifest
