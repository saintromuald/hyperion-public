"""A crash report's call stack with functions, files and lines, from the build's own symbols.

    python tools/crash_symbols.py <report folder, report.json, or pasted text> [--symbols <folder>]...

A player's build comes without its symbols, so its report (crashes\\<stamp>_<pid>\\,
port_crash.cpp) gives each frame as a module and an offset. The report's "symbols" line names
the program and the id of its build; this finds the program with that id among the builds
tools/package.py keeps in testrun/share/symbols/ (and the current builds), and names each of
Hyperion's frames:

- Windows: Hyperion.exe by its PDB's id, named through Windows' dbghelp.
- Linux: Hyperion.x86_64 or Hyperion.aarch64 by its GNU build id, named by llvm-symbolizer or
  llvm-addr2line when either is on PATH, else by addr2line (on Windows, WSL's).
- Mac: Hyperion.arm64 or its .dSYM by its UUID, named by llvm-symbolizer only.

The text a player copies from the error window and pastes in an issue or on Discord
(port_share.cpp) has the same lines: save it to a file, or give - and paste it in.
"""
import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
BUILD = ENGINE / "src117" / "build"
SEARCH = [ENGINE / "testrun" / "share" / "symbols", BUILD / "x64", BUILD / "linux-x64", BUILD / "linux-arm64",
          BUILD / "mac-arm64"]
POSIX_PROGRAMS = ("Hyperion.x86_64", "Hyperion.aarch64", "Hyperion.arm64")


def pe_id(exe: Path):
    """The exe's PDB id as the report writes it (port_sysinfo.cpp): the GUID's fields in hex,
    then the age; and its preferred base and size."""
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        return None  # not a 64-bit image
    base = struct.unpack_from("<Q", data, opt + 24)[0]
    size = struct.unpack_from("<I", data, opt + 56)[0]
    debug_rva, debug_size = struct.unpack_from("<II", data, opt + 112 + 6 * 8)
    table = opt + struct.unpack_from("<H", data, pe + 20)[0]

    def offset(rva):
        for i in range(sections):
            # a section header's VirtualAddress, SizeOfRawData and PointerToRawData
            va, raw_size, raw = struct.unpack_from("<III", data, table + i * 40 + 12)
            if va <= rva < va + raw_size:
                return raw + rva - va
        return None

    at = offset(debug_rva)
    for i in range(debug_size // 28 if at is not None else 0):
        kind, _, _, ptr = struct.unpack_from("<IIII", data, at + i * 28 + 12)
        if kind != 2 or data[ptr:ptr + 4] != b"RSDS":  # IMAGE_DEBUG_TYPE_CODEVIEW
            continue
        d1, d2, d3 = struct.unpack_from("<IHH", data, ptr + 4)
        d4 = data[ptr + 12:ptr + 20]
        age = struct.unpack_from("<I", data, ptr + 20)[0]
        name = data[ptr + 24:data.index(b"\0", ptr + 24)].decode("ascii", "replace")
        name = name.replace("/", "\\").rsplit("\\", 1)[-1]
        return f"{name} {d1:08X}{d2:04X}{d3:04X}{d4.hex().upper()}{age:X}", base, size
    return None


def elf_id(path: Path):
    """A 64-bit little-endian ELF's GNU build id in hex, and the address its offsets start from:
    the report's offsets are from the program's first byte in memory (__executable_start)."""
    with open(path, "rb") as f:
        head = f.read(64)
        if head[:6] != b"\x7fELF\x02\x01":
            return None
        phoff, = struct.unpack_from("<Q", head, 32)
        phentsize, phnum = struct.unpack_from("<HH", head, 54)
        f.seek(phoff)
        table = f.read(phentsize * phnum)
        start, notes = None, []
        for i in range(phnum):
            kind, _, offset, vaddr, _, filesz = struct.unpack_from("<IIQQQQ", table, i * phentsize)
            if kind == 1 and start is None:  # PT_LOAD
                start = vaddr - offset
            elif kind == 4:  # PT_NOTE
                notes.append((offset, filesz))
        for offset, size in notes:
            f.seek(offset)
            data, at = f.read(size), 0
            while at + 12 <= len(data):
                namesz, descsz, kind = struct.unpack_from("<III", data, at)
                desc = at + 12 + (namesz + 3) // 4 * 4
                if kind == 3 and data[at + 12:at + 12 + namesz] == b"GNU\0":  # NT_GNU_BUILD_ID
                    return data[desc:desc + descsz].hex(), start or 0
                at = desc + (descsz + 3) // 4 * 4
    return None


def macho_id(path: Path):
    """A 64-bit Mach-O's UUID in hex, and its __TEXT segment's address, where the report's
    offsets start (_mh_execute_header). A .dSYM's DWARF file has the same."""
    with open(path, "rb") as f:
        head = f.read(32)
        if len(head) < 32 or struct.unpack_from("<I", head)[0] != 0xFEEDFACF:
            return None
        ncmds, sizeofcmds = struct.unpack_from("<II", head, 16)
        data = f.read(sizeofcmds)
    uuid, text, at = None, 0, 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, at)
        if cmd == 0x1B:  # LC_UUID
            uuid = data[at + 8:at + 24].hex().upper()
        elif cmd == 0x19 and data[at + 8:at + 24].rstrip(b"\0") == b"__TEXT":  # LC_SEGMENT_64
            text = struct.unpack_from("<Q", data, at + 24)[0]
        at += size
    return (uuid, text) if uuid else None


