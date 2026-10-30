"""Fetch stb's single-file libraries into engine/third_party.

The front end (docs/front-end.md) draws its text from a glyph atlas that stb_truetype
rasterizes out of a TrueType or OpenType font, and a Remastered install's videos have their
sound in Vorbis, which stb_vorbis decodes (src117/port/data/port_webm.cpp). A mod's
art sheets and icons in PNG or TGA are stb_image's (src117/port/data/port_imagefile.cpp).
The files land in engine/third_party/stb-<commit>/, which is gitignored: the repository
carries the commits and the checksums, not the files. stb is public domain, or MIT where that
is not recognised.

    python tools/fetch_stb.py

stb has no releases; each file is pinned at a commit, the folder's or its own, and checked
against the SHA-256 it had when it was pinned. A file that is already there is left alone.
Then Hyperion's patches in tools/stb-patches/ are applied (third_party_patches.py).
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

VERSION = "6e9f34d"  # stb_truetype.h 1.26, the last commit to touch it (2024-07-15)
COMMIT = "6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760"
FILES = {
    "stb_truetype.h": (COMMIT, "ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab"),
    "stb_vorbis.c": (COMMIT, "4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636"),  # 1.22
    # 2.30, the last commit to touch it (2024-05-31)
    "stb_image.h": ("013ac3beddff3dbffafd5177e7972067cd2b5083",
                    "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"),
}

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"stb-{VERSION}"
PATCHES = ENGINE / "tools" / "stb-patches"

# stb_image's code, as the builds compile it into the image library beside libwebp's decoder:
# PNG and TGA alone, from memory (port_imagefile.cpp).
STB_IMAGE_IMPL = """#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
"""


def main() -> int:
    for name, (commit, sha256) in FILES.items():
        path = DEST / name
        if path.exists():
            print(f"{name} {VERSION} already in {DEST}")
            continue
        url = f"https://raw.githubusercontent.com/nothings/stb/{commit}/{name}"
        print(f"downloading {url}")
        with urllib.request.urlopen(url) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != sha256:
            print(f"checksum mismatch: {digest}, want {sha256}")
            return 1
        DEST.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"{name} {VERSION} -> {DEST}")
    import third_party_patches
    return 0 if third_party_patches.apply(DEST, PATCHES) else 1


if __name__ == "__main__":
    sys.exit(main())
