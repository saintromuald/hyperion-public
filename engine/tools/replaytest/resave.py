"""Copy a replay with its map saved the way StarCraft: Remastered saves it.

A 1.16/1.17 replay carries its map's scenario.chk as its last section. This writes a
copy whose map is the same map in Remastered's form: the version 206 for Brood War
maps (205) and 64 for the classic ones (59 and 63), and the string table as STRx, with
32-bit offsets, in place of STR. 64 is 63's format, so a 59 map's MRGN also grows from
its 64 locations to 63's 255, the new ones empty; the 64 it had keep their numbers, and
Anywhere is 64 in both. Nothing else changes, so Hyperion playing the copy has to match
the original playing the unchanged replay on every frame. 1.17 itself
refuses the copy.

The sections are written stored, without implode, which the reader accepts for any
block that did not shrink.

    python tools/replaytest/resave.py <replay> <out replay>
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import repfile  # noqa: E402

REMASTERED_VERSION = {59: 64, 63: 64, 205: 206}
MRGN_SIZE = {59: 64 * 20, 63: 255 * 20}  # maphdr_MRGN and maphdr_MRGN_Ext take no other


def read_sections(path) -> tuple[list[bytes], bytes]:
    """The replay's sections, decompressed (magic, header, command length, commands,
    map length, map), and any bytes after them, which the game does not read."""
    out = []
    with open(path, "rb") as f:
        for length in (4, 0x279, 4):
            out.append(repfile._section(f, length))
        out.append(repfile._section(f, struct.unpack("<I", out[-1])[0]))
        out.append(repfile._section(f, 4))
        out.append(repfile._section(f, struct.unpack("<I", out[-1])[0]))
        return out, f.read()


def write_section(data: bytes) -> bytes:
    """compress's layout with every block stored: the CRC, the block count, then each
    block's size and bytes."""
    blocks = [data[i:i + 0x2000] for i in range(0, len(data), 0x2000)] or [b""]
    out = bytearray(struct.pack("<II", zlib.crc32(data) ^ 0xFFFFFFFF, len(blocks)))
    for block in blocks:
        out += struct.pack("<I", len(block)) + block
    return bytes(out)


def chk_sections(chk: bytes) -> list[tuple[bytes, bytes]]:
    """(tag, body) in file order. Stops where a header would run past the end, as
    the game's reader does."""
    out, pos = [], 0
    while pos + 8 <= len(chk):
        tag = chk[pos:pos + 4]
        size = struct.unpack_from("<I", chk, pos + 4)[0]
        out.append((tag, chk[pos + 8:pos + 8 + size]))
        pos += 8 + size
    return out


def str_to_strx(body: bytes) -> bytes:
    """STR's u16 count and offsets, from the section's start, as STRx's u32 ones. A
    body too short for its table, like the one-byte STR some protected maps put before
    the real one, stays as it is: the game runs the handler for every copy in file
    order, so the last copy is the table either way."""
    if len(body) < 2 or len(body) < 2 + 2 * struct.unpack_from("<H", body, 0)[0]:
        return body
    count = struct.unpack_from("<H", body, 0)[0]
    offsets = struct.unpack_from("<%dH" % count, body, 2)
    grow = 2 + 2 * count
    table = struct.pack("<I", count) + struct.pack("<%dI" % count, *(o + grow for o in offsets))
    return table + body[2 + 2 * count:]


def resave(chk: bytes) -> bytes:
    # A section may come more than once; the game keeps the last, so each copy is
    # converted alike.
    sections = chk_sections(chk)
    tags = [t for t, _ in sections]
    for tag in (b"VER ", b"STR "):
        if tag not in tags:
            raise ValueError("the map has no %r section" % tag)
    # The game reads the last VER.
    version = [struct.unpack("<H", b)[0] for t, b in sections if t == b"VER "][-1]
    if version not in REMASTERED_VERSION:
        raise ValueError("map version %d" % version)
    out = bytearray()
    for tag, body in sections:
        if tag == b"VER ":
            body = struct.pack("<H", REMASTERED_VERSION[version])
        elif tag == b"STR ":
            tag, body = b"STRx", str_to_strx(body)
        elif tag == b"MRGN" and version == 59 and len(body) == MRGN_SIZE[59]:
            body = body + bytes(MRGN_SIZE[63] - len(body))
        out += tag + struct.pack("<I", len(body)) + body
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    sections, trailing = read_sections(argv[1])
    before = len(sections[5])
    chk = resave(sections[5])
    sections[4] = struct.pack("<I", len(chk))
    sections[5] = chk
    Path(argv[2]).write_bytes(b"".join(write_section(s) for s in sections) + trailing)
    print("%s -> %s: map %d -> %d bytes" % (argv[1], argv[2], before, len(chk)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
