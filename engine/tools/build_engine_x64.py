"""Build Hyperion for x64 with VS 2026.

    python tools/build_engine_x64.py [--no-stormlib] [--casclib] [--no-install]

Writes src117/build/x64/: Hyperion.exe, the engine and its start-up in one exe, and
the replay observer, rt_observer.dll. A build that succeeds is then installed, as the Windows
package's files, into each folder engine/install_to.txt names (tools/package.py --install),
unless --no-install is given. The game's C++ (src117/lang) and the port's
(src117/port) are the TUs src117/tus.txt lists, which the tools that probe the build
read too; the game's globals are bound to the engine's block of 1.17's data by
tools/wave117/abs_globals.py --x64; src117/gen/code_map.cpp maps the code addresses
1.17's data holds to the engine's functions; hyperion/hyperion.cpp is the start. The front
end (docs/front-end.md) brings in Clay (src117/port/front/port_clay.cpp), stb_truetype, CascLib,
built once into its own library (--casclib builds it again), and its typefaces, Saira and
Michroma (tools/fetch_fonts.py), which go into the exe as resources.
"""
import argparse
import ctypes
import glob
import os
import re
import shutil
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, "tools"))
import coff  # noqa: E402
import third_party_patches  # noqa: E402
import shader_layout  # noqa: E402
import shader_msl  # noqa: E402
from port_files import port_path, tu_path, SHADERS as SHADER_SRC  # noqa: E402

SRC = os.path.join(ENGINE, "src117")
OUT = os.path.join(SRC, "build", "x64")
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
SDL3 = os.path.join(ENGINE, "third_party", "SDL3-3.4.16")
PKLIB = os.path.join(ENGINE, "third_party", "StormLib-9.40", "src", "pklib")
DXC = os.path.join(ENGINE, "third_party", "dxc-1.9.2607", "bin", "x64", "dxc.exe")
CLAY = os.path.join(ENGINE, "third_party", "clay-0.14")
STB = os.path.join(ENGINE, "third_party", "stb-6e9f34d")
STB_PATCHES = os.path.join(ENGINE, "tools", "stb-patches")
CASCLIB = os.path.join(ENGINE, "third_party", "CascLib-3.0")
# WebM video, a Remastered install's: libvpx's VP9 decoder and libwebm's parser
# (tools/fetch_libvpx.py, its configured headers in tools/libvpx/), with stb_vorbis
LIBVPX = os.path.join(ENGINE, "third_party", "libvpx-1.17.0")
LIBVPX_CONFIG = os.path.join(ENGINE, "tools", "libvpx")
LIBWEBM = os.path.join(LIBVPX, "third_party", "libwebm")
FONTS = os.path.join(ENGINE, "third_party", "fonts-8b0a1d0")
# the MP3, FLAC and WAV decoders for a mod's sounds and music (port_audiodecode.cpp; tools/fetch_drlibs.py)
DRLIBS = os.path.join(ENGINE, "third_party", "dr_libs-dfe8377")
# a mod's art in PNG, TGA (stb_image) and WebP (libwebp's decoder; port_imagefile.cpp,
# tools/fetch_libwebp.py)
LIBWEBP = os.path.join(ENGINE, "third_party", "libwebp-1.5.0")
# a mod's AV1 videos: dav1d in plain C (tools/fetch_dav1d.py, its configuration in tools/dav1d/)
DAV1D = os.path.join(ENGINE, "third_party", "dav1d-1.5.4")
DAV1D_CONFIG = os.path.join(ENGINE, "tools", "dav1d")
# The front end's typefaces (tools/fetch_fonts.py, OFL), carried in Hyperion.exe as resources
# under these names (port_ui.cpp).
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
SHADERS = os.path.join(OUT, "shaders")
# The GPU presenter's shaders (src117/port/render/shaders/, docs/renderer.md, R4): each entry
# point to DXIL and to SPIR-V, as a header of bytes port_gpu.cpp includes.
SHADER_ENTRIES = [("present.hlsl", "VSMain", "vs_6_0"), ("present.hlsl", "PSResolve", "ps_6_0"),
                  ("present.hlsl", "PSCompose", "ps_6_0"), ("present.hlsl", "PSScale", "ps_6_0"),
                  ("scene.hlsl", "CSScene", "cs_6_0"), ("scene.hlsl", "CSDistort", "cs_6_0"),
                  ("scene.hlsl", "CSShow", "cs_6_0"), ("scene.hlsl", "CSSceneFull", "cs_6_0"),
                  ("scene.hlsl", "CSDistortFull", "cs_6_0"), ("scene.hlsl", "CSShowFull", "cs_6_0"),
                  ("present.hlsl", "PSComposeFull", "ps_6_0"), ("present.hlsl", "PSGround", "ps_6_0"), ("scene.hlsl", "CSHud", "cs_6_0"),
                  ("ui.hlsl", "VSUi", "vs_6_0"), ("ui.hlsl", "PSUi", "ps_6_0"),
                  ("ui.hlsl", "VSFull", "vs_6_0"), ("ui.hlsl", "PSPlanet", "ps_6_0"),
                  ("effects.hlsl", "PSBright", "ps_6_0"), ("effects.hlsl", "PSDown", "ps_6_0"),
                  ("effects.hlsl", "PSUp", "ps_6_0"), ("effects.hlsl", "PSPost", "ps_6_0"),
                  ("effects.hlsl", "PSLights", "ps_6_0"),
                  ("hd.hlsl", "VSHdQuad", "vs_6_0"), ("hd.hlsl", "PSHdSprite", "ps_6_0"),
                  ("hd.hlsl", "PSHdBase", "ps_6_0"), ("hd.hlsl", "VSHdFull", "vs_6_0"),
                  ("hd.hlsl", "PSHdBlend", "ps_6_0"), ("hd.hlsl", "PSHdConsole", "ps_6_0"),
                  ("hd.hlsl", "PSHdUi", "ps_6_0"), ("hd.hlsl", "VSHdBatch", "vs_6_0"),
                  ("hd.hlsl", "PSHdBatch", "ps_6_0"), ("fog.hlsl", "CSFogLight", "cs_6_0"),
                  ("hud.hlsl", "VSHudQuad", "vs_6_0"), ("hud.hlsl", "VSHudPlace", "vs_6_0"),
                  ("hud.hlsl", "PSHudPlaced", "ps_6_0"), ("hud.hlsl", "PSHudFx", "ps_6_0"),
                  ("hud.hlsl", "PSHudBlur", "ps_6_0"), ("hud.hlsl", "PSHudDown", "ps_6_0"),
                  ("hud.hlsl", "PSHudUp", "ps_6_0"), ("hudshape.hlsl", "PSHudShape", "ps_6_0"),
                  ("hudshape.hlsl", "PSHudShadow", "ps_6_0"), ("video.hlsl", "PSVideoColour", "ps_6_0"),
                  ("video.hlsl", "PSVideoScale", "ps_6_0")]

