"""Compile and run the native ReplayKraken/library contract tests (no game data/network)."""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ENGINE, "tools", "replay-tests")


def run_transport(compiler, out):
    binary = os.path.join(out, "http_core_tests")
    curl = os.path.join(ENGINE, "src117", "build", "linux-x64", "curl", "lib", "libcurl.a")
    webrtc = os.path.join(ENGINE, "src117", "build", "linux-x64", "webrtc", "lib")
    if not os.path.isfile(curl) or not os.path.isdir(webrtc):
        print("build the native libcurl/Mbed TLS libraries before transport tests")
        return 2
    sources = [
        os.path.join(TOOLS, "http_core_tests.cpp"),
        "src117/port/platform/port_http.cpp",
        "src117/port/replays/port_replaykraken.cpp",
        "src117/port/platform/port_json.cpp",
    ]
    libraries = [curl] + [os.path.join(webrtc, name) for name in
                          ["libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a", "libeverest.a", "libp256m.a"]]
    command = [compiler, "-std=c++20", "-O2", "-Isrc117", "-Ithird_party/curl-8.23.0/include"] + sources + libraries + [
        "-lpthread", "-ldl", "-lm", "-o", binary]
    built = subprocess.run(command, cwd=ENGINE)
    if built.returncode:
        return built.returncode
    server = subprocess.Popen([sys.executable, os.path.join(TOOLS, "transport_fixture.py")],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", 18790), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            return 1
        return subprocess.run([binary], cwd=ENGINE).returncode
    finally:
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()


def main():
    compiler = shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        print("no clang++/c++ compiler for native replay contract tests")
        return 2
    with tempfile.TemporaryDirectory(prefix="hyperion-replay-tests-") as out:
        binary = os.path.join(out, "replay_core_tests")
        includes = [
            "src117", "src117/compat", "src117/h", "src117/port/front", "src117/port/replays",
            "src117/port/platform", "src117/port/data", "src117/port/render",
            "third_party/SDL3-3.4.16-src/include",
        ]
        sources = [
            os.path.join(TOOLS, "replay_core_tests.cpp"),
            "src117/port/replays/port_replaykraken.cpp",
            "src117/port/replays/port_replaylibrary.cpp",
            "src117/port/replays/port_replayclaims.cpp",
            "src117/port/replays/port_replaycatalog.cpp",
            "src117/port/platform/port_json.cpp",
        ]
        command = [compiler, "-std=c++20", "-O2", "-ffunction-sections", "-fdata-sections",
                   "-Wno-ignored-attributes"] + ["-I" + p for p in includes] + sources + [
                   "-Wl,--gc-sections", "-o", binary]
        built = subprocess.run(command, cwd=ENGINE)
        if built.returncode:
            return built.returncode
        core = subprocess.run([binary, os.path.join(TOOLS, "fixtures")], cwd=ENGINE).returncode
        if core:
            return core
        return run_transport(compiler, out)


if __name__ == "__main__":
    sys.exit(main())
