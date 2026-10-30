#!/usr/bin/env python3
"""Write the absolute-symbol object that binds game globals for the payload link.

The payload compiles every ported TU in the proof form, where a game global is
`extern T g;`, a named symbol with a DIR32 fixup. This tool reads the payload's
objects, collects every undefined data external that no payload object defines,
and writes one COFF object with a public absolute symbol (section number -1,
IMAGE_SYM_ABSOLUTE) per decorated spelling, valued at the global's oracle VA
from re/globals117.json. The linker resolves each DIR32 to VA plus addend and
emits no base relocation for it, so the DLL reads the oracle's own storage
wherever it loads (decided 2026-09-24, docs/open-decisions.md).

Hyperion: the engine keeps 1.17's data as a block of its own instead
(src117/h/oc_data.h). The object has one
uninitialized section the size of 1.17's .rdata and .data, and each symbol is
defined at its VA's offset in it, so the DLL's reads are relocated like any of
its own data, and the engine fills the block from the player's exe as it starts.
A VA outside that range is refused.

A name is bound by its registry entry: the entry's "name", or one of its
"aliases". A name the registry does not hold is a refusal, listed by object,
and nothing is written. So is a payload object that defines storage under a
game global's name (a registry name, or a name a shipped-form arm spells as a
literal): a file-static proof arm compiled into the DLL would give it a
private copy the oracle's code never sees. The link's unresolved set is the registration worklist.
Hyperion: with --x64 the objects are AMD64: a C
name has no leading underscore, the block's object is an AMD64 one, and the
vftables are left to the compiler, whose tables hold 64-bit pointers; the
block's copies of 1.17's hold 32-bit ones.
A name that must keep its literal address in the payload (registry "payload":
"literal", a src117 arm opened `#if defined(OC_DLL_MODE) ||
defined(OC_PAYLOAD)`, or the #else of an `#if !defined(OC_DLL_MODE) &&
!defined(OC_PAYLOAD)` block) is also a refusal when an object whose own source
(or an src117/h header) keeps it literal still references it as an extern: the
payload compiled the proof arm where the source asks for the literal. Another
TU keeping the same name literal does not refuse this one; both reach one VA.
CRT and linker names the payload gets from LIBCMT are left to the link, and so
are the port's own data, defined in the objects --exclude names.
Hyperion: the objects --bind-only names are the port's own TUs, which name
game globals as externs too. Their registry
names are bound like any other; a name the registry does not hold is theirs or
the link's, and they are left out of the literal-arm, CRT-datum and vftable
checks, which are about the lang TUs' proof forms.
One CRT datum is the oracle's instead, by the user's decision of
2026-09-24: `__isa_available`, which the compiler's vectorised loops read.
LIBCMT's cpu_disp.obj defines it and is always linked, so an absolute
definition would collide. The build rewrites each object's undefined
reference to a same-length private name in place and binds that name to the
registry's VA for `__isa_available`.
A registered vftable (a registry name ending in ::`vftable') is bound the
same way, by the user's decision of 2026-09-25: every code reference to our
`??_7` COMDAT is repointed at an appended external `__oc_vft_<VA>` and bound
to the oracle's table (retarget_vftables), so objects keep the oracle's vftable
whichever side constructed them. A code reference to a vftable the registry
does not bind is a refusal.

Usage:
  python tools/wave117/abs_globals.py --objdir DIR --out FILE.obj [--exclude a.obj,b.obj]
                                       [--bind-only c.obj,d.obj]
  python tools/wave117/abs_globals.py --objdir DIR --check     (report only)
Exit 0 written or clean, 2 refusal.
"""
import argparse
import bisect
import glob
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import coff                                                    # noqa: E402

REGISTRY = os.path.join(ROOT, "re", "globals117.json")

# names the link resolves from the CRT or the linker itself
LINK_OWNED = {"__fltused", "___isa_available", "__isa_available",
              "??_7type_info@@6B@", "___security_cookie", "__tls_index",
              "__tls_array", "___ImageBase", "__except_list"}

# CRT data the game objects read at the oracle's address: the decorated name,
# the same-length name each object's reference is rewritten to, and the
# registry name that gives the VA.
ORACLE_CRT_DATA = {"___isa_available": ("___oc_isa_oracle", "__isa_available")}

