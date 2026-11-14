#!/usr/bin/env python3
"""Convert an HXP3 replay to strict HXP4 for receiver-only official-DLL tests.

Common HXP3 fields are preserved. HXP3's one-active-entry queue is expanded
using its legacy fallback. The first two own units exercise ResearchTech and
Upgrade with deterministic late state; this is synthetic replay, not a game.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER = struct.Struct("<4s5I")
UNIT = struct.Struct("<9I4B")
EXTRA = struct.Struct("<5I2i4I5H10B")
PLAYER = struct.Struct("<7I")
LATE = struct.Struct("<6H2B")


def convert(payload: bytes) -> bytes:
    output = bytearray()
    offset = 0
    while offset < len(payload):
        if offset + HEADER.size > len(payload):
            raise ValueError("truncated HXP3 header")
        magic, frame, count, width, height, players = HEADER.unpack_from(payload, offset)
        if magic != b"HXP3":
            raise ValueError(f"expected HXP3 at {offset}, got {magic!r}")
        unit_bytes = count * UNIT.size
        extra_bytes = count * EXTRA.size
        player_bytes = players * PLAYER.size
        tile_bytes = width * height * 4
        frame_size = HEADER.size + unit_bytes + extra_bytes + player_bytes + tile_bytes
        if offset + frame_size > len(payload):
            raise ValueError(f"truncated HXP3 frame {frame}")
        units_at = offset + HEADER.size
        extras_at = units_at + unit_bytes
        players_at = extras_at + extra_bytes
        tiles_at = players_at + player_bytes
        if tiles_at + tile_bytes != offset + frame_size:
            raise AssertionError("internal frame extent")

        output += struct.pack("<4s7I", b"HXP4", frame, count, width, height, players, 78, 119)
        units = bytearray(payload[units_at:extras_at])
        output += units
        own_index = 0
        for index in range(count):
            unit = UNIT.unpack_from(payload, units_at + index * UNIT.size)
            old = bytearray(payload[extras_at + index * EXTRA.size : extras_at + (index + 1) * EXTRA.size])
            slot = old[54] if old[54] < 5 else 0
            queue = [228] * 5
            build_type = struct.unpack_from("<I", old, 28)[0]
            if build_type <= 228:
                queue[slot] = build_type
            else:
                build_type = 228
            remaining = 0
            tech, upgrade = 44, 61
            if unit[2] == 0 and own_index == 0:
                struct.pack_into("<I", units, index * UNIT.size + 32, 75)
                queue = [8, 9, 10, 11, 12]
                slot = 2
                build_type = 10
                remaining, tech, upgrade = 120, 5, 61
                own_index += 1
            elif unit[2] == 0 and own_index == 1:
                struct.pack_into("<I", units, index * UNIT.size + 32, 76)
                remaining, tech, upgrade = 88, 44, 7
                own_index += 1
            struct.pack_into("<I", old, 28, build_type)
            old[54] = slot
            output += old
            output += LATE.pack(*queue, remaining, tech, upgrade)

        output += payload[players_at:tiles_at]
        for player in range(players):
            tech = bytearray(119)
            if player == 0:
                tech[0] = tech[24] = tech[43] = 1
                tech[44] = 2
                tech[44 + 46] = 3
                tech[44 + 60] = 2
                for bit in (0, 24, 43):
                    tech[105 + bit // 8] |= 1 << (bit % 8)
                for bit in (0, 46, 60):
                    tech[111 + bit // 8] |= 1 << (bit % 8)
            elif player == 1:
                tech[44] = 1  # native enemy owner level; PlayerData remains masked
            output += tech
        output += payload[tiles_at : tiles_at + tile_bytes]
        # Replace the unit section emitted above with the order-mutated copy.
        unit_start = len(output) - tile_bytes - players * 119 - player_bytes - count * 78 - unit_bytes
        output[unit_start : unit_start + unit_bytes] = units
        offset += frame_size
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    converted = convert(args.input.read_bytes())
    args.output.write_bytes(converted)
    print(f"wrote {len(converted)} HXP4 bytes to {args.output}")


if __name__ == "__main__":
    main()