def find_exe(wanted: str, folders):
    for folder in folders:
        for exe in sorted(folder.rglob("Hyperion.exe")) if folder.is_dir() else []:
            got = pe_id(exe)
            if got and got[0] == wanted and exe.with_suffix(".pdb").is_file():
                return exe, got[1], got[2]
    return None


def find_program(wanted: str, folders):
    """The Linux or Mac program with the id the report names, a .dSYM's first; and where its
    offsets start."""
    wanted = wanted.split()[-1].lower()
    for folder in folders:
        found = [p for name in POSIX_PROGRAMS for p in folder.rglob(name)] if folder.is_dir() else []
        for path in sorted((p for p in found if p.is_file()), key=lambda p: ".dSYM" not in str(p)):
            got = elf_id(path) or macho_id(path)
            if got and got[0].lower() == wanted:
                return path, got[1]
    return None


def from_text(text: str) -> dict:
    """The parts of a report a pasted text has: what, where, the version, the symbols, the frames."""
    report = {"what": "", "where": "", "system": {"build": {}}, "stack": []}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("what: "):
            report["what"] = line[6:]
        elif line.startswith("where: "):
            report["where"] = line[7:].split(" ")[0]
        elif line.startswith("symbols: "):
            report["system"]["build"]["symbols"] = line[9:]
        elif re.match(r"Hyperion \d+\.\d+\.\d+", line):
            report["system"]["build"]["version"] = line[9:]
        m = re.match(r"#(\d+) (\S+)\+(0x[0-9A-Fa-f]+)(?: (.*))?$", line)
        if m:
            report["stack"].append({"n": int(m.group(1)), "module": m.group(2), "offset": m.group(3),
                                    "function": m.group(4) or ""})
    return report


def windows_names(exe: Path, base: int, size: int, frames) -> dict:
    """{frame index: text} for Hyperion.exe's frames, through dbghelp."""
    import ctypes
    from ctypes import wintypes

    class SymbolInfo(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG),
                    ("Reserved", ctypes.c_uint64 * 2), ("Index", wintypes.ULONG), ("Size", wintypes.ULONG),
                    ("ModBase", ctypes.c_uint64), ("Flags", wintypes.ULONG), ("Value", ctypes.c_uint64),
                    ("Address", ctypes.c_uint64), ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG),
                    ("Tag", wintypes.ULONG), ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG),
                    ("Name", ctypes.c_wchar * 512)]

    class LineInfo(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wintypes.DWORD),
                    ("FileName", ctypes.c_wchar_p), ("Address", ctypes.c_uint64)]

    dbghelp = ctypes.WinDLL("dbghelp")
    process = wintypes.HANDLE(0x4859)  # any value of our own, not a process
    dbghelp.SymSetOptions(0x2 | 0x10)  # SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
    if not dbghelp.SymInitializeW(process, ctypes.c_wchar_p(str(exe.parent)), False):
        sys.exit("SymInitialize failed")
    dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
    dbghelp.SymLoadModuleExW.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                         ctypes.c_uint64, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    if not dbghelp.SymLoadModuleExW(process, None, str(exe), None, base, size, None, 0):
        sys.exit("SymLoadModuleEx failed")
    dbghelp.SymFromAddrW.argtypes = [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                     ctypes.POINTER(SymbolInfo)]
    dbghelp.SymGetLineFromAddrW64.argtypes = [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(wintypes.DWORD),
                                              ctypes.POINTER(LineInfo)]
    names = {}
    for i, offset, exact in frames:
        address = base + offset
        sym = SymbolInfo(SizeOfStruct=88, MaxNameLen=512)  # sizeof(SYMBOL_INFOW), its name of one
        disp = ctypes.c_uint64()
        ok = dbghelp.SymFromAddrW(process, address, ctypes.byref(disp), ctypes.byref(sym))
        name = f"{sym.Name}+0x{disp.value:X}" if ok else "?"
        line = LineInfo(SizeOfStruct=ctypes.sizeof(LineInfo))
        col = wintypes.DWORD()
        lookup = address if exact else address - 1
        at = (f"  {Path(line.FileName).name}:{line.LineNumber}"
              if dbghelp.SymGetLineFromAddrW64(process, lookup, ctypes.byref(col), ctypes.byref(line)) else "")
        names[i] = name + at
    dbghelp.SymCleanup(process)
    return names


