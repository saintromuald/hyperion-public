"""Build Hyperion for browsers, as WebAssembly with Emscripten.

    python tools/build_engine_web.py [--sdl] [--libs] [--clean] [-j N] [--compile-only]

Writes src117/build/web/: hyperion.js and hyperion.wasm, the engine and its start-up in one
module, which engine/web/play's page loads. On Windows the build runs in Emscripten's own
image, emscripten/emsdk at the version EMSDK names, with the repository mounted at /src.

The same TUs as the Linux build (src117/tus.txt), compiled as tools/build_engine_linux.py
compiles them, with Emscripten's clang for wasm32: the Win32 calls they make are
port_posix.cpp's, declared by src117/compat/. A browser gives a page on the LAN's plain
http no threads, so the build has none: port_posix.cpp runs Hyperion's threads as fibers that
take turns where a thread waits, and the game's loop gives the browser its turns where it
sleeps, both by Asyncify. The game's globals are bound to the engine's block of 1.17's data
as on Linux, in WebAssembly's assembler: one data section, a label at each name's offset.
SDL3 is built from third_party/SDL3-3.4.16-src, and StormLib and CascLib from their sources,
once (--sdl and --libs build them again). The engine's files are compiled again only when they,
a header they read or their flags changed (each object's .d and .cmd beside it; --clean compiles
them all), and Emscripten's cache lives in a Docker volume, hyperion-emcache-<version>.

What keeps the simulation the same as the Windows build's is the Linux build's flags, the
same layouts the 64-bit builds use (the x64 binding of the globals), and the replay suite.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ENGINE)
sys.path.insert(0, os.path.join(ENGINE, "tools"))
import build_engine_linux as bel  # noqa: E402
from port_files import port_path, tu_path  # noqa: E402

EMSDK = "6.0.10"
IMAGE = "emscripten/emsdk:" + EMSDK
SRC = bel.SRC
OUT = os.path.join(SRC, "build", "web")
LLVM = "/emsdk/upstream/bin"

# the Linux build's flags, for 64-bit WebAssembly (Memory64), so the engine's types lay out as the
# 64-bit builds' do; SSE2 as Emscripten gives it over WebAssembly's SIMD
WASM = ["-sMEMORY64=1", "-msimd128", "-msse2"]
COMMON = bel.COMMON + WASM
LANG = ["-std=c++17"] + COMMON + ["-DOC_PAYLOAD", "-DOC_WITH_HELD"]
GEN = ["-std=c++17"] + COMMON + ["-DOC_DLL_MODE"]
PORT = ["-std=c++20"] + COMMON + ["-DOC_WITH_HELD", "-I" + OUT, "-I" + os.path.join(bel.SDL_SRC, "include"),
                                  "-I" + bel.SHADERS, "-I" + bel.CLAY, "-I" + bel.STB,
                                  "-I" + os.path.join(bel.CASCLIB, "src"), "-DCASCLIB_NO_AUTO_LINK_LIBRARY",
                                  "-I" + bel.DRLIBS, "-I" + os.path.join(bel.LIBWEBP, "src")]
THIRD = [f for f in bel.THIRD if f != "-fPIC"] + ["-sMEMORY64=1"]
# what the web build leaves out of tus.txt's list
NOT_BUILT = []


def say(what):
    print("[build_engine_web]", what, flush=True)


def write_changed(path, text):
    """path holding text, written only when it holds something else, so its object stays current."""
    try:
        if open(path, encoding="utf-8").read() == text:
            return
    except OSError:
        pass
    open(path, "w", encoding="utf-8").write(text)


def run(cmd, what, cwd=None):
    say(what)
    r = subprocess.run(cmd, cwd=cwd or OUT)
    if r.returncode:
        sys.exit("[build_engine_web] %s FAILED" % what)


def build_sdl(again):
    """SDL3 as a static library, built once into OUT/sdl3."""
    lib = os.path.join(OUT, "libSDL3.a")
    if os.path.exists(lib) and not again:
        return lib
    work = os.path.join(OUT, "sdl3")
    shutil.rmtree(work, ignore_errors=True)
    run(["emcmake", "cmake", "-S", bel.SDL_SRC, "-B", work, "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=OFF",
         "-DSDL_STATIC=ON", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL=OFF",
         "-DCMAKE_C_FLAGS=-sMEMORY64=1", "-DCMAKE_CXX_FLAGS=-sMEMORY64=1"], "cmake SDL3")
    run(["cmake", "--build", work, "-j", str(os.cpu_count() or 4)], "make SDL3")
    shutil.copy2(os.path.join(work, "libSDL3.a"), lib)
    return lib


def build_libs(again, workers):
    """StormLib and CascLib into OUT/libs/*.a, once, as the Linux build makes them."""
    libs = os.path.join(OUT, "libs")
    storm, casc = os.path.join(libs, "libstorm.a"), os.path.join(libs, "libcasc.a")
    if os.path.exists(storm) and os.path.exists(casc) and not again:
        return storm, casc
    os.makedirs(libs, exist_ok=True)
    jobs = []
    for f in bel.STORMLIB_FILES:
        src = os.path.join(bel.SL, f)
        cxx = f.endswith(".cpp")
        obj = os.path.join(libs, "sl_" + f.replace("/", "_").rsplit(".", 1)[0] + ".o")
        jobs.append(([("em++" if cxx else "emcc")] + THIRD + (["-std=c++11"] if cxx else [])
                     + ["-D_LIB", "-D__STORMLIB_SELF__", "-D_7ZIP_ST", "-D_fdopen=fdopen",
                        "-include", port_path("port_stormlib_names.h"), "-o", obj, src], obj))
    flags = THIRD + ["-D_LIB", "-D__CASCLIB_SELF__"] + bel.CASCLIB_DEFS
    for unity, cc, std in (("sources-cpp.cpp", "em++", ["-std=c++17"]), ("sources-c.c", "emcc", [])):
        text = open(os.path.join(bel.CASCLIB, unity), encoding="utf-8").read()
        text = re.sub(r'#include "([^"]+)"', lambda m: '#include "%s"' % os.path.join(
            bel.CASCLIB, m.group(1).replace("\\", "/")), text)
        mine = os.path.join(libs, "casc_" + unity)
        open(mine, "w", encoding="utf-8").write(text)
        obj = os.path.join(libs, "casc_cpp.o" if cc == "em++" else "casc_c.o")
        jobs.append(([cc] + flags + std + ["-o", obj, mine], obj))
    bel.compile_many(jobs, "StormLib and CascLib", workers)
    for lib in (storm, casc):
        if os.path.exists(lib):
            os.remove(lib)
    run(["emar", "rcs", storm] + [j[1] for j in jobs if os.path.basename(j[1]).startswith("sl_")], "ar StormLib")
    run(["emar", "rcs", casc, os.path.join(libs, "casc_cpp.o"), os.path.join(libs, "casc_c.o")], "ar CascLib")
    return storm, casc


def bind_globals(objs, out_s):
    """The block, in WebAssembly's assembler: 1.17's data and the wide storage each one
    section, every game global the objects name and none defines a label at its offset in
    it, as build_engine_linux.py's bind_globals places them."""
    import abs_globals as ag
    ag.set_x64()
    names, clash = ag.registry_names()
    if clash:
        sys.exit("[build_engine_web] the registry names %s twice" % clash[0][0])
    defined, undef = bel.symbols(objs)
    open_names = sorted(n for n in undef if n not in defined)
    dem = bel.demangle([n for n in open_names if n.startswith("_Z")])
    bound = []
    for n in open_names:
        if n in (ag.BLOCK_NAME, ag.WIDE_NAME):
            continue
        va = names.get(dem.get(n, n))
        if va is not None:
            bound.append((n, va))
    far = ag.outside_block(bound)
    if far:
        sys.exit("[build_engine_web] outside 1.17's data: %s" % ", ".join("%s 0x%X" % f for f in far[:10]))
    wide_size, wide = ag.load_wide(os.path.join(SRC, "gen", "wide117.json"))
    block, own, inside = ag.place_wide(bound, wide)
    if inside:
        sys.exit("[build_engine_web] inside a wide global: %s" % ", ".join(i[0] for i in inside[:10]))
    lines = ["# written by tools/build_engine_web.py: 1.17's data block and the wide storage"]

    def section(sect, start, size, labels, align):
        lines.extend(['.section %s,"",@' % sect, ".p2align %d" % align])
        at = 0
        for n, off in sorted([(start, 0)] + labels, key=lambda x: (x[1], x[0] != start)):
            if off > at:
                lines.append(".skip %d" % (off - at))
                at = off
            lines.extend([".globl %s" % n, ".type %s,@object" % n, "%s:" % n])
        if size > at:
            lines.append(".skip %d" % (size - at))
        for n, off in [(start, 0)] + labels:
            lines.append(".size %s, %d" % (n, max(size - off, 1)))

    section(".bss.oc117", ag.BLOCK_NAME, ag.BLOCK_HI - ag.BLOCK_LO,
            [(n, va - ag.BLOCK_LO) for n, va in block], 12)
    section(".bss.ocwide", ag.WIDE_NAME, max(wide_size, 16), list(own), 4)
    open(out_s, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    say("bound %d game globals (%d in the wide storage)" % (len(bound), len(own)))


def write_resources():
    """resources_posix.cpp: port_posix.cpp's table of the program's own resources, the
    typefaces as arrays (WebAssembly's assembler has no .incbin)."""
    c = ["// written by tools/build_engine_web.py: Hyperion's own resources (port_posix.cpp)",
         "struct PortPosixResource {", "  int nType;", "  const char *pszName;", "  int nName;",
         "  const unsigned char *pData;", "  unsigned cb;", "};"]
    rows = []
    for i, (name, file) in enumerate(bel.FONT_RESOURCES):
        data = open(os.path.join(bel.FONTS, file), "rb").read() + b"\0"
        c.append("static const unsigned char kRes%d[] = {%s};" % (i, ",".join(str(b) for b in data)))
        rows.append('  {10, "%s", 0, kRes%d, %du},' % (name, i, len(data) - 1))
    ver = bel.version_block()
    c.append("static const unsigned char kVersion[] = {%s};" % ", ".join(str(b) for b in ver))
    c.append("extern const PortPosixResource g_aPortPosixResources[] = {")
    c += rows
    c.append("  {16, nullptr, 1, kVersion, %du}," % len(ver))
    c.append("};")
    c.append("extern const int g_nPortPosixResources = %d;" % (len(rows) + 1))
    write_changed(os.path.join(OUT, "resources_posix.cpp"), "\n".join(c) + "\n")


def current(cmd, obj):
    """Whether obj was made by cmd and is newer than its source and every header the compiler
    said it read (its .d, -MMD's)."""
    try:
        if open(obj + ".cmd", encoding="utf-8").read() != "\n".join(cmd):
            return False
        text = open(obj + ".d", encoding="utf-8").read()
        made = os.path.getmtime(obj)
    except OSError:
        return False
    deps = text.replace("\\\n", " ").replace("\\ ", "\0").split(":", 1)[-1].split()
    try:
        return all(os.path.getmtime(d.replace("\0", " ")) <= made for d in deps)
    except OSError:
        return False


def compile_changed(jobs, what, workers, clean):
    """compile_many for the jobs whose object is not current, each writing its .d and .cmd."""
    todo = []
    for cmd, obj in jobs:
        cmd = cmd + ["-MMD", "-MF", obj + ".d"]
        if clean or not current(cmd, obj):
            todo.append((cmd, obj))
    say("%s: %d of %d to compile" % (what, len(todo), len(jobs)))
    if todo:
        bel.compile_many(todo, what, workers)
    for cmd, obj in todo:
        open(obj + ".cmd", "w", encoding="utf-8").write("\n".join(cmd))


def build(a):
    workers = a.jobs or os.cpu_count() or 4
    bel.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    import fetch_clay  # Hyperion's change to Clay, made once
    fetch_clay.patch()
    import fetch_fonts  # the Noto cuts, made again when the catalogs need a character they lack
    fetch_fonts.cut()
    os.environ["PATH"] = LLVM + os.pathsep + os.environ.get("PATH", "")
    sdl = build_sdl(a.sdl)
    storm, casc = build_libs(a.libs, workers)
    image = bel.build_image_lib(os.path.join(OUT, "libs"), "emcc", "emar", THIRD, a.libs, workers)

    obj_of = bel.obj_of
    jobs = []
    for t in bel.tus("lang"):
        src = os.path.join(SRC, "lang", t + ".cpp")
        jobs.append((["em++"] + LANG + ["-o", obj_of(src), src], obj_of(src)))
    for g in ("code_map", "data117"):
        src = os.path.join(SRC, "gen", g + ".cpp")
        jobs.append((["em++"] + GEN + ["-o", obj_of(src), src], obj_of(src)))
    for p in bel.tus("port"):
        if p in NOT_BUILT:
            continue
        src = tu_path("port", p)
        jobs.append((["em++"] + PORT + ["-o", obj_of(src), src], obj_of(src)))
    for p in ("implode", "explode", "crc32"):
        src = os.path.join(OUT, "pk_%s.c" % p)
        write_changed(src,
            "#include <assert.h>\n#include <limits.h>\n#include <string.h>\n#define long int\n"
            '#include "%s"\n' % os.path.join(bel.PKLIB, p + ".c"))
        obj = os.path.join(OUT, "pk_%s.o" % p)
        jobs.append((["emcc"] + THIRD + ["-Dimplode=PkImplode", "-Dexplode=PkExplode", "-Dcrc32_pklib=PkCrc32",
                                         "-o", obj, src], obj))
    src = port_path("port_pklib.cpp")
    jobs.append((["em++", "-std=c++17"] + COMMON + ["-I" + bel.PKLIB, "-o", obj_of(src), src], obj_of(src)))
    src = port_path("port_stormlib.cpp")
    jobs.append((["em++", "-std=c++17"] + COMMON + ["-o", obj_of(src), src], obj_of(src)))
    src = port_path("port_clay.cpp")
    jobs.append((["em++", "-std=c++20"] + THIRD + ["-I" + bel.CLAY, "-o", obj_of(src), src], obj_of(src)))
    # compiled on its own in the other builds; in a browser it is its stubs
    src = port_path("port_lanstream.cpp")
    jobs.append((["em++"] + PORT + ["-o", obj_of(src), src], obj_of(src)))
    src = os.path.join(ENGINE, "hyperion", "hyperion.cpp")
    jobs.append((["em++", "-std=c++17"] + COMMON + (["-g"] if a.debug else []) + ["-o", obj_of(src), src], obj_of(src)))
    write_resources()
    jobs.append((["em++", "-std=c++17", "-sMEMORY64=1", "-c", "-o", os.path.join(OUT, "resources_posix.o"),
                  os.path.join(OUT, "resources_posix.cpp")], os.path.join(OUT, "resources_posix.o")))
    compile_changed(jobs, "em++ -c", workers, a.clean)
    if a.compile_only:
        say("compiled")
        return

    objs = sorted(obj for _, obj in jobs)
    not_game = bel.NOT_GAME
    game = [o for o in objs if os.path.basename(o) not in not_game]
    abs_s = os.path.join(OUT, "abs_globals.s")
    bind_globals(game, abs_s)
    run(["emcc", "-sMEMORY64=1", "-c", "-o", os.path.join(OUT, "abs_globals.o"), abs_s], "as abs_globals.s")
    objs.append(os.path.join(OUT, "abs_globals.o"))
    run(["em++", "-O2", "-o", os.path.join(OUT, "hyperion.js")] + objs + [storm, casc, image, sdl]
        + ["-sMEMORY64=1", "-sASYNCIFY=1", "-Wl,--fatal-warnings", "--profiling-funcs",
           # 1.17 calls through pointers of other types than their functions' (a dialog's
           # procedure returns BOOL, DLGPROC INT_PTR), which x86 allows and WebAssembly traps
           "-sEMULATE_FUNCTION_POINTER_CASTS=1", "-sASYNCIFY_STACK_SIZE=1048576", "-sALLOW_MEMORY_GROWTH=1",
           # the program's data, stack and heap from 2 GB up, above every integer 1.17 keeps
           # where a pointer could be, as x64's memory lies above them; the state hash tells
           # the two apart by that (port_synchash.cpp's IsInteger)
           # the pools' storage for 262,144 units, 330 MB, among the static data
           "-sGLOBAL_BASE=2147483648", "-sINITIAL_MEMORY=2952790016",
           "-sSTACK_SIZE=8388608", "-sFORCE_FILESYSTEM=1", "-lidbfs.js",
           "-sMODULARIZE=1", "-sEXPORT_NAME=HyperionModule", "-sINVOKE_RUN=0", "-sEXIT_RUNTIME=0",
           "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,ENV",
           # the C library's calls that take a path, which port_posix.cpp translates
           "-Wl,--wrap=fopen", "-Wl,--wrap=remove"]
        + (["-g", "-sASSERTIONS=2", "-Wl,-Map=" + os.path.join(OUT, "hyperion.map")] if a.debug else []), "link hyperion.js")
    # a direct call through a pointer of another type than its function's links to a stub
    # that traps; the names --profiling-funcs keeps show where
    wasm = open(os.path.join(OUT, "hyperion.wasm"), "rb").read()
    bad = sorted(set(re.findall(rb"[A-Za-z0-9_.$]*_bitcast_invalid", wasm)))
    if bad:
        sys.exit("[build_engine_web] calls through a mismatched type: %s" % b", ".join(bad).decode())
    # the page that runs it (engine/web/play), with the module beside it, in OUT/play
    play = os.path.join(OUT, "play")
    shutil.copytree(os.path.join(ENGINE, "web", "play"), play, dirs_exist_ok=True)
    for f in ("hyperion.js", "hyperion.wasm"):
        shutil.copy2(os.path.join(OUT, f), os.path.join(play, f))
    say("OK -> %s" % os.path.relpath(os.path.join(play, "hyperion.wasm"), ENGINE))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sdl", action="store_true", help="build SDL3 again")
    ap.add_argument("--libs", action="store_true", help="build StormLib and CascLib again")
    ap.add_argument("-j", "--jobs", type=int, default=0)
    ap.add_argument("--compile-only", action="store_true", help="compile, and stop before the link")
    ap.add_argument("--clean", action="store_true", help="compile every file again, not only the changed ones")
    ap.add_argument("--debug", action="store_true", help="link with Emscripten's checks (Asyncify's among them)")
    ap.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.inside or sys.platform.startswith("linux"):
        if not a.inside:
            bel.OUT = OUT
            bel.write_build_id()
        return build(a)
    # elsewhere: the build_id here, where git has the repository, then the build in the image
    bel.OUT = OUT
    bel.write_build_id()
    # Emscripten's cache in a volume of its own, so the system libraries it builds for 64-bit
    # WebAssembly at the link are built once, not in every new container
    cmd = ["docker", "run", "--rm", "-v", "%s:/src" % REPO.replace("\\", "/"),
           "-v", "hyperion-emcache-%s:/emsdk/upstream/emscripten/cache" % EMSDK, IMAGE, "python3",
           "/src/engine/tools/build_engine_web.py", "--inside"]
    cmd += (["--sdl"] if a.sdl else []) + (["--libs"] if a.libs else []) + (["-j", str(a.jobs)] if a.jobs else [])
    cmd += ["--compile-only"] if a.compile_only else []
    cmd += ["--clean"] if a.clean else []
    cmd += ["--debug"] if a.debug else []
    say("in the %s image" % IMAGE)
    sys.exit(subprocess.run(cmd).returncode)


if __name__ == "__main__":
    main()
