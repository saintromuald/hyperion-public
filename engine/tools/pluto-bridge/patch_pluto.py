#!/usr/bin/env python3
"""Patch a copy of pluto.dll: every 4-byte little-endian constant in the range
[0x400000, 0x800000) inside an instruction (a memory displacement or a pointer
immediate) is remapped onto the rig's stand-in buffers, because the bot assumes
StarCraft 1.16.1's address space:

  addr <  0x400000  ->  0x21000000 + addr
  addr >= 0x400000  ->  0x21400000 + (addr - 0x400000)

fakehost.cpp's ReserveScMemory() reserves both buffers. Instruction boundaries come
from the objdump disassembly, so a window that merely overlaps two instructions is
never patched.
"""
import re
import struct
import subprocess
import sys

LOW_BASE = 0x21000000
SC_BASE = 0x400000
SC_STANDIN = 0x21400000
TEXT_RAW = 0x400


def remap(v):
    return (LOW_BASE + v) if v < SC_BASE else (SC_STANDIN + (v - SC_BASE))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "pluto.dll"
    dst = sys.argv[2] if len(sys.argv) > 2 else "pluto.patched.dll"
    dis = subprocess.run(["objdump", "-d", src], capture_output=True, text=True).stdout
    data = bytearray(open(src, "rb").read())

    # The known SC addresses: values that appear as memory operands or pointer
    # immediates in the disassembly (a whole-range scan over-fires in the places the
    # linear disassembler desyncs on padding).
    known = set()
    for line in dis.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)", line)
        if not m:
            continue
        for mm in re.finditer(r"0x([0-9a-f]{6})(?=\(|,|$)", m.group(3)):
            v = int(mm.group(1), 16)
            if 0x400000 <= v < 0x800000:
                known.add(v)
    hits = []
    rows = [m for m in (re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)", line)
                        for line in dis.splitlines()) if m]
    for idx, m in enumerate(rows):
        addr = int(m.group(1), 16)
        byts = bytes.fromhex(m.group(2))
        # objdump's byte column can be short (its disassembler misparses a final 00
        # byte); the true length is the gap to the next instruction.
        nxt = int(rows[idx + 1].group(1), 16) if idx + 1 < len(rows) else addr + len(byts)
        byts = byts.ljust(nxt - addr, b"\x00")
        file_base = TEXT_RAW + (addr - 0x63F80000 - 0x1000)
        for p in range(len(byts) - 3):
            v = struct.unpack_from("<I", byts, p)[0]
            if v in known:
                off = file_base + p
                new = remap(v)
                struct.pack_into("<I", data, off, new)
                hits.append((v, new, off))
    open(dst, "wb").write(bytes(data))
    print(f"{len(hits)} SC-memory references patched -> {dst}")
    for v, new, off in hits[:20]:
        print(f"  {v:#08x} -> {new:#010x} (file +0x{off:x})")
    if len(hits) > 20:
        print(f"  ... {len(hits) - 20} more")


if __name__ == "__main__":
    main()
