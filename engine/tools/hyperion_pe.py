"""Lay out Hyperion.exe so its image covers the range StarCraft 1.17 occupies.

The 1.17 exe's image is 0x400000..0x6FF000 (base 0x400000, SizeOfImage 0x2FF000).
Hyperion.exe copies the player's exe into that range at start-up
(hyperion/hyperion.cpp), so its own image has to start at 0x400000 with that
range free: a placeholder section from 0x401000 to 0x700000 and its own code and
data after. The linker always puts .text first, so the exe is linked with
/BASE:0x700000 /FIXED and this script moves the header down instead:

  - the image base becomes 0x400000 and every RVA grows by 0x300000, so every
    section keeps its virtual address and no code or absolute address changes;
  - a placeholder section, uninitialized and read-write-execute, is added at RVA
    0x1000, 0x2FF000 bytes;
  - a copy of the linked header is added at RVA 0x300000 (0x700000, the link
    base), because the C runtime reads its own header at __ImageBase, the link
    base, to check which of its pointers sit in read-only sections.

RVAs are rewritten in the section table, the optional header, the data directory,
the import descriptors and their name thunks, the resource data entries and the
debug directory. The link must be /FIXED (no relocations), without delay loads,
exports or TLS callbacks, and /SAFESEH:NO, since a SafeSEH table would reject the
original's exception handlers, which run inside this image.

    python tools/hyperion_pe.py <linked.exe> <Hyperion.exe>
"""
import struct
import sys

NEW_BASE = 0x400000
LINK_BASE = 0x700000
DELTA = LINK_BASE - NEW_BASE           # 0x300000
GAME_RVA, GAME_SIZE = 0x1000, 0x2FF000  # 0x401000..0x700000
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
    print(f"hyperion_pe: {dst}: image 0x{NEW_BASE:x}, placeholder 0x{NEW_BASE + GAME_RVA:x}..0x{NEW_BASE + GAME_RVA + GAME_SIZE:x}")
