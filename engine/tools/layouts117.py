"""1.17's dialog layouts as Hyperion's source (milestone B).

    python tools/layouts117.py                write src117/gen/layouts117.cpp
    python tools/layouts117.py --check        say whether the source still makes 1.17's files
    python tools/layouts117.py --check-dump <folder>
                                              compare the rez#*.bin a run wrote with
                                              HYPERION_REMASTERED_DUMP against 1.17's

The game builds every dialog from a layout file, rez\\<name>.bin, which 1.17's MPQs have and a
StarCraft: Remastered install does not (its SD\\rez\\*.ui.json are re-edited and cannot be
turned back). The user, 2026-10-20, on putting 1.17's layouts in source: "why would that be
blizzard data be an issue, is it no different than devilution for example? or any other
decomp done" (PRD). So this script reads them once from 1.17's MPQs, as data117_source.py read
the exe, and writes them as a table: per dialog its controls in the order the game walks them,
each with its id, type, rect, size, flags, response rect and text, and an animated button's
pictures. port_layouts.cpp makes the file back from the table, byte for byte; --check proves
that for every layout here, the way the C++ lays it out.

A .bin is 0x56-byte records in walk order from 0 (the dialog, then its controls along their
next links), the animated buttons' 0x1E-byte items after them in the controls' order, then the
strings: each control's text and its items' file names, in walk order, each ended by a zero.
Every other field of a record is zero in all of 1.17's layouts, which the script checks. One
file ends with bytes nothing points at (snd_dlg.bin's "ns" and its zero), kept as its tail.
"""
import argparse
import glob
import os
import struct
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ENGINE), "tools", "assets"))
OUT = os.path.join(ENGINE, "src117", "gen", "layouts117.cpp")
MPQ = os.path.join(ENGINE, "testrun", "front_w1")

# The layouts 1.17 loads: by name in its code and data, the race's command cards and chat lines
# by a letter, and the load and character screens'.
NAMES = """abrtmenu allyfltr credits dlgFatal gamemenu gluChat gluCmpgn gluConn gluCreat gluCustm
gluDelCh gluExpCmpgn gluGameMode gluHist gluJoin gluLoad gluLogin gluMain gluModem gluModemEntry
gluModemList gluModemStatus gluNewCh gluPEdit gluPOk gluPOkCancel gluPOkSplit glurdyp glurdyt
glurdyz gluScore help helpmenu lmission loadgame minimap minimappreview msgfltr netdlg objctdlg ok
okcancel options quit quit2mnu quitrepl restart savegame snd_dlg spd_dlg stat_f10 statbtnn
statbtnp statbtnt statbtnz statdata statfluf statlb statport statres teamfltr timeout tips_dlg
titledlg ptextbox ttextbox ztextbox video wait wmission""".split()

REC, ITEM = 0x56, 0x1E


def parse(b):
    """A layout's controls in walk order, as dicts; raises when a field the table leaves out is set."""
    ctrls = []
    off = 0
    while True:
        r = b[off:off + REC]
        if len(r) < REC:
            raise ValueError("a record past the end")
        typ = struct.unpack_from("<H", r, 0x22)[0]
        for f, k in ((0x10, 4), (0x1C, 4), (0x24, 6), (0x2A, 4), (0x2E, 4), (0x32, 4), (0x3E, 4), (0x4E, 8)):
            if any(r[f:f + k]):
                raise ValueError("field 0x%X set" % f)
        if typ != 0x0E and any(r[0x46:0x4E]):
            raise ValueError("field 0x46 set on type %d" % typ)
        c = {
            "rect": struct.unpack_from("<4h", r, 0x04),
            "size": struct.unpack_from("<2H", r, 0x0C),
            "text": cstr(b, struct.unpack_from("<I", r, 0x14)[0]),
            "flags": struct.unpack_from("<I", r, 0x18)[0],
            "id": struct.unpack_from("<h", r, 0x20)[0],
            "type": typ,
            "resp": struct.unpack_from("<4h", r, 0x36),
            "items_rect": struct.unpack_from("<4h", r, 0x46) if typ == 0x0E else (0, 0, 0, 0),
            "items": [],
        }
        if typ == 0x0E:
            i = struct.unpack_from("<I", r, 0x42)[0]
            while i:
                q = b[i:i + ITEM]
                if any(q[0x08:0x0A]) or any(q[0x0E:0x12]) or any(q[0x16:0x1E]):
                    raise ValueError("an item field set")
                c["items"].append({"flags": struct.unpack_from("<I", q, 4)[0],
                                   "file": cstr(b, struct.unpack_from("<I", q, 0x0A)[0]),
                                   "x": struct.unpack_from("<h", q, 0x12)[0], "y": struct.unpack_from("<h", q, 0x14)[0]})
                i = struct.unpack_from("<I", q, 0)[0]
        ctrls.append(c)
        link = struct.unpack_from("<I", r, 0x42 if typ == 0 else 0)[0]
        if not link:
            return ctrls
        off = link


def cstr(b, off):
    if not off:
        return None
    return b[off:b.index(b"\0", off)]


