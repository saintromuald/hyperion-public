"""What a StarCraft install may lack of the classic game's small files, as Hyperion's source.

    python tools/local117.py            write src117/gen/local117.cpp
    python tools/local117.py --check    say whether the source still makes the classic files

A StarCraft: Remastered install has Local.dll only in English, German, French, Spanish and Italian,
and on Windows alone, and the game types (Templates\\*.got) only in English. Without them the
game cannot start or list its game types. The user decided the game's small tables and layouts
may be generated source, as decompilation projects carry them (PRD 2026-10-19 and 2026-10-20),
and on 2026-10-29 that Hyperion stands in for Local.dll this way, with its own wording for the
dialogs and none of Local.dll's pictures. So this reads, once:

- from the classic Local.dll, its three key tables (RT_ACCELERATOR 0x65, 0x66 and 0x67: a
  single-player game's keys, a multiplayer game's, and the music and sound keys; event_Win.cpp)
  and the two layouts of the menus' error pop-up (GLUEOK_BIN and GLUEOKCANCEL_BIN, in the form
  layouts117.py writes);
- from the classic MPQs, the 30 game types templates.lst names.

port_local.cpp serves them when the install has none; the pop-up's pictures come from the
install (SD\\glue\\error\\), and Local.dll's dialogs are Hyperion's own.
"""
import argparse
import os
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ENGINE), "tools", "assets"))
import layouts117  # noqa: E402
from data117_source import Exe, c_bytes  # noqa: E402

OUT = os.path.join(ENGINE, "src117", "gen", "local117.cpp")
DLL = os.path.join(ENGINE, "testrun", "play", "Local.dll")

ACCELS = ((0x65, "a single-player game's keys"), (0x66, "a multiplayer game's keys"),
          (0x67, "the music and sound keys"))
LAYOUTS = ("GLUEOK_BIN", "GLUEOKCANCEL_BIN")
RT_BINARY = 0x1B5C  # the type gluError.cpp asks GetResource for


def read(dll, mpq):
    exe = Exe(dll)
    accels = [(n, what, exe.resource(9, n)) for n, what in ACCELS]
    layouts = []
    for name in LAYOUTS:
        b = exe.resource(RT_BINARY, name)
        ctrls = layouts117.parse(b)
        made = layouts117.make(ctrls)
        if not b.startswith(made):
            sys.exit("%s: the table would not make it back" % name)
        layouts.append((name, ctrls, b[len(made):]))
    from scmpq import MpqChain
    m = MpqChain(mpq)
    lst = m.read("Templates\\templates.lst")
    names = [n.strip().strip('"') for n in lst.decode("latin-1").splitlines() if n.strip()]
    templates = [("templates.lst", lst)]
    for n in names:
        templates.append((n, m.read("Templates\\" + n)))
    return accels, layouts, templates


def write(accels, layouts, templates):
    o = [
        "// local117.cpp -- what a StarCraft install may lack of the classic game's small files, as",
        "// Hyperion's source: Local.dll's key tables and the menus' error pop-up's layouts, and the",
        "// classic game types. Written by tools/local117.py from the classic Local.dll and MPQs; served by",
        "// port_local.cpp when the install has none (the user, 2026-10-29; PRD 2026-10-19 and 2026-10-20).",
        "// clang-format off",
        '#include "../port/data/port_layouts.h"',
        '#include "../port/data/port_local.h"',
        "",
    ]
    for n, what, b in accels:
        o.append("// RT_ACCELERATOR 0x%X, %s (event_Win.cpp)" % (n, what))
        o.append("static const unsigned char kAccel%X[] = {" % n)
        o.append(c_bytes(b))
        o.append("};")
        o.append("")
    o.append("const PortLocalFile g_aPortLocalAccels[] = {")
    for n, _, _ in accels:
        o.append('    {"0x%X", kAccel%X, sizeof kAccel%X},' % (n, n, n))
    o.append("};")
    o.append("const int g_nPortLocalAccels = %d;" % len(accels))
    o.append("")
    o.append("// The pop-up's layouts, per control as layouts117.cpp has them.")
    table = []
    for name, ctrls, tail in layouts:
        k = "k" + "".join(w.capitalize() for w in name.split("_"))
        o.append("static const PortLayoutCtrl %s[] = {" % k)
        for c in ctrls:
            if c["items"]:
                sys.exit("%s: an animated button, which this does not write" % name)
            o.append("    {%d, %d, {%d, %d, %d, %d}, %d, %d, 0x%X, {%d, %d, %d, %d}, %s, {%d, %d, %d, %d}, NULL, 0}," % (
                c["id"], c["type"], *c["rect"], *c["size"], c["flags"], *c["resp"], layouts117.c_string(c["text"]),
                *c["items_rect"]))
        o.append("};")
        table.append('    {"%s", %s, %d, %s, %d},' % (name, k, len(ctrls), layouts117.c_string(tail) if tail else "NULL",
                                                    len(tail)))
    o.append("const PortLayout g_aPortLocalLayouts[] = {")
    o += table
    o.append("};")
    o.append("const int g_nPortLocalLayouts = %d;" % len(layouts))
    o.append("")
    o.append("// The classic game types, Templates\\<name>, with the list that names them.")
    rows = []
    for i, (name, b) in enumerate(templates):
        o.append("// %s" % name)
        o.append("static const unsigned char kTemplate%d[] = {" % i)
        o.append(c_bytes(b))
        o.append("};")
        rows.append('    {%s, kTemplate%d, sizeof kTemplate%d},' % (layouts117.c_string(name.encode("latin-1")), i, i))
    o.append("const PortLocalFile g_aPortLocalTemplates[] = {")
    o += rows
    o.append("};")
    o.append("const int g_nPortLocalTemplates = %d;" % len(templates))
    o.append("")
    with open(OUT, "w", newline="\r\n") as f:
        f.write("\n".join(o))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DLL, help="the classic Local.dll (English)")
    ap.add_argument("--mpq", default=layouts117.MPQ, help="a folder with the classic MPQs")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    parts = read(a.dll, a.mpq)
    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
    write(*parts)
    if a.check:
        same = open(OUT, encoding="utf-8").read() == old
        print("local117.cpp is %s" % ("up to date" if same else "rewritten: it was out of date"))
        return 0 if same else 1
    accels, layouts, templates = parts
    print("%d key tables, %d layouts, %d game types -> %s" % (len(accels), len(layouts), len(templates) - 1, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
