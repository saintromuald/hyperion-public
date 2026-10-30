"""Fetch dav1d's source into engine/third_party.

A mod's videos may be AV1; Hyperion decodes them with dav1d (src117/port/data/port_video.cpp).
The source lands in engine/third_party/dav1d-<version>/, which is gitignored: the repository
carries the version and the checksum, not the files. dav1d is under the BSD 2-clause licence
(its COPYING).

    python tools/fetch_dav1d.py

The download is GitHub's archive of the release tag, checked against the SHA-256 it had when
the version was pinned. A folder that is already there is left alone. The builds compile the
files below themselves, in plain C with no assembly, against the configuration written by hand
in tools/dav1d/ (config.h, vcs_version.h) in place of the one dav1d's meson build makes. The
DAV1D_TMPL_SOURCES are compiled twice, with BITDEPTH 8 and 16, into folders of their own.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

NAME = "dav1d"
VERSION = "1.5.4"
URL = f"https://github.com/videolan/dav1d/archive/refs/tags/{VERSION}.zip"
SHA256 = "0b26d5ac2cb6e91154f893d8f471b3ca93afb5f6fead9f3443f3fc36e9975ff0"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"dav1d-{VERSION}"
CHECK = DEST / "include" / "dav1d" / "dav1d.h"

# src/meson.build's libdav1d_sources and libdav1d_tmpl_sources, under DEST; Windows adds
# src/win32/thread.c, its pthread layer.
DAV1D_SOURCES = ["src/%s.c" % n for n in (
    "cdf", "cpu", "ctx", "data", "decode", "dequant_tables", "getbits", "intra_edge", "itx_1d", "lf_mask", "lib",
    "log", "mem", "msac", "obu", "pal", "picture", "qm", "ref", "refmvs", "scan", "tables", "thread_task",
    "warpmv", "wedge")]
DAV1D_WIN32_SOURCES = ["src/win32/thread.c"]
DAV1D_TMPL_SOURCES = ["src/%s.c" % n for n in (
    "cdef_apply_tmpl", "cdef_tmpl", "fg_apply_tmpl", "filmgrain_tmpl", "ipred_prepare_tmpl", "ipred_tmpl",
    "itx_tmpl", "lf_apply_tmpl", "loopfilter_tmpl", "looprestoration_tmpl", "lr_apply_tmpl", "mc_tmpl",
    "recon_tmpl")]
# the include folders, under DEST, before tools/dav1d/
DAV1D_INCLUDES = [".", "include", "include/dav1d"]


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
    # the archive's files sit under one top folder, dav1d-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    print(f"{NAME} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
