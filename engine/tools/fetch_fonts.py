"""Fetch the front end's typefaces into engine/third_party (docs/front-end.md).

The front end sets its text in Saira, its titles and labels in Michroma and the numbers that
count in IBM Plex Mono, and the HUD's tooltips are Chakra Petch. Noto Sans,
and Noto Sans JP, KR, SC and TC, draw the letters those faces lack, Greek, Cyrillic, Japanese,
Korean and Chinese, when no StarCraft: Remastered install has them. All are under the SIL Open
Font License, so Hyperion carries them: the build puts them into Hyperion.exe as resources
(build_engine_x64.py), and each package carries their licenses (package.py, licenses/). They come from Google Fonts' repository at a pinned commit,
each checked against the SHA-256 it had when it was pinned, and land in
engine/third_party/fonts-<commit>/, which is gitignored: the repository carries the commit and
the checksums, not the files.

Saira is published as one variable font (width and weight axes). stb_truetype draws a variable
font's default outlines only, so the static cuts the front end uses are made here from it with
fontTools' instancer: Regular (weight 400) and SemiBold (weight 600), both at normal width.

The Noto faces are variable too, and the East Asian ones are 10 to 18 MB, so each is cut to
its regular weight and to the characters it is there for (cut()): the language's common set
(the first level of JIS X 0208, KS X 1001, GB 2312 and Big5) and every character Hyperion's
catalog for that language uses (engine/locale/*.po). The builds call cut(), which makes the
cuts again when the catalogs use a character the cuts lack. A cut keeps the face's name: the
license reserves only "Source", which the Noto names do not use.

    python tools/fetch_fonts.py

Needs fontTools (pip install fonttools). A folder that is already complete is left alone.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

COMMIT = "8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5"  # google/fonts, 2026-03-12
VERSION = COMMIT[:7]
BASE = f"https://raw.githubusercontent.com/google/fonts/{COMMIT}/ofl"

# (path in the repository, file here, SHA-256)
FILES = [
    ("michroma/Michroma-Regular.ttf", "Michroma-Regular.ttf",
     "b62301163788bc5b7f8fcac0b74b184e34e1827e577b499ecb724da065098f87"),
    ("michroma/OFL.txt", "Michroma-OFL.txt", "33c39ce59a45bfb0dae3280e98bdb46be59d6d68efc62d4dc5fae90a26d2f393"),
    ("saira/Saira[wdth,wght].ttf", "Saira-Variable.ttf",
     "9d050fc5a01c85f74c4257c207d50b55d1e40c37308c642f974a2c5003231dde"),
    ("saira/OFL.txt", "Saira-OFL.txt", "f2665d4718b452b3818a877191355ac884a6b9b419d35408fe7ee487e9e8f30f"),
    ("ibmplexmono/IBMPlexMono-Medium.ttf", "IBMPlexMono-Medium.ttf",
     "a9b4c49bb299e05b5f6c481e7fb5e78943d2793249a0c8874ab574a2d1ea6755"),
    ("ibmplexmono/OFL.txt", "IBMPlexMono-OFL.txt", "7e6b2818edbd8f6a01ae80641cc8f16a51080d08fb4e532be3a0b6f74adb07da"),
    ("chakrapetch/ChakraPetch-Regular.ttf", "ChakraPetch-Regular.ttf",
     "98fcd638baa5c81ff0316b7538ce330ee3b23b1302726de3526d5933a8ecf986"),
    ("chakrapetch/ChakraPetch-Medium.ttf", "ChakraPetch-Medium.ttf",
     "d480f4f97405fac3600652e2e14fd0b14339031c0af4da11582994878f04d919"),
    ("chakrapetch/ChakraPetch-SemiBold.ttf", "ChakraPetch-SemiBold.ttf",
     "45264de3204ddbd5fb3e14a2402acd5c630d16650ae5fc221d2c52da46a6734b"),
    ("chakrapetch/ChakraPetch-Bold.ttf", "ChakraPetch-Bold.ttf",
     "65fbf76d95651697275e19db4d717c0e95a789ddd3476478b05292104db278a0"),
    ("chakrapetch/OFL.txt", "ChakraPetch-OFL.txt", "13831d02389d917d22fcfa6c79f98f8acbf61d230add0e73a4d4dc5f3bfb9e56"),
    ("notosans/NotoSans[wdth,wght].ttf", "NotoSans-Variable.ttf",
     "bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d"),
    ("notosans/OFL.txt", "NotoSans-OFL.txt", "cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a"),
    ("notosansjp/NotoSansJP[wght].ttf", "NotoSansJP-Variable.ttf",
     "c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f"),
    ("notosanskr/NotoSansKR[wght].ttf", "NotoSansKR-Variable.ttf",
     "194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252"),
    ("notosanssc/NotoSansSC[wght].ttf", "NotoSansSC-Variable.ttf",
     "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"),
    ("notosanstc/NotoSansTC[wght].ttf", "NotoSansTC-Variable.ttf",
     "864727d210d54f2537bbe23b3a839436c3992af72de9322af5270897246bd44f"),
    # one license for the four East Asian faces
    ("notosansjp/OFL.txt", "NotoSansCJK-OFL.txt", "1c05c68c34f9708415aada51f17e1b0092d2cea709bf4a94cd38114f9e73d7d9"),
]

# the static cuts made from the variable font: (file, weight, width)
CUTS = [("Saira-Regular.ttf", 400, 100), ("Saira-SemiBold.ttf", 600, 100)]

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"fonts-{VERSION}"
LOCALE = ENGINE / "locale"

# the Noto cuts: (file, variable source, catalogs whose characters it draws, common set)
NOTO = [("NotoSans-Regular.ttf", "NotoSans-Variable.ttf", (), "latin"),
        ("NotoSansJP-Regular.ttf", "NotoSansJP-Variable.ttf", ("jaJP",), "euc_jp"),
        ("NotoSansKR-Regular.ttf", "NotoSansKR-Variable.ttf", ("koKR",), "euc_kr"),
        ("NotoSansSC-Regular.ttf", "NotoSansSC-Variable.ttf", ("zhCN",), "gb2312"),
        ("NotoSansTC-Regular.ttf", "NotoSansTC-Variable.ttf", ("zhTW",), "big5")]

# the licenses a package carries (package.py): (file here, the faces it covers)
LICENSES = [("Saira-OFL.txt", "Saira"), ("Michroma-OFL.txt", "Michroma"),
            ("IBMPlexMono-OFL.txt", "IBM Plex Mono"), ("ChakraPetch-OFL.txt", "Chakra Petch"),
            ("NotoSans-OFL.txt", "Noto Sans"), ("NotoSansCJK-OFL.txt", "Noto Sans JP, KR, SC and TC")]


def common_set(name: str) -> set[int]:
    """The characters a cut keeps whatever the catalogs say: Latin, Greek and Cyrillic for Noto
    Sans; for the East Asian faces the first level of the language's classic character set, read
    from Python's codec for it, with the CJK punctuation, kana, Hangul letters and full-width
    forms."""
    if name == "latin":
        ranges = [(0x20, 0x250), (0x370, 0x530), (0x1E00, 0x1F00), (0x2000, 0x2070), (0x20A0, 0x20D0),
                  (0x2100, 0x2200)]
        return {c for a, b in ranges for c in range(a, b)}
    s = {c for a, b in [(0x2000, 0x2070), (0x3000, 0x3100), (0x3130, 0x3190), (0xFF00, 0xFFF0)]
         for c in range(a, b)}
    if name == "big5":
        rows, cells = range(0xA1, 0xC7), [*range(0x40, 0x7F), *range(0xA1, 0xFF)]
    else:
        last = {"euc_jp": 0xCF, "euc_kr": 0xC8, "gb2312": 0xD7}[name]
        rows, cells = range(0xA1, last + 1), range(0xA1, 0xFF)
    for a in rows:
        for b in cells:
            try:
                s.update(ord(c) for c in bytes([a, b]).decode(name))
            except UnicodeDecodeError:
                pass
    return s


def catalog_chars(locales) -> set[int]:
    import re
    s = set()
    for loc in locales:
        po = LOCALE / f"{loc}.po"
        if po.exists():
            for m in re.finditer(r'^msgstr(?:\[\d+\])? "(.*)"$', po.read_text(encoding="utf-8"), re.M):
                s.update(ord(c) for c in m.group(1))
    return s


def download() -> int:
    """Every file of FILES the folder lacks, checked against its SHA-256."""
    DEST.mkdir(parents=True, exist_ok=True)
    for path, name, sha in FILES:
        if (DEST / name).exists():
            continue  # a folder of an earlier list keeps what it has
        url = f"{BASE}/{path}".replace("[", "%5B").replace("]", "%5D").replace(",", "%2C")
        print(f"downloading {url}")
        with urllib.request.urlopen(url) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if sha is not None and digest != sha:
            print(f"checksum mismatch for {name}: {digest}, want {sha}")
            return 1
        (DEST / name).write_bytes(data)
    return 0


def cut() -> int:
    """The Noto cuts, made again when one lacks a character its catalogs use (or is missing).
    A build calls it; fontTools is needed only when a cut is made."""
    import json
    stamp = DEST / "noto-cuts.json"
    made = json.loads(stamp.read_text(encoding="utf-8")) if stamp.exists() else {}
    todo = []
    for name, source, locales, common in NOTO:
        want = common_set(common) | catalog_chars(locales)
        if not (DEST / name).exists() or not want <= set(made.get(name, [])):
            todo.append((name, source, want))
    if not todo:
        return 0
    if download() != 0:
        return 1
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
        from fontTools import subset
    except ImportError:
        raise SystemExit("fontTools is needed for the Noto cuts: pip install fonttools")
    for name, source, want in todo:
        font = TTFont(DEST / source)
        axes = {"wght": 400, "wdth": 100} if "wdth" in [a.axisTag for a in font["fvar"].axes] else {"wght": 400}
        font = instancer.instantiateVariableFont(font, axes, updateFontNames=False)
        opts = subset.Options()
        opts.layout_features = ["*"]
        opts.name_IDs = ["*"]
        opts.name_languages = ["*"]
        opts.hinting = False
        sub = subset.Subsetter(opts)
        sub.populate(unicodes=want)
        sub.subset(font)
        font.save(DEST / name)
        made[name] = sorted(want)
        print(f"{name}: {len(want)} characters, {(DEST / name).stat().st_size:,} bytes")
    stamp.write_text(json.dumps(made), encoding="utf-8")
    return 0


def main() -> int:
    want = [DEST / f for _, f, _ in FILES] + [DEST / f for f, _, _ in CUTS]
    if all(p.exists() for p in want):
        print(f"fonts {VERSION} already in {DEST}")
        return cut()
    if download() != 0:
        return 1
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
    except ImportError:
        print("fontTools is needed for Saira's static cuts: pip install fonttools")
        return 1
    for name, weight, width in CUTS:
        if (DEST / name).exists():
            continue
        font = TTFont(DEST / "Saira-Variable.ttf")
        static = instancer.instantiateVariableFont(font, {"wght": weight, "wdth": width},
                                                   updateFontNames=False)
        static.save(DEST / name)
        print(f"{name}: weight {weight}, width {width}")
    print(f"fonts {VERSION} -> {DEST}")
    return cut()


if __name__ == "__main__":
    sys.exit(main())