def make(ctrls):
    """The file back from the table, as port_layouts.cpp lays it out."""
    n = len(ctrls)
    items = [it for c in ctrls for it in c["items"]]
    at_items = n * REC
    at_strings = at_items + len(items) * ITEM
    strings = bytearray()

    def put(s):
        if s is None:
            return 0
        o = at_strings + len(strings)
        strings.extend(s + b"\0")
        return o

    recs = bytearray(n * REC)
    itembytes = bytearray(len(items) * ITEM)
    k = 0
    for i, c in enumerate(ctrls):
        r = memoryview(recs)[i * REC:(i + 1) * REC]
        nxt = (i + 1) * REC if i + 1 < n else 0
        struct.pack_into("<4h", r, 0x04, *c["rect"])
        struct.pack_into("<2H", r, 0x0C, *c["size"])
        struct.pack_into("<I", r, 0x14, put(c["text"]))
        struct.pack_into("<I", r, 0x18, c["flags"])
        struct.pack_into("<h", r, 0x20, c["id"])
        struct.pack_into("<H", r, 0x22, c["type"])
        struct.pack_into("<4h", r, 0x36, *c["resp"])
        if i == 0:
            struct.pack_into("<I", r, 0x42, nxt)
        else:
            struct.pack_into("<I", r, 0x00, nxt)
        if c["type"] == 0x0E:
            struct.pack_into("<4h", r, 0x46, *c["items_rect"])
            struct.pack_into("<I", r, 0x42, at_items + k * ITEM if c["items"] else 0)
            for j, it in enumerate(c["items"]):
                q = memoryview(itembytes)[(k + j) * ITEM:(k + j + 1) * ITEM]
                struct.pack_into("<I", q, 0, at_items + (k + j + 1) * ITEM if j + 1 < len(c["items"]) else 0)
                struct.pack_into("<I", q, 4, it["flags"])
                struct.pack_into("<I", q, 0x0A, put(it["file"]))
                struct.pack_into("<h", q, 0x12, it["x"])
                struct.pack_into("<h", q, 0x14, it["y"])
            k += len(c["items"])
    return bytes(recs + itembytes + strings)


def c_string(s):
    if s is None:
        return "NULL"
    out = '"'
    hexed = False
    for ch in s:
        if ch == 0x22 or ch == 0x5C:
            out += "\\" + chr(ch)
            hexed = False
        elif 0x20 <= ch < 0x7F:
            if hexed and chr(ch) in "0123456789abcdefABCDEF":
                out += '" "'
            out += chr(ch)
            hexed = False
        else:
            out += "\\x%02X" % ch
            hexed = True
    return out + '"'


def ident(name):
    return "k" + name[0].upper() + name[1:].replace("_", "")


def write(layouts):
    lines = [
        "// layouts117.cpp -- 1.17's dialog layouts, rez\\*.bin, as Hyperion's source. Written by",
        "// tools/layouts117.py from 1.17's MPQs; port_layouts.cpp makes each file",
        "// back from its table, byte for byte (layouts117.py --check).",
        "//",
        "// Per control: id, type, rect (left, top, right, bottom), width and height, flags, response",
        "// rect, text; an animated button (type 14) also its pictures' rect and its pictures.",
        "// clang-format off",
        '#include "../port/data/port_layouts.h"',
        "",
    ]
    table = []
    for name, ctrls, tail in layouts:
        k = ident(name)
        for i, c in enumerate(ctrls):
            if c["items"]:
                lines.append("static const PortLayoutItem %sItems%d[] = {" % (k, i))
                for it in c["items"]:
                    lines.append("    {0x%X, %s, %d, %d}," % (it["flags"], c_string(it["file"]), it["x"], it["y"]))
                lines.append("};")
        lines.append("static const PortLayoutCtrl %s[] = {" % k)
        for i, c in enumerate(ctrls):
            items = ("%sItems%d, %d" % (k, i, len(c["items"]))) if c["items"] else "NULL, 0"
            lines.append("    {%d, %d, {%d, %d, %d, %d}, %d, %d, 0x%X, {%d, %d, %d, %d}, %s, {%d, %d, %d, %d}, %s}," % (
                c["id"], c["type"], *c["rect"], *c["size"], c["flags"], *c["resp"], c_string(c["text"]),
                *c["items_rect"], items))
        lines.append("};")
        lines.append("")
        table.append('    {"rez\\\\%s.bin", %s, %d, %s, %d},' % (name, k, len(ctrls), c_string(tail) if tail else "NULL",
                                                       len(tail)))
    lines.append("const PortLayout g_aPortLayouts117[] = {")
    lines += table
    lines.append("};")
    lines.append("const int g_nPortLayouts117 = %d;" % len(layouts))
    lines.append("")
    with open(OUT, "w", newline="\r\n") as f:
        f.write("\n".join(lines))


def read_all(mpq):
    from scmpq import MpqChain
    m = MpqChain(mpq)
    out = []
    for name in NAMES:
        path = "rez\\%s.bin" % name
        if not m.exists(path):
            sys.exit("no %s in %s" % (path, mpq))
        b = m.read(path)
        ctrls = parse(b)
        made = make(ctrls)
        if not b.startswith(made):
            sys.exit("%s: the table would not make the file back" % path)
        out.append((name, ctrls, b[len(made):]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mpq", default=MPQ, help="a folder with 1.17's StarDat.mpq, BrooDat.mpq and patch_rt.mpq")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--check-dump", metavar="FOLDER")
    a = ap.parse_args()
    if a.check_dump:
        from scmpq import MpqChain
        m = MpqChain(a.mpq)
        n = bad = 0
        for f in sorted(glob.glob(os.path.join(a.check_dump, "rez#*.bin"))):
            name = os.path.basename(f).replace("#", "\\")
            same = open(f, "rb").read() == m.read(name)
            n += 1
            bad += not same
            print(name, "1.17's bytes" if same else "DIFFERS")
        print("%d layouts, %d differ" % (n, bad))
        return 1 if bad else 0
    layouts = read_all(a.mpq)
    if a.check:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        write(layouts)
        same = open(OUT, encoding="utf-8").read() == old
        print("layouts117.cpp is %s" % ("up to date" if same else "rewritten: it was out of date"))
        return 0 if same else 1
    write(layouts)
    print("%d layouts, %d controls -> %s" % (len(layouts), sum(len(c) for _, c, _ in layouts), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
