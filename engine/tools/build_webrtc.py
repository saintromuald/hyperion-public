"""Build the libraries a LAN room's streamed seats use for the x64 Windows build:
static, /MT as the engine is, into src117/build/x64/webrtc/ (include/ and lib/).

    python tools/build_webrtc.py

The sources are tools/fetch_webrtc.py's, in engine/third_party/webrtc. Mbed TLS, then
libdatachannel against it (USE_MBEDTLS, its own libjuice, usrsctp and libSRTP, no WebSocket,
no examples or tests), and Opus are CMake projects, built with Ninja and the compiler
vcvarsall sets up. OpenH264's encoder is compiled here from its sources without its assembly
(its own build wants meson and nasm), so the processor's H.264 is the slower path; NVENC, through
the driver, is the fast one (port_lanstream.cpp). tools/build_engine_x64.py links what this makes
and runs it when it is missing.
"""

import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_engine_x64 import ENGINE, OUT, vs_env  # noqa: E402

SRC = os.path.join(ENGINE, "third_party", "webrtc")
LDC = os.path.join(SRC, "libdatachannel-0.24.6")
MBEDTLS = os.path.join(SRC, "mbedtls-3.6.7")
OPUS = os.path.join(SRC, "opus-1.5.2")
H264 = os.path.join(SRC, "openh264-2.6.0")
NVENC = os.path.join(SRC, "nv-codec-headers-13.1.15.0")
DEST = os.path.join(OUT, "webrtc")
PREFIX = DEST
WORK = os.path.join(DEST, "work")
# what the engine links, in an order the linker takes
LIBS = ["datachannel.lib", "juice.lib", "srtp2.lib", "usrsctp.lib", "mbedtls.lib", "mbedx509.lib", "mbedcrypto.lib",
        "opus.lib", "openh264.lib"]


def run(cmd, env, what, cwd=None):
    print("[build_webrtc]", what, flush=True)
    exe = shutil.which(cmd[0], path=env["PATH"]) or cmd[0]
    r = subprocess.run([exe] + cmd[1:], env=env, cwd=cwd or WORK)
    if r.returncode:
        sys.exit("[build_webrtc] %s FAILED" % what)


def cmake(env, name, src, defs):
    build = os.path.join(WORK, name)
    run(["cmake", "-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl",
         "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
         "-DCMAKE_INSTALL_PREFIX=" + PREFIX, "-DCMAKE_PREFIX_PATH=" + PREFIX, "-DBUILD_SHARED_LIBS=OFF",
         "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /DNDEBUG",
         "-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /DNDEBUG"] + defs, env, "cmake configure " + name)
    run(["cmake", "--build", build, "--parallel"], env, "cmake build " + name)
    run(["cmake", "--install", build], env, "cmake install " + name)


def openh264(env):
    """OpenH264's encoder, its common and processing parts, in C++ only, into openh264.lib."""
    out = os.path.join(WORK, "openh264")
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.obj")):
        os.remove(f)
    c = os.path.join(H264, "codec")
    srcs = []
    for d in ("common/src", "encoder/core/src", "encoder/plus/src", "processing/src/*"):
        srcs += glob.glob(os.path.join(c, d, "*.cpp"))
    srcs = [s for s in srcs if os.path.basename(s) != "DllEntry.cpp"]
    incs = ["api/wels", "common/inc", "encoder/core/inc", "encoder/plus/inc", "processing/interface",
            "processing/src/common", "processing/src/adaptivequantization", "processing/src/downsample",
            "processing/src/scrolldetection", "processing/src/vaacalc"]
    rsp = os.path.join(out, "srcs.rsp")
    with open(rsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % s for s in srcs) + "\n")
    run(["cl", "/nologo", "/c", "/MP", "/MT", "/O2", "/GS-", "/Gy", "/W0", "/EHsc", "/DWIN32", "/DNDEBUG",
         "/D_CRT_SECURE_NO_WARNINGS", "/Fo" + out + "\\"] + ["/I" + os.path.join(c, i) for i in incs] + ["@" + rsp],
        env, "cl /c OpenH264 (%d files)" % len(srcs), cwd=out)
    run(["lib", "/nologo", "/OUT:" + os.path.join(PREFIX, "lib", "openh264.lib")] + glob.glob(os.path.join(out, "*.obj")),
        env, "lib openh264.lib", cwd=out)
    inc = os.path.join(PREFIX, "include", "wels")
    os.makedirs(inc, exist_ok=True)
    for h in glob.glob(os.path.join(c, "api", "wels", "*.h")):
        shutil.copy2(h, inc)


def main():
    for d, mark in ((LDC, "deps/libjuice/CMakeLists.txt"), (MBEDTLS, "CMakeLists.txt"), (OPUS, "CMakeLists.txt"),
                    (H264, "codec/api/wels/codec_api.h"), (NVENC, "include/ffnvcodec/nvEncodeAPI.h")):
        if not os.path.exists(os.path.join(d, mark)):
            sys.exit("[build_webrtc] no %s: run python tools/fetch_webrtc.py" % d)
    os.makedirs(os.path.join(PREFIX, "lib"), exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    env = vs_env()
    # DTLS-SRTP, which Mbed TLS leaves out by default and libdatachannel's media needs, on for both
    srtp = ["-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DMBEDTLS_SSL_DTLS_SRTP",
            "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /DMBEDTLS_SSL_DTLS_SRTP"]
    cmake(env, "mbedtls", MBEDTLS, ["-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DMBEDTLS_FATAL_WARNINGS=OFF"] + srtp)
    cmake(env, "libdatachannel", LDC, ["-DUSE_MBEDTLS=ON", "-DNO_WEBSOCKET=ON", "-DNO_EXAMPLES=ON", "-DNO_TESTS=ON",
                                       "-DMbedTLS_ROOT=" + PREFIX] + srtp)
    cmake(env, "opus", OPUS, ["-DOPUS_BUILD_PROGRAMS=OFF", "-DOPUS_BUILD_TESTING=OFF", "-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF",
                              "-DOPUS_STATIC_RUNTIME=ON"])
    openh264(env)
    # NVIDIA's encoder API, header only
    shutil.copytree(os.path.join(NVENC, "include", "ffnvcodec"), os.path.join(PREFIX, "include", "ffnvcodec"),
                    dirs_exist_ok=True)
    missing = [l for l in LIBS if not os.path.exists(os.path.join(PREFIX, "lib", l))]
    if missing:
        print("[build_webrtc] built, but these are not where the engine looks: %s" % ", ".join(missing))
        print("  in lib/: " + ", ".join(sorted(os.listdir(os.path.join(PREFIX, "lib")))))
        sys.exit(1)
    print("[build_webrtc] OK -> src117\\build\\x64\\webrtc")


if __name__ == "__main__":
    main()
