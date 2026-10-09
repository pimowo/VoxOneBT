#!/usr/bin/env python3
"""Verify product identity in the final ESP firmware image, not an object file."""

import argparse
import pathlib
import re
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAGIC = bytes.fromhex("cf38292cc1a1484cb9b3f557fdea560a")
SIZE = 60
PRODUCT = 0x56425401


def canonical():
    source = (ROOT / "include/Version.h").read_text()
    version = re.search(r'^#define VOXONE_FIRMWARE_VERSION "([^"]+)"$', source, re.M)
    protocol = re.search(r'^#define VOXONE_UART_PROTOCOL_VERSION (\d+)$', source, re.M)
    if not version or not protocol:
        raise ValueError("canonical Version.h definitions unavailable")
    return version.group(1), int(protocol.group(1))


def verify(image, version, protocol):
    if not image or image[0] != 0xE9:
        raise ValueError("invalid ESP image magic")
    matches = []
    start = 0
    while True:
        offset = image.find(MAGIC, start)
        if offset < 0:
            break
        start = offset + 1
        candidate = image[offset:offset + SIZE]
        if len(candidate) != SIZE:
            continue
        format_version, size, product, family, revision, chip, proto = struct.unpack_from(
            "<HHIBBHB", candidate, 16)
        crc = struct.unpack_from("<I", candidate, 56)[0]
        raw_version = candidate[32:56]
        value, separator, padding = raw_version.partition(b"\0")
        if (format_version == 1 and size == SIZE and product == PRODUCT
                and family == ord("V") and revision == 0 and chip == 1
                and proto == protocol and candidate[29:32] == b"\0" * 3
                and value == version.encode() and separator and not any(padding)
                and crc == zlib.crc32(candidate[:56])):
            matches.append(offset)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one valid manifest, found {len(matches)}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", nargs="?", type=pathlib.Path,
                        default=ROOT / ".pio/build/esp32dev/firmware.bin")
    args = parser.parse_args()
    version, protocol = canonical()
    data = args.image.read_bytes()
    offset = verify(data, version, protocol)
    print(f"PASS: ESP image {len(data)} bytes, one valid VoxOneBT V0 manifest "
          f"at 0x{offset:x}, FW_VERSION {version}, PROTO {protocol}")


if __name__ == "__main__":
    main()
