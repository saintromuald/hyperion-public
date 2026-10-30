"""The saved game's records in 1.17's layout.

    python tools/save_layouts.py

A save holds 1.17's records as 1.17 laid them out, on both widths: the x64 build
translates each record between its own layout and 1.17's as it writes and reads it
(src117/port/port_save117.cpp). This writes the translation, src117/gen/save117.h,
from the compiler's types: the fields of each saved type on x86 and on x64, read
from the probe objects tools/wide_globals.py compiles (src117/build/wide/), paired
by name. A field is one operation:

  copy      bytes that lay out the same on both widths;
  pointer   a pointer, 4 bytes in the save: its low half written, sign-extended when
            read, since a saved pointer holds an id or -1 (the codecs' PreSave);
  signed,   an integer of pointer width, 4 bytes in the save, extended by its type;
  unsigned
  unsigned16  an unsigned integer of 2 bytes in 1.17 that the x64 build keeps in 4, a
            unit id: its low half written, zero-extended when read;
  unsigned8  an unsigned integer of 1 byte in 1.17 that the x64 build keeps in 2, an id of
            h/typelimits.h's: its low byte written, zero-extended
            when read;
  pos16s,   a map position, 2 bytes in 1.17 and a spos_t or upos_t of 4 on x64 (stage E,
  pos16u    h/pos.h): its low half written, extended by its sign when read;
  ptrpoint  a union of a pointer and a point of positions (CUnit's rally point and
            pick-up position): 4 bytes in 1.17, the pointer's low half or the point's two
            positions' low halves, by the arm the record uses (port_formats.cpp asks);
  ptrbytes  a union of a pointer and bytes whose fields x64 lays out apart (CUnit's +0xC8:
            a worker's saved target or a fighter's hangar link, else a building's tech and
            upgrade, 16 bits on x64, or a carrier's hangar counts): 4 bytes in
            1.17, the pointer's low half or the bytes' fields by a layout of their own, by
            the arm the record uses (port_formats.cpp asks);
  array     a run of elements of another layout.

Run it after wide_globals.py, whenever a saved type changes.
"""
import os
import struct
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, "tools"))
sys.path.insert(0, os.path.join(ENGINE, "tools", "wave117"))
import codeview  # noqa: E402

WIDE = os.path.join(ENGINE, "src117", "build", "wide")
OUT = os.path.join(ENGINE, "src117", "gen", "save117.h")

# The saved types: the enum's name, the type and the TU whose probe has it first.
SAVED = [
    ("CImage", "CImage", "CImage"),
    ("CSprite", "CSprite", "CSprite"),
    ("CThingy", "CThingy", "CThingy"),
    ("CFlingy", "CFlingy", "CFlingy"),
    ("CUnit", "CUnit", "CUnitSave"),
    ("CBullet", "CBullet", "CBullet"),
    ("COrder", "COrder", "COrder"),
    ("DecayingCreep", "decaying_creep_t", "tile2"),
    ("PathGlob", "ep_glob_t", "sai_PathCreate"),
    ("Contours", "contours_t", "sai_LoadSave"),
    # the AI's pools in 1.17's shape, which a save of 1.17's sizes holds whole (h/aipools.h,
    # TAiPool117): the storage on 64 bits holds more records
    ("SoldierPool", "TAiPool117<TSoldier,1000>", "TAI_Captain"),
    ("Captain", "TAI_Captain", "TAI_Captain"),
    ("GuardPool", "TAiPool117<TGuard,1000>", "TAI_GuardPost"),
    ("PlayerGuards", "TPlayerGuards", "TAI_GuardPost"),
    ("WorkerPool", "TAiPool117<TWorker,1000>", "SAI_Build"),
    ("BldgPool", "TAiPool117<TBldg,1000>", "SAI_Build"),
    ("TownPool", "TAiPool117<TTown,100>", "SAI_Build"),
    ("PlayerTowns", "TPlayerTowns", "SAI_Build"),
    ("Banker", "AI_Banker", "SAI_Banker"),
    ("ScriptPool", "TAiPool117<TScript,100>", "SAI_Scripts"),
    ("ScriptMgr", "TSaveContainer<TScript,100>", "SAI_Scripts"),
    ("TownPlan", "TTownPlan", "sai_TownBuilder"),
    # the AI's records alone, which a game past 1.17's pools saves as a run (h/aipools.h)
    ("Soldier", "TSoldier", "TAI_Captain"),
    ("Guard", "TGuard", "TAI_GuardPost"),
    ("Worker", "TWorker", "SAI_Build"),
    ("Bldg", "TBldg", "SAI_Build"),
    ("Town", "TTown", "SAI_Build"),
    ("Script", "TScript", "SAI_Scripts"),
]

