"""Fetch StormLib's source into engine/third_party.

The standalone build replaces what the original took from its own code and from Storm
with StormLib: its pklib, PKWARE's implode and explode, from step 3,
and its MPQ reading from step 4. The source lands in engine/third_party/StormLib-<version>/,
which is gitignored: the repository carries the version and the checksum, not the files.
StormLib is under the MIT licence.

    python tools/fetch_stormlib.py

The download is GitHub's archive of the release tag, checked against the SHA-256 it had
when the version was pinned. A folder that is already there is left alone.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

VERSION = "9.40"
URL = f"https://github.com/ladislav-zezula/StormLib/archive/refs/tags/v{VERSION}.zip"
SHA256 = "d8422b3dcb4a73b90764d3f3e810e99407d21b9445935b11635c52a87bdc8608"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"StormLib-{VERSION}"


def main() -> int:
    if (DEST / "src" / "pklib" / "pklib.h").exists():
        print(f"StormLib {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    # the archive's files sit under one top folder, StormLib-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not (DEST / "src" / "pklib" / "pklib.h").exists():
        print(f"unexpected archive layout: no src/pklib/pklib.h under {DEST}")
        return 1
    print(f"StormLib {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
