"""Fetch dr_libs' audio decoders into engine/third_party.

A mod's sounds and music in MP3, FLAC and WAV (src117/port/data/port_audiodecode.cpp) are read
through dr_mp3, dr_flac and dr_wav; Ogg
Vorbis goes through stb_vorbis (tools/fetch_stb.py). The files land in
engine/third_party/dr_libs-<commit>/, which is gitignored: the repository carries the commit and
the checksums, not the files. dr_libs is public domain, or MIT-0 where that is not recognised.

    python tools/fetch_drlibs.py

dr_libs has no releases; the files are pinned at a commit, each checked against the SHA-256 it
had when it was pinned. A file that is already there is left alone.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

VERSION = "dfe8377"  # dr_mp3 0.7.4, dr_flac 0.13.4, dr_wav 0.14.6 in development (2026-08-31)
COMMIT = "dfe8377631000664666519fdb83da193fd8037f4"
FILES = {
    "dr_mp3.h": "997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f",
    "dr_flac.h": "111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c",
    "dr_wav.h": "03e70c1a2d9787cd7ed3e966c075bea7bac6373f759db9cd7ca9ccdfc4ec4493",
}

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"dr_libs-{VERSION}"


def main() -> int:
    for name, sha256 in FILES.items():
        path = DEST / name
        if path.exists():
            print(f"{name} {VERSION} already in {DEST}")
            continue
        url = f"https://raw.githubusercontent.com/mackron/dr_libs/{COMMIT}/{name}"
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
