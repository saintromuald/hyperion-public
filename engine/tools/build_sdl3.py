"""Build SDL3's x64 DLL from SDL's source with the port's patches (tools/sdl3-patches/).

fetch_sdl3.py brings SDL's Visual C++ package: the headers, SDL3.lib and a released
SDL3.dll. Hyperion runs on a patched DLL instead, built here from the same version's source
and put in the package's lib/x64/ folder, so the engine build, play.py, the replay harness
and package.py take it as they took the released one. The released SDL3.dll and SDL3.pdb
are kept beside it in lib/x64/released/.

    python tools/build_sdl3.py

The source zip is checked against the SHA-256 the release publishes and unpacked into
engine/third_party/SDL3-<version>-src/ (gitignored, like the package). The patches are
applied in name order with git apply, then CMake builds the shared library for x64 with
Visual Studio's generator and the flags SDL's own release build uses (Release, a .pdb, the
static runtime). A DLL already built from the same patches is left
alone; --force builds again. SDL is under the zlib licence.

The patches:
  d3d12-descriptor-room.patch  a draw or dispatch makes sure its descriptor tables fit the
                               GPU heaps before writing them (the crash of 2026-10-15)
"""

import argparse
import hashlib
import io
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "3.4.16"
SOURCE = f"SDL3-{VERSION}.zip"
URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{VERSION}/{SOURCE}"
SHA256 = "5f399fdfbc040169ef4418a3cb99ccc5a0641681f7ccc1da66eeec3c97fa4076"

ENGINE = Path(__file__).resolve().parents[1]
PATCHES = ENGINE / "tools" / "sdl3-patches"
PACKAGE = ENGINE / "third_party" / f"SDL3-{VERSION}"
LIB = PACKAGE / "lib" / "x64"
SRC = ENGINE / "third_party" / f"SDL3-{VERSION}-src"
BUILD = SRC / "build-x64"
# what the DLL in lib/x64 was built from: the patches' names and digests
STAMP = LIB / "SDL3.patches.txt"


def patches() -> list:
    return sorted(PATCHES.glob("*.patch"))


def stamp_text() -> str:
    lines = []
    for p in patches():
        lines.append(f"{p.name} {hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest()}")
    return "\n".join(lines) + "\n"


def fetch_source() -> None:
    if (SRC / "CMakeLists.txt").exists():
        return
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        sys.exit(f"checksum mismatch: {digest}, want {SHA256}")
    # the zip's files sit under one top folder, SDL3-<version>/, unpacked here as SDL3-<version>-src/
    with tempfile.TemporaryDirectory(dir=SRC.parent) as tmp:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(tmp)
        shutil.move(str(Path(tmp) / f"SDL3-{VERSION}"), str(SRC))
    print(f"SDL3 {VERSION} source -> {SRC}")


def apply_patches() -> None:
    """Each patch onto the source as released: a file a patch touches is first put back from
    the zip's copy (kept as <file>.orig), so a second run applies to the same text."""
    for p in patches():
        text = p.read_bytes().replace(b"\r\n", b"\n")
        # the files it touches, from its +++ lines
        targets = [line[6:].decode().strip() for line in text.split(b"\n") if line.startswith(b"+++ b/")]
        for t in targets:
            f = SRC / t
            orig = f.with_name(f.name + ".orig")
            if not orig.exists():
                shutil.copy2(f, orig)
            # git apply wants the line endings the patch has
            f.write_bytes(orig.read_bytes().replace(b"\r\n", b"\n"))
        with tempfile.NamedTemporaryFile("wb", suffix=".patch", delete=False) as tmp:
            tmp.write(text)
        try:
            r = subprocess.run(["git", "apply", "--unsafe-paths", "-p1", tmp.name], cwd=SRC)
        finally:
            Path(tmp.name).unlink()
        if r.returncode != 0:
            sys.exit(f"{p.name} does not apply to SDL {VERSION}")
        print(f"  applied {p.name}")


def build() -> None:
    cmake = shutil.which("cmake") or r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    # the flags SDL's own release build gives CMake for Visual C++ (build-scripts/build-release.py)
    configure = [cmake, "-S", str(SRC), "-B", str(BUILD), "-A", "x64", "--fresh", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF",
                 "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_POLICY_DEFAULT_CMP0141=NEW", "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=ProgramDatabase",
                 "-DCMAKE_SHARED_LINKER_FLAGS=-INCREMENTAL:NO -DEBUG -OPT:REF -OPT:ICF",
                 "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
    if subprocess.run(configure).returncode != 0:
        sys.exit("cmake configure failed")
    if subprocess.run([cmake, "--build", str(BUILD), "--config", "Release", "--parallel"]).returncode != 0:
        sys.exit("cmake build failed")


def install() -> None:
    built = BUILD / "Release"
    kept = LIB / "released"
    kept.mkdir(exist_ok=True)
    for name in ("SDL3.dll", "SDL3.pdb"):
        if not (kept / name).exists() and (LIB / name).exists():
            shutil.copy2(LIB / name, kept / name)
        shutil.copy2(built / name, LIB / name)
        print(f"  {name} -> {LIB}")
    STAMP.write_text(stamp_text())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--force", action="store_true", help="build again even when the DLL is built from these patches")
    args = ap.parse_args()
    if not (PACKAGE / "include" / "SDL3" / "SDL.h").exists():
        sys.exit(f"no SDL3 package in {PACKAGE}: run tools/fetch_sdl3.py first")
    if not args.force and STAMP.exists() and STAMP.read_text() == stamp_text():
        print(f"SDL3.dll in {LIB} is built from these patches")
        return 0
    fetch_source()
    apply_patches()
    build()
    install()
    return 0


if __name__ == "__main__":
    sys.exit(main())