def wsl_path(path: Path) -> str:
    p = path.resolve().as_posix()
    return f"/mnt/{p[0].lower()}{p[2:]}" if p[1:2] == ":" else p


def symbolizer(program: Path, elf: bool):
    """The command that names addresses in program, and whether its output is addr2line's."""
    for tool in ("llvm-symbolizer", "llvm-addr2line"):
        if shutil.which(tool):
            if tool == "llvm-symbolizer":
                return [tool, "--obj=" + str(program), "--demangle", "--inlining", "--output-style=GNU", "--addresses"]
            return [tool, "-f", "-C", "-i", "-a", "-e", str(program)]
    if not elf:
        return None
    if sys.platform == "win32":
        if not shutil.which("wsl.exe"):
            return None
        return ["wsl.exe", "-e", "addr2line", "-f", "-C", "-i", "-a", "-e", wsl_path(program)]
    return ["addr2line", "-f", "-C", "-i", "-a", "-e", str(program)] if shutil.which("addr2line") else None


def dwarf_names(program: Path, start: int, frames, elf: bool) -> dict:
    """{frame index: text} for Hyperion's frames, through addr2line's output: for each address,
    the address, then a function and its file:line for each function inlined there, innermost first."""
    command = symbolizer(program, elf)
    if command is None:
        print("no llvm-symbolizer on PATH" + (", nor addr2line" if elf else "") + ": the frames stay unnamed")
        return {}
    addresses = [start + offset - (0 if exact else 1) for _, offset, exact in frames]
    r = subprocess.run(command + [f"0x{a:x}" for a in addresses], capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(command[:3]), "failed:", r.stderr.strip()[:300])
        return {}
    blocks, lines = [], [l for l in r.stdout.splitlines() if l.strip()]
    for line in lines:
        if re.fullmatch(r"0x[0-9a-fA-F]+", line.strip()):
            blocks.append([])
        elif blocks:
            blocks[-1].append(line.strip())
    names = {}
    for (i, _, _), block in zip(frames, blocks):
        pairs = []
        for function, place in zip(block[0::2], block[1::2]):
            where = place.split(" (discriminator")[0]
            file, _, line = where.rpartition(":")
            pairs.append(f"{function}  {Path(file).name}:{line}" if file and file != "??" else function)
        names[i] = "\n       inlined in ".join(pairs) or "?"
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("report", help="a crash's folder, its report.json, a pasted text in a file, or - for one pasted in")
    ap.add_argument("--symbols", type=Path, action="append", default=[], help="another folder to look in")
    args = ap.parse_args()
    if args.report == "-":
        text = sys.stdin.read()
    else:
        path = Path(args.report)
        text = (path / "report.json" if path.is_dir() else path).read_text(encoding="utf-8")
    try:
        report = json.loads(text)
    except ValueError:
        report = from_text(text)
    build = report.get("system", {}).get("build", {})
    wanted = build.get("symbols", "")
    print(report.get("what", ""))
    print("build", build.get("version", "?") + (f" ({build['commit']})" if build.get("commit") else ""),
          "| symbols", wanted or "?")
    folders = args.symbols + SEARCH
    windows = wanted.split(" ")[0].lower().endswith(".pdb")
    found = (find_exe(wanted, folders) if windows else find_program(wanted, folders)) if wanted else None
    if found is None:
        print("no program with that id, and its symbols, in", ", ".join(str(f) for f in folders))
        print("(tools/package.py keeps each packaged build's, and each Windows build it installs)")
        return 1
    print("read with", found[0])

    stack = report.get("stack", [])
    module = "Hyperion.exe" if windows else "Hyperion"
    # a return address is past its call, so its line is looked up a byte before; the frame
    # where the fault is, is looked up where it is
    where = report.get("where", "")
    ours = []
    for k, frame in enumerate(stack):
        if frame["module"] == module:
            i = frame.get("n", k)
            exact = f"{frame['module']}+{frame['offset']}" == where if where else i == 0
            ours.append((i, int(frame["offset"], 16), exact))
    if windows:
        names = windows_names(found[0], found[1], found[2], ours)
    else:
        names = dwarf_names(found[0], found[1], ours, elf_id(found[0]) is not None)
    for k, frame in enumerate(stack):
        i = frame.get("n", k)
        place = f"{frame['module']}+{frame['offset']}"
        print(f"{i:3}  {place}  {names.get(i, frame.get('function', '')) if frame['module'] == module else frame.get('function', '')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