# The game's C++ as it was compiled for x86, less /Zi (/Z7 here: each object
# carries its own debug information, which /MP needs), /Gr and /arch:SSE2.
COMMON = ["/nologo", "/c", "/Z7", "/W3", "/WX-", "/Ox", "/Oi", "/Oy-", "/DWIN32",
          "/D_CRT_SECURE_NO_WARNINGS", "/DHIGHFIRST", "/DNDEBUG", "/D_WINDOWS", "/GF", "/Gm-",
          "/MT", "/GS-", "/Gy", "/fp:precise", "/Zc:wchar_t", "/Zc:forScope",
          "/wd4018", "/wd4244", "/wd4731", "/wd4996", "/wd4146", "/wd4800"]
LANG = COMMON + ["/TP", "/DOC_PAYLOAD", "/DOC_WITH_HELD", "/I" + os.path.join(SRC, "h")]
GEN = COMMON + ["/TP", "/DOC_DLL_MODE", "/I" + os.path.join(SRC, "h")]
# The port's own TUs name game globals as externs, bound by abs_globals.py --bind-only
#. They use the standard library, so they unwind its
# exceptions (/EHsc); the game's TUs keep the original flags.
PORT = COMMON + ["/TP", "/EHsc", "/DOC_WITH_HELD", "/I" + os.path.join(SRC, "h"), "/I" + OUT,
                 "/I" + os.path.join(SDL3, "include"), "/I" + SHADERS,
                 "/I" + CLAY, "/I" + STB, "/I" + os.path.join(CASCLIB, "src"),
                 "/DCASCLIB_NO_AUTO_LINK_LIBRARY", "/I" + LIBVPX, "/I" + LIBVPX_CONFIG, "/I" + LIBWEBM,
                 "/I" + DRLIBS, "/I" + os.path.join(LIBWEBP, "src"), "/I" + os.path.join(DAV1D, "include"),
                 # libopus's headers, the streamed seats' build of it, for the Opus decoder
                 # (port_audiodecode.cpp)
                 "/I" + os.path.join(OUT, "webrtc", "include")]

# The objects abs_globals.py leaves to the link.
NOT_GAME = ["code_map.obj", "data117.obj", "port_pklib.obj", "pk_implode.obj", "pk_explode.obj", "pk_crc32.obj",
            "port_stormlib.obj", "port_clay.obj", "port_lanstream.obj"]
