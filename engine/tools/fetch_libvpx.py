"""Fetch libvpx's source into engine/third_party.

A StarCraft: Remastered install keeps its videos, the cinematics and the units' portraits, as
WebM: VP9 pictures (VP8 for the menus' SD animations) and Vorbis sound (milestone B). Hyperion plays them with libvpx's
decoders and the WebM parser libvpx carries, libwebm's mkvparser (src117/port/data/
port_webm.cpp); the sound is stb_vorbis's (tools/fetch_stb.py). The source lands in
engine/third_party/libvpx-<version>/, which is gitignored: the repository carries the version
and the checksum, not the files. libvpx and libwebm are under the BSD licence (their LICENSE
and PATENTS files).

    python tools/fetch_libvpx.py

The download is GitHub's archive of the release tag, checked against the SHA-256 it had when
the version was pinned. A folder that is already there is left alone.

Built as plain C, with no assembly, and as a decoder of VP8 and VP9 alone: the files in
tools/libvpx/ are what libvpx's configure wrote for that, run once as

    configure --target=generic-gnu --disable-multithread --disable-vp8-encoder --enable-vp8-decoder
              --disable-vp9-encoder --enable-vp9-decoder --disable-examples --disable-tools --disable-docs
              --disable-unit-tests --disable-install-docs --disable-install-bins
              --disable-install-libs --disable-webm-io --disable-libyuv --size-limit=8192x8192

(vpx_config.h, vpx_config.c, vpx_version.h and the four *_rtcd.h, with vpx_config.h's INLINE,
HAVE_PTHREAD_H and HAVE_UNISTD_H made to follow the compiler and the system), and sources.txt,
the C files its libvpx_srcs.txt lists that the decoder needs. The build scripts compile those
files themselves. Run configure again, in a folder of its own, for a new version.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

NAME = "libvpx"
VERSION = "1.17.0"
URL = f"https://github.com/webmproject/libvpx/archive/refs/tags/v{VERSION}.zip"
SHA256 = "7c23e98494b8896f128af9661c4c02ea9d4913ea6006898347030683d7686517"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"libvpx-{VERSION}"
CHECK = DEST / "vpx" / "vpx_decoder.h"


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
    # the archive's files sit under one top folder, libvpx-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    print(f"{NAME} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
