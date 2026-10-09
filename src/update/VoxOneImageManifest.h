#pragma once

#include <stddef.h>
#include <stdint.h>

namespace VoxOneImageManifest {

constexpr size_t kSize = 60;
constexpr size_t kVersionOffset = 32;
constexpr size_t kVersionCapacity = 24;
constexpr uint16_t kFormatVersion = 1;
constexpr uint32_t kProductVoxOneBt = 0x56425401u;
constexpr uint16_t kChipEsp32 = 1;
constexpr uint16_t kChipEsp32S3 = 2;

#pragma pack(push, 1)
struct Data {
  uint8_t magic[16];
  uint16_t manifestVersion;
  uint16_t manifestSize;
  uint32_t productId;
  uint8_t hardwareFamily;
  uint8_t hardwareRevision;
  uint16_t targetChip;
  uint8_t uartProtocolVersion;
  uint8_t reserved[3];
  char firmwareVersion[kVersionCapacity];
  uint32_t manifestCrc32;
};
#pragma pack(pop)

static_assert(sizeof(Data) == kSize, "Manifest wire size changed");
static_assert(offsetof(Data, firmwareVersion) == kVersionOffset,
              "Manifest version offset changed");
static_assert(offsetof(Data, manifestCrc32) == 56,
              "Manifest CRC offset changed");

extern const uint8_t kMagic[16];
extern const Data kCurrent;

enum class Error {
  None, Truncated, Magic, Format, Size, Product, Family, Revision,
  Chip, Protocol, FirmwareVersion, Reserved, Crc, Count
};

uint32_t crc32(const uint8_t* bytes, size_t length);
Error validate(const uint8_t* bytes, size_t length);
Error findExactlyOne(const uint8_t* image, size_t length,
                     size_t* offset = nullptr);

}  // namespace VoxOneImageManifest