# A LAN room's streamed seats (port_lanstream.cpp): WebRTC, H.264 and Opus, built by
# tools/build_webrtc.py into WEBRTC, and the Windows libraries they call.
WEBRTC = os.path.join(OUT, "webrtc")
WEBRTC_LIBS = ["datachannel.lib", "juice.lib", "srtp2.lib", "usrsctp.lib", "mbedtls.lib", "mbedx509.lib",
               "mbedcrypto.lib", "everest.lib", "p256m.lib", "opus.lib", "openh264.lib"]
LIBS = ["user32.lib", "shell32.lib", "imm32.lib", "gdi32.lib", "advapi32.lib",
        "version.lib", "crypt32.lib", "wintrust.lib", "winmm.lib",
        "ws2_32.lib",  # the live channel (port_control.cpp)
        "ole32.lib",  # the first start's folder dialog (port_install.cpp)
        "mfuuid.lib",  # Media Foundation's names, for H.264, H.265 and AAC (port_sysvideo_win.cpp)
        # the Universal CRT's stdio functions are inline in its headers; the game's TUs
        # that declare them themselves (statwrite.cpp's sscanf_s) take them from here
        "legacy_stdio_definitions.lib"]


def vs_env():
    out = subprocess.run('cmd /s /c ""%s" amd64 >nul && set"' % VCVARS,
                         capture_output=True, text=True, check=True).stdout
    env = {k.upper(): v for k, v in os.environ.items()}
    for line in out.splitlines():
        k, eq, v = line.partition("=")
        if eq:
            env[k.upper()] = v
    return env


