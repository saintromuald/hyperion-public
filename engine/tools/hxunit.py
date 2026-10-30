"""A mod's unit art from the SC2 sprite tool's sheets.

    python tools/hxunit.py <render folder> <mod data folder> <name> [--team-glow V,S] [--check]

The render folder is one of the sprite tool's 1x runs (tools/local/sc2-sprites, local only;
its sheets sit in design/_assets/sc2render/<model>/<run>/): sheet.json and the layers
sheet_color.png, sheet_team.png and sheet_shadow.png, a frame a cell, 17 directions to a
row (0 facing up, 8 right, 16 down; the game mirrors the left side), the rows the actions'
frames one after the other. It writes, into <mod data folder>/art/:

    <name>.grp          the unit: frame row*17 + direction, as 1.17's turning images are
                        laid out; the colours the nearest of the palette indexes every
                        tileset shares (16 to 199), the team pixels (the team layer at half
                        or more) the nearest of player 1's ramp, written as 8 to 15, so the
                        game gives them each player's colour
    <name>_shadow.grp   the shadow: the same frames, one index where the shadow layer has
                        any, for an image with 1.17's shadow draw function (10)
    <name>.iscript.txt  a first script for the unit's image, from the actions' frames and
                        ticks, to finish by hand into the mod's iscript.txt

Each GRP's canvas is centred on the sheet's origin, which is where the game puts the
sprite's position. The art is Blizzard's: the art/ folder is never committed or published
(.gitignore), and the package leaves it out (tools/package.py). A model with no team-colour area (StarCraft II's hybrids have none) leaves the team layer
empty, and the unit would show its owner only by its selection circle. --team-glow V,S
makes its bright, saturated pixels the team's instead: those whose brightest channel is V
or more and whose saturation is S or more (80,0.4 takes the HybridZerg's glow, 3 per cent
of its pixels), each given the ramp's step of its brightness among them.

--check reads the GRPs back
through tools/assets/scformats.py and compares every frame with what was meant.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "assets"))
import scformats  # noqa: E402
import scmpq  # noqa: E402

DIRS = 17
COLOURS = range(16, 200)  # the indexes every tileset's palette has alike
TEAM_FIRST = 8
OPAQUE = 128  # a pixel of the colour layer with this alpha or more is drawn
SHADOW_INDEX = 1

# The header's animations for a unit's image (header type 12), in order.
ANIMS = ["Init", "Death", "GndAttkInit", "AirAttkInit", "Unused1", "GndAttkRpt", "AirAttkRpt",
         "CastSpell", "GndAttkToIdle", "AirAttkToIdle", "Unused2", "Walking", "WalkingToIdle",
         "SpecialState1"]


def palette_and_ramp() -> tuple[np.ndarray, np.ndarray]:
    chain = scmpq.MpqChain()
    pal = scformats.wpe_palette(chain.read("tileset\\badlands.wpe")).astype(np.int32)
    teams = scformats.team_colors(chain.read("game\\tunit.pcx"))
    return pal, pal[teams[0]]  # player 1's ramp, the colours of 8 to 15


def nearest(rgb: np.ndarray, colours: np.ndarray) -> np.ndarray:
    """For each pixel (n, 3), the index of the nearest colour (m, 3), weighted for the eye."""
    w = np.array([2, 4, 3], np.int32)
    out = np.empty(len(rgb), np.int32)
    for s in range(0, len(rgb), 65536):
        d = ((rgb[s:s + 65536, None, :] - colours[None, :, :]) ** 2 * w).sum(axis=2)
        out[s:s + 65536] = d.argmin(axis=1)
    return out


def luminance(rgb: np.ndarray) -> np.ndarray:
    return rgb @ np.array([299, 587, 114]) / 1000


def by_brightness(rgb: np.ndarray, ramp: np.ndarray, ref: np.ndarray) -> np.ndarray:
    """Each pixel the step of the ramp (0 to 7) whose place in the ramp's brightness order is
    the pixel's place among ref's pixels, from their 5th to their 95th percentile."""
    order = np.argsort(-luminance(ramp))  # the ramp's steps, brightest first
    lum, ref = luminance(rgb), luminance(ref)
    lo, hi = np.percentile(ref, 5), np.percentile(ref, 95)
    rank = np.clip((hi - lum) / max(hi - lo, 1e-6), 0, 1)  # 0 the brightest
    return order[np.minimum((rank * len(ramp)).astype(int), len(ramp) - 1)]


def frames_of(layer: np.ndarray, sheet: dict) -> list[np.ndarray]:
    fw, fh = sheet["frame_w"], sheet["frame_h"]
    return [layer[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw]
            for r in range(sheet["rows"]) for c in range(sheet["columns"])]


def canvas_of(masks: list[np.ndarray], origin: tuple[float, float]) -> tuple[int, int, int, int]:
    """The canvas, centred on the origin, that holds every frame's pixels: its size, and where
    the frame cell's corner falls on it."""
    ox, oy = origin
    hw = hh = 1.0
    for m in masks:
        ys, xs = np.nonzero(m)
        if len(xs):
            hw = max(hw, ox - xs.min(), xs.max() + 1 - ox)
            hh = max(hh, oy - ys.min(), ys.max() + 1 - oy)
    w, h = 2 * int(np.ceil(hw)), 2 * int(np.ceil(hh))
    return w, h, int(round(w / 2 - ox)), int(round(h / 2 - oy))


