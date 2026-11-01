#!/usr/bin/env python3
"""Patch a copy of pluto.dll: its absolute memory operands that point into StarCraft
1.16.1's address space are rewritten into the rig's stand-in buffer (see fakehost.cpp's
ReserveScMemory). The bot ships without relocations for these (it assumes StarCraft's
layout), so the rig provides the memory and this maps every reference onto it.

  addr <  0x400000  ->  0x21000000 + addr
  addr >= 0x400000  ->  0x21400000 + (addr - 0x400000)

Only unambiguous absolute-memory instruction forms are patched:
  A1 (mov eax,[m32]), A3 (mov [m32],eax), 8B 0D/15/1D/25/2D/35/3D (mov r32,[m32]),
  89 05/0D/15/1D/25/2D/35/3D (mov [m32],r32), C7 05 (mov [m32],imm32),
  FF 05/0D/15/1D/25/2D/35/3D (inc/dec/call/jmp [m32]).
"""
import struct
import sys

TEXT_RAW = 0x400
TEXT_SIZE = 0x524EC
LOW_BASE = 0x21000000
SC_BASE = 0x400000
SC_STANDIN = 0x21400000


def patch(data):
    text = bytearray(data[TEXT_RAW:TEXT_RAW + TEXT_SIZE])
    hits = []
    i = 0
    n = len(text)
    while i < n - 6:
        b = text[i]
        addr = None
        alen = 0
        if b == 0xA1 or b == 0xA3:
            addr = struct.unpack_from("<I", text, i + 1)[0]
            alen = 5
        elif b == 0x8B and text[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            addr = struct.unpack_from("<I", text, i + 2)[0]
            alen = 6
        elif b == 0x89 and text[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            addr = struct.unpack_from("<I", text, i + 2)[0]
            alen = 6
        elif b == 0xC7 and text[i + 1] == 0x05:
            addr = struct.unpack_from("<I", text, i + 2)[0]
            alen = 10
        elif b == 0xFF and text[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            addr = struct.unpack_from("<I", text, i + 2)[0]
            alen = 6
        if addr is not None and addr < 0x800000:
            new = (LOW_BASE + addr) if addr < SC_BASE else (SC_STANDIN + (addr - SC_BASE))
            # C7 05 is `mov [m32], imm32`: the address operand sits at i+2 and the
            # immediate at i+6 must stay untouched; every other form's operand ends the
            # instruction, at i+alen-4.
            at = i + 2 if b == 0xC7 else i + alen - 4
            struct.pack_into("<I", text, at, new)
            hits.append((addr, new, at))
            i += alen
            continue
        i += 1
    data = data[:TEXT_RAW] + bytes(text) + data[TEXT_RAW + TEXT_SIZE:]
    return data, hits


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "pluto.dll"
    dst = sys.argv[2] if len(sys.argv) > 2 else "pluto.patched.dll"
    data = open(src, "rb").read()
    out, hits = patch(data)
    open(dst, "wb").write(out)
    print(f"{len(hits)} absolute references patched -> {dst}")
    for addr, new, off in hits:
        print(f"  {addr:#08x} -> {new:#010x} (file +0x{off:x})")


if __name__ == "__main__":
    main()
