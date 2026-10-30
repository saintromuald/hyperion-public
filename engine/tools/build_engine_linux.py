"""Build Hyperion for Linux or a Mac with clang.

    python tools/build_engine_linux.py [--sdl] [--libs] [-j N] [--arch x64|arm64]

Writes src117/build/linux-<arch>/: Hyperion.x86_64 (Hyperion.aarch64 on ARM), the engine
and its start-up in one program, and libSDL3.so.0 beside it, which the program finds
through its rpath ($ORIGIN). <arch> is x64 or arm64, the machine's own. The program is not
named Hyperion alone, as on Linux a file cannot have the name of a folder beside it, and the
data folder beside it was Hyperion\ until 0.6.2's last build, Data\ since
(src117/port/platform/port_paths.cpp). Beside them rt_observer.so, the replay harness's observer
(tools/replaytest/rt_observer.cpp), which Hyperion loads from HYPERION_DLLS as it loads
rt_observer.dll on Windows; the Windows calls it makes are Hyperion's, which the link
exports for it.

On Windows the build runs in the hyperion-linux image (tools/linux/Dockerfile), with the
repository mounted at /src; on Linux it runs where it is, with clang, lld, CMake and
Ninja and the packages the Dockerfile lists. --arch arm64 builds for ARM on Windows, in
the hyperion-linux-arm64 image, which Docker runs under emulation (slowly).

The same TUs as the Windows build (src117/tus.txt), compiled as tools/build_engine_x64.py
compiles them, with clang. The Win32 calls they make are port_posix.cpp's, declared by
src117/compat/, which only this build puts on the include path. The game's globals are
bound to the engine's block of 1.17's data by an assembler file this script writes, the
ELF form of tools/wave117/abs_globals.py --x64: each name is set at its address's offset
in one uninitialized section. SDL3 is built from third_party/SDL3-3.4.16-src, and
StormLib and CascLib from their sources, once (--sdl and --libs build them again), and so are
a LAN room's streamed seats' libraries (tools/fetch_webrtc.py's: libdatachannel with
Mbed TLS, Opus and OpenH264's encoder; --webrtc builds them again).

The GPU presenter's shaders are the SPIR-V the Windows build compiled
(src117/build/x64/shaders); a Linux dxc is for later.

What keeps the simulation the same as the Windows build's: bitfields laid out as
Microsoft's compiler lays them out (-mms-bitfields), no fused multiply-add
(-ffp-contract=off), char signed as on Windows (-fsigned-char, for ARM), and signed
overflow and aliasing as MSVC treats them (-fwrapv, -fno-strict-aliasing).

On a Mac it runs where it is, with Xcode's command-line tools and CMake and Ninja (Homebrew's),
and builds for macOS 13 on Apple Silicon into src117/build/mac-arm64/: Hyperion.arm64 with
libSDL3.0.dylib beside it (an rpath of @executable_path), and rt_observer.dylib. What differs
from Linux: Mach-O puts an underscore before every name, so the globals' block and the
resources are written in its assembler; Apple's linker has no --wrap (port_posix.cpp says how
the Mac does without it) and no --exclude-libs, so the program exports only the names listed
for it; and the C++ library is the system's libc++, which nothing links statically.
"""
import argparse
import concurrent.futures
import glob
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ENGINE)
SRC = os.path.join(ENGINE, "src117")
ARCH = {"x86_64": "x64", "amd64": "x64", "aarch64": "arm64", "arm64": "arm64"}.get(platform.machine().lower(), "x64")
MAC = sys.platform == "darwin"
OUT = os.path.join(SRC, "build", ("mac-" if MAC else "linux-") + ARCH)
PROGRAM = {"x64": "Hyperion.x86_64", "arm64": "Hyperion.aarch64"}
# a Mac's: one arm64 build for every Apple Silicon Mac, macOS 13 on
MAC_PROGRAM = "Hyperion.arm64"
MACOS_MIN = "13.0"
MAC_MIN = ["-mmacosx-version-min=" + MACOS_MIN] if MAC else []
SDL_LIB = "libSDL3.0.dylib" if MAC else "libSDL3.so.0"
AR = "ar" if MAC else "llvm-ar"
NM = "nm" if MAC else "llvm-nm"
CXXFILT = ["c++filt", "-n"] if MAC else ["llvm-cxxfilt"]
SDL_SRC = os.path.join(ENGINE, "third_party", "SDL3-3.4.16-src")
SL = os.path.join(ENGINE, "third_party", "StormLib-9.40", "src")
PKLIB = os.path.join(SL, "pklib")
CLAY = os.path.join(ENGINE, "third_party", "clay-0.14")
STB = os.path.join(ENGINE, "third_party", "stb-6e9f34d")
STB_PATCHES = os.path.join(ENGINE, "tools", "stb-patches")
CASCLIB = os.path.join(ENGINE, "third_party", "CascLib-3.0")
# WebM video: libvpx's VP9 decoder, libwebm's parser, stb_vorbis (build_engine_x64.py)
LIBVPX = os.path.join(ENGINE, "third_party", "libvpx-1.17.0")
LIBVPX_CONFIG = os.path.join(ENGINE, "tools", "libvpx")
LIBWEBM = os.path.join(LIBVPX, "third_party", "libwebm")
FONTS = os.path.join(ENGINE, "third_party", "fonts-8b0a1d0")
# the MP3, FLAC and WAV decoders for a mod's sounds and music (tools/fetch_drlibs.py)
DRLIBS = os.path.join(ENGINE, "third_party", "dr_libs-dfe8377")
# a mod's art in PNG, TGA (stb_image) and WebP (libwebp's decoder; tools/fetch_libwebp.py)
LIBWEBP = os.path.join(ENGINE, "third_party", "libwebp-1.5.0")
# a mod's AV1 videos: dav1d in plain C (tools/fetch_dav1d.py, its configuration in tools/dav1d/)
DAV1D = os.path.join(ENGINE, "third_party", "dav1d-1.5.4")
DAV1D_CONFIG = os.path.join(ENGINE, "tools", "dav1d")
SHADERS = os.path.join(SRC, "build", "x64", "shaders")
IMAGE = "hyperion-linux"

sys.path.insert(0, os.path.join(ENGINE, "tools"))
sys.path.insert(0, os.path.join(ENGINE, "tools", "wave117"))
from port_files import port_path, tu_path  # noqa: E402
import third_party_patches  # noqa: E402

