"""Fetch the dated Mozilla CA bundle used by libcurl's Mbed TLS backend.

A pinned bundle makes HTTPS verification behave the same on Windows, macOS and
Linux instead of silently disabling verification where Mbed TLS has no native
root store. The build places it beside Hyperion as cacert.pem.
"""
import hashlib
import sys
import urllib.request
from pathlib import Path

DATE = "2026-11-05"
URL = f"https://curl.se/ca/cacert-{DATE}.pem"
SHA256 = "427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285"
ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"cacert-{DATE}.pem"


def main() -> int:
    if DEST.exists() and hashlib.sha256(DEST.read_bytes()).hexdigest() == SHA256:
        print(f"Mozilla CA bundle {DATE} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    DEST.parent.mkdir(parents=True, exist_ok=True)
    DEST.write_bytes(data)
    print(f"Mozilla CA bundle {DATE} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
