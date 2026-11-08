"""Lay out pluto-bridge.exe with an image-backed StarCraft 1.16.1 reservation.

The official Pluto DLL accesses genuine StarCraft addresses in
0x400000..0x800000.  The bridge is therefore linked fixed at 0x800000 with
LARGE_ADDRESS_AWARE, and this script moves the PE image header down without
moving any linked virtual address:

  - the final image base becomes 0x400000 and every RVA grows by 0x400000, so
    section virtual addresses and absolute code/data pointers remain unchanged;
  - .game, an uninitialized read-write-execute section at RVA 0x1000 with size
    0x3FF000, reserves 0x401000..0x800000 for the SC image;
  - .hdr at RVA 0x400000 (VMA 0x800000) preserves a header copy for the CRT;
  - real bridge code/data remains at or above 0x801000, with .hdr the only
    special section starting at 0x800000.

Section-table, optional-header, data-directory, import, resource, and debug RVAs
are rewritten.  TLS internal raw-data/index/callback values are absolute VAs,
not RVAs, and stay valid because their linked addresses do not move.  The build
has no actual TLS callback: verify_pe.py checks those internal VAs and requires
a null first callback when a callback array is present.  The input must have no
relocations, exports, delay loads, or bound imports and no SafeSEH handler table.
Debug sections are separated before this transformation to leave header space
for .game and .hdr.

    python3 pluto_pe.py <linked-stripped.exe> <pluto-bridge.exe>
"""
import struct
import sys

NEW_BASE = 0x400000
LINK_BASE = 0x800000
DELTA = LINK_BASE - NEW_BASE           # 0x400000
GAME_RVA, GAME_SIZE = 0x1000, 0x3FF000  # 0x401000..0x800000
HDR_RVA = DELTA                         # the header copy at the link base

DIR_EXPORT, DIR_IMPORT, DIR_RESOURCE, DIR_EXCEPTION, DIR_SECURITY, DIR_RELOC, DIR_DEBUG = range(7)
DIR_TLS, DIR_LOADCONFIG, DIR_BOUND, DIR_IAT, DIR_DELAY = 9, 10, 11, 12, 13


def align(v, a):
    return (v + a - 1) // a * a


