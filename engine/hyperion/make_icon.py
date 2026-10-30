"""Build Hyperion's icon, hyperion.ico, from the player's 1.17 install.

The icon is the Battlecruiser (the Hyperion is one) as 1.17 draws it: frame 12 of
unit\\terran\\BattleCr.grp, facing south-east like the Wraith on 1.17's own icon, in
the first player's red, on the badlands palette the units use. The frame is trimmed to
the ship and centred on a square; the sizes from the frame's own size up are scaled by
whole pixels, the smaller ones filtered down.

Committing the result is the user's decision (2026-09-27, PRD); nothing else drawn from
the game goes into git.

    python engine/hyperion/make_icon.py [install folder]
"""
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools" / "assets"))
from scmpq import MpqChain  # noqa: E402
import scformats as sf  # noqa: E402
from PIL import Image  # noqa: E402

FRAME = 12          # of 0 (north) to 16 (south), clockwise through east
TEAM_SLOT = 0       # red
SIZES = (16, 24, 32, 48, 64, 128, 256)


def ship(install: str | None) -> Image.Image:
    mpq = MpqChain(install)
    palette = sf.wpe_palette(mpq.read("tileset\\badlands.wpe"))
    lut = sf.team_lut(sf.team_colors(mpq.read("game\\tunit.pcx")), TEAM_SLOT)
    frame = sf.to_rgba(sf.Grp(mpq.read("unit\\terran\\BattleCr.grp")).frame(FRAME), palette, lut)
    frame = frame.crop(frame.getbbox())
    side = max(frame.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.alpha_composite(frame, ((side - frame.width) // 2, (side - frame.height) // 2))
    return square


def sized(square: Image.Image, size: int) -> Image.Image:
    side = square.width
    if size < side:
        return square.resize((size, size), Image.LANCZOS)
    scale = size // side
    big = square.resize((side * scale, side * scale), Image.NEAREST)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(big, ((size - big.width) // 2, (size - big.height) // 2))
    return out


def main(argv: list[str]) -> int:
    square = ship(argv[1] if len(argv) > 1 else None)
    images = [sized(square, s) for s in SIZES]
    out = HERE / "hyperion.ico"
    images[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print("%s: %s, from a %dx%d ship" % (out, ", ".join(str(s) for s in SIZES), square.width, square.height))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
