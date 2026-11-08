#!/usr/bin/env python3
"""Validate the fixed Pluto bridge PE layout without relying on assertions."""

from dataclasses import dataclass
import struct
import sys
from pathlib import Path

IMAGE_BASE = 0x400000
GAME_RVA = 0x1000
GAME_SIZE = 0x3FF000
HDR_RVA = 0x400000
HIGH_RVA = 0x401000  # VMA 0x801000; .hdr at VMA 0x800000 is the sole exception.
SECTION_ALIGNMENT = 0x1000
FILE_ALIGNMENT = 0x200
EXECUTE = 0x20000000
READ = 0x40000000
WRITE = 0x80000000
UNINITIALIZED = 0x00000080


class PeVerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise PeVerificationError(message)


def unpack(data, fmt, offset, what):
    size = struct.calcsize(fmt)
    require(offset >= 0 and offset + size <= len(data),
            f"{what} is outside the file")
    return struct.unpack_from(fmt, data, offset)


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


@dataclass
class Section:
    name: str
    virtual_size: int
    rva: int
    raw_size: int
    raw_pointer: int
    flags: int

    @property
    def end_rva(self):
        return self.rva + max(self.virtual_size, self.raw_size)

    @property
    def aligned_end_rva(self):
        return align(self.end_rva, SECTION_ALIGNMENT)


