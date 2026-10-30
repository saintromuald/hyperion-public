"""Hyperion's tests that need none of the game's files.

    python tools/run_tests.py [--exe <Hyperion>] [--game <StarCraft folder>] [--only <name>,...]

Run from engine/ after a build. Each test prints a line, and the exit code is 0 when all
pass. The tests:

  notes      the release notes are well formed and name the version (tools/release_notes.py)
  catalogs   the translation catalogs match the sources and each other (tools/locale_catalog.py)
  huds       every built-in HUD design passes Hyperion's own check (--hud-check)
  maps       a map written here from nothing passes the map check (--hxm-check), and compiles
             to the same classic map twice (--hxm-compile)
  mods       every built-in mod's JSON reads; with --game, each also passes --mod-check against
             that StarCraft

The replay comparisons that keep the game the same as the original need the game's files and
a replay collection, and are run before each release.
"""
import argparse
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILT = [os.path.join("src117", "build", "x64", "Hyperion.exe"),
         os.path.join("src117", "build", "linux-x64", "Hyperion.x86_64"),
         os.path.join("src117", "build", "linux-arm64", "Hyperion.aarch64"),
         os.path.join("src117", "build", "mac-arm64", "Hyperion.arm64")]


def exe_path(given):
    if given:
        return given
    for b in BUILT:
        p = os.path.join(ENGINE, b)
        if os.path.exists(p):
            return p
    return ""


def run(cmd, timeout=120):
    r = subprocess.run(cmd, cwd=ENGINE, capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=timeout)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def tool(name, *args):
    code, out = run([sys.executable, os.path.join(ENGINE, "tools", name)] + list(args))
    return code == 0, out.strip().splitlines()[-1:] if code == 0 else out.strip().splitlines()[-8:]


def test_notes(a):
    return tool("release_notes.py", "check")


def test_catalogs(a):
    return tool("locale_catalog.py", "check")


def test_huds(a):
    if not a.exe:
        return None, ["no Hyperion built"]
    folder = os.path.join(ENGINE, "huds")
    styles = os.path.join(folder, "styles")
    bad, pairs = [], 0
    for n in sorted(n for n in os.listdir(folder) if n.endswith(".json")):
        # each layout with the styles Settings gives it (PortHudDesignFiles)
        wear = ["terran-r8"] if n.startswith("terran-") else \
            [s[:-5] for s in sorted(os.listdir(styles)) if s.startswith("mech-")]
        for s in wear:
            code, out = run([a.exe, "--hud-check", os.path.join(folder, n), "--style", os.path.join(styles, s + ".json")])
            pairs += 1
            if code != 0:
                bad.append("%s with %s: exit %d %s" % (n, s, code, out.strip()[-300:]))
    return not bad, bad or ["%d layouts and styles" % pairs]


def blank_map():
    """A 64 by 64 jungle map for two players, each with a start location, as an editor would
    write one."""
    w = h = 64
    doc = {
        "format": "hyperion-map", "version": 1, "name": "Test map", "tileset": "jungle", "size": [w, h],
        "players": [{"slot": "human", "race": "user_select", "force": 0},
                    {"slot": "human", "race": "user_select", "force": 0}],
        "forces": [{"name": "Players", "flags": ["random_start"]}],
        "units": [{"type": "start_location", "x": 8 * 32 + 64, "y": 8 * 32 + 48, "owner": 0},
                  {"type": "start_location", "x": 54 * 32 + 64, "y": 54 * 32 + 48, "owner": 1}],
        "layers": {"tiles": {"file": "layers/tiles.bin", "type": "u16"}},
    }
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("map.json", json.dumps(doc, indent=1))
        z.writestr("layers/tiles.bin", struct.pack("<%dH" % (w * h), *([0] * (w * h))))
    return buf.getvalue()


def test_maps(a):
    if not a.exe:
        return None, ["no Hyperion built"]
    with tempfile.TemporaryDirectory() as tmp:
        hxm = os.path.join(tmp, "test.hxm")
        with open(hxm, "wb") as f:
            f.write(blank_map())
        code, out = run([a.exe, "--hxm-check", hxm])
        if code != 0:
            return False, ["--hxm-check exit %d" % code] + out.strip().splitlines()[-8:]
        chks = []
        for i in (1, 2):
            chk = os.path.join(tmp, "test%d.chk" % i)
            code, out = run([a.exe, "--hxm-compile", hxm, chk])
            if code != 0 or not os.path.exists(chk):
                return False, ["--hxm-compile exit %d" % code] + out.strip().splitlines()[-8:]
            with open(chk, "rb") as f:
                chks.append(f.read())
        if chks[0] != chks[1]:
            return False, ["two compiles of the same map differ"]
        return True, ["plays; %d-byte classic map, the same twice" % len(chks[0])]


def test_mods(a):
    folder = os.path.join(ENGINE, "mods")
    bad, mods = [], sorted(d for d in os.listdir(folder) if os.path.isdir(os.path.join(folder, d)))
    for m in mods:
        for root, _, names in os.walk(os.path.join(folder, m)):
            for n in names:
                if n.endswith(".json"):
                    p = os.path.join(root, n)
                    try:
                        with open(p, encoding="utf-8") as f:
                            json.load(f)
                    except ValueError as e:
                        bad.append("%s: %s" % (os.path.relpath(p, ENGINE), e))
    if a.game and a.exe:
        for m in mods:
            code, out = run([a.exe, "--mod-check", m, "--install", a.game], timeout=600)
            if code != 0:
                bad.append("%s: --mod-check exit %d %s" % (m, code, out.strip()[-300:]))
    return not bad, bad or ["%d mods%s" % (len(mods), ", checked against the game" if a.game else "")]


TESTS = [("notes", test_notes), ("catalogs", test_catalogs), ("huds", test_huds), ("maps", test_maps),
         ("mods", test_mods)]


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="the Hyperion to test (default: the newest build here)")
    ap.add_argument("--game", help="a StarCraft folder, for the mod checks")
    ap.add_argument("--only", help="the tests to run, by name, with commas")
    a = ap.parse_args()
    a.exe = exe_path(a.exe)
    only = set(a.only.split(",")) if a.only else None
    failed = 0
    for name, fn in TESTS:
        if only and name not in only:
            continue
        ok, lines = fn(a)
        mark = "pass" if ok else "skip" if ok is None else "FAIL"
        failed += ok is False
        print("%-4s  %-9s %s" % (mark, name, lines[0] if lines else ""))
        for line in lines[1:] if ok is False else []:
            print("                " + line)
    print("%d failed" % failed if failed else "all passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
