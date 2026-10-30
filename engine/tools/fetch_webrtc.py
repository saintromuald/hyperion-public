"""Fetch the sources a LAN room's streamed seats are built from into engine/third_party/webrtc
(R5: the game streamed to a browser over WebRTC).

    python tools/fetch_webrtc.py          fetch what is missing, each archive checked
    python tools/fetch_webrtc.py --pin    print each archive's SHA-256, for pinning

- libdatachannel (MPL-2.0): WebRTC's peer connection, data channels and media tracks, with the
  submodules it pins at its tag: libjuice (MPL-2.0, ICE), usrsctp (BSD-3, SCTP), libSRTP
  (BSD-3), plog (MIT) and nlohmann json (MIT);
- Mbed TLS 3.6 (Apache-2.0), its DTLS (libdatachannel's USE_MBEDTLS);
- Opus (BSD-3), the sound;
- OpenH264 (BSD-2), H.264 on the processor, for a machine with no NVENC;
- nv-codec-headers (MIT), NVIDIA's encoder API, loaded from the driver at run time.

The folder is gitignored: the repository carries the versions and checksums, not the files. Each
download is GitHub's archive of a tag or commit (Mbed TLS's release tarball, which carries its
submodule), checked against the SHA-256 it had when pinned. A folder already there is left alone.
"""

import hashlib
import io
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / "webrtc"

# (folder, url, sha256, a file that says it is there)
LDC = "libdatachannel-0.24.6"
SOURCES = [
    (LDC, "https://github.com/paullouisageneau/libdatachannel/archive/refs/tags/v0.24.6.zip",
     "c78ca1b7ce45f08c7e6233eba4d714bde10c6e3a4e60aad0be891e2594807519", "CMakeLists.txt"),
    # libdatachannel's submodules, at the commits its v0.24.6 pins, into its deps/
    (LDC + "/deps/libjuice", "https://github.com/paullouisageneau/libjuice/archive/b89c792e3612faf2f12cf35bcc56857313a06be3.zip",
     "c937ec97110b559e4a27885a3098c7a65dad2459c813569441a1329f74662381", "CMakeLists.txt"),
    (LDC + "/deps/libsrtp", "https://github.com/cisco/libsrtp/archive/24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5.zip",
     "76f2decca689144058bcade866c1bd9f5649ac466806ba090dcc368db5525d5f", "CMakeLists.txt"),
    (LDC + "/deps/usrsctp", "https://github.com/sctplab/usrsctp/archive/fec583d54493f879d2ae44a743423bf8a04371ab.zip",
     "752b4337bece1138f4f4151cec4240956a7230b007e11bc1a6915cbda58d7086", "CMakeLists.txt"),
    (LDC + "/deps/plog", "https://github.com/SergiusTheBest/plog/archive/94899e0b926ac1b0f4750bfbd495167b4a6ae9ef.zip",
     "195415f664dccc6682c344ca0e1966674555f0be07e9552d4f760860acce719d", "CMakeLists.txt"),
    (LDC + "/deps/json", "https://github.com/nlohmann/json/archive/55f93686c01528224f448c19128836e7df245f72.zip",
     "0746352e4e9532e7aeabbcbaa79079b6bc6008e9261e4d459c9486a9b48a172e", "CMakeLists.txt"),
    ("mbedtls-3.6.7", "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2",
     "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6", "CMakeLists.txt"),
    ("opus-1.5.2", "https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz", "65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1", "CMakeLists.txt"),
    ("openh264-2.6.0", "https://github.com/cisco/openh264/archive/refs/tags/v2.6.0.zip", "b1ca643eadbbf16f74c86a66ab487f38691d8dbdda435683b149b2ce2c8077ec", "codec/api/wels/codec_api.h"),
    ("nv-codec-headers-13.1.15.0", "https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.1.15.0.zip",
     "d948f673d0cec5f0d58cce890864dea3d3a507b9979226bdb02b0bddd6e464ae", "include/ffnvcodec/nvEncodeAPI.h"),
]


def unpack(data: bytes, url: str, to: Path) -> None:
    """The archive's files, from under its one top folder, into `to`."""
    to.mkdir(parents=True, exist_ok=True)
    if url.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for info in z.infolist():
                parts = info.filename.split("/", 1)
                if len(parts) < 2 or not parts[1]:
                    continue
                out = to / parts[1]
                if info.is_dir():
                    out.mkdir(parents=True, exist_ok=True)
                else:
                    out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(z.read(info))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as t:
            for m in t.getmembers():
                parts = m.name.split("/", 1)
                if len(parts) < 2 or not parts[1] or not (m.isfile() or m.isdir()):
                    continue
                out = to / parts[1]
                if m.isdir():
                    out.mkdir(parents=True, exist_ok=True)
                else:
                    out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(t.extractfile(m).read())


def main() -> int:
    pin = "--pin" in sys.argv
    bad = 0
    for folder, url, sha, mark in SOURCES:
        to = DEST / folder
        if (to / mark).exists() and not pin:
            print(f"{folder}: already there")
            continue
        print(f"downloading {url}")
        with urllib.request.urlopen(url) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if pin:
            print(f"  {folder}: {digest}")
            continue
        if sha is not None and digest != sha:
            print(f"  checksum mismatch: {digest}, want {sha}")
            bad += 1
            continue
        unpack(data, url, to)
        if not (to / mark).exists():
            print(f"  unexpected archive layout: no {mark} under {to}")
            bad += 1
            continue
        print(f"  -> {to}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