def rle_row(row: np.ndarray) -> bytes:
    out = bytearray()
    i, n = 0, len(row)
    while i < n:
        if row[i] == 0:
            j = i
            while j < n and row[j] == 0 and j - i < 127:
                j += 1
            out.append(0x80 | (j - i))
            i = j
            continue
        j = i
        while j < n and row[j] == row[i] and j - i < 63:
            j += 1
        if j - i >= 3:
            out += bytes([0x40 | (j - i), row[i]])
            i = j
            continue
        j = i
        while j < n and row[j] != 0 and j - i < 63:
            if j + 2 < n and row[j] == row[j + 1] == row[j + 2]:
                break
            j += 1
        out.append(j - i)
        out += bytes(row[i:j].tolist())
        i = j
    return bytes(out)


def grp(frames: list[np.ndarray], w: int, h: int) -> bytes:
    """A GRP of frames already on a w x h canvas (palette indexes, 0 clear)."""
    heads, blobs = [], []
    at = 6 + 8 * len(frames)
    seen: dict[bytes, tuple] = {}
    for f in frames:
        ys, xs = np.nonzero(f)
        if len(xs) == 0:
            x0 = y0 = 0
            box = np.zeros((1, 1), np.uint8)
        else:
            x0, y0 = int(xs.min()), int(ys.min())
            box = f[y0:ys.max() + 1, x0:xs.max() + 1]
        bh, bw = box.shape
        if bw > 255 or bh > 255:
            sys.exit(f"a frame of {bw} x {bh}: a GRP's frame is 255 pixels a side at most")
        key = bytes([x0, y0]) + box.tobytes() + struct.pack("<HH", bw, bh)
        if key in seen:
            heads.append(seen[key])
            continue
        rows = [rle_row(box[r]) for r in range(bh)]
        table, off = [], 2 * bh
        for r in rows:
            table.append(off)
            off += len(r)
        if off > 0xFFFF:
            sys.exit("a frame past 64 KiB of data")
        blob = struct.pack(f"<{bh}H", *table) + b"".join(rows)
        head = (x0, y0, bw, bh, at)
        seen[key] = head
        heads.append(head)
        blobs.append(blob)
        at += len(blob)
    out = struct.pack("<HHH", len(frames), w, h)
    out += b"".join(struct.pack("<BBBBI", *hd) for hd in heads)
    return out + b"".join(blobs)


def on_canvas(cell: np.ndarray, w: int, h: int, dx: int, dy: int) -> np.ndarray:
    c = np.zeros((h, w), np.uint8)
    ch, cw = cell.shape
    x0, y0 = max(0, dx), max(0, dy)
    x1, y1 = min(w, dx + cw), min(h, dy + ch)
    if x1 > x0 and y1 > y0:
        c[y0:y1, x0:x1] = cell[y0 - dy:y1 - dy, x0 - dx:x1 - dx]
    return c


