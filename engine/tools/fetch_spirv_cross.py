"""Fetch Khronos' SPIRV-Cross into engine/third_party and build its command line on Windows.

The Mac's GPU path takes Metal Shading Language: the build turns each shader's
SPIR-V, which dxc already makes, into MSL with SPIRV-Cross (tools/shader_msl.py). The source
lands in engine/third_party/SPIRV-Cross-<tag>/, which is gitignored: the repository carries
the tag and the checksum, not the files. SPIRV-Cross is under the Apache 2.0 licence.

    python tools/fetch_spirv_cross.py

The download is checked against its SHA-256. The program is built with Visual Studio's cl,
as CascLib is (build_engine_x64.py), into the same folder as bin/spirv-cross.exe. A folder
that already has it is left alone.
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

TAG = "vulkan-sdk-1.4.363.0"
URL = f"https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/{TAG}.zip"
SHA256 = "637ee99192da3cea88bb81c97bcb7177d7b0010f75d2afd6c97f8e7b7d2a0c90"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"SPIRV-Cross-{TAG}"
EXE = DEST / "bin" / "spirv-cross.exe"
# the command line's sources: every backend main.cpp reaches, without the C API
SOURCES = ["main", "spirv_cfg", "spirv_cpp", "spirv_cross", "spirv_cross_parsed_ir", "spirv_cross_util",
           "spirv_glsl", "spirv_hlsl", "spirv_msl", "spirv_parser", "spirv_reflect"]


def build() -> int:
    sys.path.insert(0, str(ENGINE / "tools"))
    from build_engine_x64 import vs_env  # noqa: E402

    obj = DEST / "obj"
    os.makedirs(obj, exist_ok=True)
    os.makedirs(EXE.parent, exist_ok=True)
    cmd = ["cl", "/nologo", "/O2", "/MT", "/EHsc", "/std:c++17", "/W0", "/MP", "/DNDEBUG",
           f"/Fo{obj}\\", f"/Fe{EXE}"] + [str(DEST / (s + ".cpp")) for s in SOURCES]
    env = vs_env()
    # Windows finds a program on this process's PATH, not the child's, so cl is looked up in
    # Visual Studio's, as build_engine_x64.py's run does
    cmd[0] = shutil.which("cl", path=env["PATH"]) or "cl"
    r = subprocess.run(cmd, env=env, cwd=DEST)
    if r.returncode != 0 or not EXE.exists():
        print("cl failed for spirv-cross.exe")
        return 1
    print(f"spirv-cross {TAG} -> {EXE}")
    return 0


def main() -> int:
    if EXE.exists():
        print(f"spirv-cross {TAG} already in {DEST}")
        return 0
    if not (DEST / "spirv_msl.cpp").exists():
        print(f"downloading {URL}")
        with urllib.request.urlopen(URL) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != SHA256:
            print(f"checksum mismatch: {digest}, want {SHA256}")
            return 1
        # the archive's files sit under SPIRV-Cross-<tag>/, which is DEST's own name
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(DEST.parent)
        if not (DEST / "spirv_msl.cpp").exists():
            print(f"unexpected archive layout: no spirv_msl.cpp under {DEST}")
            return 1
    return build()


if __name__ == "__main__":
    sys.exit(main())