# Hyperion: the same names as an AMD64 link spells them (set_x64)
LINK_OWNED_X64 = {"_fltused", "__isa_available", "??_7type_info@@6B@",
                  "__security_cookie", "_tls_index", "__ImageBase",
                  "_Init_thread_epoch"}  # VS 2015 on: thread-safe static locals
ORACLE_CRT_DATA_X64 = {"__isa_available": ("__oc_isa_oracle", "__isa_available")}
X64 = False


def set_x64():
    """Switch the module to AMD64 objects."""
    global X64, LINK_OWNED, ORACLE_CRT_DATA, BLOCK_NAME, WIDE_NAME
    X64 = True
    LINK_OWNED = LINK_OWNED_X64
    ORACLE_CRT_DATA = ORACLE_CRT_DATA_X64
    BLOCK_NAME = "g_data117"
    WIDE_NAME = "g_wide117"


def undecorate(dec):
    """coff.undecorate, but an AMD64 C name is its own: it has no underscore to take off."""
    if X64 and not dec.startswith("?"):
        return dec
    return coff.undecorate(dec)


def registry_names(path=REGISTRY):
    """{name: VA} over every registry entry's name and aliases."""
    g = json.load(open(path, encoding="utf-8"))["globals"]
    out, clash = {}, []
    for k, v in g.items():
        va = int(k, 16)
        for n in [v.get("name")] + list(v.get("aliases") or ()):
            if not n:
                continue
            if n in out and out[n] != va:
                clash.append((n, out[n], va))
            out.setdefault(n, va)
    return out, clash


def registry_kept(path=REGISTRY):
    """Names (and aliases) of registry entries flagged "payload": "literal"."""
    g = json.load(open(path, encoding="utf-8"))["globals"]
    out = set()
    for v in g.values():
        if v.get("payload") == "literal":
            out.update(n for n in [v.get("name")] + list(v.get("aliases") or ()) if n)
    return out


DLL_OPEN = "#ifdef OC_DLL_MODE"
KEPT_OPEN = "#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)"
SPLIT_OPEN = "#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)"
_PP = re.compile(r"\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b")
_ARM_DEF = re.compile(r"#define\s+(\w+)\s+\(.*?0x[0-9A-Fa-f]{5,8}u?\b")


def pp_blocks(text):
    """Every #if block with its own #else and #endif, nesting respected
    -> [(opener line, first arm, second arm)]. A regex that stops at the next
    `#endif` stops inside an arm's own `#ifndef NAME` guard, and one that runs
    to the next `#else` reads across a block that has none."""
    lines = text.split("\n")
    stack, out = [], []
    for i, ln in enumerate(lines):
        m = _PP.match(ln)
        if not m:
            continue
        k = m.group(1)
        if k in ("if", "ifdef", "ifndef"):
            stack.append([ln.strip(), i, None])
        elif k in ("else", "elif") and stack and stack[-1][2] is None:
            stack[-1][2] = i
        elif k == "endif" and stack:
            op, s, e = stack.pop()
            first = lines[s + 1:i if e is None else e]
            second = [] if e is None else lines[e + 1:i]
            out.append((op, "\n".join(first), "\n".join(second)))
    return out


def literal_arms(text, kept_only=False):
    """The arm texts of one file that spell literal addresses: the first arm
    under `#ifdef OC_DLL_MODE` or the kept opener, and the #else of a
    split-shape block. With kept_only, only the arms the payload compiles."""
    for op, first, second in pp_blocks(text):
        if op.startswith(DLL_OPEN) and not kept_only:
            yield first
        elif op.startswith(KEPT_OPEN):
            yield first
        elif op.startswith(SPLIT_OPEN):
            yield second


