"""Fetch curl's source for the native Replay Kraken HTTPS client.

The engine build compiles a small static libcurl with HTTP/HTTPS only. Windows uses
Schannel, macOS Secure Transport, and Linux the Mbed TLS already built for the LAN
streamed-seat library. TLS verification stays enabled at runtime.

    python tools/fetch_curl.py
"""
import hashlib
import sys
import tarfile
import urllib.request
from pathlib import Path

VERSION = "8.23.0"
URL = f"https://curl.se/download/curl-{VERSION}.tar.xz"
SHA256 = "8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5"
ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"curl-{VERSION}"
CHECK = DEST / "include" / "curl" / "curl.h"


def main() -> int:
    if CHECK.exists():
        print(f"curl {VERSION} already in {DEST}")
        return 0
    archive = ENGINE / "third_party" / f"curl-{VERSION}.tar.xz"
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    archive.parent.mkdir(parents=True, exist_ok=True)
    archive.write_bytes(data)
    with tarfile.open(archive) as tar:
        # The pinned official archive has one source root and no links outside it.
        for member in tar.getmembers():
            target = (archive.parent / member.name).resolve()
            if archive.parent.resolve() not in target.parents or member.issym() or member.islnk():
                print(f"unsafe archive member: {member.name}")
                return 1
        tar.extractall(archive.parent)
    archive.unlink()
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    print(f"curl {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
