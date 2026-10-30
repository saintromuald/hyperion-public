"""Play Hyperion from a game folder of its own, testrun/play/.

    python tools/play.py                          the menus, full screen
    python tools/play.py --window windowed        in a window (Alt+Enter switches)
    python tools/play.py --renderer direct3d12    through Direct3D 12 (or vulkan, the default, or sdl)
    python tools/play.py --renderer direct3d12 --scene gpu   the GPU draws the map (R4b)
    python tools/play.py --perf                   hyperion_perf.log in the game folder, each second
    python tools/play.py --build                  build Hyperion.exe first
    python tools/play.py --replay <file>          straight into a replay
    python tools/play.py --map <file> [--race terran] [--computers 2]
    python tools/play.py --maps <folder>          list that folder's maps (repeatable)
    python tools/play.py --view 16:9              play games wider (a width or a shape)
    python tools/play.py --setup                  (re)make the folder and stop

The folder is made on the first run, as the replay harness makes its own
(tools/replaytest/replaytest.py): hardlinks to testrun's MPQs and Local.dll,
Patch_rt.mpq, the x64 SDL3.dll, the untouched 1.17 exe as StarCraft.exe, and copies of
the player's 1.17 profiles so the ID screen has one to offer. Hyperion keeps its player data in the
folder it runs from (docs/hyperion-exe.md), so what is played here stays here, apart
from the harness's folders and from Documents/Starcraft.

Every run copies the built Hyperion.exe (src117/build/x64/) and the x64 SDL3.dll in, with
their .pdb files for crash reports, when they differ from the folder's, so the game always runs the latest build, and a build can be made
while the game runs. It copies Hyperion's own mods too (mods/), into the
folder's mods/. --canvas starts Debug Tools' canvas (src117/port/tools/port_canvas.cpp), and
--control opens the live channel (tools/control/). Any option the script does not know goes
to Hyperion.exe as it is;
--replay, --map and --maps paths are made absolute first, since the game starts in its
folder.

The folder has no maps of its own, so without a --maps the script gives Hyperion the map
folders it finds: StarCraft: Remastered's Maps folder (the Blizzard maps and the ladder
ones; those using the tiles Remastered 1.22 added are refused for now) and the
player's Documents/Starcraft/Maps (downloaded maps). Hyperion only reads them.
"""
import argparse
import filecmp
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "replaytest"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "assets"))
import replaytest as rt  # noqa: E402  (the harness's folder helpers)
import scasc  # noqa: E402  (finds the Remastered install)

ROOT = rt.ROOT
PLAY = rt.TESTRUN / "play"
# A folder made before the 64-bit build had the 32-bit SDL3.dll; it is replaced too.
BUILT = [(rt.HX64_BUILT, "Hyperion.exe"), (rt.SDL3_DLL_X64, "SDL3.dll")]
# Their symbols, so a crash report (hyperion_crash.log) names the lines of the build that ran.
SYMBOLS = [(rt.HX64_BUILT.with_suffix(".pdb"), "Hyperion.pdb"), (rt.SDL3_DLL_X64.with_suffix(".pdb"), "SDL3.pdb")]
WINDOWS = ["fullscreen", "windowed", "background", "hidden"]


def build() -> None:
    print("building: build_engine_x64.py")
    if subprocess.run([sys.executable, str(ROOT / "tools" / "build_engine_x64.py")]).returncode != 0:
        sys.exit("build_engine_x64.py failed")


def setup() -> None:
    rt.fill_game_folder(PLAY, x64=True)
    rt._link(rt.oracle_exe(), PLAY / "StarCraft.exe")
    print(f"game folder ready: {PLAY}")