def rewrite(src: bytes) -> bytes:
    d = bytearray(src)
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[pe:pe + 4] == b"PE\0\0"
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    o = pe + 24
    assert struct.unpack_from("<H", d, o)[0] == 0x10B, "PE32 only"
    base = struct.unpack_from("<I", d, o + 28)[0]
    assert base == LINK_BASE, f"link with /BASE:{LINK_BASE:#x} (got {base:#x})"
    sect_align, file_align = struct.unpack_from("<II", d, o + 32)
    size_hdrs = struct.unpack_from("<I", d, o + 60)[0]
    ndirs = struct.unpack_from("<I", d, o + 92)[0]
    dirs = o + 96
    st = o + optsz

    def dir_(i):
        return struct.unpack_from("<II", d, dirs + 8 * i) if i < ndirs else (0, 0)

    for i, what in ((DIR_RELOC, "relocations (link /FIXED)"), (DIR_EXPORT, "exports"),
                    (DIR_DELAY, "delay imports"), (DIR_BOUND, "bound imports")):
        assert dir_(i)[0] == 0, f"unsupported: {what}"
    secs = []
    for i in range(nsec):
        h = st + 40 * i
        secs.append((bytes(d[h:h + 8]), *struct.unpack_from("<IIII", d, h + 8), struct.unpack_from("<I", d, h + 36)[0]))
    assert secs[0][2] >= 0x1000 and secs[0][2] + DELTA > HDR_RVA

    def off(rva):   # file offset of a pre-rewrite RVA
        for name, vs, va, rs, rp, ch in secs:
            if va <= rva < va + max(vs, rs):
                return rp + rva - va
        raise ValueError(f"rva {rva:#x} is in no section")

    if dir_(DIR_LOADCONFIG)[0]:
        lc = off(dir_(DIR_LOADCONFIG)[0])
        if struct.unpack_from("<I", d, lc)[0] >= 0x48:
            assert struct.unpack_from("<I", d, lc + 0x40)[0] == 0, "link with /SAFESEH:NO"

    header_copy = bytes(src[:size_hdrs])

    # Import descriptors and their name thunks.
    if dir_(DIR_IMPORT)[0]:
        p = off(dir_(DIR_IMPORT)[0])
        while True:
            oft, ts, fc, name, ft = struct.unpack_from("<IIIII", d, p)
            if name == 0 and ft == 0:
                break
            for thunks in {oft, ft} - {0}:
                t = off(thunks)
                while True:
                    v = struct.unpack_from("<I", d, t)[0]
                    if v == 0:
                        break
                    if not v & 0x80000000:
                        struct.pack_into("<I", d, t, v + DELTA)
                    t += 4
            struct.pack_into("<IIIII", d, p, oft + DELTA if oft else 0, ts, fc, name + DELTA, ft + DELTA)
            p += 20

    # Resource data entries (the directories inside use section-relative offsets).
    if dir_(DIR_RESOURCE)[0]:
        root = off(dir_(DIR_RESOURCE)[0])

        def walk(p):
            nn, ni = struct.unpack_from("<HH", d, root + p + 12)
            for k in range(nn + ni):
                _, sub = struct.unpack_from("<II", d, root + p + 16 + 8 * k)
                if sub & 0x80000000:
                    walk(sub & 0x7FFFFFFF)
                else:
                    rva = struct.unpack_from("<I", d, root + sub)[0]
                    struct.pack_into("<I", d, root + sub, rva + DELTA)
        walk(0)

    if dir_(DIR_DEBUG)[0]:
        va, size = dir_(DIR_DEBUG)
        p = off(va)
        for k in range(size // 28):
            rva = struct.unpack_from("<I", d, p + 28 * k + 20)[0]
            if rva:
                struct.pack_into("<I", d, p + 28 * k + 20, rva + DELTA)

    for i in range(ndirs):
        va, size = dir_(i)
        if va and i != DIR_SECURITY:
            struct.pack_into("<I", d, dirs + 8 * i, va + DELTA)

    # The optional header.
    entry, boc, bod = struct.unpack_from("<III", d, o + 16)
    struct.pack_into("<III", d, o + 16, entry + DELTA, boc + DELTA, bod + DELTA)
    struct.pack_into("<I", d, o + 28, NEW_BASE)
    size_img = struct.unpack_from("<I", d, o + 56)[0]
    struct.pack_into("<I", d, o + 56, size_img + DELTA)
    uninit = struct.unpack_from("<I", d, o + 12)[0]
    struct.pack_into("<I", d, o + 12, uninit + GAME_SIZE)
    struct.pack_into("<I", d, o + 64, 0)   # checksum, not needed for an exe

    # The section table: the placeholder, the header copy, then the linked sections.
    hdr_raw = align(len(d), file_align)
    hdr_rawsize = align(size_hdrs, file_align)
    new = [
        struct.pack("<8sIIIIIIHHI", b".game\0\0\0", GAME_SIZE, GAME_RVA, 0, 0, 0, 0, 0, 0, 0xE0000080),
        struct.pack("<8sIIIIIIHHI", b".hdr\0\0\0\0", 0x1000, HDR_RVA, hdr_rawsize, hdr_raw, 0, 0, 0, 0, 0x40000040),
    ]
    for name, vs, va, rs, rp, ch in secs:
        new.append(struct.pack("<8sIIIIIIHHI", name, vs, va + DELTA, rs, rp, 0, 0, 0, 0, ch))
    end = st + 40 * len(new)
    assert end <= size_hdrs, "no room in the header for two more sections"
    assert all(x == 0 for x in d[st + 40 * nsec:end]), "the header space after the section table is not free"
    d[st:end] = b"".join(new)
    struct.pack_into("<H", d, pe + 6, len(new))

    d += b"\0" * (hdr_raw - len(d))
    d += header_copy + b"\0" * (hdr_rawsize - len(header_copy))
    return bytes(d)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    out = rewrite(open(src, "rb").read())
    open(dst, "wb").write(out)
    print(f"pluto_pe: {dst}: image 0x{NEW_BASE:x}, .game 0x{NEW_BASE + GAME_RVA:x}..0x{NEW_BASE + GAME_RVA + GAME_SIZE:x}")
