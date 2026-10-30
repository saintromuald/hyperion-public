"""Fetch SDL3's Visual C++ development package into engine/third_party.

The renderer is built on SDL3 (docs/renderer.md). The package holds SDL's headers and,
for x86 and x64, SDL3.lib and SDL3.dll. It lands in engine/third_party/SDL3-<version>/,
which is gitignored: the repository carries the version and the checksum, not the files.
SDL is under the zlib licence.

    python tools/fetch_sdl3.py

The download is checked against the SHA-256 the release publishes. A folder that is
already there is left alone. Then run tools/build_sdl3.py: Hyperion runs on SDL3.dll built
from the same version's source with the port's patches, which it puts in place of the
released DLL.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

VERSION = "3.4.16"
PACKAGE = f"SDL3-devel-{VERSION}-VC.zip"
URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{VERSION}/{PACKAGE}"
SHA256 = "1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"SDL3-{VERSION}"


def main() -> int:
    if (DEST / "include" / "SDL3" / "SDL.h").exists():
        print(f"SDL3 {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    # the package's files sit under one top folder, SDL3-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not (DEST / "include" / "SDL3" / "SDL.h").exists():
        print(f"unexpected package layout: no include/SDL3/SDL.h under {DEST}")
        return 1
    print(f"SDL3 {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