(OP_COPY, OP_POINTER, OP_SIGNED, OP_UNSIGNED, OP_ARRAY, OP_UNSIGNED16, OP_POS16S, OP_POS16U, OP_PTRPOINT,
 OP_SIGNED16, OP_UNSIGNED8, OP_PTRBYTES, OP_OFFSET16) = range(13)
POSITIONS = ("spos_t", "upos_t")
TWO_BYTES = (OP_COPY, OP_UNSIGNED16, OP_POS16S, OP_POS16U, OP_SIGNED16, OP_UNSIGNED8, OP_OFFSET16)  # ops whose n is bytes in 1.17
# the image's offsets into its scripts, which a mod's pass 64 KiB (h/typelimits.h, iscript_off_t)
SCRIPT_OFFSETS = ("CImage.wIscriptHeader", "CImage.wIscriptOffset", "CImage.w14")


class Width:
    """One width's probe objects, loaded as they are needed."""

    def __init__(self, arch):
        self.dir = os.path.join(WIDE, arch)
        self.tables = {}

    def table(self, tu):
        if tu not in self.tables:
            self.tables[tu] = codeview.TypeTable(os.path.join(self.dir, "probe_%s.obj" % tu))
        return self.tables[tu]

    def find(self, name, tu):
        """-> (table, type index) of a complete type, from tu's probe or else any."""
        try:
            return self.table(tu), self.table(tu).by_name(name)
        except KeyError:
            pass
        for f in sorted(os.listdir(self.dir)):
            if f.startswith("probe_") and f.endswith(".obj"):
                t = self.table(f[len("probe_"):-len(".obj")])
                try:
                    return t, t.by_name(name)
                except KeyError:
                    continue
        raise SystemExit("[save_layouts] no probe has the type %s" % name)


