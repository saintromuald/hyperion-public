"""Every direction of a unit's image at once, to check a sheet's frames: a ring of units on the
canvas, each standing in its own direction.

    python engine/tools/control/directions.py types <types.json> --flingy <flingy> [--prefix face]
    python engine/tools/control/directions.py ring [--prefix face] [--at 1500,1440] [--radius 130] [--across 260]
    python engine/tools/control/directions.py sheet <sheet.png> <out.png> --cell 64 [--numbers]

types adds 32 units to a mod's types.json, <prefix>00 to <prefix>31, each of the flingy
named and starting in its own direction (the units' "direction", 0 to 31), so a unit made
stands that way with no order. A unit based on one that turns as it is made, as the Marine
does, keeps it.

ring places those units round a point of a running game (the live channel: Hyperion with
--control and --mod <the mod>, on the canvas), direction 0 at the top and the rest clockwise,
and centres the view on them. The ring is wider than it is tall (--across, --radius), as the map
shows above the console, so units as large as a Marine stand apart. Units are named, so the mod's numbering does not matter.

sheet makes a 32-direction test sheet from a 17-direction one: directions 17 to 31 are 15
to 1 mirrored, and with --numbers each frame carries its direction in its top left corner, so
a frame the game drew mirrored would show its number backwards. Make its team layer the same
way without --numbers, and give the image "directions": 32 (docs/mod-data.md, "Art sheets").
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hyperion_ctl import PORT, Client, unit_id  # noqa: E402

DIRECTIONS = 32


def Types(args):
    with open(args.types, encoding="utf-8") as f:
        types = json.load(f)
    units = types.setdefault("units", {})
    for d in range(DIRECTIONS):
        units[f"{args.prefix}{d:02d}"] = {"base": args.base, "name": f"Facing {d}", "flingy": args.flingy,
                                          "direction": d}
    with open(args.types, "w", encoding="utf-8") as f:
        json.dump(types, f, indent=2)
    print(f"{args.types}: {args.prefix}00 to {args.prefix}{DIRECTIONS - 1:02d}")
    return 0


def Ring(args):
    cx, cy = args.at
    with Client(args.port) as client:
        for d in range(DIRECTIONS):
            t = d * 2 * math.pi / DIRECTIONS
            x, y = round(cx + args.across * math.sin(t)), round(cy - args.radius * math.cos(t))
            answer = client.call("spawn", unit=unit_id(f"{args.prefix}{d:02d}"), x=x, y=y, player=args.player,
                                 count=1)
            if not answer.get("ok"):
                print(f"{args.prefix}{d:02d}: {answer.get('error')}")
                return 1
        # a little below the centre, so the ring clears the console
        client.call("camera", x=cx, y=cy + 20)
    print(f"{DIRECTIONS} units round {cx},{cy}")
    return 0


def Sheet(args):
    from PIL import Image, ImageDraw, ImageFont

    cell = args.cell
    src = Image.open(args.sheet).convert("RGBA")
    rows = src.height // cell
    out = Image.new("RGBA", (DIRECTIONS * cell, rows * cell), (0, 0, 0, 0))
    font = ImageFont.load_default(size=13)
    for r in range(rows):
        for d in range(DIRECTIONS):
            c = d if d <= 16 else DIRECTIONS - d
            frame = src.crop((c * cell, r * cell, (c + 1) * cell, (r + 1) * cell))
            if d > 16:
                frame = frame.transpose(Image.FLIP_LEFT_RIGHT)
            if args.numbers and frame.getbbox():
                ImageDraw.Draw(frame).text((2, 1), str(d), font=font, fill=(255, 255, 255, 255), stroke_width=1,
                                           stroke_fill=(0, 0, 0, 255))
            out.paste(frame, (d * cell, r * cell))
    out.save(args.out)
    print(f"{args.out}: {out.width} x {out.height}, {rows} rows of {DIRECTIONS}")
    return 0


def Point(text):
    x, y = text.split(",")
    return int(x), int(y)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)

    t = sub.add_parser("types", help="add a unit per direction to a mod's types.json")
    t.add_argument("types")
    t.add_argument("--flingy", required=True, help="the flingy, and so the image, the units show")
    t.add_argument("--base", default="terran_marine")
    t.add_argument("--prefix", default="face")
    t.set_defaults(run=Types)

    r = sub.add_parser("ring", help="place the units round a point of a running game")
    r.add_argument("--prefix", default="face")
    r.add_argument("--at", type=Point, default=(1500, 1440), help="the centre, in map pixels")
    r.add_argument("--radius", type=int, default=130, help="from the centre to the top and bottom")
    r.add_argument("--across", type=int, default=260, help="from the centre to either side")
    r.add_argument("--player", type=int, default=0)
    r.add_argument("--port", type=int, default=PORT)
    r.set_defaults(run=Ring)

    s = sub.add_parser("sheet", help="a 32-direction test sheet from a 17-direction one")
    s.add_argument("sheet")
    s.add_argument("out")
    s.add_argument("--cell", type=int, required=True, help="a frame's cell, in the sheet's pixels")
    s.add_argument("--numbers", action="store_true", help="each frame's direction in its corner")
    s.set_defaults(run=Sheet)

    args = ap.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
