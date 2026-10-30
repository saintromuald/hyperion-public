"""Fetch Microsoft's DirectX Shader Compiler release into engine/third_party.

The GPU renderer's shaders are HLSL (docs/renderer.md, R4). dxc compiles them to DXIL
for Direct3D 12 and to SPIR-V for Vulkan; the Windows SDK's dxc has no SPIR-V, so the
build uses the released one. The package lands in engine/third_party/dxc-<version>/,
which is gitignored: the repository carries the version and the checksum, not the
files. dxc is under the University of Illinois/NCSA licence (LLVM's).

    python tools/fetch_dxc.py

The download is checked against the SHA-256 the release publishes. A folder that is
already there is left alone.
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

VERSION = "1.9.2607"
PACKAGE = "dxc_2026_07_29.zip"
URL = f"https://github.com/microsoft/DirectXShaderCompiler/releases/download/v{VERSION}/{PACKAGE}"
SHA256 = "a1dfb116ba3eeae6a1582291b53a8e7bf65ad760676bd3194685c8f7367cd241"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"dxc-{VERSION}"
DXC = DEST / "bin" / "x64" / "dxc.exe"


def main() -> int:
    if DXC.exists():
        print(f"dxc {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    # the package's folders (bin, lib, inc) sit at its top
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(DEST)
    if not DXC.exists():
        print(f"unexpected package layout: no bin/x64/dxc.exe under {DEST}")
        return 1
    print(f"dxc {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
