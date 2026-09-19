#!/usr/bin/env python3
"""Assert the flash size baked into a merged image's header.

A merged image starts with the bootloader, whose ESP32 image header carries the
flash size the ROM will configure. If the ESPHome board default says 8 MB and
the board is a 4 MB part, the image still builds and still flashes over serial
from the IDE — but a web flash writes the header verbatim and the device boots
into a loop. Checking it here is the only place it is cheap to catch.
"""

import sys

# Header byte 3, high nibble.
FLASH_SIZE_NIBBLE = {
    "1MB": 0x0,
    "2MB": 0x1,
    "4MB": 0x2,
    "8MB": 0x3,
    "16MB": 0x4,
    "32MB": 0x5,
    "64MB": 0x6,
    "128MB": 0x7,
}
ESP_IMAGE_MAGIC = 0xE9


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <image.bin> <expected-flash-size>", file=sys.stderr)
        return 2

    path, expected = sys.argv[1], sys.argv[2]
    if expected not in FLASH_SIZE_NIBBLE:
        print(f"Unknown flash size '{expected}'", file=sys.stderr)
        return 2

    with open(path, "rb") as handle:
        header = handle.read(4)

    if len(header) < 4 or header[0] != ESP_IMAGE_MAGIC:
        print(f"{path}: not an ESP image (magic 0x{header[0]:02X} != 0xE9)", file=sys.stderr)
        return 1

    found = header[3] >> 4
    want = FLASH_SIZE_NIBBLE[expected]
    if found != want:
        reverse = {v: k for k, v in FLASH_SIZE_NIBBLE.items()}
        print(
            f"{path}: header declares {reverse.get(found, hex(found))} flash, "
            f"expected {expected}. Set 'flash_size:' under 'esp32:' in the config.",
            file=sys.stderr,
        )
        return 1

    print(f"{path}: flash size header OK ({expected})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
