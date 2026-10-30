#!/usr/bin/env python3
"""coff.py -- minimal COFF (.obj) reader for the objdiff byte-match gate.

Extracts per-function COMDAT sections (the /Gy contract: one function per
section), their raw code bytes, and their relocation records with SYMBOLIC
targets. This is the ours-side of the object-level compare: no link, no PDB.

Name join: MSVC-decorated symbols are undecorated to the config-style
qualified name (`Class::Method`, free `func`) via dbghelp
UnDecorateSymbolName(UNDNAME_NAME_ONLY); C-linkage decorations
(`_cdecl`, `_stdcall@N`, `@fastcall@N`) are stripped by pattern.

API:
    obj = ObjFile(path)
    obj.functions() -> {qualified_name: FnRec(bytes, relocs, section, sym)}
        FnRec.relocs = [(offset, rtype, target_qualified_name, target_raw)]
        rtype in {"DIR32", "DIR32NB", "REL32", other-hex}
"""
import ctypes
import os
import re
import struct
import subprocess

# i386 relocation types we care about
_RELTYPES = {0x06: "DIR32", 0x07: "DIR32NB", 0x14: "REL32"}

UNDNAME_EXE = r"C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\undname.exe"
_UNDNAME_NAME_ONLY = 0x1000

_dbghelp = None
_und_cache = {}


def _dbghelp_und(decorated):
    global _dbghelp
    if _dbghelp is None:
        _dbghelp = ctypes.WinDLL("dbghelp")
    buf = ctypes.create_string_buffer(8192)
    n = _dbghelp.UnDecorateSymbolName(decorated.encode("ascii", "replace"),
                                      buf, 8192, _UNDNAME_NAME_ONLY)
    return buf.value.decode("ascii", "replace") if n else None


def _undname_exe(decorated):
    try:
        out = subprocess.run([UNDNAME_EXE, hex(_UNDNAME_NAME_ONLY), decorated],
                             capture_output=True, text=True, timeout=10).stdout
        m = re.search(r'is :- "(.*)"', out)
        return m.group(1) if m else None
    except Exception:
        return None


def norm_name(s):
    """Whitespace-normalize a qualified name so PDB/config/undname renderings join.
    (`A<B,C> >::f` vs `A<B, C>>::f` etc.)"""
    s = re.sub(r"\s+", " ", s.strip())
    s = s.replace(", ", ",")
    s = re.sub(r">\s+>", ">>", s)          # canonical: no space between closers
    return s


def name_forms(n):
    """All plausible renderings of one function name, for callee-identity
    testing between the .obj symbol and the config name. The config keeps
    literal decorated/CRT spellings (`@veclen@16`, `_toupper`,
    `rle_draw_hflip_`); the .obj side arrives undecorated. Equality =
    non-empty intersection of the two form sets."""
    n = norm_name(n)
    out = {n}
    m = re.match(r"^@(.+)@\d+$", n)          # @fastcall@N
    if m:
        out.add(m.group(1))
    m = re.match(r"^_(.+?)@\d+$", n)         # _stdcall@N
    if m:
        out.add(m.group(1))
    if n.startswith("_") and not n.startswith("__"):
        out.add(n[1:])                        # CRT cdecl `_toupper`
    if n.endswith("_"):
        out.add(n[:-1])                       # asm-alias trailing underscore
    m = re.search(r"::(sub_[0-9A-Fa-f]{6})$", n)
    if m:
        out.add(m.group(1))                   # de-class auto-named (addr in name)
    # config names may carry a parameter list (`...::Clear(void)`) -- strip it
    for x in list(out):
        s = re.sub(r"\((?:[^()]|\([^()]*\))*\)$", "", x)
        if s and s != x:
            out.add(s)
    # default template arg: PDB renders `TSGetLink<X>`, IDA/config renders `0`
    for x in list(out):
        s = re.sub(r"TSGetLink<[^<>]*>", "0", x)
        if s != x:
            out.add(s)
    # template-arg struct-tag spelling: config renders the raw tag `_DLGGRP`
    # where the PDB renders the typedef `DLGGRP` -- normalize `<_X`/`,_X` -> X
    for x in list(out):
        s = re.sub(r"([<,])_(?=[A-Za-z])", r"\1", x)
        if s != x:
            out.add(s)
    return out


def names_equal(a, b):
    return bool(name_forms(a) & name_forms(b))


def undecorate(decorated):
    """Decorated COFF symbol -> config-style qualified name."""
    if decorated in _und_cache:
        return _und_cache[decorated]
    r = decorated
    if decorated.startswith("?"):
        r = _dbghelp_und(decorated) or _undname_exe(decorated) or decorated
    elif decorated.startswith("@"):        # __fastcall: @name@N
        r = re.sub(r"@\d+$", "", decorated[1:])
    elif decorated.startswith("_"):        # __cdecl _name / __stdcall _name@N
        r = re.sub(r"@\d+$", "", decorated[1:])
    r = norm_name(r)
    _und_cache[decorated] = r
    return r