class Gen:
    def __init__(self):
        self.layouts = []     # [name, size86, size64, ops]
        self.by_key = {}

    def fixed(self, tt, ti):
        """Whether a type lays out the same on both widths: no pointer and no
        integer of pointer width in it (the x64 table is what tells)."""
        return all(k != "p" for _o, _s, k, _p in tt.leaves(ti))

    def layout(self, name, a, b):
        """a, b: (table, type index) on x86 and x64 -> the layout's index."""
        key = (a[0].obj.path, a[1])
        if key in self.by_key:
            return self.by_key[key]
        idx = len(self.layouts)
        self.by_key[key] = idx
        self.layouts.append(None)
        ops = self.ops(name, a, b, 0, 0)
        self.layouts[idx] = [name, a[0].size(a[1]), b[0].size(b[1]), ops]
        return idx

    def ops(self, what, a, b, o86, o64):
        (t86, ti86), (t64, ti64) = a, b
        ti86, ti64 = t86._resolve(ti86), t64._resolve(ti64)
        s86, s64 = t86.size(ti86), t64.size(ti64)
        k86 = t86.recs[ti86][0] if ti86 >= 0x1000 else None
        # the same bytes on both widths
        if s86 == s64 and self.fixed(t64, ti64):
            return [[OP_COPY, o86, o64, s86, 0]] if s86 else []
        if k86 == codeview.LF_ARRAY:
            e86 = struct.unpack_from("<I", t86.recs[ti86][1], 0)[0]
            e64 = struct.unpack_from("<I", t64.recs[ti64][1], 0)[0]
            es86, es64 = t86.size(e86), t64.size(e64)
            n = s86 // es86
            # a table the x64 build sizes past 1.17's, for Hyperion's larger maps
            # (src117/h/maplimits.h), is saved as its first 1.17 elements
            if n > s64 // es64:
                raise SystemExit("[save_layouts] %s: %d elements on x86, %d on x64" % (what, n, s64 // es64))
            if es86 == es64 and self.fixed(t64, e64):
                return [[OP_COPY, o86, o64, s86, 0]]
            sub = self.layout("%s[]" % what, (t86, e86), (t64, e64))
            return [[OP_ARRAY, o86, o64, n, sub]]
        if k86 == codeview.LF_UNION and len(t86.leaves(ti86)) > 1:
            # a union of views of one record (codeview.leaves): its first view's fields
            m86, m64 = t86.members(ti86), t64.members(ti64)
            return self.ops("%s.%s" % (what, m86[0][2]), (t86, m86[0][1]), (t64, m64[0][1]), o86, o64)
        if k86 in (codeview.LF_STRUCTURE, codeview.LF_CLASS):
            m86, m64 = t86.members(ti86), t64.members(ti64)
            # padding only x64 has, named _x64_... (tools/wide_globals.py, pair_ops)
            m64 = [m for m in m64 if not m[2].startswith("_x64_")]
            if [n for *_x, n in m86] != [n for *_x, n in m64]:
                raise SystemExit("[save_layouts] %s: the members differ between the widths" % what)
            out, end86 = [], -1
            for i, ((mo86, mt86, name), (mo64, mt64, _n)) in enumerate(zip(m86, m64)):
                if mt86 is None:
                    raise SystemExit("[save_layouts] %s has a vftable" % what)
                if mo86 + o86 < end86:
                    continue      # a bit-field sharing the unit just done, or another arm
                # an anonymous union's arms are members at one offset (the compiler lists
                # them flat): a pointer there beside a position is a pointer or a point
                arms = [m for m in m64[i:] if m[0] == mo64]
                if (len(arms) > 1 and t64.leaves(mt64)[:1] and t64.leaves(mt64)[0][2] == "p"
                        and any(self.is_position(t64, m[1]) for m in arms)):
                    out.append([OP_PTRPOINT, o86 + mo86, o64 + mo64, 4, 0])
                    end86 = o86 + mo86 + 4
                    continue
                # a pointer there beside bytes whose fields x64 lays out apart: a pointer or
                # the bytes, their fields a layout of their own
                if len(arms) > 1 and t64.leaves(mt64)[:1] and t64.leaves(mt64)[0][2] == "p":
                    sub_ops = []
                    for (jo86, jt86, jname), (jo64, jt64, _jn) in zip(m86[i:], m64[i:]):
                        if jo86 >= mo86 + 4:
                            break
                        if jt86 is None or (t64.leaves(jt64)[:1] and t64.leaves(jt64)[0][2] == "p"):
                            continue
                        sub_ops += self.ops("%s.%s" % (what, jname), (t86, jt86), (t64, jt64),
                                            jo86 - mo86, jo64 - mo64)
                    if any(op[0] != OP_COPY or op[1] != op[2] for op in sub_ops):
                        sub = len(self.layouts)
                        self.layouts.append(["%s.%s|bytes" % (what, name), 4, t64.size(mt64), merge(sub_ops)])
                        out.append([OP_PTRBYTES, o86 + mo86, o64 + mo64, 4, sub])
                        end86 = o86 + mo86 + 4
                        continue
                sub = self.ops("%s.%s" % (what, name), (t86, mt86), (t64, mt64), o86 + mo86, o64 + mo64)
                out += sub
                if sub:
                    last = sub[-1]
                    end86 = last[1] + (last[3] if last[0] in TWO_BYTES else 4)
            return merge(out)
        # a map position (h/pos.h): a 16-bit integer on x86, a struct of one int on x64
        if s86 == 2 and s64 == 4 and ti64 >= 0x1000 and t64.name(ti64) in ("spos_t", "upos_t"):
            return [[OP_POS16S if t64.name(ti64) == "spos_t" else OP_POS16U, o86, o64, 2, 0]]
        # a scalar, a pointer or a union: one leaf
        l86, l64 = t86.leaves(ti86), t64.leaves(ti64)
        if len(l86) != 1 or len(l64) != 1:
            raise SystemExit("[save_layouts] %s: %d leaves on x86, %d on x64" % (what, len(l86), len(l64)))
        kind = l64[0][2]
        if kind == "p" or l86[0][2] == "p":
            return [[OP_POINTER, o86, o64, 4, 0]]
        if (s86, s64) == (4, 8) and kind in "iu":
            return [[OP_SIGNED if kind == "i" else OP_UNSIGNED, o86, o64, 4, 0]]
        if (s86, s64) == (2, 4) and kind == "u" and l86[0][2] == "u" and what in SCRIPT_OFFSETS:
            return [[OP_OFFSET16, o86, o64, 2, 0]]   # a mod's script offsets
        if (s86, s64) == (2, 4) and kind == "u" and l86[0][2] == "u":
            return [[OP_UNSIGNED16, o86, o64, 2, 0]]
        if (s86, s64) == (1, 2) and kind == "u" and l86[0][2] == "u":
            return [[OP_UNSIGNED8, o86, o64, 1, 0]]  # an id of h/typelimits.h's
        if (s86, s64) == (2, 4) and kind == "i" and l86[0][2] == "i":
            return [[OP_SIGNED16, o86, o64, 2, 0]]   # a count (contour_count_t)
        raise SystemExit("[save_layouts] %s: a %s field of %d bytes becomes %d" % (what, kind, s86, s64))


def is_position(t, ti):
    """Whether a type is one of h/pos.h's positions."""
    rt = t._resolve(ti)
    return rt >= 0x1000 and t.recs[rt][0] in (codeview.LF_STRUCTURE, codeview.LF_CLASS) and t.name(rt) in POSITIONS


Gen.is_position = staticmethod(is_position)


def merge(ops):
    out = []
    for op in ops:
        last = out[-1] if out else None
        if (last and op[0] == OP_COPY and last[0] == OP_COPY
                and last[1] + last[3] == op[1] and last[2] + last[3] == op[2]):
            last[3] += op[3]
        else:
            out.append(list(op))
    return out


def main():
    w86, w64 = Width("x86"), Width("amd64")
    gen = Gen()
    top = []
    for enum, name, tu in SAVED:
        a, b = w86.find(name, tu), w64.find(name, tu)
        idx = gen.layout(name, a, b)
        top.append((enum, idx))
        # a saved type is copied whole on x86: its x86 layout must be 1.17's size
        print("[save_layouts] %-14s %-28s %6d bytes in 1.17, %6d on x64, %d ops"
              % (enum, name, gen.layouts[idx][1], gen.layouts[idx][2], len(gen.layouts[idx][3])))
    ops, rows = [], []
    for name, s86, s64, lops in gen.layouts:
        rows.append("  {0x%Xu, 0x%Xu, %du, %du},  // %s" % (s86, s64, len(ops), len(lops), name))
        ops += lops
    out = ["// save117.h -- generated by tools/save_layouts.py from the compiler's types; do not edit.",
           "//",
           "// The saved game's records in 1.17's layout, and on x64 how each field of ours",
           "// maps onto it (port_save117.cpp). On x86 every",
           "// layout is 1.17's own, so each is one copy.",
           "#pragma once",
           "",
           "enum {"]
    out += ["  SAVE117_%s = %d," % (e, i) for e, i in top]
    out += ["};",
            "enum { SAVE117_COPY = %d, SAVE117_POINTER = %d, SAVE117_SIGNED = %d, SAVE117_UNSIGNED = %d, "
            "SAVE117_ARRAY = %d, SAVE117_UNSIGNED16 = %d, SAVE117_POS16S = %d, SAVE117_POS16U = %d, "
            "SAVE117_PTRPOINT = %d, SAVE117_SIGNED16 = %d, SAVE117_UNSIGNED8 = %d, SAVE117_PTRBYTES = %d, "
            "SAVE117_OFFSET16 = %d };"
            % (OP_COPY, OP_POINTER, OP_SIGNED, OP_UNSIGNED, OP_ARRAY, OP_UNSIGNED16, OP_POS16S, OP_POS16U,
               OP_PTRPOINT, OP_SIGNED16, OP_UNSIGNED8, OP_PTRBYTES, OP_OFFSET16),
            "struct Save117Op {",
            "  unsigned int off86, off;   // in 1.17's record and in ours",
            "  unsigned int n;            // bytes to copy, or an array's elements",
            "  unsigned int kind, sub;    // an array's element layout, a ptrbytes' bytes' layout",
            "};",
            "struct Save117Layout {",
            "  unsigned int size86, size; // 1.17's and ours",
            "  unsigned int firstOp, opCount;",
            "};",
            "",
            "// The tables, for port_formats.cpp alone.",
            "#ifdef SAVE117_TABLES",
            "#if defined(_WIN64) || defined(__LP64__)",
            "static constexpr Save117Op kSave117Ops[] = {"]
    out += ["  {0x%Xu, 0x%Xu, 0x%Xu, %d, %d}," % (o86, o64, n, k, sub) for k, o86, o64, n, sub in ops]
    out += ["};", "static constexpr Save117Layout kSave117Layouts[] = {"]
    out += rows
    out += ["};", "#else", "static constexpr Save117Op kSave117Ops[] = {"]
    out += ["  {0u, 0u, 0x%Xu, %d, 0}," % (s86, OP_COPY) for _n, s86, _s, _o in gen.layouts]
    out += ["};", "static constexpr Save117Layout kSave117Layouts[] = {"]
    out += ["  {0x%Xu, 0x%Xu, %du, 1u},  // %s" % (s86, s86, i, n) for i, (n, s86, _s, _o) in enumerate(gen.layouts)]
    out += ["};", "#endif", "#endif  // SAVE117_TABLES"]
    with open(OUT, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(out) + "\n")
    print("[save_layouts] %d layouts, %d ops on x64 -> %s" % (len(gen.layouts), len(ops), OUT))


if __name__ == "__main__":
    main()
