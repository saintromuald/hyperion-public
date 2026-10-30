"""Fetch CascLib's source into engine/third_party.

Remastered keeps its files in CASC storage. The front end (docs/front-end.md) reads
Remastered's font from the player's install through CascLib, the reader the PRD named for
Remastered's data (2026-09-26), and its art later. The source lands in
engine/third_party/CascLib-<version>/, which is gitignored: the repository carries the
version and the checksum, not the files. CascLib is under the MIT licence.

    python tools/fetch_casclib.py

The download is GitHub's archive of the release tag, checked against the SHA-256 it had
when the version was pinned. A folder that is already there is left alone.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

NAME = "CascLib"
VERSION = "3.0"
URL = f"https://github.com/ladislav-zezula/CascLib/archive/refs/tags/{VERSION}.zip"
SHA256 = "a2d72255918bcae23cec9715fd7de383c058b23130d60638ae77da24e1d98f35"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"CascLib-{VERSION}"
CHECK = DEST / "src" / "CascLib.h"

def main() -> int:
    if CHECK.exists():
        print(f"{NAME} {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    # the archive's files sit under one top folder, CascLib-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    print(f"{NAME} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
