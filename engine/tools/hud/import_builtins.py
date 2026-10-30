"""Hyperion's built-in HUD designs, copied from the design center into engine/huds/.

    python engine/tools/hud/import_builtins.py [<design center's 04-widescreen-ui folder>]

The design center keeps the designs; this copies the ones Hyperion ships, as they are, but
for the changes listed in EDITS below, and says so in each file's "note". Run it again when the
design center changes them. The mech and R9 designs are drawn by the shader and carry no game
art; their sounds were generated for the HUD (each .ogg has its .prompt.txt beside it).

The changes:
- no game timer in a skin (the user, 2026-10-22): the time slot and the cell behind it are
  taken out, and in the column layouts the APM cell takes the cell the clock had. The game
  timer stays where Hyperion draws it.
- no flash on a new selection (the user, 2026-10-23: "it gets to be too much"): the timeline on
  select.change is taken out of every layout. A page or a subgroup changed still lights.
- the selection's grid at most 8 columns, 24 units a page as StarCraft II's (the user,
  2026-10-23), in the layouts whose grid grows wider (GRID_MOST).
- the tooltip shows and goes at once, as the original's does (the user, 2026-10-23): the styles'
  timelines on tip.show and tip.hide are taken out.

Each Mech position's replay layout comes with it (the format's reference, "Replays"): the file
its game layout names in "replay", worn while a replay plays. They carry no
game timer of their own, so only the grid's columns change.
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ENGINE, "huds")
DEFAULT_SOURCE = os.path.join(ENGINE, "..", "..", "local-design-center", "public", "hyperion", "04-widescreen-ui")

LAYOUTS = ["mech-span", "mech-span-min", "mech-flank", "mech-flank-right", "terran-r9",
           # the replay layouts the four Mech positions name
           "mech-span-replay", "mech-span-min-replay", "mech-flank-replay", "mech-flank-right-replay"]
STYLES = ["mech-gunmetal", "mech-graphite", "mech-blackout", "terran-r8"]

# per layout: the layers taken out ("panel.layer") and fields changed ({"panel.layer": {...}})
EDITS = {
    "mech-span": {"remove": ["minimap.time", "minimap.timeCell", "minimap.btnWell3"]},
    "mech-span-min": {"remove": ["minimap.time", "minimap.timeCell", "minimap.btnWell3"]},
    "mech-flank": {
        "remove": ["minimap.time", "minimap.timeCell"],
        "set": {"minimap.apmCell": {"x": 18, "w": 204}, "minimap.apm": {"x": 21, "w": 198}},
    },
    "mech-flank-right": {
        "remove": ["minimap.time", "minimap.timeCell"],
        "set": {"minimap.apmCell": {"w": 204}, "minimap.apm": {"w": 198}},
    },
    "terran-r9": {"remove": ["minimap.time", "minimap.clockBox"]},
}
NOTE = ("Hyperion's copy: no game timer in the skin; the time slot and its cell are left out. No flash on a new "
        "selection.")
# timelines taken out of every layout, by the event they start on
TIMELINES_REMOVED = ["select.change"]
# the most columns of the selection's grid, by layout
GRID_MOST = {"mech-span": 8, "mech-span-min": 8, "terran-r9": 8, "mech-span-replay": 8, "mech-span-min-replay": 8}
NOTE_GRID = "Hyperion's copy: the selection's grid shows at most 8 columns, 24 units a page as StarCraft II's (the user, 2026-10-23)."
# timelines taken out of every style, by the event they start on
STYLE_TIMELINES_REMOVED = ["tip.show", "tip.hide"]
NOTE_STYLE = ("Hyperion's copy: the tooltip shows and goes at once, as the original's does, so its tip.show and "
              "tip.hide timelines are left out (the user, 2026-10-23).")


def write_json(path, obj):
    text = json.dumps(obj, indent=2, ensure_ascii=False) + "\n"
    with open(path, "wb") as f:
        f.write(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def edit_layout(name, hud):
    edits = EDITS.get(name, {})
    removed = set(edits.get("remove", []))
    found = set()
    for p in hud["panels"]:
        kept = []
        for layer in p.get("layers", []):
            key = "%s.%s" % (p["id"], layer.get("id"))
            if key in removed:
                found.add(key)
                continue
            for k, v in edits.get("set", {}).get(key, {}).items():
                layer[k] = v
            kept.append(layer)
        p["layers"] = kept
    missing = removed - found
    if missing:
        sys.exit("%s: no layer %s; the design center's file changed, so EDITS needs a look" % (name, ", ".join(sorted(missing))))
    hud["timelines"] = [t for t in hud.get("timelines", []) if t.get("on") not in TIMELINES_REMOVED]
    for t in hud.get("timelines", []):
        t["tracks"] = [tr for tr in t.get("tracks", []) if tr.get("target") not in removed]
    for o in hud.get("options", []):
        for v in o.get("values", []):
            for key in list(v.get("set", {})):
                if key in removed:
                    del v["set"][key]
    hud["note"] = (hud.get("note", "") + " " + NOTE).strip() if removed else hud.get("note", "")
    if name in GRID_MOST:
        grids = [l["grid"] for p in hud["panels"] for l in p.get("layers", []) if l.get("slot") == "selection" and "grid" in l]
        if not grids:
            sys.exit("%s: no selection grid; the design center's file changed, so GRID_MOST needs a look" % name)
        for g in grids:
            g["cols"][1] = min(g["cols"][1], GRID_MOST[name])
        hud["note"] = (hud["note"] + " " + NOTE_GRID).strip()
    return hud


def edit_style(style):
    kept = {k: t for k, t in style.get("timelines", {}).items() if t.get("on") not in STYLE_TIMELINES_REMOVED}
    if len(kept) != len(style.get("timelines", {})):
        style["note"] = (style.get("note", "") + " " + NOTE_STYLE).strip()
    if kept:
        style["timelines"] = kept
    else:
        style.pop("timelines", None)
    return style


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SOURCE
    huds = os.path.join(source, "huds")
    os.makedirs(os.path.join(OUT, "styles"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "files"), exist_ok=True)
    sounds = set()
    for name in LAYOUTS:
        with open(os.path.join(huds, name + ".json"), encoding="utf-8") as f:
            hud = json.load(f)
        if "replay" in hud and hud["replay"] not in [n + ".json" for n in LAYOUTS]:
            sys.exit("%s names the replay layout %s, which LAYOUTS does not copy" % (name, hud["replay"]))
        write_json(os.path.join(OUT, name + ".json"), edit_layout(name, hud))
        print("layout", name)
    for name in STYLES:
        with open(os.path.join(huds, "styles", name + ".json"), encoding="utf-8") as f:
            style = json.load(f)
        for a in style.get("assets", {}).values():
            if "art" in a:
                sys.exit("style %s names the game's art (%s); a built-in carries none" % (name, a["art"]))
            for k in ("audio", "file"):
                if k in a:
                    sounds.add(a[k])
        write_json(os.path.join(OUT, "styles", name + ".json"), edit_style(style))
        print("style", name)
    for s in sorted(sounds):
        for f in (s, s + ".prompt.txt"):
            shutil.copyfile(os.path.join(huds, "files", f), os.path.join(OUT, "files", f))
        print("file", s)


if __name__ == "__main__":
    main()
