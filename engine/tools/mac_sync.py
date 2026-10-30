"""Send the engine's tree to a Mac to build and package there.

    python tools/mac_sync.py --host user@mac [--key <ssh key>] [--third [<folder> ...]] [--to <folder>]

The Mac's copy has no git: this sends what git tracks under engine/ (and new files not
ignored), the shaders the Windows build wrote (src117/build/x64/shaders/*.h), Hyperion for the
browser that tools/build_engine_web.py built (a LAN room serves it), the documents and test
maps tools/package.py puts in a package, and build_origin.json with the commit,
branch and build number, which tools/build_engine_linux.py takes in git's place. --third adds
the third-party sources, needed for the first build or a new library (with folders named,
only those of engine/third_party/). The files go to
~/hyperion/src on the Mac (--to changes it), as a tar over SSH; HYPERION_MAC and
HYPERION_MAC_KEY stand for --host and --key.

    python tools/mac_sync.py --out <file.tar.gz> --extras [--third]

writes the tar to a file instead, for a Mac that builds from a checkout of the source (the
release's Mac build on GitHub's runners, .github/workflows/mac-release.yml): --extras leaves out
the tracked files and keeps what a checkout lacks.

On the Mac, then:
    cd ~/hyperion/src/engine && python3 tools/build_engine_linux.py && python3 tools/package.py --mac
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
REPO = ENGINE.parent
THIRD = ["SDL3-3.4.16-src", "StormLib-9.40", "CascLib-3.0", "clay-0.14", "stb-6e9f34d", "fonts-8b0a1d0", "webrtc",
         "libvpx-1.17.0", "dr_libs-dfe8377", "libwebp-1.5.0", "dav1d-1.5.4"]
# what tools/package.py reads from outside engine/'s tracked files
PACKAGE_DOCS = ["hxm-for-editors.md", "hxm-format.md", "live-channel-for-editors.md", "release-notes.md"]
MAPS = ENGINE / "testrun" / "play" / "maps" / "Hyperion"


def git(*args) -> str:
    r = subprocess.run(["git"] + list(args), capture_output=True, text=True, cwd=REPO)
    return r.stdout.strip() if r.returncode == 0 else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default=os.environ.get("HYPERION_MAC"), help="user@host of the Mac")
    ap.add_argument("--key", default=os.environ.get("HYPERION_MAC_KEY"), help="the SSH key for it")
    ap.add_argument("--third", nargs="*", help="the third-party sources too (all, or the folders named)")
    ap.add_argument("--to", default="~/hyperion/src", help="the folder on the Mac")
    ap.add_argument("--out", type=Path, help="write the tar to this file instead of sending it")
    ap.add_argument("--extras", action="store_true", help="only what a checkout of the source lacks")
    a = ap.parse_args()
    if not a.host and not a.out:
        sys.exit("no Mac: --host user@host, or HYPERION_MAC")

    files = [] if a.extras else git("ls-files", "-co", "--exclude-standard", "engine").splitlines()
    files += [p.relative_to(REPO).as_posix() for p in (ENGINE / "src117" / "build" / "x64" / "shaders").rglob("*.h")]
    files += ["engine/src117/build/web/play/" + n for n in ("hyperion.js", "hyperion.wasm")]
    files += [] if a.extras else ["docs/" + n for n in PACKAGE_DOCS]
    files += [p.relative_to(REPO).as_posix() for p in sorted(MAPS.glob("*.hxm"))] if MAPS.is_dir() else []
    if a.third is not None:
        for d in a.third or THIRD:
            files += [p.relative_to(REPO).as_posix() for p in (ENGINE / "third_party" / d).rglob("*") if p.is_file()]
    files = [f for f in dict.fromkeys(files) if (REPO / f).is_file()]
    origin = {"commit": git("rev-parse", "--short", "HEAD"), "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
              "dirty": bool(git("status", "--porcelain", "--untracked-files=no", "--", "engine")),
              "number": int(git("rev-list", "--count", "HEAD") or 0)}

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for f in files:
            t.add(str(REPO / f), arcname=f, recursive=False)
        data = json.dumps(origin, indent=2).encode("utf-8")
        info = tarfile.TarInfo("engine/build_origin.json")
        info.size, info.mtime = len(data), int(time.time())
        t.addfile(info, io.BytesIO(data))
    if a.out:
        a.out.write_bytes(buf.getvalue())
        print("%d files (%s bytes), %s -> %s" % (len(files), format(buf.tell(), ","), origin["commit"], a.out))
        return 0
    ssh =["ssh", "-o", "BatchMode=yes"] + (["-i", a.key] if a.key else []) + [a.host]
    remote = "mkdir -p {0} && cd {0} && tar xzf - && echo unpacked".format(a.to)
    print("%d files (%s bytes), %s%s" % (len(files), format(buf.tell(), ","), origin["commit"],
                                          "+" if origin["dirty"] else ""))
    return subprocess.run(ssh + [remote], input=buf.getvalue()).returncode


if __name__ == "__main__":
    sys.exit(main())