class FnRec:
    __slots__ = ("name", "decorated", "bytes", "relocs", "section", "static",
                 "secnum", "labels")

    def __init__(self, name, decorated, data, relocs, section, static,
                 secnum=0, labels=None):
        self.name, self.decorated = name, decorated
        self.bytes, self.relocs = data, relocs
        # secnum + labels exist for ONE caller: the jump-table tail check in
        # cpp117_objdiff.compare().  A `$LN` label's symbol `value` is its offset
        # inside its own section, and under /Gy that section IS this function's
        # COMDAT -- so `labels` turns a tail DIR32's target name into an offset
        # comparable with the oracle's table entry.  Resolve by (name, SECTION):
        # `$LNn` names collide across COMDATs in one object and resolving by name
        # alone silently returns another function's label (wave-45 survey trap 2).
        self.secnum, self.labels = secnum, (labels or {})
        self.section, self.static = section, static


class ObjFile:
    def __init__(self, path):
        self.path = path
        d = open(path, "rb").read()
        self.data = d
        (machine, nsec, _, symptr, nsym, optsz, _) = struct.unpack_from("<HHIIIHH", d, 0)
        # Hyperion: AMD64 objects too, for the x64 build
        if machine not in (0x14C, 0x8664):
            raise ValueError(f"{path}: not an i386 or AMD64 COFF (machine=0x{machine:X})")
        self.machine = machine
        self.nsec, self.symptr, self.nsym = nsec, symptr, nsym
        # string table sits right after the symbol table
        strtab_off = symptr + nsym * 18
        self.strtab = d[strtab_off:]
        # ---- section table (header is 20 bytes; no optional header in objs) ----
        self.sections = []            # 1-based index -> dict
        base = 20 + optsz
        for i in range(nsec):
            o = base + i * 40
            raw_name = d[o:o + 8]
            name = self._secname(raw_name)
            vsz, va, rawsz, rawptr, relptr, _, nrel, _, chars = \
                struct.unpack_from("<IIIIIIHHI", d, o + 12 - 4)  # unpack from Name end
            self.sections.append({"name": name, "rawsz": rawsz, "rawptr": rawptr,
                                  "relptr": relptr, "nrel": nrel, "chars": chars,
                                  "vsz": vsz})
        # ---- symbol table ----
        self.symbols = []             # flat list incl. aux (aux entries are None)
        i = 0
        while i < nsym:
            o = symptr + i * 18
            name = self._symname(d[o:o + 8])
            value, secnum, stype, sclass, naux = struct.unpack_from("<IhHBB", d, o + 8)
            self.symbols.append({"name": name, "value": value, "sec": secnum,
                                 "type": stype, "class": sclass})
            for _ in range(naux):
                self.symbols.append(None)
            i += 1 + naux

    # -- name helpers ------------------------------------------------------
    def _secname(self, raw):
        s = raw.rstrip(b"\x00").decode("ascii", "replace")
        if s.startswith("/"):                      # long name -> string-table offset
            try:
                off = int(s[1:])
                end = self.strtab.index(b"\x00", off)
                return self.strtab[off:end].decode("ascii", "replace")
            except Exception:
                return s
        return s

    def _symname(self, raw8):
        if raw8[:4] == b"\x00\x00\x00\x00":        # long name
            off = struct.unpack_from("<I", raw8, 4)[0]
            end = self.strtab.index(b"\x00", off)
            return self.strtab[off:end].decode("ascii", "replace")
        return raw8.rstrip(b"\x00").decode("ascii", "replace")

    # -- extraction --------------------------------------------------------
    def _section_relocs(self, si):
        """Relocations of 1-based section si -> [(offset, rtype, sym_decorated)]."""
        sec = self.sections[si - 1]
        out = []
        for i in range(sec["nrel"]):
            o = sec["relptr"] + i * 10
            va, symidx, rtype = struct.unpack_from("<IIH", self.data, o)
            sym = self.symbols[symidx] if symidx < len(self.symbols) else None
            nm = sym["name"] if sym else f"<sym#{symidx}>"
            out.append((va, _RELTYPES.get(rtype, f"0x{rtype:02X}"), nm))
        return out

    def functions(self):
        """{qualified_name: FnRec} for every function symbol defined at the
        start of a code section (the /Gy COMDAT contract)."""
        out = {}
        for s in self.symbols:
            if s is None:
                continue
            # DTYPE_FUNCTION in the upper nibble; defined in a real section at offset 0
            if (s["type"] >> 4) & 0xF != 2 or s["sec"] <= 0 or s["value"] != 0:
                continue
            if s["class"] not in (2, 3):          # EXTERNAL / STATIC
                continue
            sec = self.sections[s["sec"] - 1]
            if not sec["name"].startswith(".text"):
                continue
            data = self.data[sec["rawptr"]:sec["rawptr"] + sec["rawsz"]]
            relocs = [(off, rt, undecorate(nm), nm)
                      for off, rt, nm in self._section_relocs(s["sec"])]
            q = undecorate(s["name"])
            # a section can carry >1 symbol at offset 0 (aliases); first wins,
            # prefer EXTERNAL over STATIC
            if q in out and out[q].static and s["class"] == 3:
                continue
            labels = {t["name"]: t["value"] for t in self.symbols
                      if t is not None and t["sec"] == s["sec"]}
            out[q] = FnRec(q, s["name"], data, sorted(relocs), sec["name"],
                           s["class"] == 3, s["sec"], labels)
        return out


if __name__ == "__main__":                        # tiny smoke test
    import sys
    fns = ObjFile(sys.argv[1]).functions()
    print(f"{len(fns)} functions")
    for q, f in sorted(fns.items())[: int(sys.argv[2]) if len(sys.argv) > 2 else 10]:
        print(f"  {len(f.bytes):>6}B {len(f.relocs):>3}rel  {q}")
