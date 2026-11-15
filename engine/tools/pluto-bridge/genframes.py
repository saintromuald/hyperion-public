#!/usr/bin/env python3
"""Generate public, synthetic HXPF frames; no StarCraft data is read or required."""

import struct
import sys


UNITS = (
    (1000, 45, 0, 608, 928, 1250, 0, 0, 11, 1, 1),
    (1001, 41, 0, 672, 1064, 40, 0, 0, 11, 1, 1),
    (1002, 41, 0, 640, 864, 40, 0, 0, 11, 1, 1),
    (1003, 42, 0, 512, 1224, 200, 0, 0, 11, 1, 1),
    (2000, 106, 1, 2976, 2976, 1500, 0, 0, 11, 0, 1),
    (2001, 7, 1, 2912, 3112, 60, 0, 0, 11, 0, 1),
    (2002, 7, 1, 3112, 2912, 60, 0, 0, 11, 0, 1),
    (2003, 0, 1, 3272, 3272, 40, 0, 0, 11, 0, 1),
)
PLAYERS = ((0, 50, 0, 400, 6, 512, 1024), (1, 50, 0, 400, 6, 3072, 3072))


def frame(number, width=128):
    # The legacy protocol is named HXPF; its actual on-wire magic is the historical bytes HXFP.
    output = bytearray(struct.pack("<4s5I", b"HXFP", number, len(UNITS), width, width, len(PLAYERS)))
    for unit in UNITS:
        output += struct.pack("<9I4B", *unit, 0, 0)
    for player in PLAYERS:
        output += struct.pack("<7I", *player)

    # Two visibility bitplanes, one bit per tile in row order. A 24-tile circle around the bot's
    # base (19,29) is seen and explored. These are generated bits, not map or game assets.
    plane_bytes = (width * width + 7) // 8
    seen = bytearray(plane_bytes)
    explored = bytearray(plane_bytes)
    for y in range(width):
        for x in range(width):
            if (x - 19) ** 2 + (y - 29) ** 2 <= 24**2:
                tile = y * width + x
                seen[tile >> 3] |= 1 << (tile & 7)
                explored[tile >> 3] |= 1 << (tile & 7)
    output += seen + explored
    return bytes(output)


def stream(count=30, width=128):
    return b"".join(frame(number, width) for number in range(count))


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    if not 1 <= count <= 10000:
        raise SystemExit("frame count must be in 1..10000")
    sys.stdout.buffer.write(stream(count))


if __name__ == "__main__":
    main()