def refresh() -> None:
    """Copy in the builds that differ from the folder's."""
    for src, name in BUILT + SYMBOLS:
        if not src.exists():
            if (src, name) in SYMBOLS:
                continue
            sys.exit(f"no {src}: build it, or run with --build")
        dst = PLAY / name
        if dst.exists() and filecmp.cmp(src, dst, shallow=False):
            continue
        try:
            shutil.copy2(src, dst)
        except PermissionError:
            sys.exit(f"{dst} is in use: close the Hyperion running from {PLAY} first")
        print(f"  {name} <- {src}")
    # Hyperion's own mods, each a folder of text: its mod.json and what it brings.
    # What a mod writes into its folder as it runs (the canvas's map) stays.
    # A mod's data is in folders of its own: data/, and the art built into it.
    for src in sorted((ROOT / "mods").glob("*/**/*")):
        rel = src.relative_to(ROOT / "mods")
        dst = PLAY / "mods" / rel
        if src.is_file() and not (dst.exists() and filecmp.cmp(src, dst, shallow=False)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            print(f"  mods/{rel.as_posix()} <- {src}")
    # the page a LAN room serves to browsers (web/lan), beside the program as the builds put it
    for src in sorted((ROOT / "web").glob("**/*")):
        rel = src.relative_to(ROOT)
        dst = PLAY / rel
        if src.is_file() and not (dst.exists() and filecmp.cmp(src, dst, shallow=False)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            print(f"  {rel.as_posix()} <- {src}")
    # the built-in HUD designs (huds/), beside the program as the builds put them
    for src in sorted((ROOT / "huds").glob("**/*")):
        rel = src.relative_to(ROOT)
        dst = PLAY / rel
        if src.is_file() and not (dst.exists() and filecmp.cmp(src, dst, shallow=False)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            print(f"  {rel.as_posix()} <- {src}")
    # and Hyperion for the browser beside its page in web/play, once tools/build_engine_web.py has built it
    for name in ("hyperion.js", "hyperion.wasm"):
        src = ROOT / "src117" / "build" / "web" / "play" / name
        dst = PLAY / "web" / "play" / name
        if src.is_file() and not (dst.exists() and filecmp.cmp(src, dst, shallow=False)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            print(f"  web/play/{name} <- {src}")
    # the release notes' pictures (notes/media, docs/versioning.md); the notes are in the exe
    for src in sorted((ROOT / "notes" / "media").glob("**/*")):
        rel = src.relative_to(ROOT / "notes")
        dst = PLAY / "notes" / rel
        if src.is_file() and not (dst.exists() and filecmp.cmp(src, dst, shallow=False)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            print(f"  notes/{rel.as_posix()} <- {src}")


def default_map_folders() -> list[Path]:
    found = []
    install = scasc.find_install()
    if install and (install / "Maps").is_dir():
        found.append(install / "Maps")
    docs = rt.player_data_folder()
    if docs and (docs / "Maps").is_dir():
        found.append(docs / "Maps")
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--window", choices=WINDOWS, help="how the window shows (HYPERION_WINDOW); "
                    "Hyperion's default is fullscreen")
    ap.add_argument("--renderer", choices=["sdl", "direct3d12", "vulkan", "gpu"],
                    help="the presenter (HYPERION_RENDERER, docs/renderer.md, R4a); "
                    "Hyperion's default is vulkan, then the driver SDL picks (direct3d12 on Windows), sdl when no GPU driver starts")
    ap.add_argument("--scene", choices=["terrain", "sprites", "map", "screen", "gpu", "off"],
                    help="the GPU scene (HYPERION_GPU_SCENE, docs/renderer.md, R4b); gpu, the GPU "
                    "alone drawing the map, is Hyperion's default with a GPU presenter, off keeps "
                    "the software drawing it")
    ap.add_argument("--perf", action="store_true",
                    help="write frames and times each second to hyperion_perf.log (HYPERION_PERF)")
    ap.add_argument("--build", action="store_true", help="build Hyperion.exe first")
    ap.add_argument("--setup", action="store_true", help="make the game folder again and stop")
    ap.add_argument("--wait", action="store_true", help="wait for the game to close")
    args, rest = ap.parse_known_args()

    if args.build:
        build()
    if args.setup or not (PLAY / "StarCraft.exe").exists():
        setup()
        if args.setup:
            return 0
    refresh()

    # --replay, --map and --maps name files and folders from where this script was started
    for i, a in enumerate(rest):
        for opt in ("--replay", "--map", "--maps"):
            if a == opt and i + 1 < len(rest):
                rest[i + 1] = str(Path(rest[i + 1]).resolve())
            elif a.startswith(opt + "="):
                rest[i] = opt + "=" + str(Path(a[len(opt) + 1:]).resolve())
    if not any(a == "--maps" or a.startswith("--maps=") for a in rest):
        for folder in default_map_folders():
            rest += ["--maps", str(folder)]

    env = dict(os.environ)
    env.pop("HYPERION_DLLS", None)  # no development DLLs
    if args.window:
        env["HYPERION_WINDOW"] = args.window
    if args.renderer:
        env["HYPERION_RENDERER"] = args.renderer
    if args.scene:
        env["HYPERION_GPU_SCENE"] = args.scene
    if args.perf:
        env["HYPERION_PERF"] = "1"
    # the folder's own data stays where it was before Hyperion\ became the default
    # (port_paths.cpp): profiles, settings, saves and logs in testrun/play/ itself
    cmd = [str(PLAY / "Hyperion.exe"), f"--data={PLAY}"] + rest
    print("starting: " + subprocess.list2cmdline(cmd))
    proc = subprocess.Popen(cmd, cwd=PLAY, env=env)
    return proc.wait() if args.wait else 0


if __name__ == "__main__":
    sys.exit(main())