# Hyperion's own resources: the front end's typefaces (port_ui.cpp) as RT_RCDATA, and the
# version (init.cpp's get_program_version_info) as RT_VERSION, as hyperion.rc gives them on
# Windows.
FONT_RESOURCES = [("HYPERION_FONT_TEXT", "Saira-Regular.ttf"), ("HYPERION_FONT_WIDE", "Michroma-Regular.ttf"),
                  ("HYPERION_FONT_BOLD", "Saira-SemiBold.ttf"), ("HYPERION_FONT_MONO", "IBMPlexMono-Medium.ttf"),
                  ("HYPERION_FONT_PETCH", "ChakraPetch-Regular.ttf"),
                  ("HYPERION_FONT_PETCH_MEDIUM", "ChakraPetch-Medium.ttf"),
                  ("HYPERION_FONT_PETCH_SEMI", "ChakraPetch-SemiBold.ttf"),
                  ("HYPERION_FONT_PETCH_BOLD", "ChakraPetch-Bold.ttf"),
                  # the letters the faces above lack, when no Remastered install has them
                  ("HYPERION_FONT_NOTO", "NotoSans-Regular.ttf"), ("HYPERION_FONT_NOTO_JP", "NotoSansJP-Regular.ttf"),
                  ("HYPERION_FONT_NOTO_KR", "NotoSansKR-Regular.ttf"), ("HYPERION_FONT_NOTO_SC", "NotoSansSC-Regular.ttf"),
                  ("HYPERION_FONT_NOTO_TC", "NotoSansTC-Regular.ttf")]

# The objects the binder leaves to the link, as in the Windows build.
NOT_GAME = ["code_map.o", "data117.o", "port_pklib.o", "pk_implode.o", "pk_explode.o", "pk_crc32.o", "port_stormlib.o",
            "port_clay.o", "port_lanstream.o", "abs_globals.o", "resources_posix.o", "hyperion.o"]
WEBRTC_SRC = os.path.join(ENGINE, "third_party", "webrtc")
# what the streamed seats link (tools/build_webrtc.py builds the same for Windows)
WEBRTC_LIBS = ["libdatachannel.a", "libjuice.a", "libsrtp2.a", "libusrsctp.a", "libmbedtls.a", "libmbedx509.a",
               "libmbedcrypto.a", "libeverest.a", "libp256m.a", "libopus.a", "libopenh264.a"]

COMMON = ["-c", "-g", "-O2", "-w", "-Wno-c++11-narrowing", "-Wno-incompatible-ms-struct", "-fno-strict-aliasing",
          "-fwrapv", "-fno-delete-null-pointer-checks", "-ffp-contract=off", "-fsigned-char", "-mms-bitfields",
          # every delete through the game's operator delete (Storm.cpp), which has no sized form
          "-fno-sized-deallocation",
          "-DNDEBUG", "-DHIGHFIRST", "-include", os.path.join(SRC, "h", "compiler.h"),
          "-I" + os.path.join(SRC, "compat"), "-I" + os.path.join(SRC, "h")]
COMMON += MAC_MIN
LANG = ["-std=c++17"] + COMMON + ["-DOC_PAYLOAD", "-DOC_WITH_HELD"]
GEN = ["-std=c++17"] + COMMON + ["-DOC_DLL_MODE"]
PORT = ["-std=c++20"] + COMMON + ["-DOC_WITH_HELD", "-I" + OUT, "-I" + os.path.join(SDL_SRC, "include"),
                                  "-I" + SHADERS, "-I" + CLAY, "-I" + STB, "-I" + os.path.join(CASCLIB, "src"),
                                  "-DCASCLIB_NO_AUTO_LINK_LIBRARY", "-I" + LIBVPX, "-I" + LIBVPX_CONFIG,
                                  "-I" + LIBWEBM, "-I" + DRLIBS, "-I" + os.path.join(LIBWEBP, "src"),
                                  "-I" + os.path.join(DAV1D, "include"),
                                  # libopus's headers, for the Opus decoder (port_audiodecode.cpp)
                                  "-I" + os.path.join(OUT, "webrtc", "include")]
# third-party code, compiled as its own makers do
THIRD = ["-c", "-g", "-O2", "-w", "-fPIC", "-fsigned-char", "-DNDEBUG"]
THIRD += MAC_MIN
# CMake's for a Mac: the same lowest macOS and the one architecture
CMAKE_MAC = ["-DCMAKE_OSX_DEPLOYMENT_TARGET=" + MACOS_MIN, "-DCMAKE_OSX_ARCHITECTURES=arm64"] if MAC else []
STORMLIB_FILES = [
    "FileStream.cpp", "LibTomCrypt.c", "LibTomMath.c", "LibTomMathDesc.c", "SBaseCommon.cpp", "SBaseFileTable.cpp",
    "SBaseSubTypes.cpp", "SCompression.cpp", "SFileAddFile.cpp", "SFileAttributes.cpp", "SFileCompactArchive.cpp",
    "SFileCreateArchive.cpp", "SFileExtractFile.cpp", "SFileFindFile.cpp", "SFileGetFileInfo.cpp",
    "SFileListFile.cpp", "SFileOpenArchive.cpp", "SFileOpenFileEx.cpp", "SFilePatchArchives.cpp",
    "SFileReadFile.cpp", "SFileVerify.cpp", "SMemUtf8.cpp", "adpcm/adpcm.cpp", "huffman/huff.cpp",
    "sparse/sparse.cpp", "jenkins/lookup3.c", "bzip2/blocksort.c", "bzip2/bzlib.c", "bzip2/compress.c",
    "bzip2/crctable.c", "bzip2/decompress.c", "bzip2/huffman.c", "bzip2/randtable.c", "lzma/C/LzFind.c",
    "lzma/C/LzmaDec.c", "lzma/C/LzmaEnc.c", "zlib/adler32.c", "zlib/compress_zlib.c", "zlib/crc32.c",
    "zlib/deflate.c", "zlib/inffast.c", "zlib/inflate.c", "zlib/inftrees.c", "zlib/trees.c", "zlib/zutil.c"]
# CascLib's names that StormLib or the C library has too (build_engine_x64.py's CASCLIB_DEFS)
CASCLIB_DEFS = ["-DZ_PREFIX", "-Dhashlittle=CascHashLittle", "-Dhashlittle2=CascHashLittle2",
                "-Dhashword=CascHashWord", "-Dhashword2=CascHashWord2", "-Dhashbig=CascHashBig",
                "-Dz_errmsg=casc_z_errmsg"]