def verify(data):
    """Return a layout report or raise PeVerificationError for any invalid PE."""
    data = bytes(data)
    require(len(data) >= 0x40, "file is too short for a PE header")
    pe, = unpack(data, "<I", 0x3C, "PE header pointer")
    require(pe + 24 <= len(data), "COFF header is outside the file")
    require(data[pe:pe + 4] == b"PE\0\0", "missing PE signature")
    count, = unpack(data, "<H", pe + 6, "section count")
    optional_size, = unpack(data, "<H", pe + 20, "optional-header size")
    characteristics, = unpack(data, "<H", pe + 22, "COFF characteristics")
    require(count != 0, "PE has no sections")
    o = pe + 24
    require(optional_size >= 96 and o + optional_size <= len(data),
            "invalid optional-header extent")
    magic, = unpack(data, "<H", o, "optional-header magic")
    require(magic == 0x10B, "PE32 is required")
    entry_rva, = unpack(data, "<I", o + 16, "entrypoint")
    base, = unpack(data, "<I", o + 28, "image base")
    section_alignment, file_alignment = unpack(data, "<II", o + 32, "alignment")
    image_size, = unpack(data, "<I", o + 56, "SizeOfImage")
    size_headers, = unpack(data, "<I", o + 60, "SizeOfHeaders")
    dll_characteristics, = unpack(data, "<H", o + 70, "DLL characteristics")
    directory_count, = unpack(data, "<I", o + 92, "data-directory count")
    require(directory_count <= (optional_size - 96) // 8,
            "data directories exceed the optional header")
    section_table = o + optional_size
    section_table_end = section_table + 40 * count
    require(section_table_end <= len(data), "section table is outside the file")
    require(size_headers >= section_table_end and size_headers <= len(data),
            "SizeOfHeaders does not cover the section table")
    require(size_headers % FILE_ALIGNMENT == 0,
            "SizeOfHeaders is not file aligned")

    require(base == IMAGE_BASE, f"image base is {base:#x}, expected {IMAGE_BASE:#x}")
    require(characteristics & 0x0001, "relocations must be stripped")
    require(characteristics & 0x0020, "LARGE_ADDRESS_AWARE is required")
    require(not dll_characteristics & 0x0040, "DYNAMIC_BASE must be disabled")
    require(section_alignment == SECTION_ALIGNMENT,
            f"unexpected section alignment {section_alignment:#x}")
    require(file_alignment == FILE_ALIGNMENT,
            f"unexpected file alignment {file_alignment:#x}")
    require(image_size != 0 and image_size % section_alignment == 0,
            "SizeOfImage is not section aligned")
    require(image_size >= align(size_headers, section_alignment),
            "SizeOfImage does not cover the headers")
    require(base + image_size <= 0x100000000, "image wraps the 32-bit address space")

    directories = o + 96

    def directory(index):
        if index >= directory_count:
            return 0, 0
        return unpack(data, "<II", directories + 8 * index, f"data directory {index}")

    require(directory(5) == (0, 0), "base-relocation directory must be empty")
    sections = []
    names = set()
    for index in range(count):
        h = section_table + 40 * index
        raw_name = data[h:h + 8].rstrip(b"\0")
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError as error:
            raise PeVerificationError(f"section {index} has a non-ASCII name") from error
        require(name and name not in names, f"empty or duplicate section name {name!r}")
        names.add(name)
        virtual_size, rva, raw_size, raw_pointer = unpack(
            data, "<IIII", h + 8, f"section {name} extents")
        flags, = unpack(data, "<I", h + 36, f"section {name} flags")
        section = Section(name, virtual_size, rva, raw_size, raw_pointer, flags)
        require(rva % section_alignment == 0, f"section {name} RVA is not aligned")
        require(section.end_rva <= 0x100000000, f"section {name} wraps RVA space")
        require(section.end_rva > rva, f"section {name} has an empty mapped extent")
        require(section.aligned_end_rva <= image_size,
                f"section {name} end exceeds SizeOfImage")
        if raw_size:
            require(raw_pointer % file_alignment == 0,
                    f"section {name} raw pointer is not file aligned")
            require(raw_pointer >= size_headers,
                    f"section {name} raw data overlaps PE headers")
            require(raw_pointer + raw_size <= len(data),
                    f"section {name} raw data exceeds the file")
        sections.append(section)

    by_rva = sorted(sections, key=lambda section: section.rva)
    require(by_rva[0].rva >= align(size_headers, section_alignment),
            "first section overlaps the aligned PE headers")
    for previous, current in zip(by_rva, by_rva[1:]):
        require(current.rva >= previous.end_rva,
                f"sections {previous.name} and {current.name} overlap")
    max_image_end = max(section.aligned_end_rva for section in sections)
    require(image_size == max_image_end,
            f"SizeOfImage {image_size:#x} does not equal aligned section end {max_image_end:#x}")

    raw_sections = sorted((section for section in sections if section.raw_size),
                          key=lambda section: section.raw_pointer)
    for previous, current in zip(raw_sections, raw_sections[1:]):
        require(current.raw_pointer >= previous.raw_pointer + previous.raw_size,
                f"raw data of {previous.name} and {current.name} overlap")

    by_name = {section.name: section for section in sections}
    require(".game" in by_name and ".hdr" in by_name, "missing .game or .hdr")
    game = by_name[".game"]
    require((game.virtual_size, game.rva, game.raw_size, game.raw_pointer) ==
            (GAME_SIZE, GAME_RVA, 0, 0), "incorrect .game extent/raw layout")
    require(game.flags & (EXECUTE | READ | WRITE) == (EXECUTE | READ | WRITE),
            ".game must be readable, writable, and executable")
    require(game.flags & UNINITIALIZED, ".game must be uninitialized")
    require(game.end_rva == HDR_RVA, ".game must end at RVA 0x400000")
    header_copy = by_name[".hdr"]
    require(header_copy.rva == HDR_RVA, ".hdr must start at VMA 0x800000")
    for section in sections:
        if section.name not in (".game", ".hdr"):
            require(section.rva >= HIGH_RVA,
                    f"real section {section.name} starts below VMA 0x801000")

    entry_section = None
    for section in sections:
        if section.rva <= entry_rva < section.end_rva:
            entry_section = section
            break
    require(entry_section is not None, "entrypoint is outside every section")
    require(entry_section.name not in (".game", ".hdr") and
            entry_section.rva >= HIGH_RVA and entry_section.flags & EXECUTE,
            "entrypoint is not in an executable high section")

    def raw_location(rva, size, what):
        for section in sections:
            if section.rva <= rva:
                delta = rva - section.rva
                if delta <= section.raw_size and size <= section.raw_size - delta:
                    return section, section.raw_pointer + delta
        raise PeVerificationError(f"{what} is not backed by raw bytes in one section")

    def va_location(va, size, what):
        require(va >= base, f"{what} is below the image base")
        rva = va - base
        require(rva >= HIGH_RVA, f"{what} is below VMA 0x801000")
        require(rva <= 0xFFFFFFFF and size <= 0x100000000 - rva,
                f"{what} wraps RVA space")
        return raw_location(rva, size, what)

    tls_rva, tls_size = directory(9)
    tls_report = "absent"
    if tls_rva or tls_size:
        require(tls_size >= 24, "TLS directory is shorter than IMAGE_TLS_DIRECTORY32")
        _, tls_offset = raw_location(tls_rva, 24, "TLS directory")
        raw_start, raw_end, index_va, callbacks_va = unpack(
            data, "<IIII", tls_offset, "TLS internal VAs")
        if raw_start or raw_end:
            require(raw_start and raw_end and raw_end > raw_start,
                    "TLS raw-data VAs are empty or reversed")
            raw_length = raw_end - raw_start
            raw_section, _ = va_location(raw_start, raw_length, "TLS raw data")
            require(raw_section.name not in (".game", ".hdr"),
                    "TLS raw data is in a reserved section")
        else:
            require(raw_start == 0 and raw_end == 0,
                    "only one TLS raw-data VA is zero")
        index_section, _ = va_location(index_va, 4, "TLS index VA")
        require(index_section.flags & WRITE, "TLS index VA is not writable")
        if callbacks_va:
            _, callback_offset = va_location(callbacks_va, 4, "TLS callback array VA")
            first_callback, = unpack(data, "<I", callback_offset, "first TLS callback")
            require(first_callback == 0,
                    "TLS callback array is present but its first callback is non-null")
            tls_report = f"raw={raw_start:#x}..{raw_end:#x}, first-callback=null"
        else:
            tls_report = f"raw={raw_start:#x}..{raw_end:#x}, callback-array=absent"

    return {
        "base": base,
        "entry_rva": entry_rva,
        "entry_section": entry_section.name,
        "image_size": image_size,
        "sections": sections,
        "tls_rva": tls_rva,
        "tls_report": tls_report,
    }


def main(argv):
    if len(argv) != 2:
        print(f"usage: {argv[0]} <pluto-bridge.exe>", file=sys.stderr)
        return 2
    try:
        data = Path(argv[1]).read_bytes()
        report = verify(data)
    except (OSError, PeVerificationError, struct.error) as error:
        print(f"PE verification failed: {error}", file=sys.stderr)
        return 1
    base = report["base"]
    print(f"PE OK: base={base:#010x} entry={base + report['entry_rva']:#010x} "
          f"section={report['entry_section']} image-size={report['image_size']:#x}")
    print(f"PE OK: aligned nonoverlapping sections, raw bounds, SizeOfImage coverage")
    print(f"PE OK: LAA, fixed/no relocations, DYNAMIC_BASE off, TLS {report['tls_report']}")
    for section in report["sections"]:
        print(f"  {section.name:8s} {base + section.rva:#010x}..{base + section.end_rva:#010x} "
              f"raw={section.raw_size:#x} flags={section.flags:#010x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