def arm_names(kept_only=False):
    """Every name a shipped-form arm in src117 spells as a literal address;
    with kept_only, only the arms that also apply to the payload form."""
    out = set()
    for f in (glob.glob(os.path.join(ROOT, "src117", "lang", "*.cpp"))
              + glob.glob(os.path.join(ROOT, "src117", "h", "**", "*.h*"),
                          recursive=True)):
        s = open(f, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
        for arm in literal_arms(s, kept_only):
            out.update(m.group(1) for m in _ARM_DEF.finditer(arm))
    return out


def file_kept(path):
    """The names one source file keeps literal in the payload."""
    try:
        s = open(path, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
    except OSError:
        return set()
    return {m.group(1) for arm in literal_arms(s, kept_only=True)
            for m in _ARM_DEF.finditer(arm)}


def kept_names(tu=None):
    """The names the payload keeps literal for one TU: the registry's
    "payload": "literal" entries, every kept arm in src117/h (a header
    applies to each TU that includes it), and the TU's own kept arms. Another
    TU keeping a name literal says nothing about this one: each spelling binds
    to the same VA."""
    out = registry_kept()
    for f in glob.glob(os.path.join(ROOT, "src117", "h", "**", "*.h*"), recursive=True):
        out |= file_kept(f)
    if tu:
        out |= file_kept(tu if os.path.isabs(tu) else os.path.join(ROOT, tu))
    return out


def owned_storage(objs, game_names):
    """Data a payload object defines under a game global's name. The oracle's
    code keeps using the real one, so such a copy is a silent split."""
    bad = []
    for o in objs:
        for s in coff.ObjFile(o).symbols:
            if (s is None or s["class"] not in (2, 3) or s["sec"] <= 0
                    or ((s["type"] >> 4) & 0xF) == 2
                    or s["name"].startswith("??_7")):
                # a vftable COMDAT is left to retarget_vftables: once no code
                # references it, the link drops it
                continue
            und = undecorate(s["name"])
            if und in game_names:
                bad.append((os.path.basename(o), und, s["name"]))
    return bad


def is_data_external(s):
    return (s is not None and s["class"] == 2 and s["sec"] == 0
            and s["value"] == 0 and ((s["type"] >> 4) & 0xF) != 2)


def collect(objs):
    """-> (defined decorated names, {undefined decorated name: [objects]})."""
    defined, undef = set(), {}
    for o in objs:
        of = coff.ObjFile(o)
        for s in of.symbols:
            if s is None or s["class"] != 2:
                continue
            if s["sec"] > 0 or (s["sec"] == 0 and s["value"] != 0):
                defined.add(s["name"])
            elif is_data_external(s):
                undef.setdefault(s["name"], []).append(os.path.basename(o))
    return defined, undef


def bind(undef, defined, names, kept=frozenset()):
    """-> ([(decorated, VA)], [(decorated, undecorated, objects)] refused,
    [(decorated, undecorated, objects)] kept-literal names left extern).
    `kept` is one set for every object, or a function object -> set, so a
    name is refused only in an object whose own source keeps it literal."""
    kept_for = kept if callable(kept) else (lambda _o: kept)
    bound, refused, kept_ext = [], [], []
    for dec in sorted(undef):
        if dec in defined or dec in LINK_OWNED or dec.startswith("__imp_"):
            continue
        if dec in (BLOCK_NAME, WIDE_NAME):  # Hyperion: write_obj defines them
            continue
        und = undecorate(dec)
        who = sorted({o for o in undef[dec] if und in kept_for(o)})
        if who:
            kept_ext.append((dec, und, who))
            continue
        va = names.get(und)
        if va is None:
            refused.append((dec, und, sorted(set(undef[dec]))))
        else:
            bound.append((dec, va))
    return bound, refused, kept_ext


def rename_refs(objs, table=None):
    """Rewrite each object's UNDEFINED external named by `table` to its private
    name, in place in the string table. A definition is never touched.
    -> {private name: [objects]}."""
    table = ORACLE_CRT_DATA if table is None else table
    done = {}
    for o in objs:
        d = bytearray(open(o, "rb").read())
        _m, _n, _t, symptr, nsym, _o, _c = struct.unpack_from("<HHIIIHH", d, 0)
        strtab = symptr + nsym * 18
        changed = False
        i = 0
        while i < nsym:
            e = symptr + i * 18
            naux = d[e + 17]
            if d[e:e + 4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", d, e + 4)[0]
                end = d.index(b"\0", strtab + off)
                name = bytes(d[strtab + off:end]).decode("ascii", "replace")
                value, sec = struct.unpack_from("<Ih", d, e + 8)
                if name in table and sec == 0 and value == 0 and d[e + 16] == 2:
                    new = table[name][0].encode("ascii")
                    assert len(new) == len(name), name
                    d[strtab + off:end] = new
                    done.setdefault(table[name][0], []).append(os.path.basename(o))
                    changed = True
            i += 1 + naux
        if changed:
            open(o, "wb").write(bytes(d))
    return done


VFT_PRIVATE = "__oc_vft_%06X"


def vftable_names(names):
    """{undecorated vftable name: VA} for the registry's vftable entries."""
    return {n: va for n, va in names.items() if n.endswith("::`vftable'")}


def retarget_vftables(objs, vft, write=True):
    """Point every code reference to a registered vftable at the oracle's.

    MSVC emits a class's vftable as a COMDAT in each object whose code
    constructs or destroys one, and the vptr store is a DIR32 to that `??_7`
    symbol. The user's decision of 2026-09-25 lets constructors and
    destructors be hooked only if the objects keep the oracle's vftable
    whichever side built them. So each such relocation in a code section is
    repointed at an undefined external appended to the object's symbol table,
    `__oc_vft_<VA>`, which the absolute-symbol object binds to the registry
    VA. Nothing then references our COMDAT, and /OPT:REF drops it together
    with any virtual body only it reached.

    Refused, and nothing is written for that object: a /bigobj or non-i386
    object, a vftable referenced from a data section (anything other than code
    and .debug), a code reference that is not DIR32, a code reference to a
    vftable the registry does not bind, and an object whose symbol and string
    tables are not the last thing in the file.
    -> ({private name: [objects]}, [(object, reason)] refused)."""
    done, refused = {}, []
    for o in objs:
        base = os.path.basename(o)
        d = bytearray(open(o, "rb").read())
        machine, nsec, _t, symptr, nsym, optsz, _c = struct.unpack_from("<HHIIIHH", d, 0)
        if machine == 0 and nsec == 0xFFFF:
            if b"??_7" in d:
                refused.append((base, "a /bigobj object; its 20-byte symbol "
                                      "records are not rewritten here"))
            continue
        if machine != 0x14C:
            refused.append((base, "not an i386 COFF object (machine 0x%X)" % machine))
            continue
        strtab = symptr + nsym * 18

        def name_at(e):
            if d[e:e + 4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", d, e + 4)[0]
                end = d.index(b"\0", strtab + off)
                return bytes(d[strtab + off:end]).decode("ascii", "replace")
            return bytes(d[e:e + 8]).rstrip(b"\0").decode("ascii", "replace")

        vidx, i = {}, 0
        while i < nsym:
            e = symptr + i * 18
            if d[e + 16] == 2:
                n = name_at(e)
                if n.startswith("??_7") and n not in LINK_OWNED:
                    vidx[i] = n
            i += 1 + d[e + 17]
        if not vidx:
            continue
        secs, hits, why = [], {}, []
        for s in range(nsec):
            h = 20 + optsz + s * 40
            sname = name_at(h) if d[h] != ord("/") else bytes(d[h:h + 8]).rstrip(b"\0").decode()
            rawsz, rawptr, relptr, lnptr, nrel, nln, chars = struct.unpack_from(
                "<IIIIHHI", d, h + 16)
            secs.append((rawptr + rawsz if rawptr else 0,
                         relptr + nrel * 10 if nrel else 0,
                         lnptr + nln * 6 if nln else 0))
            if chars & 0x01000000 and nrel:             # IMAGE_SCN_LNK_NRELOC_OVFL
                why.append("%s: a relocation count overflow" % sname)
                continue
            for k in range(nrel):
                r = relptr + k * 10
                sym = struct.unpack_from("<I", d, r + 4)[0]
                if sym not in vidx:
                    continue
                rtype = struct.unpack_from("<H", d, r + 8)[0]
                if chars & 0x20:                                # IMAGE_SCN_CNT_CODE
                    if rtype != 6:
                        why.append("%s: a type-0x%X relocation to %s in code"
                                   % (sname, rtype, vidx[sym]))
                    hits.setdefault(sym, []).append(r)
                elif not sname.startswith(".debug"):
                    why.append("%s references %s from a data section"
                               % (sname, vidx[sym]))
        if any(max(t) > symptr for t in secs):
            why.append("section data follows the symbol table")
        if strtab + struct.unpack_from("<I", d, strtab)[0] != len(d):
            why.append("the string table is not the end of the file")
        vas = {}
        for sym in hits:
            va = vft.get(undecorate(vidx[sym]))
            if va is None:
                why.append("code stores %s, which re/globals117.json does not "
                           "bind, so the payload would ship our own vftable"
                           % undecorate(vidx[sym]))
            vas[sym] = va
        if why:
            refused.extend((base, w) for w in why)
            continue
        if not hits:
            continue
        new_idx, ents, strs = {}, b"", b""
        strsize = struct.unpack_from("<I", d, strtab)[0]
        for va in sorted(set(vas.values())):
            nb = (VFT_PRIVATE % va).encode("ascii")
            new_idx[va] = nsym + len(new_idx)
            # a long name: offset into the string table, undefined external data
            ents += (b"\0\0\0\0" + struct.pack("<I", strsize + len(strs))
                     + struct.pack("<IhHBB", 0, 0, 0, 2, 0))
            strs += nb + b"\0"
            done.setdefault(VFT_PRIVATE % va, []).append(base)
        for sym, rels in hits.items():
            for r in rels:
                struct.pack_into("<I", d, r + 4, new_idx[vas[sym]])
        out = (d[:strtab] + ents + struct.pack("<I", strsize + len(strs))
               + d[strtab + 4:] + strs)
        struct.pack_into("<I", out, 12, nsym + len(new_idx))
        if write:
            open(o, "wb").write(bytes(out))
    return done, refused


# Hyperion: the engine keeps 1.17's .rdata and .data as a block of its own
# (src117/h/oc_data.h), so each global is defined at its
# offset in one uninitialized section, BLOCK_SECTION, instead of as an absolute
# symbol at 1.17's address. BLOCK_NAME names the block's start.
BLOCK_LO, BLOCK_HI = 0x4EB000, 0x6DF000
BLOCK_SECTION = b".oc117"
BLOCK_NAME = "_g_data117"
# uninitialized data, 4096-byte aligned, read and write
BLOCK_CHARS = 0x00000080 | 0x00D00000 | 0x40000000 | 0x80000000
# Hyperion: the globals whose layout changes at 64 bits have storage of their
# own, laid out for each width (src117/gen/wide117.json),
# in a second section that WIDE_NAME starts. A name
# bound to one of them is defined there; a name inside one is a refusal.
WIDE_SECTION = b".ocwide"
WIDE_NAME = "_g_wide117"
# uninitialized data, 16-byte aligned, read and write
WIDE_CHARS = 0x00000080 | 0x00500000 | 0x40000000 | 0x80000000


def load_wide(path):
    """-> (size, {VA: (offset, size, size in 1.17)}) for this width."""
    d = json.load(open(path, encoding="utf-8"))["x64" if X64 else "x86"]
    return d["size"], {int(k, 16): tuple(v) for k, v in d["globals"].items()}


def place_wide(syms, wide):
    """Split the bound names into the block's and the wide storage's:
    -> ([(name, VA)], [(name, offset)], [(name, VA, start)] inside one)."""
    starts = sorted(wide)
    block, own, inside = [], [], []
    for name, va in syms:
        if va in wide:
            own.append((name, wide[va][0]))
            continue
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and va < starts[i] + wide[starts[i]][2]:
            inside.append((name, va, starts[i]))
        else:
            block.append((name, va))
    return block, own, inside


def outside_block(syms):
    """The bound names whose address is not in 1.17's data."""
    return [(n, va) for n, va in syms if not BLOCK_LO <= va < BLOCK_HI]


def write_obj_fixed(path, syms):
    """One i386 COFF object: no sections, one absolute external per symbol."""
    strtab, ents = b"", b""
    for name, va in syms:
        nb = name.encode("ascii")
        if len(nb) <= 8:
            raw = nb.ljust(8, b"\0")
        else:
            raw = b"\0\0\0\0" + struct.pack("<I", 4 + len(strtab))
            strtab += nb + b"\0"
        # value, section -1 (absolute), type 0, class 2 (external), no aux
        ents += raw + struct.pack("<IhHBB", va, -1, 0, 2, 0)
    hdr = struct.pack("<HHIIIHH", 0x14C, 0, 0, 20, len(syms), 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + ents + struct.pack("<I", 4 + len(strtab)) + strtab)


def write_obj(path, syms, own=(), wide_size=0):
    """One i386 COFF object: the block's section, with one external per symbol at
    its address's offset in it, and BLOCK_NAME at its start; with wide_size, the
    wide storage's section too, with each of `own` at its offset and WIDE_NAME at
    its start."""
    strtab, ents = b"", b""

    def name_field(nb):
        nonlocal strtab
        if len(nb) <= 8:
            return nb.ljust(8, b"\0")
        raw = b"\0\0\0\0" + struct.pack("<I", 4 + len(strtab))
        strtab += nb + b"\0"
        return raw

    size = BLOCK_HI - BLOCK_LO
    # the section's own symbol, static, with its aux record (length, no relocations)
    ents += (BLOCK_SECTION.ljust(8, b"\0") + struct.pack("<IhHBB", 0, 1, 0, 3, 1)
             + struct.pack("<IHHIHBBH", size, 0, 0, 0, 0, 0, 0, 0))
    for name, va in [(BLOCK_NAME, BLOCK_LO)] + list(syms):
        # value, section 1, type 0, class 2 (external), no aux
        ents += name_field(name.encode("ascii")) + struct.pack("<IhHBB", va - BLOCK_LO, 1, 0, 2, 0)
    nsym = 2 + len(syms) + 1
    nsec = 1
    sec = (BLOCK_SECTION.ljust(8, b"\0")
           + struct.pack("<IIIIIIHHI", 0, 0, size, 0, 0, 0, 0, 0, BLOCK_CHARS))
    if wide_size:
        nsec = 2
        ents += (WIDE_SECTION.ljust(8, b"\0") + struct.pack("<IhHBB", 0, 2, 0, 3, 1)
                 + struct.pack("<IHHIHBBH", wide_size, 0, 0, 0, 0, 0, 0, 0))
        for name, off in [(WIDE_NAME, 0)] + list(own):
            ents += name_field(name.encode("ascii")) + struct.pack("<IhHBB", off, 2, 0, 2, 0)
        nsym += 2 + len(own) + 1
        sec += (WIDE_SECTION.ljust(8, b"\0")
                + struct.pack("<IIIIIIHHI", 0, 0, wide_size, 0, 0, 0, 0, 0, WIDE_CHARS))
    symptr = 20 + 40 * nsec
    hdr = struct.pack("<HHIIIHH", 0x8664 if X64 else 0x14C, nsec, 0, symptr, nsym, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + sec + ents + struct.pack("<I", 4 + len(strtab)) + strtab)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--objdir", required=True)
    ap.add_argument("--out")
    ap.add_argument("--exclude", default="")
    ap.add_argument("--bind-only", default="")
    ap.add_argument("--check", action="store_true")
    # Hyperion: the absolute symbols at 1.17's addresses, for a build that still
    # runs the original's code (the 32-bit builds, gone)
    ap.add_argument("--fixed", action="store_true")
    # Hyperion: AMD64 objects
    ap.add_argument("--x64", action="store_true")
    # Hyperion: the wide storage's layout
    ap.add_argument("--wide")
    a = ap.parse_args(argv)
    if a.x64:
        if a.fixed:
            print("abs_globals: --fixed has no x64 form: an AMD64 image cannot reach 1.17's addresses")
            return 2
        set_x64()
    skip = {x.strip().lower() for x in a.exclude.split(",") if x.strip()}
    only = {x.strip().lower() for x in a.bind_only.split(",") if x.strip()}
    out_base = os.path.basename(a.out).lower() if a.out else None
    every = [o for o in sorted(glob.glob(os.path.join(a.objdir, "*.obj")))
             if os.path.basename(o).lower() not in skip
             and os.path.basename(o).lower() != out_base]
    missing = only - {os.path.basename(o).lower() for o in every}
    if missing:
        print("abs_globals: no --bind-only object %s" % ", ".join(sorted(missing)))
        return 2
    objs = [o for o in every if os.path.basename(o).lower() not in only]
    port = [o for o in every if os.path.basename(o).lower() in only]
    names, clash = registry_names()
    for n, v1, v2 in clash:
        print("abs_globals: registry name %s at both 0x%X and 0x%X" % (n, v1, v2))
    if clash:
        return 2
    renamed = {} if a.check else rename_refs(objs)
    for priv, who in sorted(renamed.items()):
        reg = next(v[1] for v in ORACLE_CRT_DATA.values() if v[0] == priv)
        if reg not in names:
            print("abs_globals: %s is renamed but the registry has no %s"
                  % (priv, reg))
            return 2
        names[undecorate(priv)] = names[reg]
        print("  ORACLE-CRT %-40s -> 0x%X  (%s)" % (priv, names[reg], ", ".join(who)))
    vft = vftable_names(names)
    retargeted, vft_refused = ({}, []) if X64 else retarget_vftables(objs, vft, write=not a.check)
    for priv, who in sorted(retargeted.items()):
        va = int(priv.rsplit("_", 1)[1], 16)
        names[undecorate(priv)] = va
        print("  VFTABLE %-43s -> 0x%X  (%s)" % (priv, va, ", ".join(who)))
    for o, why in vft_refused:
        print("  VFTABLE-REFUSED %s: %s" % (o, why))
    if vft_refused:
        return 2
    defined, undef = collect(objs + port)
    # A datum the port owns and a ported TU reads, such as the view
    # (src117/port/render/port_view.cpp), is defined in one of the excluded objects:
    # it is the link's to resolve, not a game global.
    for o in sorted(glob.glob(os.path.join(a.objdir, "*.obj"))):
        if os.path.basename(o).lower() in skip:
            defined |= collect([o])[0]
    owned = owned_storage(objs + port, set(names) | arm_names())
    for o, und, dec in owned:
        print("  DLL-OWNED %-40s %s  (%s)" % (und, dec, o))
    shared = kept_names()
    per_obj = {}

    def kept_for(obj):
        if obj.lower() in only:
            return frozenset()
        if obj not in per_obj:
            src = os.path.join(ROOT, "src117", "lang",
                               os.path.splitext(obj)[0] + ".cpp")
            per_obj[obj] = shared | file_kept(src)
        return per_obj[obj]

    bound, refused, kept_ext = bind(undef, defined, names, kept_for)
    refused = [r for r in refused if not all(o.lower() in only for o in r[2])]
    print("abs_globals: %d objects (%d bind-only), %d data externals bound, %d refused"
          % (len(objs) + len(port), len(port), len(bound), len(refused) + len(kept_ext)))
    for dec, und, who in kept_ext:
        print("  KEPT-LITERAL but compiled as extern %-24s %s  (%s)"
              % (und, dec, ", ".join(who[:4]) + (" ..." if len(who) > 4 else "")))
    for dec, und, who in refused:
        print("  UNREGISTERED %-40s %s  (%s)" % (und, dec, ", ".join(who[:4])
                                                 + (" ..." if len(who) > 4 else "")))
    far = [] if a.fixed else outside_block(bound)
    for dec, va in far:
        print("  OUTSIDE-DATA %-40s 0x%X is not in 1.17's .rdata or .data" % (dec, va))
    wide_size, own, inside = 0, [], []
    if a.wide and not a.fixed:
        wide_size, wide = load_wide(a.wide)
        bound, own, inside = place_wide(bound, wide)
        print("abs_globals: %d names in the wide storage (%d bytes)" % (len(own), wide_size))
    for dec, va, start in inside:
        print("  INSIDE-WIDE %-40s 0x%X is inside the wide global at 0x%X" % (dec, va, start))
    if refused or owned or kept_ext or far or inside:
        return 2
    if a.out and not a.check:
        if a.fixed:
            write_obj_fixed(a.out, bound)
        else:
            write_obj(a.out, bound, own, wide_size)
        print("abs_globals: wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