def write_build_id():
    """build_id.h in the build's folder: the commit and branch the build is made from, with a
    + when src117 has changes not committed (what a benchmark's run records, port_bench.cpp),
    and the build's number, the commits on its branch (Settings shows it by the version,
    docs/versioning.md). release_notes.h beside it carries engine/notes/release-notes.json."""
    def git(*args):
        r = subprocess.run(["git"] + list(args), capture_output=True, text=True, cwd=SRC)
        return r.stdout.strip() if r.returncode == 0 else ""
    commit = git("rev-parse", "--short", "HEAD")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = git("status", "--porcelain", "--untracked-files=no", "--", ".")
    number = git("rev-list", "--count", "HEAD")
    with open(os.path.join(OUT, "build_id.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// build_id.h -- written by tools/build_engine_x64.py for each build.\n#pragma once\n")
        f.write('#define HYPERION_COMMIT "%s%s"\n' % (commit, "+" if commit and dirty else ""))
        f.write('#define HYPERION_BRANCH "%s"\n' % branch)
        f.write('#define HYPERION_BUILD_NUMBER %s\n' % (number or "0"))
    subprocess.run([sys.executable, os.path.join(ENGINE, "tools", "release_notes.py"), "header",
                    os.path.join(OUT, "release_notes.h")], check=True)
    subprocess.run([sys.executable, os.path.join(ENGINE, "tools", "locale_catalog.py"), "header",
                    os.path.join(OUT, "locale_catalogs.h")], check=True)


def run(cmd, env, what):
    print("[build_engine_x64]", what, flush=True)
    exe = shutil.which(cmd[0], path=env["PATH"]) or cmd[0]
    r = subprocess.run([exe] + cmd[1:], env=env, cwd=OUT)
    if r.returncode:
        sys.exit("[build_engine_x64] %s FAILED" % what)


TUS = os.path.join(SRC, "tus.txt")


def tus(group):
    """The TUs of a group of src117/tus.txt, lang or port, in its order."""
    out = []
    for line in open(TUS, encoding="utf-8"):
        f = line.split()
        if f and not f[0].startswith("#") and f[0] == group:
            out.append(f[1])
    if not out:
        sys.exit("[build_engine_x64] no %s TUs in %s" % (group, TUS))
    return out


_dbghelp = ctypes.WinDLL("dbghelp")


def undecorate_full(dec):
    buf = ctypes.create_string_buffer(8192)
    n = _dbghelp.UnDecorateSymbolName(dec.encode("ascii", "replace"), buf, 8192, 0)
    return buf.value.decode("ascii", "replace") if n else dec


def signature(dec):
    """A decorated name's signature with what differs between x86 and x64 taken out:
    the calling convention and __ptr64 (tools/code_map.py)."""
    s = undecorate_full(dec)
    s = re.sub(r"\b(__thiscall|__cdecl|__stdcall|__fastcall|__ptr64)\b", " ", s)
    s = re.sub(r"\s*([,*&()<>])\s*", r"\1", s)
    return re.sub(r"\s+", " ", s).strip()


def symbol_tables(objs):
    """-> ({decorated: object} defined functions, {decorated: [objects]} undefined)."""
    defined, undef = {}, {}
    for o in objs:
        for sym in coff.ObjFile(o).symbols:
            if sym is None or sym["class"] != 2:
                continue
            if sym["sec"] > 0:
                defined.setdefault(sym["name"], os.path.basename(o))
            elif sym["sec"] == 0 and sym["value"] == 0:
                undef.setdefault(sym["name"], []).append(os.path.basename(o))
    return defined, undef


# CascLib's names that StormLib or the C runtime has too, given a prefix of their own: its
# zlib (Z_PREFIX) and Bob Jenkins' hashes, which StormLib carries as well.
CASCLIB_DEFS = ["/DZ_PREFIX", "/Dhashlittle=CascHashLittle", "/Dhashlittle2=CascHashLittle2",
                "/Dhashword=CascHashWord", "/Dhashword2=CascHashWord2", "/Dhashbig=CascHashBig",
                "/Dz_errmsg=casc_z_errmsg"]


def build_casclib(env):
    """CascLib's two unity files into build/x64/casclib/casclib.lib (MIT, docs/front-end.md)."""
    out = os.path.join(OUT, "casclib")
    lib = os.path.join(out, "casclib.lib")
    if not os.path.exists(os.path.join(CASCLIB, "src", "CascLib.h")):
        sys.exit("[build_engine_x64] no CascLib in %s: run python tools/fetch_casclib.py" % CASCLIB)
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.obj")):
        os.remove(f)
    flags = ["cl", "/nologo", "/c", "/MT", "/O2", "/GS-", "/Gy", "/W0", "/Z7", "/DWIN32", "/DNDEBUG", "/D_LIB",
             "/D__CASCLIB_SELF__", "/D_CRT_SECURE_NO_DEPRECATE", "/D_CRT_NONSTDC_NO_DEPRECATE"] + CASCLIB_DEFS
    run(flags + ["/TP", "/EHsc", "/Fo" + os.path.join(out, "casc_cpp.obj"), os.path.join(CASCLIB, "sources-cpp.cpp")],
        env, "cl /c CascLib (C++)")
    run(flags + ["/TC", "/Fo" + os.path.join(out, "casc_c.obj"), os.path.join(CASCLIB, "sources-c.c")],
        env, "cl /c CascLib (C)")
    run(["lib", "/nologo", "/OUT:" + lib, os.path.join(out, "casc_cpp.obj"), os.path.join(out, "casc_c.obj")],
        env, "lib casclib.lib")
    return lib


def build_webm(env):
    """libvpx's VP8 and VP9 decoders, libwebm's parser and stb_vorbis into build/x64/webm/webm.lib
    (BSD and public domain; port_webm.cpp)."""
    out = os.path.join(OUT, "webm")
    lib = os.path.join(out, "webm.lib")
    if not os.path.exists(os.path.join(LIBVPX, "vpx", "vpx_decoder.h")):
        sys.exit("[build_engine_x64] no libvpx in %s: run python tools/fetch_libvpx.py" % LIBVPX)
    if not os.path.exists(os.path.join(STB, "stb_vorbis.c")):
        sys.exit("[build_engine_x64] no stb_vorbis in %s: run python tools/fetch_stb.py" % STB)
    third_party_patches.check(STB, STB_PATCHES, "fetch_stb.py")
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.obj")):
        os.remove(f)
    flags = ["cl", "/nologo", "/c", "/MT", "/O2", "/GS-", "/Gy", "/W0", "/Z7", "/DWIN32", "/DNDEBUG", "/D_LIB",
             "/D_CRT_SECURE_NO_WARNINGS", "/I" + LIBVPX_CONFIG, "/I" + LIBVPX]
    sources = [os.path.join(LIBVPX, s.strip()) for s in open(os.path.join(LIBVPX_CONFIG, "sources.txt"))
               if s.strip()] + [os.path.join(LIBVPX_CONFIG, "vpx_config.c")]
    # no two of libvpx's files have the same name, so the objects share one folder
    run(flags + ["/TC", "/MP", "/Fo" + out + os.sep] + sources, env, "cl /c %d libvpx files" % len(sources))
    objs = [os.path.join(out, os.path.splitext(os.path.basename(s))[0] + ".obj") for s in sources]
    obj = os.path.join(out, "mkvparser.obj")
    run(flags + ["/TP", "/EHsc", "/I" + LIBWEBM, "/Fo" + obj, os.path.join(LIBWEBM, "mkvparser", "mkvparser.cc")],
        env, "cl /c mkvparser.cc")
    objs.append(obj)
    obj = os.path.join(out, "stb_vorbis.obj")
    run(flags + ["/TC", "/DSTB_VORBIS_NO_STDIO", "/Fo" + obj, os.path.join(STB, "stb_vorbis.c")],
        env, "cl /c stb_vorbis.c")
    objs.append(obj)
    run(["lib", "/nologo", "/OUT:" + lib] + objs, env, "lib webm.lib")
    return lib


def build_dav1d(env):
    """dav1d's AV1 decoder, plain C at both bit depths, into build/x64/dav1d/dav1d.lib (BSD;
    port_video.cpp)."""
    from fetch_dav1d import DAV1D_SOURCES, DAV1D_WIN32_SOURCES, DAV1D_TMPL_SOURCES, DAV1D_INCLUDES
    out = os.path.join(OUT, "dav1d")
    lib = os.path.join(out, "dav1d.lib")
    if not os.path.exists(os.path.join(DAV1D, "include", "dav1d", "dav1d.h")):
        sys.exit("[build_engine_x64] no dav1d in %s: run python tools/fetch_dav1d.py" % DAV1D)
    flags = ["cl", "/nologo", "/c", "/MT", "/O2", "/GS-", "/Gy", "/W0", "/Z7", "/TC", "/MP", "/DNDEBUG",
             "/D_CRT_SECURE_NO_WARNINGS", "/I" + DAV1D_CONFIG] +         ["/I" + os.path.join(DAV1D, d) for d in DAV1D_INCLUDES] + ["/I" + os.path.join(DAV1D, "include", "compat", "msvc")]
    objs = []
    for sub, extra, sources in (("common", [], DAV1D_SOURCES + DAV1D_WIN32_SOURCES),
                                ("bd8", ["/DBITDEPTH=8"], DAV1D_TMPL_SOURCES),
                                ("bd16", ["/DBITDEPTH=16"], DAV1D_TMPL_SOURCES)):
        d = os.path.join(out, sub)
        os.makedirs(d, exist_ok=True)
        for f in glob.glob(os.path.join(d, "*.obj")):
            os.remove(f)
        paths = [os.path.join(DAV1D, s) for s in sources]
        run(flags + extra + ["/Fo" + d + os.sep] + paths, env, "cl /c %d dav1d files (%s)" % (len(paths), sub))
        objs += [os.path.join(d, os.path.splitext(os.path.basename(s))[0] + ".obj") for s in sources]
    run(["lib", "/nologo", "/OUT:" + lib] + objs, env, "lib dav1d.lib")
    return lib


def build_image(env):
    """libwebp's decoder and stb_image's PNG and TGA into build/x64/image/image.lib (BSD and public
    domain; port_imagefile.cpp)."""
    from fetch_libwebp import WEBP_SOURCES
    from fetch_stb import STB_IMAGE_IMPL
    out = os.path.join(OUT, "image")
    lib = os.path.join(out, "image.lib")
    if not os.path.exists(os.path.join(LIBWEBP, "src", "webp", "decode.h")):
        sys.exit("[build_engine_x64] no libwebp in %s: run python tools/fetch_libwebp.py" % LIBWEBP)
    if not os.path.exists(os.path.join(STB, "stb_image.h")):
        sys.exit("[build_engine_x64] no stb_image in %s: run python tools/fetch_stb.py" % STB)
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.obj")):
        os.remove(f)
    flags = ["cl", "/nologo", "/c", "/MT", "/O2", "/GS-", "/Gy", "/W0", "/Z7", "/DWIN32", "/DNDEBUG", "/D_LIB",
             "/D_CRT_SECURE_NO_WARNINGS", "/I" + LIBWEBP]
    sources = [os.path.join(LIBWEBP, s) for s in WEBP_SOURCES]
    run(flags + ["/TC", "/MP", "/Fo" + out + os.sep] + sources, env, "cl /c %d libwebp files" % len(sources))
    objs = [os.path.join(out, os.path.splitext(os.path.basename(s))[0] + ".obj") for s in sources]
    impl = os.path.join(out, "stb_image_impl.c")
    with open(impl, "w", encoding="utf-8") as f:
        f.write(STB_IMAGE_IMPL)
    obj = os.path.join(out, "stb_image.obj")
    run(flags + ["/TC", "/I" + STB, "/Fo" + obj, impl], env, "cl /c stb_image")
    objs.append(obj)
    run(["lib", "/nologo", "/OUT:" + lib] + objs, env, "lib image.lib")
    return lib


# The page a LAN room serves (engine/web/lan), beside the program in web/lan, where the
# room reads it (port_lanweb.cpp); and Hyperion for the browser in web/play, the page with the
# program tools/build_engine_web.py built, once it has (W3).
WEB_BUILT = os.path.join(ENGINE, "src117", "build", "web", "play")
WEB_PROGRAM = ("hyperion.js", "hyperion.wasm")


# Hyperion's built-in HUD designs (engine/huds), beside the program in huds/, where it
# reads them as a package's Data\huds\ (PortPackagedFile).
def copy_huds(out):
    src = os.path.join(ENGINE, "huds")
    if os.path.isdir(src):
        shutil.copytree(src, os.path.join(out, "huds"), dirs_exist_ok=True)


def copy_web(out):
    src = os.path.join(ENGINE, "web")
    if os.path.isdir(src):
        shutil.copytree(src, os.path.join(out, "web"), dirs_exist_ok=True)
    for name in WEB_PROGRAM:
        built = os.path.join(WEB_BUILT, name)
        if os.path.isfile(built):
            shutil.copy2(built, os.path.join(out, "web", "play", name))
    # a browser build of another version cannot play in this one's rooms (the room refuses it), so
    # say so: it carries its version text, as Hyperion.exe does
    wasm = os.path.join(WEB_BUILT, "hyperion.wasm")
    if os.path.isfile(wasm):
        m = re.search(rb'HYPERION_VERSION_TEXT "([^"]+)"', open(os.path.join(ENGINE, "hyperion", "version.h"), "rb").read())
        if m and m.group(1) not in open(wasm, "rb").read():
            print("[build_engine_x64] warning: web/play's hyperion.wasm is not %s, so browsers cannot play in "
                  "this build's rooms; run python tools/build_engine_web.py" % m.group(1).decode())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--no-stormlib", action="store_true", help="keep the StormLib already built")
    ap.add_argument("--casclib", action="store_true", help="build CascLib again")
    ap.add_argument("--webm", action="store_true",
                    help="build libvpx, libwebm's parser and stb_vorbis again, and libwebp's decoder and stb_image")
    ap.add_argument("--dav1d", action="store_true", help="build dav1d again")
    ap.add_argument("--no-install", action="store_true", help="leave the folders install_to.txt names as they are")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, "*.obj")):
        os.remove(f)
    env = vs_env()
    for d, fetch in ((CLAY, "fetch_clay.py"), (STB, "fetch_stb.py"), (FONTS, "fetch_fonts.py"),
                     (DRLIBS, "fetch_drlibs.py")):
        if not os.path.isdir(d):
            sys.exit("[build_engine_x64] no %s: run python tools/%s" % (d, fetch))
    import fetch_clay  # Hyperion's change to Clay, made once
    fetch_clay.patch()
    import fetch_fonts  # the Noto cuts, made again when the catalogs need a character they lack
    fetch_fonts.cut()
    for _, file in FONT_RESOURCES:
        if not os.path.exists(os.path.join(FONTS, file)):
            sys.exit("[build_engine_x64] no %s in %s: run python tools/fetch_fonts.py" % (file, FONTS))
    casclib = os.path.join(OUT, "casclib", "casclib.lib")
    if a.casclib or not os.path.exists(casclib):
        casclib = build_casclib(env)
    webm = os.path.join(OUT, "webm", "webm.lib")
    webm_config = max(os.path.getmtime(f) for f in glob.glob(os.path.join(LIBVPX_CONFIG, "*"))
                      + glob.glob(os.path.join(STB_PATCHES, "*.patch")))
    if a.webm or not os.path.exists(webm) or os.path.getmtime(webm) < webm_config:
        webm = build_webm(env)
    image = os.path.join(OUT, "image", "image.lib")
    if a.webm or not os.path.exists(image):
        image = build_image(env)
    dav1d = os.path.join(OUT, "dav1d", "dav1d.lib")
    dav1d_config = max(os.path.getmtime(f) for f in glob.glob(os.path.join(DAV1D_CONFIG, "*")))
    if a.dav1d or not os.path.exists(dav1d) or os.path.getmtime(dav1d) < dav1d_config:
        dav1d = build_dav1d(env)

    langs = tus("lang")
    rsp = os.path.join(OUT, "langtus.rsp")
    with open(rsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % os.path.join(SRC, "lang", t + ".cpp") for t in langs) + "\n")
    run(["cl"] + LANG + ["/MP", "@" + rsp], env, "cl /c %d lang TUs" % len(langs))
    run(["cl"] + GEN + [os.path.join(SRC, "gen", "code_map.cpp")], env, "cl /c code_map.cpp")
    # 1.17's starting values, as Hyperion's own source
    run(["cl"] + GEN + [os.path.join(SRC, "gen", "data117.cpp")], env, "cl /c data117.cpp")
    if not os.path.exists(DXC):
        sys.exit("[build_engine_x64] no %s: run python tools/fetch_dxc.py" % DXC)
    if not os.path.exists(shader_msl.SPIRV_CROSS):
        sys.exit("[build_engine_x64] no %s: run python tools/fetch_spirv_cross.py" % shader_msl.SPIRV_CROSS)
    os.makedirs(SHADERS, exist_ok=True)
    # the C++ fills each uniform block as Direct3D lays it out; SPIR-V's must agree
    layout = shader_layout.check(SHADER_SRC)
    if layout:
        sys.exit("[build_engine_x64] uniform blocks laid out differently for SPIR-V:\n  " + "\n  ".join(layout))
    for src, entry, profile in SHADER_ENTRIES:
        hlsl = os.path.join(SHADER_SRC, src)
        run([DXC, "-nologo", "-O3", "-Wno-ignored-attributes", "-T", profile, "-E", entry, hlsl,
             "-Fh", os.path.join(SHADERS, entry + ".dxil.h"), "-Vn", "k%sDxil" % entry],
            env, "dxc %s %s DXIL" % (src, entry))
        run([DXC, "-nologo", "-O3", "-T", profile, "-E", entry, "-spirv", "-fspv-target-env=vulkan1.0",
             hlsl, "-Fh", os.path.join(SHADERS, entry + ".spv.h"), "-Vn", "k%sSpv" % entry],
            env, "dxc %s %s SPIR-V" % (src, entry))
        # the Mac's: MSL made from that SPIR-V, numbered as SDL's Metal path wants
        stage = {"vs": "vertex", "ps": "fragment", "cs": "compute"}[profile[:2]]
        msl = shader_msl.convert(os.path.join(SHADERS, entry + ".spv.h"), stage, entry)
        shader_msl.write_header(msl, os.path.join(SHADERS, entry + ".msl.h"), "k%sMsl" % entry, entry)
    # before the port TUs, whose Opus decoder includes its headers
    if not all(os.path.exists(os.path.join(WEBRTC, "lib", l)) for l in WEBRTC_LIBS):
        run([sys.executable, os.path.join(ENGINE, "tools", "build_webrtc.py")], env, "build_webrtc.py")
    ports = tus("port")
    write_build_id()
    # the snapshots' ranges follow the global blocks' layout (tools/snapranges.py); a tree without
    # the globals' list (re/globals117.json) builds with the committed gen/snapranges.h
    snapranges = os.path.join(ENGINE, "tools", "snapranges.py")
    if os.path.exists(snapranges) and os.path.exists(os.path.join(ENGINE, "re", "globals117.json")):
        subprocess.run([sys.executable, snapranges, "--if-stale"], check=True)
    run(["cl"] + PORT + ["/MP"] + [tu_path("port", p) for p in ports],
        env, "cl /c %d port TUs" % len(ports))
    for p in ("implode", "explode", "crc32"):
        run(["cl", "/nologo", "/c", "/Z7", "/GF", "/Gm-", "/MT", "/GS-", "/Gy", "/fp:precise", "/TC",
             "/Ox", "/Oi", "/Oy-", "/DWIN32", "/DNDEBUG", "/Dimplode=PkImplode", "/Dexplode=PkExplode",
             "/Dcrc32_pklib=PkCrc32", "/W3", "/Fo" + os.path.join(OUT, "pk_%s.obj" % p),
             os.path.join(PKLIB, p + ".c")], env, "cl /c pklib %s.c" % p)
    run(["cl"] + COMMON + ["/TP", "/I" + os.path.join(SRC, "h"), "/I" + PKLIB,
                           port_path("port_pklib.cpp")], env, "cl /c port_pklib.cpp")
    if not a.no_stormlib:
        run([os.path.join(ENGINE, "tools", "build_stormlib.bat"), "x64"], env, "StormLib")
    run(["cl"] + COMMON + ["/TP", port_path("port_stormlib.cpp")], env,
        "cl /c port_stormlib.cpp")
    run(["cl", "/nologo", "/c", "/Z7", "/O2", "/MT", "/GS-", "/Gy", "/W0", "/DWIN32", "/DNDEBUG", "/TP",
         "/std:c++20", "/I" + CLAY, "/Fo" + os.path.join(OUT, "port_clay.obj"),
         port_path("port_clay.cpp")], env, "cl /c port_clay.cpp")
    # libdatachannel's C++ throws, and wants C++17
    run(["cl"] + PORT + ["/EHsc", "/std:c++17", "/DRTC_STATIC", "/I" + os.path.join(WEBRTC, "include"),
                         port_path("port_lanstream.cpp")], env, "cl /c port_lanstream.cpp")

    abs_obj = os.path.join(OUT, "abs_globals.obj")
    run([sys.executable, os.path.join(ENGINE, "tools", "wave117", "abs_globals.py"), "--x64",
         "--objdir", OUT, "--out", abs_obj, "--exclude", ",".join(NOT_GAME),
         "--bind-only", ",".join(p + ".obj" for p in ports),
         "--wide", os.path.join(SRC, "gen", "wide117.json")], env, "abs_globals --x64")

    hyp = os.path.join(ENGINE, "hyperion")
    run(["cl", "/nologo", "/c", "/Z7", "/O2", "/MT", "/W3", "/DWIN32", "/D_WINDOWS", "/DNDEBUG",
         "/Fo" + os.path.join(OUT, "hyperion.o"), os.path.join(hyp, "hyperion.cpp")], env, "cl /c hyperion.cpp")
    run(["rc", "/nologo", "/fo" + os.path.join(OUT, "hyperion.res"), os.path.join(hyp, "hyperion.rc")],
        env, "rc hyperion.rc")
    fonts_rc = os.path.join(OUT, "fonts.rc")
    with open(fonts_rc, "w", encoding="utf-8") as f:
        f.write("// written by build_engine_x64.py: the front end's typefaces\n")
        for name, file in FONT_RESOURCES:
            f.write('%s RCDATA "%s"\n' % (name, os.path.join(FONTS, file).replace("\\", "\\\\")))
    run(["rc", "/nologo", "/fo" + os.path.join(OUT, "fonts.res"), fonts_rc], env, "rc fonts.rc")
    objs = sorted(glob.glob(os.path.join(OUT, "*.obj")))
    lrsp = os.path.join(OUT, "link.rsp")
    with open(lrsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % o for o in objs + [os.path.join(OUT, "hyperion.o")]) + "\n")
    # The C runtime enters at wWinMain, Hyperion's start (hyperion.cpp), which calls
    # the game's WinMain last. The manifest names Common Controls 6, so SDL's message boxes
    # are Windows' task dialogs, whose buttons fit their text (the first start, port_install.cpp).
    # SDL3.dll is loaded on its first call, so a start without it can say so in a box of its own
    # (port_startup.cpp) instead of Windows' loader.
    run(["link", "/nologo", "/MACHINE:X64", "/SUBSYSTEM:WINDOWS", "/ENTRY:wWinMainCRTStartup", "/DEBUG",
         "/MANIFEST:EMBED", "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' "
         "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'",
         "/OPT:REF", "/OPT:ICF", "/DEF:" + os.path.join(hyp, "hyperion.def"),
         "/OUT:" + os.path.join(OUT, "Hyperion.exe"), "/MAP:" + os.path.join(OUT, "Hyperion.map"),
         "@" + lrsp, os.path.join(OUT, "hyperion.res"), os.path.join(OUT, "fonts.res"),
         os.path.join(OUT, "stormlib", "stormlib.lib"), casclib, webm, image, dav1d,
         os.path.join(SDL3, "lib", "x64", "SDL3.lib"), "/DELAYLOAD:SDL3.dll", "delayimp.lib",
         "bcrypt.lib"] + [os.path.join(WEBRTC, "lib", l) for l in WEBRTC_LIBS] + ["iphlpapi.lib"] + LIBS,
        env, "link Hyperion.exe")
    # The replay observer (docs/replay-test.md), which the harness loads into the game.
    rt = os.path.join(ENGINE, "tools", "replaytest")
    run(["cl", "/nologo", "/EHsc", "/MT", "/O2", "/W3", "/Z7", "/DWIN32", "/D_WINDOWS", "/DNDEBUG",
         "/D_CRT_SECURE_NO_WARNINGS", "/LD", "/Fo" + os.path.join(OUT, "rt_observer.o"),
         os.path.join(rt, "rt_observer.cpp"), "/Fe" + os.path.join(OUT, "rt_observer.dll"),
         "/link", "/MACHINE:X64", "/DEBUG", "/DEF:" + os.path.join(rt, "rt_observer.def"), "user32.lib"],
        env, "cl rt_observer.cpp")
    copy_web(OUT)
    copy_huds(OUT)
    # SDL3.dll beside the exe, the patched one tools/build_sdl3.py made, so the build starts where it is
    dll = os.path.join(SDL3, "lib", "x64", "SDL3.dll")
    if not os.path.isfile(dll):
        sys.exit("[build_engine_x64] no SDL3.dll in %s: run python tools/get_libraries.py" % os.path.dirname(dll))
    beside = os.path.join(OUT, "SDL3.dll")
    if not os.path.isfile(beside) or open(beside, "rb").read() != open(dll, "rb").read():
        shutil.copyfile(dll, beside)
    print("[build_engine_x64] OK -> src117\\build\\x64\\Hyperion.exe, SDL3.dll, rt_observer.dll", flush=True)
    # the build into the player's own Hyperion folders; the build stands whatever the install says
    if not a.no_install and os.path.isfile(os.path.join(ENGINE, "install_to.txt")):
        subprocess.call([sys.executable, os.path.join(ENGINE, "tools", "package.py"), "--install"])


if __name__ == "__main__":
    main()
