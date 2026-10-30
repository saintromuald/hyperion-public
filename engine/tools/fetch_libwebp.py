"""Fetch libwebp's source into engine/third_party.

A mod's art sheets, portraits' stills and icons may be WebP; Hyperion
decodes them with libwebp's decoder (src117/port/data/port_imagefile.cpp). The source lands in
engine/third_party/libwebp-<version>/, which is gitignored: the repository carries the version
and the checksum, not the files. libwebp is under the BSD licence (its COPYING and PATENTS
files).

    python tools/fetch_libwebp.py

The download is GitHub's archive of the release tag, checked against the SHA-256 it had when
the version was pinned. A folder that is already there is left alone. The builds compile the
decoder's files themselves, WEBP_SOURCES below (its makefile.unix's DEC_OBJS, DSP_DEC_OBJS and
UTILS_DEC_OBJS), with no threads; a SIMD file the target has no instructions for compiles to
nothing.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

NAME = "libwebp"
VERSION = "1.5.0"
URL = f"https://github.com/webmproject/libwebp/archive/refs/tags/v{VERSION}.zip"
SHA256 = "7def360f5294e416cf05f1e9bfc12da54ecd6498a9df85d1a61f820e881cfd42"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"libwebp-{VERSION}"
CHECK = DEST / "src" / "webp" / "decode.h"

# The decoder's files, under DEST; no two share a name, so their objects can share a folder.
WEBP_SOURCES = (
    ["src/dec/%s_dec.c" % n for n in ("alpha", "buffer", "frame", "idec", "io", "quant", "tree", "vp8", "vp8l",
                                      "webp")]
    + ["src/dsp/%s.c" % n for n in (
        "alpha_processing", "alpha_processing_mips_dsp_r2", "alpha_processing_neon", "alpha_processing_sse2",
        "alpha_processing_sse41", "cpu", "dec", "dec_clip_tables", "dec_mips32", "dec_mips_dsp_r2", "dec_msa",
        "dec_neon", "dec_sse2", "dec_sse41", "filters", "filters_mips_dsp_r2", "filters_msa", "filters_neon",
        "filters_sse2", "lossless", "lossless_mips_dsp_r2", "lossless_msa", "lossless_neon", "lossless_sse2",
        "lossless_sse41", "rescaler", "rescaler_mips32", "rescaler_mips_dsp_r2", "rescaler_msa", "rescaler_neon",
        "rescaler_sse2", "upsampling", "upsampling_mips_dsp_r2", "upsampling_msa", "upsampling_neon",
        "upsampling_sse2", "upsampling_sse41", "yuv", "yuv_mips32", "yuv_mips_dsp_r2", "yuv_neon", "yuv_sse2",
        "yuv_sse41")]
    + ["src/utils/%s.c" % n for n in ("bit_reader_utils", "color_cache_utils", "filters_utils", "huffman_utils",
                                      "palette", "quant_levels_dec_utils", "random_utils", "rescaler_utils",
                                      "thread_utils", "utils")])


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
    # the archive's files sit under one top folder, libwebp-<version>/
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST.parent)
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    print(f"{NAME} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
