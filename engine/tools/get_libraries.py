"""Fetches every third-party library the build needs into third_party/, then builds SDL3.

    python tools/get_libraries.py

Each tools/fetch_*.py downloads one library at the version it names and skips one already
there; THIRD-PARTY.md lists them with their licenses. On Windows, tools/build_sdl3.py then
builds SDL3 from its source with the build's patches. Run once before the first build, and
again when a library's version changes.
"""
import glob
import os
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(TOOLS)


def main():
    steps = sorted(glob.glob(os.path.join(TOOLS, "fetch_*.py")))
    if sys.platform == "win32":
        steps.append(os.path.join(TOOLS, "build_sdl3.py"))
    for s in steps:
        print("[get_libraries] " + os.path.basename(s), flush=True)
        if subprocess.run([sys.executable, s], cwd=ENGINE).returncode != 0:
            sys.exit("[get_libraries] %s failed" % os.path.basename(s))
    print("[get_libraries] done")


if __name__ == "__main__":
    main()
