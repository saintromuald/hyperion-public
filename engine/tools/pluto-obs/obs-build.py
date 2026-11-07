#!/usr/bin/env python3
# obs-build.py — build Pluto's observation (the obs the infer server consumes)
# from a game state, and diff it against the rig's captured references.
#
# The layout is from docs/pluto-protocol.md; the reference dumps live in the
# rig's dump/ (live-fNNNNN.bin = the fork's own obs for its scripted scenario).
# Running:  python3 obs-build.py <reference-obs.bin>
#
# The scenario (the rig's scenario 1, 128x128, 2 players):
#   p0: hatchery (608,928) hp1250, drone (672,1064) hp40, drone (640,864) hp40,
#       overlord (512,1224) hp200
#   p1: command center (2976,2976) hp1500, scv (2912,3112) hp60,
#       scv (3112,2912) hp60, marine (3272,3272) hp40
import struct, sys, math

ENTRY = 0x1B4  # 436 bytes = 109 floats
HDR_N, HDR_W, HDR_H = 0xC0, 0xC4, 0xC8
UNITS_AT = 0xC100

# unit type ids: BWAPI's enum (the fork's own ids)
ZERG_HATCHERY, ZERG_DRONE, ZERG_OVERLORD = 45, 41, 42
TERRAN_CC, TERRAN_SCV, TERRAN_MARINE = 106, 7, 0

SCENARIO = [
    # type, player, x, y, hp
    (ZERG_HATCHERY, 0, 608, 928, 1250),
    (ZERG_DRONE, 0, 672, 1064, 40),
    (ZERG_DRONE, 0, 640, 864, 40),
    (ZERG_OVERLORD, 0, 512, 1224, 200),
    (TERRAN_CC, 1, 2976, 2976, 1500),
    (TERRAN_SCV, 1, 2912, 3112, 60),
    (TERRAN_SCV, 1, 3112, 2912, 60),
    (TERRAN_MARINE, 1, 3272, 3272, 40),
]


def f32(v):
    return struct.pack('<f', v)


def build_entry(u, out):
    """The known fields (docs/pluto-protocol.md). Unknown fields stay zero."""
    ty, pl, x, y, hp = u
    entry = bytearray(ENTRY)
    put = lambda off, v: struct.pack_into('<f', entry, off, v)
    put(0x00, x / 4096.0)
    put(0x04, y / 4096.0)
    put(0x08, 1.0)                      # exists
    for off in (0x28, 0x30, 0x40, 0x58, 0x70):
        put(off, 1.0)                   # completed and friends
    put(0x48, min(1.0, hp / 1600.0))    # hit points
    zerg = 1.0 if pl == 0 else 0.0
    put(0x50, zerg)                     # a race bit
    put(0x5C, zerg)
    return entry


def build(units):
    out = bytearray(0x85F00 + 0x8000)
    struct.pack_into('<I', out, HDR_N, len(units))
    struct.pack_into('<I', out, HDR_W, 128)
    struct.pack_into('<I', out, HDR_H, 128)
    at = UNITS_AT
    for u in units:
        out[at:at + ENTRY] = build_entry(u, out)
        at += ENTRY
    return bytes(out)


def diff(a, b):
    ndiff = 0
    fields = {}
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            ndiff += 1
            region = 'hdr' if i < UNITS_AT else 'entry+0x%03x' % ((i - UNITS_AT) % ENTRY)
            fields[region] = fields.get(region, 0) + 1
    return ndiff, fields


def main():
    ref = open(sys.argv[1], 'rb').read()
    ours = build(SCENARIO)
    ndiff, fields = diff(ours, ref)
    print('reference bytes:', len(ref), 'ours:', len(ours))
    print('differing bytes:', ndiff)
    by_region = sorted(fields.items(), key=lambda kv: -kv[1])
    for region, n in by_region[:12]:
        print('  %-14s %d' % (region, n))
    # the coverage: how much of the obs is currently known
    known = 0
    for u in SCENARIO:
        known += 28  # the named floats' bytes
    print('known-bytes coverage: %d / %d entries bytes' % (known, len(SCENARIO) * ENTRY))


if __name__ == '__main__':
    main()