def script(name: str, sheet: dict) -> str:
    acts = {a["name"]: a for a in sheet["actions"]}
    label = "".join(p.capitalize() for p in name.split("_"))

    def frames(a: str, wait_last: bool = True) -> list[str]:
        act = acts[a]
        out = []
        for k in range(act["frames"]):
            out.append(f"        playfram      0x{(act['first_row'] + k) * DIRS:02x}")
            if wait_last or k + 1 < act["frames"]:
                out.append(f"        wait          {act['ticks_per_frame']}")
        return out

    lines = [f"# {name}: a first script from tools/hxunit.py; finish it by hand.", "",
             ".headerstart", f"Name          {name}", "Type          12"]
    named = {"Init": "Init", "Death": "Death", "GndAttkInit": "GndAttkInit",
             "GndAttkRpt": "GndAttkInit", "CastSpell": "CastSpell" if "CastSpell" in acts else None,
             "GndAttkToIdle": "GndAttkToIdle", "Walking": "Walking", "WalkingToIdle": "WalkingToIdle"}
    for an in ANIMS:
        lab = named.get(an)
        lines.append(f"{an:<13} {label + lab if lab and lab in acts or an == 'Init' else '[NONE]'}")
    lines += [".headerend", "",
              f"{label}Init:", f"        imgul         {name}_shadow 0 0", "        playfram      0x00",
              f"        goto          {label}Local", "",
              f"{label}Local:", "        wait          125", f"        goto          {label}Local", ""]
    if "Death" in acts:
        lines += [f"{label}Death:"] + frames("Death") + ["        end", ""]
    if "GndAttkInit" in acts:
        body = frames("GndAttkInit")
        mid = 2 * max(1, acts["GndAttkInit"]["frames"] // 2)
        body.insert(mid, "        attackwith    1")
        lines += [f"{label}GndAttkInit:", "        nobrkcodestart"] + body + [
            "        nobrkcodeend", "        gotorepeatattk", "        ignorerest", ""]
    if "GndAttkToIdle" in acts:
        lines += [f"{label}GndAttkToIdle:", "        playfram      0x00", f"        goto          {label}Local", ""]
    if "CastSpell" in acts:
        lines += [f"{label}CastSpell:", "        nobrkcodestart"] + frames("CastSpell") + [
            "        castspell", "        nobrkcodeend", "        sigorder      2", f"        goto          {label}GndAttkToIdle", ""]
    if "Walking" in acts:
        lines += [f"{label}Walking:"] + frames("Walking") + [f"        goto          {label}Walking", ""]
    if "WalkingToIdle" in acts:
        lines += [f"{label}WalkingToIdle:", "        playfram      0x00", f"        goto          {label}Local", ""]
    return "\r\n".join(lines) + "\r\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render", type=Path)
    ap.add_argument("data", type=Path)
    ap.add_argument("name")
    ap.add_argument("--team-glow", metavar="V,S", help="bright, saturated pixels as the team colour")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    sheet = json.loads((a.render / "sheet.json").read_text())
    if sheet.get("scale", 1) != 1 or sheet["columns"] != DIRS:
        sys.exit("a 1x run with 17 directions to a row is wanted")
    layer = {n: np.asarray(Image.open(a.render / f"sheet_{n}.png").convert("RGBA"))
             for n in ("color", "team", "shadow")}
    pal, ramp = palette_and_ramp()
    colours = pal[list(COLOURS)]

    col = frames_of(layer["color"], sheet)
    team = frames_of(layer["team"], sheet)
    shad = frames_of(layer["shadow"], sheet)
    origin = tuple(sheet["origin"])

    drawn = [c[..., 3] >= OPAQUE for c in col]
    w, h, dx, dy = canvas_of(drawn, origin)
    if a.team_glow:
        v, s = (float(x) for x in a.team_glow.split(","))

        def team_of(c, t, m):
            rgb = c[..., :3].astype(np.int32)
            mx, mn = rgb.max(axis=2), rgb.min(axis=2)
            return m & (mx >= v) & ((mx - mn) >= s * np.maximum(mx, 1))
    else:
        def team_of(c, t, m):
            return m & (t[..., :3].max(axis=2) >= 128)
    teams = [team_of(c, t, m) for c, t, m in zip(col, team, drawn)]
    glow = np.concatenate([c[..., :3][k] for c, k in zip(col, teams)]).astype(np.int32) if a.team_glow else None
    if glow is not None and len(glow) == 0:
        sys.exit("--team-glow takes no pixels; lower V or S")
    unit = []
    for c, m, is_team in zip(col, drawn, teams):
        idx = np.zeros(m.shape, np.uint8)
        rgb = c[..., :3].astype(np.int32)
        plain = m & ~is_team
        if plain.any():
            idx[plain] = np.array(list(COLOURS), np.uint8)[nearest(rgb[plain], colours)]
        if is_team.any():
            if glow is not None:  # the glow's own brightness order, not the team colour's
                step = by_brightness(rgb[is_team], ramp, glow)
            else:
                step = nearest(rgb[is_team], ramp)
            idx[is_team] = TEAM_FIRST + step.astype(np.uint8)
        unit.append(on_canvas(idx, w, h, dx, dy))

    smask = [s[..., 3] > 0 for s in shad]
    sw, sh, sdx, sdy = canvas_of(smask, origin)
    shadow = [on_canvas(m.astype(np.uint8) * SHADOW_INDEX, sw, sh, sdx, sdy) for m in smask]

    art = a.data / "art"
    art.mkdir(parents=True, exist_ok=True)
    outs = [(art / f"{a.name}.grp", unit, w, h), (art / f"{a.name}_shadow.grp", shadow, sw, sh)]
    for path, frames, fw, fh in outs:
        path.write_bytes(grp(frames, fw, fh))
        print(f"{path}: {len(frames)} frames on {fw} x {fh}, {path.stat().st_size:,} bytes")
    (art / f"{a.name}.iscript.txt").write_bytes(script(a.name, sheet).encode())
    print(f"{art / (a.name + '.iscript.txt')}")
    if a.check:
        for path, frames, fw, fh in outs:
            g = scformats.Grp(path.read_bytes())
            bad = [i for i, f in enumerate(frames) if not np.array_equal(g.frame(i), f)]
            print(f"{path.name}: {'every frame reads back' if not bad else f'{len(bad)} frames differ, first {bad[0]}'}")
            if bad:
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
