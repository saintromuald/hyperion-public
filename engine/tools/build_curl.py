"""Build the small static libcurl used by Replay Kraken, for either native build.

The browser never calls this. All native targets use the Mbed TLS libraries already
built for streamed seats and the dated CA bundle fetched beside curl's source.
"""
import os
import shutil
import subprocess

VERSION = "8.23.0"
CA_DATE = "2026-11-05"


def build(engine, out, webrtc, *, windows=False, mac=False, again=False, env=None, mac_flags=None):
    source = os.path.join(engine, "third_party", "curl-" + VERSION)
    ca_source = os.path.join(engine, "third_party", "cacert-" + CA_DATE + ".pem")
    if not os.path.isdir(source):
        raise SystemExit("build_curl: no %s: run python tools/fetch_curl.py" % source)
    if not os.path.isfile(ca_source):
        raise SystemExit("build_curl: no %s: run python tools/fetch_cacert.py" % ca_source)
    dest = os.path.join(out, "curl")
    lib = os.path.join(dest, "lib", "libcurl.lib" if windows else "libcurl.a")
    ca = os.path.join(out, "cacert.pem")
    if not os.path.isfile(ca) or open(ca, "rb").read() != open(ca_source, "rb").read():
        shutil.copyfile(ca_source, ca)
    if os.path.isfile(lib) and not again:
        return {"include": os.path.join(source, "include"), "lib": lib, "ca": ca}
    work = os.path.join(dest, "work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    mbedtls = os.path.join(webrtc, "lib")
    defs = [
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_CURL_EXE=OFF", "-DBUILD_TESTING=OFF",
        "-DBUILD_EXAMPLES=OFF", "-DBUILD_MISC_DOCS=OFF", "-DCURL_DISABLE_INSTALL=ON",
        "-DCURL_ENABLE_EXPORT_TARGET=OFF", "-DCURL_USE_OPENSSL=OFF", "-DCURL_USE_MBEDTLS=ON",
        "-DMBEDTLS_INCLUDE_DIR=" + os.path.join(webrtc, "include"),
        "-DMBEDTLS_LIBRARY=" + os.path.join(mbedtls, "mbedtls.lib" if windows else "libmbedtls.a"),
        "-DMBEDX509_LIBRARY=" + os.path.join(mbedtls, "mbedx509.lib" if windows else "libmbedx509.a"),
        "-DMBEDCRYPTO_LIBRARY=" + os.path.join(mbedtls, "mbedcrypto.lib" if windows else "libmbedcrypto.a"),
        "-DCURL_USE_LIBSSH2=OFF", "-DCURL_USE_LIBPSL=OFF", "-DCURL_USE_PKGCONFIG=OFF",
        "-DCURL_USE_CMAKECONFIG=OFF", "-DCURL_ZLIB=OFF", "-DCURL_BROTLI=OFF", "-DCURL_ZSTD=OFF",
        "-DUSE_NGHTTP2=OFF", "-DUSE_LIBIDN2=OFF", "-DCURL_DISABLE_PROXY=ON",
        "-DCURL_DISABLE_COOKIES=ON", "-DCURL_DISABLE_FTP=ON", "-DCURL_DISABLE_DICT=ON",
        "-DCURL_DISABLE_FILE=ON", "-DCURL_DISABLE_GOPHER=ON", "-DCURL_DISABLE_IMAP=ON",
        "-DCURL_DISABLE_LDAP=ON", "-DCURL_DISABLE_MQTT=ON", "-DCURL_DISABLE_POP3=ON",
        "-DCURL_DISABLE_RTSP=ON", "-DCURL_DISABLE_SMTP=ON", "-DCURL_DISABLE_TELNET=ON",
        "-DCURL_DISABLE_TFTP=ON", "-DCURL_DISABLE_IPFS=ON", "-DCURL_DISABLE_WEBSOCKETS=ON",
        "-DCURL_DISABLE_HSTS=ON", "-DCURL_DISABLE_ALTSVC=ON", "-DCURL_DISABLE_DOH=ON",
        "-DCURL_DISABLE_NETRC=ON", "-DCURL_DISABLE_CA_SEARCH=ON",
        "-DCURL_CA_BUNDLE=" + ca_source, "-DCURL_CA_PATH=none",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    ]
    if windows:
        defs += ["-DCMAKE_C_COMPILER=cl", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
    else:
        defs += ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_C_FLAGS=-fsigned-char -fPIC"]
    defs += list(mac_flags or [])
    print("[build_curl] cmake libcurl", flush=True)
    subprocess.run(["cmake", "-S", source, "-B", work, "-G", "Ninja"] + defs,
                   cwd=engine, env=env, check=True)
    print("[build_curl] ninja libcurl", flush=True)
    subprocess.run(["ninja", "-C", work], cwd=engine, env=env, check=True)
    built = os.path.join(work, "lib", os.path.basename(lib))
    if not os.path.isfile(built):
        raise SystemExit("build_curl: libcurl did not produce %s" % built)
    os.makedirs(os.path.dirname(lib), exist_ok=True)
    shutil.copyfile(built, lib)
    return {"include": os.path.join(source, "include"), "lib": lib, "ca": ca}
