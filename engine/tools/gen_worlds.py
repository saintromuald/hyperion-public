"""Write the front end's world table, src117/port/front/port_world_table.h, from the design's worlds.js.

    python tools/gen_worlds.py [--src <worlds.js>]

worlds.js is the design (docs/front-end.md): each planet the campaigns are fought on, as the
lore has it, written as the planet shader's parameters. Node evaluates it, its get() fills
in the defaults and a world's states ("tarsonis:fallen"), and every world and state becomes
one row of the table port_world.cpp draws from. The table is generated, never edited; run
this again when worlds.js changes. Without --src the script takes design/11-front-end/worlds.js,
or the design session's copy when the repository has none.
"""
import argparse
import json
import os
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ENGINE)
OUT = os.path.join(ENGINE, "src117", "port", "front", "port_world_table.h")
SOURCES = [os.path.join(REPO, "design", "11-front-end", "worlds.js"),
           os.path.join(os.path.dirname(REPO), "local-design-center", "public", "hyperion", "11-front-end",
                        "worlds.js")]

# worlds.js wants a window; give it one, then print every world and state with its defaults
NODE = r"""
const fs = require('fs');
global.window = {};
eval(fs.readFileSync(process.argv[1], 'utf8'));
const W = window.Worlds;
process.stdout.write(JSON.stringify(W.list().map(x => W.get(x.slug))));
"""

def marked(s):
    """A C string literal marked PT_ for the translation catalog, or NULL."""
    return "NULL" if s is None else "PT_(%s)" % c_string(s)


def c_string(s):
    """A C string literal; octal escapes for UTF-8, which never run into the next character."""
    if s is None:
        return "NULL"
    out = []
    for b in s.encode("utf-8"):
        if b == 0x22 or b == 0x5C:
            out.append("\\" + chr(b))
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def f(x):
    s = repr(float(x))
    return (s if "e" in s or "." in s else s + ".0") + "f"


def vec(v):
    return "{" + ", ".join(f(x) for x in v) + "}"


def row(w):
    at = w.get("at")
    moons = (w.get("moons") or [])[:3]
    if len(w.get("moons") or []) > 3:
        sys.exit("%s: more than three moons" % w["slug"])
    moon_rows = []
    for m in moons:
        moon_rows.append("{%s, %s, %s, %s, %s}" % (vec(m["at"]), f(m["r"]), vec(m["col"]), f(m.get("craters") or 0),
                                                   "true" if m.get("gas") else "false"))
    while len(moon_rows) < 3:
        moon_rows.append("{{0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, false}")
    s2 = w.get("star2")
    fields = [
        # the plate's two lines marked for the catalog (tools/locale_catalog.py), translated where shown
        "%s, %s, %s, %s, %s" % (c_string(w["slug"]), c_string(w["name"]), marked(w["area"]),
                                marked(w["note"]), c_string(w.get("state"))),
        "%s, %s, %s" % (f(w["sea"]), vec(w["deep"]), vec(w["shallow"])),
        "{%s}" % ", ".join(vec(x) for x in w["land"]),
        "{%s}, %s" % (", ".join(vec(x) for x in w["alt"]), f(w["moist"])),
        "%s, %s" % (f(w["ice"]), vec(w["iceCol"])),
        "%s, %s, %s, %s, %s, %s" % (f(w["seed"]), f(w["scale"]), f(w["relief"]), f(w["dunes"]), f(w["craters"]),
                                    f(w["tilt"])),
        "%s, %s, %s, %s, %s, %s" % (f(w["city"]), vec(at[0] if at else [0, 0, 0]), f(at[1] if at else 0),
                                    vec(w["cityDay"]), vec(w["cityCol"]), f(w["cityGain"])),
        "%s, %s, %s" % (f(w["scar"]), vec(w["scarCol"]), f(w["glass"])),
        "%s, %s, %s" % (f(w["creep"]), vec(w["creepCol"]), vec(w["creepGlow"])),
        "%s, %s" % (f(w["lava"]), vec(w["lavaCol"])),
        "%s, %s, %s, %s, %s" % (f(w["cloud"]), vec(w["cloudCol"]), f(w["bands"]), f(w["cloudSpeed"]),
                                vec(w["haze"])),
        "%s, %s" % (vec(w["atmo"]), f(w["atmoThick"])),
        "%s, %s, %s, %s, %s" % (vec(w["star"]), f(w["starGain"]), vec(s2["col"] if s2 else [0, 0, 0]),
                                f(s2["gain"] if s2 else 0), f(s2["angle"] if s2 else 0)),
        "{%s}" % ", ".join(vec(x) for x in w["neb"]),
        "%d, {%s}" % (len(moons), ",\n    ".join(moon_rows)),
    ]
    return "  {" + ",\n   ".join(fields) + "},"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", help="worlds.js to read")
    args = ap.parse_args()
    src = args.src or next((p for p in SOURCES if os.path.exists(p)), None)
    if src is None or not os.path.exists(src):
        sys.exit("no worlds.js (give --src)")
    out = subprocess.run(["node", "-e", NODE, src], capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        sys.exit(out.stderr)
    worlds = json.loads(out.stdout)
    lines = [
        "// port_world_table.h -- generated by tools/gen_worlds.py from the design's worlds.js; do not edit.",
        "// Every world and state, with worlds.js's defaults filled in; port_world.cpp's World says",
        "// what each field is, and worlds.js's PARAMS what each does.",
        "// %d rows: %s" % (len(worlds), ", ".join(w["slug"] for w in worlds)),
        "",
    ]
    lines += [row(w) for w in worlds]
    with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(lines) + "\n")
    print("%s: %d worlds from %s" % (os.path.relpath(OUT, ENGINE), len(worlds), src))


if __name__ == "__main__":
    main()
