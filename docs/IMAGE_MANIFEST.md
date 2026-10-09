# VoxOneImageManifest v1

The image embeds a fixed product identity so VoxOne MAIN can reject an image
for another product before starting a UART update. `esp_app_desc_t` remains
Arduino/ESP-IDF framework metadata and is not used as product identity.

All multibyte integers are little-endian. The structure is packed, has no
pointers, and occupies exactly 60 bytes. Offsets are relative to the first
magic byte:

| Offset | Size | Field | v1 value |
|---:|---:|---|---|
| 0 | 16 | binary magic | `cf 38 29 2c c1 a1 48 4c b9 b3 f5 57 fd ea 56 0a` (UUID `cf38292c-c1a1-484c-b9b3-f557fdea560a`) |
| 16 | 2 | manifestVersion | 1 |
| 18 | 2 | manifestSize | 60 |
| 20 | 4 | productId | `0x56425401` = VOXONE_BT |
| 24 | 1 | hardwareFamily | ASCII `V` (`0x56`) |
| 25 | 1 | hardwareRevision | 0 (V0) |
| 26 | 2 | targetChip | 1 = ESP32; 2 reserved for ESP32-S3 |
| 28 | 1 | uartProtocolVersion | 2 |
| 29 | 3 | reserved | zero |
| 32 | 24 | firmwareVersion | NUL-terminated ASCII, max 23 characters, zero padded |
| 56 | 4 | manifestCrc32 | CRC-32/ISO-HDLC of bytes 0–55 |

CRC uses reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and
final XOR `0xFFFFFFFF`. The CRC field does not cover itself. Current V0 images
require family `V`, revision 0 and ESP32. Later V revisions can be distinguished
by revision; compatibility across revisions is not assumed in v1.

`include/Version.h` defines the single firmware version and protocol constants.
The manifest and UART identity use them, and the boot log uses the same version.
The UART identity reads the manifest at runtime, retaining it in the linked
firmware image. The current firmware version is `0.6.2-dev`; protocol is 2.

An image reader first checks ESP image magic `0xE9`, then scans for the 16-byte
binary magic. Each candidate must have the complete 60 bytes, supported format
and size, expected product/family/revision/chip/protocol, valid version string,
zero reserved bytes, and valid CRC. Exactly one valid manifest is required.
`python3 scripts/verify_image_manifest.py` performs this check on the final
`.pio/build/esp32dev/firmware.bin` and additionally checks the version against
the canonical source. The pure C++ parser can be reused by VoxOne MAIN.

The manifest prevents accidental selection of MAIN firmware, another product,
family or chip, damaged manifest data, or mismatched version metadata. It is
not a cryptographic signature, Secure Boot, or a defense against deliberately
crafted malicious firmware.
