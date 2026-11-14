#!/usr/bin/env python3
"""Hermetic valid/mutation tests for verify_pe.py (also run with PYTHONOPTIMIZE)."""

import importlib.util
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("verify_pe", ROOT.parent / "verify_pe.py")
verify_pe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = verify_pe
SPEC.loader.exec_module(verify_pe)

PE = 0x80
OPTIONAL = PE + 24
SECTION_TABLE = OPTIONAL + 0xE0
SECTION_FLAGS = {
    ".game": 0xE0000080,
    ".hdr": 0x40000040,
    ".text": 0x60000020,
    ".rdata": 0x40000040,
    ".data": 0xC0000040,
    ".tls": 0xC0000040,
}


def put(data, fmt, offset, *values):
    struct.pack_into(fmt, data, offset, *values)


def section_offset(name):
    names = (".game", ".hdr", ".text", ".rdata", ".data", ".tls")
    return SECTION_TABLE + 40 * names.index(name)


def valid_fixture():
    data = bytearray(0x1000)
    put(data, "<I", 0x3C, PE)
    data[PE:PE + 4] = b"PE\0\0"
    put(data, "<HHIIIHH", PE + 4, 0x14C, 6, 0, 0, 0, 0xE0, 0x123)
    put(data, "<H", OPTIONAL, 0x10B)
    put(data, "<I", OPTIONAL + 16, 0x401000)
    put(data, "<III", OPTIONAL + 20, 0x401000, 0x402000, 0x400000)
    put(data, "<II", OPTIONAL + 32, 0x1000, 0x200)
    put(data, "<I", OPTIONAL + 56, 0x405000)
    put(data, "<I", OPTIONAL + 60, 0x400)
    put(data, "<H", OPTIONAL + 70, 0x8100)
    put(data, "<I", OPTIONAL + 92, 16)
    put(data, "<II", OPTIONAL + 96 + 9 * 8, 0x402080, 24)
    sections = (
        (".game", 0x3FF000, 0x1000, 0, 0),
        (".hdr", 0x1000, 0x400000, 0x400, 0xC00),
        (".text", 0x100, 0x401000, 0x200, 0x400),
        (".rdata", 0x100, 0x402000, 0x200, 0x600),
        (".data", 0x100, 0x403000, 0x200, 0x800),
        (".tls", 0x10, 0x404000, 0x200, 0xA00),
    )
    for name, virtual_size, rva, raw_size, raw_pointer in sections:
        put(data, "<8sIIIIIIHHI", section_offset(name), name.encode(), virtual_size,
            rva, raw_size, raw_pointer, 0, 0, 0, 0, SECTION_FLAGS[name])
    put(data, "<IIIIII", 0x680, 0x804000, 0x80400C, 0x803020, 0x802040, 0, 0)
    put(data, "<I", 0x640, 0)  # first callback is the required null terminator
    return data


def expect(condition, message):
    if not condition:
        raise RuntimeError(message)


def expect_invalid(name, data, text):
    try:
        verify_pe.verify(data)
    except verify_pe.PeVerificationError as error:
        expect(text.lower() in str(error).lower(),
               f"{name}: expected {text!r} in verifier error, got {error}")
        print(f"PASS: PE mutation {name}: {error}")
        return
    raise RuntimeError(f"{name}: invalid PE unexpectedly verified")


def main():
    valid = valid_fixture()
    report = verify_pe.verify(valid)
    expect(report["entry_section"] == ".text", "valid fixture entry section")
    print("PASS: valid synthetic fixed PE (assertions cannot be optimized away)")

    no_tls = bytearray(valid)
    put(no_tls, "<II", OPTIONAL + 96 + 9 * 8, 0, 0)
    verify_pe.verify(no_tls)
    no_callbacks = bytearray(valid)
    put(no_callbacks, "<I", 0x68C, 0)
    verify_pe.verify(no_callbacks)
    print("PASS: valid PE variants with absent TLS directory/callback array")

    changed = bytearray(valid)
    put(changed, "<I", section_offset(".text") + 8, 0x3000)
    expect_invalid("section overlap", changed, "overlap")

    changed = bytearray(valid)
    put(changed, "<I", section_offset(".data") + 20, 0x600)
    expect_invalid("raw overlap", changed, "raw data")

    changed = bytearray(valid)
    put(changed, "<I", section_offset(".text") + 16, 0x1000)
    expect_invalid("raw bounds", changed, "exceeds the file")

    changed = bytearray(valid)
    put(changed, "<I", OPTIONAL + 56, 0x405001)
    expect_invalid("SizeOfImage alignment", changed, "section aligned")

    changed = bytearray(valid)
    put(changed, "<I", OPTIONAL + 56, 0x404000)
    expect_invalid("SizeOfImage coverage", changed, "SizeOfImage")

    changed = bytearray(valid)
    put(changed, "<I", OPTIONAL + 16, 0x402000)
    expect_invalid("entrypoint not executable", changed, "entrypoint")

    changed = bytearray(valid)
    put(changed, "<I", OPTIONAL + 16, 0x1000)
    expect_invalid("entrypoint in reserved game section", changed, "entrypoint")

    changed = bytearray(valid)
    put(changed, "<II", 0x680, 0x80400C, 0x804000)
    expect_invalid("TLS raw data reversed", changed, "TLS raw-data")

    changed = bytearray(valid)
    put(changed, "<II", 0x680, 0x400000, 0x40000C)
    expect_invalid("TLS raw data low", changed, "TLS raw data")

    changed = bytearray(valid)
    put(changed, "<I", 0x688, 0)
    expect_invalid("TLS index invalid", changed, "TLS index")

    changed = bytearray(valid)
    put(changed, "<I", 0x68C, 0x400000)
    expect_invalid("TLS callback array low", changed, "TLS callback")

    changed = bytearray(valid)
    put(changed, "<I", 0x640, 0x801000)
    expect_invalid("TLS first callback non-null", changed, "first callback")

    print("PASS: 14 PE verifier valid/mutation groups")


if __name__ == "__main__":
    main()