def say(what):
    print("[build_engine_linux]", what, flush=True)


def run(cmd, what, cwd=None):
    say(what)
    r = subprocess.run(cmd, cwd=cwd or OUT)
    if r.returncode:
        sys.exit("[build_engine_linux] %s FAILED" % what)


def tus(group):
    out = []
    for line in open(os.path.join(SRC, "tus.txt"), encoding="utf-8"):
        f = line.split()
        if f and not f[0].startswith("#") and f[0] == group:
            out.append(f[1])
    return out


def compile_many(jobs, what, workers):
    """Each (command, object) at once, as many as workers; every failure's output shown."""
    say("%s (%d)" % (what, len(jobs)))
    failed = []

    def one(job):
        cmd, obj = job
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=OUT)
        return obj, r.returncode, r.stdout + r.stderr

    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for obj, rc, text in pool.map(one, jobs):
            if rc:
                failed.append(obj)
                sys.stdout.write(text[-6000:])
    if failed:
        sys.exit("[build_engine_linux] %s FAILED: %s" % (what, ", ".join(os.path.basename(f) for f in failed)))


def obj_of(src, prefix=""):
    return os.path.join(OUT, prefix + os.path.splitext(os.path.basename(src))[0] + ".o")


def write_build_id():
    """build_id.h and release_notes.h, as build_engine_x64.py writes them. A copy of the tree
    without git, as tools/mac_sync.py sends a Mac, takes them from its build_origin.json, and so
    does a checkout given one, as the release's Mac build is."""
    os.makedirs(OUT, exist_ok=True)

    def git(*args):
        try:
            r = subprocess.run(["git"] + list(args), capture_output=True, text=True, cwd=SRC)
        except OSError:
            return ""
        return r.stdout.strip() if r.returncode == 0 else ""
    commit = git("rev-parse", "--short", "HEAD")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = git("status", "--porcelain", "--untracked-files=no", "--", ".")
    number = git("rev-list", "--count", "HEAD")
    origin = os.path.join(ENGINE, "build_origin.json")
    if os.path.exists(origin):
        with open(origin, encoding="utf-8") as f:
            o = json.load(f)
        commit, branch, number = o.get("commit", ""), o.get("branch", ""), str(o.get("number", ""))
        dirty = "+" if o.get("dirty") else ""
    with open(os.path.join(OUT, "build_id.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// build_id.h -- written by tools/build_engine_linux.py for each build.\n#pragma once\n")
        f.write('#define HYPERION_COMMIT "%s%s"\n' % (commit, "+" if commit and dirty else ""))
        f.write('#define HYPERION_BRANCH "%s"\n' % branch)
        f.write('#define HYPERION_BUILD_NUMBER %s\n' % (number or "0"))
    subprocess.run([sys.executable, os.path.join(ENGINE, "tools", "release_notes.py"), "header",
                    os.path.join(OUT, "release_notes.h")], check=True)
    subprocess.run([sys.executable, os.path.join(ENGINE, "tools", "locale_catalog.py"), "header",
                    os.path.join(OUT, "locale_catalogs.h")], check=True)


def build_sdl(again):
    """SDL3 as a shared library, built once into OUT/sdl3."""
    lib = os.path.join(OUT, SDL_LIB)
    if os.path.exists(lib) and not again:
        return
    work = os.path.join(OUT, "sdl3")
    # a build from the start: CMake's cache keeps what it found of the last system's C
    # library (a newer glibc's strlcpy, for one)
    shutil.rmtree(work, ignore_errors=True)
    run(["cmake", "-S", SDL_SRC, "-B", work, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF",
         "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL=OFF"] + CMAKE_MAC, "cmake SDL3")
    run(["ninja", "-C", work], "ninja SDL3")
    real = os.path.realpath(os.path.join(work, SDL_LIB))
    shutil.copy2(real, lib)


def build_libs(again, workers):
    """StormLib and CascLib into OUT/libs/*.a, once."""
    libs = os.path.join(OUT, "libs")
    storm, casc = os.path.join(libs, "libstorm.a"), os.path.join(libs, "libcasc.a")
    if os.path.exists(storm) and os.path.exists(casc) and not again:
        return storm, casc
    os.makedirs(libs, exist_ok=True)
    jobs = []
    for f in STORMLIB_FILES:
        src = os.path.join(SL, f)
        cxx = f.endswith(".cpp")
        obj = os.path.join(libs, "sl_" + f.replace("/", "_").rsplit(".", 1)[0] + ".o")
        jobs.append(([("clang++" if cxx else "clang")] + THIRD + (["-std=c++11"] if cxx else [])
                     + ["-D_LIB", "-D__STORMLIB_SELF__", "-D_7ZIP_ST", "-D_fdopen=fdopen",
                        "-include", port_path("port_stormlib_names.h"), "-o", obj, src], obj))
    flags = THIRD + ["-D_LIB", "-D__CASCLIB_SELF__"] + CASCLIB_DEFS
    # CascLib's unity files name their sources with backslashes; the same lists with slashes
    for unity, cc, std in (("sources-cpp.cpp", "clang++", ["-std=c++17"]), ("sources-c.c", "clang", [])):
        text = open(os.path.join(CASCLIB, unity), encoding="utf-8").read()
        text = re.sub(r'#include "([^"]+)"', lambda m: '#include "%s"' % os.path.join(
            CASCLIB, m.group(1).replace("\\", "/")), text)
        mine = os.path.join(libs, "casc_" + unity)
        open(mine, "w", encoding="utf-8").write(text)
        obj = os.path.join(libs, "casc_cpp.o" if cc == "clang++" else "casc_c.o")
        jobs.append(([cc] + flags + std + ["-o", obj, mine], obj))
    compile_many(jobs, "StormLib and CascLib", workers)
    for lib in (storm, casc):
        if os.path.exists(lib):
            os.remove(lib)
    run([AR, "rcs", storm] + [j[1] for j in jobs if os.path.basename(j[1]).startswith("sl_")], "ar StormLib")
    run([AR, "rcs", casc, os.path.join(libs, "casc_cpp.o"), os.path.join(libs, "casc_c.o")], "ar CascLib")
    return storm, casc


def build_image_lib(libs, cc, ar, third, again, workers):
    """libwebp's decoder and stb_image's PNG and TGA into libs/libimage.a, once (port_imagefile.cpp);
    the web build makes it with its own compilers."""
    from fetch_libwebp import WEBP_SOURCES
    from fetch_stb import STB_IMAGE_IMPL
    lib = os.path.join(libs, "libimage.a")
    if os.path.exists(lib) and not again:
        return lib
    if not os.path.exists(os.path.join(LIBWEBP, "src", "webp", "decode.h")):
        sys.exit("no libwebp in %s: run python tools/fetch_libwebp.py" % LIBWEBP)
    if not os.path.exists(os.path.join(STB, "stb_image.h")):
        sys.exit("no stb_image in %s: run python tools/fetch_stb.py" % STB)
    os.makedirs(libs, exist_ok=True)
    jobs = []
    for s in WEBP_SOURCES:
        obj = os.path.join(libs, "webp_" + os.path.basename(s)[:-2] + ".o")
        jobs.append(([cc] + third + ["-I" + LIBWEBP, "-o", obj, os.path.join(LIBWEBP, s)], obj))
    impl = os.path.join(libs, "stb_image_impl.c")
    with open(impl, "w", encoding="utf-8") as f:
        f.write(STB_IMAGE_IMPL)
    obj = os.path.join(libs, "stb_image.o")
    jobs.append(([cc] + third + ["-I" + STB, "-o", obj, impl], obj))
    compile_many(jobs, "libwebp's decoder and stb_image", workers)
    if os.path.exists(lib):
        os.remove(lib)
    run([ar, "rcs", lib] + [j[1] for j in jobs], "ar libimage")
    return lib


def build_dav1d_lib(libs, cc, ar, third, again, workers):
    """dav1d's AV1 decoder, plain C at both bit depths, into libs/libdav1d.a, again when
    tools/dav1d/ is newer (port_video.cpp)."""
    from fetch_dav1d import DAV1D_SOURCES, DAV1D_TMPL_SOURCES, DAV1D_INCLUDES
    lib = os.path.join(libs, "libdav1d.a")
    newest = max(os.path.getmtime(f) for f in glob.glob(os.path.join(DAV1D_CONFIG, "*")))
    if os.path.exists(lib) and not again and os.path.getmtime(lib) >= newest:
        return lib
    if not os.path.exists(os.path.join(DAV1D, "include", "dav1d", "dav1d.h")):
        sys.exit("no dav1d in %s: run python tools/fetch_dav1d.py" % DAV1D)
    os.makedirs(libs, exist_ok=True)
    flags = third + ["-std=c11", "-D_GNU_SOURCE", "-pthread", "-I" + DAV1D_CONFIG] +         ["-I" + os.path.join(DAV1D, d) for d in DAV1D_INCLUDES]
    jobs = []
    for prefix, extra, sources in (("dav1d_", [], DAV1D_SOURCES), ("dav1d8_", ["-DBITDEPTH=8"], DAV1D_TMPL_SOURCES),
                                   ("dav1d16_", ["-DBITDEPTH=16"], DAV1D_TMPL_SOURCES)):
        for s in sources:
            obj = os.path.join(libs, prefix + os.path.basename(s)[:-2] + ".o")
            jobs.append(([cc] + flags + extra + ["-o", obj, os.path.join(DAV1D, s)], obj))
    compile_many(jobs, "dav1d", workers)
    if os.path.exists(lib):
        os.remove(lib)
    run([ar, "rcs", lib] + [j[1] for j in jobs], "ar libdav1d")
    return lib


def webm_config_time():
    """When libvpx's configuration in tools/libvpx/, or a patch to stb, last changed."""
    return max(os.path.getmtime(f) for f in glob.glob(os.path.join(LIBVPX_CONFIG, "*"))
               + glob.glob(os.path.join(STB_PATCHES, "*.patch")))


def build_webm_lib(libs, cc, cxx, ar, third, again, workers):
    """libvpx's VP8 and VP9 decoders, libwebm's parser and stb_vorbis into libs/libwebm.a, again
    when tools/libvpx/ is newer; the web build makes it with its own compilers."""
    lib = os.path.join(libs, "libwebm.a")
    if os.path.exists(lib) and not again and os.path.getmtime(lib) >= webm_config_time():
        return lib
    if not os.path.exists(os.path.join(LIBVPX, "vpx", "vpx_decoder.h")):
        sys.exit("no libvpx in %s: run python tools/fetch_libvpx.py" % LIBVPX)
    if not os.path.exists(os.path.join(STB, "stb_vorbis.c")):
        sys.exit("no stb_vorbis in %s: run python tools/fetch_stb.py" % STB)
    third_party_patches.check(STB, STB_PATCHES, "fetch_stb.py")
    os.makedirs(libs, exist_ok=True)
    flags = third + ["-I" + LIBVPX_CONFIG, "-I" + LIBVPX]
    jobs = []
    sources = [s.strip() for s in open(os.path.join(LIBVPX_CONFIG, "sources.txt")) if s.strip()]
    for s in sources + ["vpx_config.c"]:
        src = os.path.join(LIBVPX_CONFIG if s == "vpx_config.c" else LIBVPX, s)
        obj = os.path.join(libs, "vpx_" + os.path.basename(s)[:-2] + ".o")
        jobs.append(([cc] + flags + ["-o", obj, src], obj))
    obj = os.path.join(libs, "webm_mkvparser.o")
    jobs.append(([cxx] + flags + ["-std=c++11", "-I" + LIBWEBM, "-o", obj,
                                  os.path.join(LIBWEBM, "mkvparser", "mkvparser.cc")], obj))
    obj = os.path.join(libs, "webm_stb_vorbis.o")
    jobs.append(([cc] + flags + ["-DSTB_VORBIS_NO_STDIO", "-o", obj, os.path.join(STB, "stb_vorbis.c")], obj))
    compile_many(jobs, "libvpx, libwebm's parser and stb_vorbis", workers)
    if os.path.exists(lib):
        os.remove(lib)
    run([ar, "rcs", lib] + [j[1] for j in jobs], "ar libwebm")
    return lib


def symbols(objs):
    """-> ({defined names}, {undefined name: [objects]}), from nm."""
    defined, undef = set(), {}
    r = subprocess.run([NM, "-P", "-A"] + objs, capture_output=True, text=True, check=True)
    for line in r.stdout.splitlines():
        head, _, rest = line.rpartition(": ")
        f = rest.split()
        if len(f) < 2:
            continue
        name, kind = f[0], f[1]
        if kind in ("U", "w", "v"):
            undef.setdefault(name, []).append(os.path.basename(head.split("[")[0].rstrip(":")))
        else:
            defined.add(name)
    return defined, undef


def demangle(names):
    if not names:
        return {}
    r = subprocess.run(CXXFILT, input="\n".join(names), capture_output=True, text=True, check=True)
    return dict(zip(names, r.stdout.splitlines()))


def bind_globals(objs, out_s):
    """The block's assembler file: every game global the objects name and none defines,
    set at its 1.17 address's offset in the block, or in the wide storage
    (abs_globals.py's write_obj, for ELF)."""
    import abs_globals as ag
    ag.set_x64()
    names, clash = ag.registry_names()
    if clash:
        sys.exit("[build_engine_linux] the registry names %s twice" % clash[0][0])
    defined, undef = symbols(objs)
    open_names = sorted(n for n in undef if n not in defined)
    plain = {n: c_name(n) for n in open_names}
    dem = demangle([plain[n] for n in open_names if plain[n].startswith("_Z")])
    bound = []
    for n in open_names:
        if plain[n] in (ag.BLOCK_NAME, ag.WIDE_NAME):
            continue
        va = names.get(dem.get(plain[n], plain[n]))
        if va is not None:
            bound.append((n, va))
    far = ag.outside_block(bound)
    if far:
        sys.exit("[build_engine_linux] outside 1.17's data: %s" % ", ".join("%s 0x%X" % f for f in far[:10]))
    wide_size, wide = ag.load_wide(os.path.join(SRC, "gen", "wide117.json"))
    block, own, inside = ag.place_wide(bound, wide)
    if inside:
        sys.exit("[build_engine_linux] inside a wide global: %s" % ", ".join(i[0] for i in inside[:10]))
    if MAC:
        lines = ["# written by tools/build_engine_linux.py: 1.17's data block and the wide storage"]
        lines += macho_block("__oc117", 12, "_" + ag.BLOCK_NAME, ag.BLOCK_HI - ag.BLOCK_LO,
                             [(n, va - ag.BLOCK_LO) for n, va in block])
        lines += macho_block("__ocwide", 4, "_" + ag.WIDE_NAME, max(wide_size, 16), own)
        open(out_s, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        say("bound %d game globals (%d in the wide storage)" % (len(bound), len(own)))
        return
    lines = ["# written by tools/build_engine_linux.py: 1.17's data block and the wide storage",
             '.section .oc117,"aw",@nobits', ".balign 4096", ".globl %s" % ag.BLOCK_NAME,
             ".type %s,@object" % ag.BLOCK_NAME, "%s:" % ag.BLOCK_NAME, ".zero 0x%X" % (ag.BLOCK_HI - ag.BLOCK_LO)]
    for n, va in block:
        lines += [".globl %s" % n, ".type %s,@object" % n, ".set %s, %s + 0x%X" % (n, ag.BLOCK_NAME, va - ag.BLOCK_LO)]
    lines += ['.section .ocwide,"aw",@nobits', ".balign 16", ".globl %s" % ag.WIDE_NAME,
              ".type %s,@object" % ag.WIDE_NAME, "%s:" % ag.WIDE_NAME, ".zero 0x%X" % max(wide_size, 16)]
    for n, off in own:
        lines += [".globl %s" % n, ".type %s,@object" % n, ".set %s, %s + 0x%X" % (n, ag.WIDE_NAME, off)]
    lines.append('.section .note.GNU-stack,"",@progbits')
    open(out_s, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    say("bound %d game globals (%d in the wide storage)" % (len(bound), len(own)))


def c_name(symbol):
    """A symbol's name as the C or C++ source spells it: Mach-O puts an underscore before it."""
    return symbol[1:] if MAC and symbol.startswith("_") else symbol


def macho_block(section, align, base, size, rows):
    """A zero-filled Mach-O section of size bytes, base its start and each (name, offset) row a
    label at its offset: Mach-O's form of the ELF block's .set names (no data in the file)."""
    lines = [".section __DATA,%s,zerofill" % section, ".p2align %d" % align, ".globl %s" % base, "%s:" % base]
    at = 0
    for name, off in sorted(rows, key=lambda r: r[1]):
        if off > at:
            lines.append(".space 0x%X" % (off - at))
            at = off
        lines += [".globl %s" % name, "%s:" % name]
    if size > at:
        lines.append(".space 0x%X" % (size - at))
    return lines


def version_block():
    """hyperion.rc's VS_VERSIONINFO root: the key and the fixed file information, which is
    all VerQueryValueA("\\") reads (port_posix.cpp)."""
    text = open(os.path.join(ENGINE, "hyperion", "version.h"), encoding="utf-8").read()
    fv = [int(x) for x in re.search(r"#define HYPERION_FILE_VERSION\s+([\d,]+)", text).group(1).split(",")]
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, (fv[0] << 16) | fv[1], (fv[2] << 16) | fv[3],
                        (1 << 16) | 17, (0 << 16) | 1, 0x3F, 0, 0x00040004, 1, 0, 0, 0)
    key = "VS_VERSION_INFO\0".encode("utf-16-le")
    head = 6 + len(key)
    pad = b"\0" * ((4 - head % 4) % 4)
    body = key + pad + fixed
    return struct.pack("<HHH", 6 + len(body), len(fixed), 0) + body


def write_resources():
    """resources_posix.S and .cpp: the table port_posix.cpp's FindResourceA reads for the
    program's own module."""
    s = ["# written by tools/build_engine_linux.py: Hyperion's own resources",
         ".section __TEXT,__const" if MAC else ".section .rodata", ".p2align 4"]
    under = "_" if MAC else ""
    rows = []
    for i, (name, file) in enumerate(FONT_RESOURCES):
        path = os.path.join(FONTS, file)
        s += [".globl %shx_res_%d" % (under, i), "%shx_res_%d:" % (under, i), '.incbin "%s"' % path, ".byte 0",
              ".p2align 4"]
        rows.append((10, '"%s"' % name, 0, "hx_res_%d" % i, os.path.getsize(path)))
    ver = version_block()
    if not MAC:
        s.append('.section .note.GNU-stack,"",@progbits')
    open(os.path.join(OUT, "resources_posix.S"), "w", encoding="utf-8").write("\n".join(s) + "\n")
    c = ["// written by tools/build_engine_linux.py: Hyperion's own resources (port_posix.cpp)",
         "struct PortPosixResource {", "  int nType;", "  const char *pszName;", "  int nName;",
         "  const unsigned char *pData;", "  unsigned cb;", "};"]
    c += ['extern "C" const unsigned char hx_res_%d[];' % i for i in range(len(FONT_RESOURCES))]
    c.append("static const unsigned char kVersion[] = {%s};" % ", ".join(str(b) for b in ver))
    c.append("extern const PortPosixResource g_aPortPosixResources[] = {")
    for t, n, num, data, cb in rows:
        c.append("  {%d, %s, %d, %s, %du}," % (t, n, num, data, cb))
    c.append("  {16, nullptr, 1, kVersion, %du}," % len(ver))
    c.append("};")
    c.append("extern const int g_nPortPosixResources = %d;" % (len(rows) + 1))
    open(os.path.join(OUT, "resources_posix.cpp"), "w", encoding="utf-8").write("\n".join(c) + "\n")


def build_webrtc(again, workers):
    """Mbed TLS (DTLS-SRTP on), libdatachannel against it, Opus, and OpenH264's encoder in C++ alone,
    as tools/build_webrtc.py builds them for Windows, into OUT/webrtc, once."""
    dest = os.path.join(OUT, "webrtc")
    lib = os.path.join(dest, "lib")
    if all(os.path.exists(os.path.join(lib, l)) for l in WEBRTC_LIBS) and not again:
        return dest
    ldc = os.path.join(WEBRTC_SRC, "libdatachannel-0.24.6")
    for d in (ldc, os.path.join(WEBRTC_SRC, "mbedtls-3.6.7"), os.path.join(WEBRTC_SRC, "opus-1.5.2"),
              os.path.join(WEBRTC_SRC, "openh264-2.6.0"), os.path.join(WEBRTC_SRC, "nv-codec-headers-13.1.15.0")):
        if not os.path.isdir(d):
            sys.exit("[build_engine_linux] no %s: run python tools/fetch_webrtc.py" % d)
    work = os.path.join(dest, "work")
    flags = "-fsigned-char -DMBEDTLS_SSL_DTLS_SRTP"
    for name, src, defs in (
            ("mbedtls", os.path.join(WEBRTC_SRC, "mbedtls-3.6.7"),
             ["-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DMBEDTLS_FATAL_WARNINGS=OFF"]),
            ("libdatachannel", ldc, ["-DUSE_MBEDTLS=ON", "-DNO_WEBSOCKET=ON", "-DNO_EXAMPLES=ON", "-DNO_TESTS=ON"]),
            ("opus", os.path.join(WEBRTC_SRC, "opus-1.5.2"),
             ["-DOPUS_BUILD_PROGRAMS=OFF", "-DOPUS_BUILD_TESTING=OFF", "-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF"])):
        build = os.path.join(work, name)
        shutil.rmtree(build, ignore_errors=True)
        run(["cmake", "-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
             "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_INSTALL_PREFIX=" + dest, "-DCMAKE_PREFIX_PATH=" + dest,
             "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
             "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags] + CMAKE_MAC + defs, "cmake " + name)
        run(["ninja", "-C", build], "ninja " + name)
        run(["cmake", "--install", build], "install " + name)
    # OpenH264's encoder, its common and processing parts, without their assembly
    c = os.path.join(WEBRTC_SRC, "openh264-2.6.0", "codec")
    h264 = os.path.join(work, "openh264")
    os.makedirs(h264, exist_ok=True)
    incs = ["api/wels", "common/inc", "encoder/core/inc", "encoder/plus/inc", "processing/interface",
            "processing/src/common", "processing/src/adaptivequantization", "processing/src/downsample",
            "processing/src/scrolldetection", "processing/src/vaacalc"]
    jobs = []
    for d in ("common/src", "encoder/core/src", "encoder/plus/src", "processing/src/*"):
        for src in sorted(glob.glob(os.path.join(c, d, "*.cpp"))):
            if os.path.basename(src) == "DllEntry.cpp":
                continue
            obj = os.path.join(h264, os.path.relpath(src, c).replace("/", "_").replace(".cpp", ".o"))
            jobs.append((["clang++", "-std=c++11"] + THIRD + ["-I" + os.path.join(c, i) for i in incs]
                         + ["-o", obj, src], obj))
    compile_many(jobs, "OpenH264", workers)
    out = os.path.join(lib, "libopenh264.a")
    if os.path.exists(out):
        os.remove(out)
    run([AR, "rcs", out] + [j[1] for j in jobs], "ar OpenH264")
    inc = os.path.join(dest, "include", "wels")
    os.makedirs(inc, exist_ok=True)
    for hdr in glob.glob(os.path.join(c, "api", "wels", "*.h")):
        shutil.copy2(hdr, inc)
    shutil.copytree(os.path.join(WEBRTC_SRC, "nv-codec-headers-13.1.15.0", "include", "ffnvcodec"),
                    os.path.join(dest, "include", "ffnvcodec"), dirs_exist_ok=True)
    missing = [l for l in WEBRTC_LIBS if not os.path.exists(os.path.join(lib, l))]
    if missing:
        sys.exit("[build_engine_linux] the stream's libraries are missing %s (in lib/: %s)"
                 % (", ".join(missing), ", ".join(sorted(os.listdir(lib)))))
    return dest


def build(a):
    workers = a.jobs or os.cpu_count() or 4
    os.makedirs(OUT, exist_ok=True)
    for d in (CLAY, STB, FONTS, SDL_SRC, SL, CASCLIB, DRLIBS):
        if not os.path.isdir(d):
            sys.exit("[build_engine_linux] no %s (the tools/fetch_*.py scripts fetch it)" % d)
    import fetch_clay  # Hyperion's change to Clay, made once
    fetch_clay.patch()
    import fetch_fonts  # the Noto cuts, made again when the catalogs need a character they lack
    fetch_fonts.cut()
    if not os.path.exists(os.path.join(SHADERS, "VSMain.spv.h")):
        sys.exit("[build_engine_linux] no shaders in %s: build for Windows once first" % SHADERS)
    for f in os.listdir(OUT):
        if f.endswith(".o"):
            os.remove(os.path.join(OUT, f))
    build_sdl(a.sdl)
    storm, casc = build_libs(a.libs, workers)
    webm = build_webm_lib(os.path.join(OUT, "libs"), "clang", "clang++", AR, THIRD, a.libs, workers)
    image = build_image_lib(os.path.join(OUT, "libs"), "clang", AR, THIRD, a.libs, workers)
    dav1d = build_dav1d_lib(os.path.join(OUT, "libs"), "clang", AR, THIRD, a.libs, workers)
    webrtc = build_webrtc(a.webrtc, workers)

    jobs = []
    for t in tus("lang"):
        src = os.path.join(SRC, "lang", t + ".cpp")
        jobs.append((["clang++"] + LANG + ["-o", obj_of(src), src], obj_of(src)))
    for g in ("code_map", "data117"):
        src = os.path.join(SRC, "gen", g + ".cpp")
        jobs.append((["clang++"] + GEN + ["-o", obj_of(src), src], obj_of(src)))
    ports = tus("port")
    for p in ports:
        src = tu_path("port", p)
        jobs.append((["clang++"] + PORT + ["-o", obj_of(src), src], obj_of(src)))
    for p in ("implode", "explode", "crc32"):
        # long as int, after the C library's headers: pklib's unsigned long is 32 bits, as
        # on Windows (port_pklib.cpp)
        src = os.path.join(OUT, "pk_%s.c" % p)
        open(src, "w", encoding="utf-8").write(
            "#include <assert.h>\n#include <limits.h>\n#include <string.h>\n#define long int\n"
            '#include "%s"\n' % os.path.join(PKLIB, p + ".c"))
        obj = os.path.join(OUT, "pk_%s.o" % p)
        jobs.append((["clang"] + THIRD + ["-Dimplode=PkImplode", "-Dexplode=PkExplode", "-Dcrc32_pklib=PkCrc32",
                                          "-o", obj, src], obj))
    src = port_path("port_pklib.cpp")
    jobs.append((["clang++", "-std=c++17"] + COMMON + ["-I" + PKLIB, "-o", obj_of(src), src], obj_of(src)))
    src = port_path("port_stormlib.cpp")
    jobs.append((["clang++", "-std=c++17"] + COMMON + ["-o", obj_of(src), src], obj_of(src)))
    src = port_path("port_clay.cpp")
    jobs.append((["clang++", "-std=c++20"] + THIRD + ["-I" + CLAY, "-o", obj_of(src), src], obj_of(src)))
    # the streamed seats: libdatachannel's C++, which it alone uses
    src = port_path("port_lanstream.cpp")
    jobs.append((["clang++"] + PORT + ["-I" + os.path.join(webrtc, "include"), "-o", obj_of(src), src], obj_of(src)))
    src = os.path.join(ENGINE, "hyperion", "hyperion.cpp")
    jobs.append((["clang++", "-std=c++17"] + COMMON + ["-o", obj_of(src), src], obj_of(src)))
    # the replay harness's observer, in a folder of its own: it is not part of the program
    obs_dir = os.path.join(OUT, "observer")
    os.makedirs(obs_dir, exist_ok=True)
    obs_obj = os.path.join(obs_dir, "rt_observer.o")
    src = os.path.join(ENGINE, "tools", "replaytest", "rt_observer.cpp")
    jobs.append((["clang++", "-std=c++17"] + COMMON + ["-fPIC", "-fno-exceptions", "-fno-rtti", "-o", obs_obj, src],
                 obs_obj))
    write_resources()
    jobs.append((["clang++", "-std=c++17", "-c"] + MAC_MIN + ["-o", os.path.join(OUT, "resources_posix.o"),
                  os.path.join(OUT, "resources_posix.cpp")], os.path.join(OUT, "resources_posix.o")))
    jobs.append((["clang", "-c"] + MAC_MIN + ["-o", os.path.join(OUT, "resources_s.o"),
                  os.path.join(OUT, "resources_posix.S")],
                 os.path.join(OUT, "resources_s.o")))
    compile_many(jobs, "clang -c", workers)

    objs = sorted(os.path.join(OUT, f) for f in os.listdir(OUT) if f.endswith(".o"))
    game = [o for o in objs if os.path.basename(o) not in NOT_GAME + ["resources_s.o"]]
    abs_s = os.path.join(OUT, "abs_globals.S")
    bind_globals(game, abs_s)
    run(["clang", "-c"] + MAC_MIN + ["-o", os.path.join(OUT, "abs_globals.o"), abs_s], "as abs_globals.S")
    objs.append(os.path.join(OUT, "abs_globals.o"))
    # hyperion.def's exports, and the observer's hooks (port_posix.cpp), for dlsym
    exports = [ln.strip() for ln in open(os.path.join(ENGINE, "hyperion", "hyperion.def"), encoding="utf-8")
               if ln.strip() and ln.strip() != "EXPORTS"] + ["oc_posix_hook"]
    # and the Windows calls the observer makes, which are Hyperion's (port_posix.cpp): only
    # C names, so no part of the C++ library is exported
    defined, _ = symbols(objs)
    _, obs_undef = symbols([obs_obj])
    for_observer = sorted(c_name(n) for n in obs_undef if n in defined and not c_name(n).startswith("_Z"))
    exports += for_observer
    libs = [storm, casc, webm, image, dav1d] + [os.path.join(webrtc, "lib", l) for l in WEBRTC_LIBS]
    if MAC:
        link_mac(objs, libs, exports, obs_obj, len(for_observer))
        return
    # The C++ library linked in, as /MT links it on Windows, and none of it exported: the
    # game's operator new and delete are Storm's (Storm.cpp), and so only the program's
    # own, not those of the libraries it loads (Mesa's, which are C++ too).
    run(["clang++", "-fuse-ld=lld", "-g", "-static-libstdc++", "-static-libgcc", "-Wl,--exclude-libs,ALL",
         "-o", os.path.join(OUT, PROGRAM[ARCH])] + objs
        + libs[:2] + ["-Wl,--start-group"] + libs[2:] + ["-Wl,--end-group"]
        + [os.path.join(OUT, SDL_LIB), "-Wl,-rpath,$ORIGIN", "-Wl,--no-undefined", "-Wl,--error-limit=0"]
        # the id a crash report names, by which tools/crash_symbols.py finds this build's symbols
        + ["-Wl,--build-id"]
        + ["-Wl,--export-dynamic-symbol=" + e for e in exports]
        # the C library's calls that take a path, which port_posix.cpp translates
        + ["-Wl,--wrap=fopen", "-Wl,--wrap=remove"]
        + ["-lpthread", "-ldl", "-lm", "-lrt"], "link Hyperion")
    run(["clang++", "-shared", "-fuse-ld=lld", "-g", "-static-libstdc++", "-static-libgcc", "-Wl,--exclude-libs,ALL",
         "-o", os.path.join(OUT, "rt_observer.so"), obs_obj], "link rt_observer.so (%d calls from Hyperion)"
        % len(for_observer))
    copy_web()
    say("OK -> %s" % os.path.relpath(os.path.join(OUT, PROGRAM[ARCH]), ENGINE))
    if a.check_align:
        check_align(workers)


def link_mac(objs, libs, exports, obs_obj, n_observer):
    """The Mac's link: Apple's linker, the system's libc++, and only the listed names exported, so
    the game's operator new and delete (Storm.cpp) stay the program's own and dyld cannot give them
    to the libraries it loads."""
    listed = os.path.join(OUT, "exports.txt")
    open(listed, "w", encoding="utf-8").write("".join("_%s\n" % e for e in exports))
    program = os.path.join(OUT, MAC_PROGRAM)
    run(["clang++", "-g"] + MAC_MIN + ["-o", program] + objs + libs
        + [os.path.join(OUT, SDL_LIB), "-Wl,-rpath,@executable_path", "-Wl,-exported_symbols_list," + listed]
        # NSProcessInfo's activity, through the Objective-C runtime (port_posix.cpp, PortPosixKeepRunning)
        + ["-lobjc", "-framework", "Foundation"]
        # H.264, H.265 and AAC (port_sysvideo_mac.cpp)
        + [w for f in ("VideoToolbox", "CoreMedia", "CoreVideo", "AudioToolbox", "CoreFoundation")
           for w in ("-framework", f)],
        "link Hyperion")
    # the debug info gathered from the objects, for naming a crash report's frames elsewhere
    # (tools/package.py keeps it, tools/crash_symbols.py reads it)
    if subprocess.run(["dsymutil", program, "-o", program + ".dSYM"], cwd=OUT).returncode:
        say("dsymutil failed: no %s.dSYM, so a crash report's frames are named only here" % MAC_PROGRAM)
    # the observer's calls into Hyperion are found when Hyperion loads it
    run(["clang++", "-dynamiclib", "-g"] + MAC_MIN + ["-undefined", "dynamic_lookup",
         "-o", os.path.join(OUT, "rt_observer.dylib"), obs_obj],
        "link rt_observer.dylib (%d calls from Hyperion)" % n_observer)
    copy_web()
    say("OK -> %s" % os.path.relpath(program, ENGINE))


def copy_web():
    """The page a LAN room serves (engine/web/lan), beside the program in web/lan, and
    Hyperion for the browser in web/play, with the program tools/build_engine_web.py built (W3)."""
    if os.path.isdir(os.path.join(ENGINE, "web")):
        shutil.copytree(os.path.join(ENGINE, "web"), os.path.join(OUT, "web"), dirs_exist_ok=True)
    for name in ("hyperion.js", "hyperion.wasm"):
        built = os.path.join(ENGINE, "src117", "build", "web", "play", name)
        if os.path.isfile(built):
            shutil.copy2(built, os.path.join(OUT, "web", "play", name))


def check_align(workers):
    """Every TU's LLVM IR, for a game global whose declaration claims more alignment than its
    place in the block or the wide storage has (h/platform.h's OC_UNALIGNED says why)."""
    place = {}
    for line in open(os.path.join(OUT, "abs_globals.S"), encoding="utf-8"):
        m = re.match(r"\.set (\S+), (\w+) \+ 0x([0-9A-F]+)", line)
        if m:
            place[m.group(1)] = (m.group(2), int(m.group(3), 16))
    jobs = [(LANG, os.path.join(SRC, "lang", t + ".cpp")) for t in tus("lang")]
    jobs.append((GEN, os.path.join(SRC, "gen", "code_map.cpp")))
    jobs.append((GEN, os.path.join(SRC, "gen", "data117.cpp")))
    jobs += [(PORT, tu_path("port", t)) for t in tus("port")]
    decl = re.compile(r"^@(\"?[^ ]+\"?) = external (?:\w+ )*?(?:global|constant) .*, align (\d+)", re.M)

    def one(job):
        flags, src = job
        r = subprocess.run(["clang++"] + [f for f in flags if f not in ("-c", "-g")] + ["-S", "-emit-llvm", "-o", "-", src],
                           capture_output=True, text=True, cwd=OUT)
        return [(m.group(1).strip('"'), int(m.group(2)), os.path.basename(src)) for m in decl.finditer(r.stdout)
                if m.group(1).strip('"') in place and place[m.group(1).strip('"')][1] % int(m.group(2))]

    say("check the globals' alignment (%d TUs)" % len(jobs))
    bad = {}
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for hits in pool.map(one, jobs):
            for name, align, src in hits:
                bad.setdefault(name, (align, set()))[1].add(src)
    for name, (align, srcs) in sorted(bad.items()):
        print("  %s at %s+0x%X, declared %d-aligned in %s" % (name, place[name][0], place[name][1], align,
                                                              ", ".join(sorted(srcs))))
    if bad:
        sys.exit("[build_engine_linux] %d globals need OC_UNALIGNED" % len(bad))
    say("every global's declaration is as aligned as its place, or less")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sdl", action="store_true", help="build SDL3 again")
    ap.add_argument("--libs", action="store_true", help="build StormLib and CascLib again")
    ap.add_argument("--webrtc", action="store_true", help="build the streamed seats' libraries again")
    ap.add_argument("-j", "--jobs", type=int, default=0)
    ap.add_argument("--check-align", action="store_true",
                    help="after the build, list the game globals declared more aligned than their place")
    ap.add_argument("--image", default=None, help="the Docker image to build in (on Windows)")
    ap.add_argument("--arch", choices=["x64", "arm64"], default="x64", help="the machine to build for (on Windows)")
    ap.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if sys.platform.startswith("linux") or MAC:
        if not a.inside:
            write_build_id()
        return build(a)
    # elsewhere: the build_id here, where git has the repository, then the build in the image
    global OUT
    OUT = os.path.join(SRC, "build", "linux-" + a.arch)
    write_build_id()
    image = a.image or (IMAGE if a.arch == "x64" else IMAGE + "-arm64")
    cmd = ["docker", "run", "--rm", "--platform", "linux/amd64" if a.arch == "x64" else "linux/arm64",
           "-v", "%s:/src" % REPO.replace("\\", "/"), image, "python3",
           "/src/engine/tools/build_engine_linux.py", "--inside"]
    cmd += (["--sdl"] if a.sdl else []) + (["--libs"] if a.libs else []) + (["--webrtc"] if a.webrtc else [])
    cmd += ["-j", str(a.jobs)] if a.jobs else []
    cmd += ["--check-align"] if a.check_align else []
    say("in the %s image" % image)
    sys.exit(subprocess.run(cmd).returncode)


if __name__ == "__main__":
    main()
