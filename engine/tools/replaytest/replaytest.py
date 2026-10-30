#!/usr/bin/env python3
"""Replay test runner: play a replay on the 1.17 oracle and on Hyperion, at full
speed and in the background, log the game state every frame, and compare.

Every run carries the same observer, rt_observer.dll (see rt_observer.cpp): the
oracle, the untouched 1.17 exe, has it injected; Hyperion.exe loads it itself.

    python tools/replaytest/replaytest.py setup
    python tools/replaytest/replaytest.py run --build oracle --replay X.rep
    python tools/replaytest/replaytest.py suite            (the full set, at a merge)
    python tools/replaytest/replaytest.py suite --quick    (four replays, on every commit)
    python tools/replaytest/replaytest.py suite --replays DIR
    python tools/replaytest/replaytest.py gate [--hx] [--merge]   (the gates, docs/modules/build.md)
    python tools/replaytest/replaytest.py corpus           (the replays corpus.json names, checked)
    python tools/replaytest/replaytest.py report testrun/rt_runs/<stamp>

An engine build plays each replay once for each kind of game files --data names, by default
StarCraft: Remastered's install alone and then 1.17's MPQs (the decision of 2026-10-21: new
players have Remastered's files, and the checks go by them first). The oracle reads only the
MPQs, so both are held to the same oracle run. A run on Remastered's files is tagged hxr, one
on the MPQs hx64, as before; each must show it read the files it was given (its
hyperion_init.log, and for Remastered's its file logs: nothing from an MPQ, nothing refused).

`setup` builds the oracle's game folder testrun/rt/ from hardlinks to testrun's
files, plus Patch_rt.mpq (a replay needs game\\nconsole.pcx, which only that
archive holds) and a copy of the oracle exe with DYNAMICBASE cleared, so that its
addresses are the ones the observer reads. The copy is named Hyperion.exe so runs
here and another harness's runs can go at the same time: such a harness
waits on any process named Starcraft.exe, and ours only on Hyperion.exe. The
observer lets the game start next to another copy (rt_observer.cpp,
hk_CreateEventA), and Hyperion scopes the check to its folder
(src117/port/lan/port_instance.cpp).

The build "hx64" is Hyperion, the x64 Hyperion.exe, run
from a second game folder, testrun/hx64/, laid out like a player's install: Hyperion.exe
with the MPQs, Local.dll and the x64 SDL3.dll, and no StarCraft.exe, which Hyperion has not
never needed. Nothing is injected; Hyperion.exe loads the x64 observer itself,
from HYPERION_DLLS. Its observer does not hash a unit's raw bytes, so h_raw is not
compared for it.

The oracle reaches the replay through the menus, driven by the observer (MENU).
Hyperion starts it from the command line instead, with --replay
(src117/port/gameplay/port_launch.cpp), unless --menus is given: the menus stop while the
window is in the background, and a run that waits on them can time out.

The build "lx64" is the Linux Hyperion (tools/build_engine_linux.py), run from
testrun/lx64/, laid out as hx64's folder is, with the Linux program Hyperion.x86_64,
libSDL3.so.0 and the observer as rt_observer.so. The build "la64" is the same for ARM64
Linux (Hyperion.aarch64, testrun/la64/): on an ARM machine it runs where it is, and on
Windows in the hyperion-linux-arm64 Docker image, which Docker runs under emulation, with
SDL's offscreen video and no sound. On Windows it runs in WSL (the distribution HX_WSL_DISTRO
names, Ubuntu by default); on Linux, where it is. Its reference is the oracle's kept run
(rt_cache/oracle), which a Linux machine takes from a Windows one. The build "ma64" is the
Mac's (Hyperion.arm64, libSDL3.0.dylib, rt_observer.dylib, testrun/ma64/), which runs only on
an Apple-silicon Mac, where it is.

Output goes to testrun/rt_runs/<stamp>/ (gitignored): a CSV and a JSON per run, the run's
summary.json (each verdict, and what it ran on: the commit, the files, the install's build) and
report.html. A suite exits 1 when any run fails. docs/replay-test.md describes the log format
and the checks.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTRUN = ROOT / "testrun"
GAME = TESTRUN / "rt"
# Where the oracle's Storm keeps user data, saves included (SFileGetUserDataPath).
ORACLE_USER_DATA = Path.home() / "Documents" / "StarCraft"
GAME_EXE = GAME / "Hyperion.exe"
RUNS = TESTRUN / "rt_runs"
BUILD = ROOT / "src117" / "build"
ORACLE_SHA1 = "f7e2c31dcb69752e1729825de62317879c32732e"
ORACLE_CANDIDATES = [ROOT / "oracle" / "StarCraft.exe"]
PATCH_RT_CANDIDATES = [
    Path(r"D:\Downloads\StacraftFiles\mpqs_from_repo\Patch_rt.mpq"),
    TESTRUN / "Patch_rt.mpq",  # a machine without the Windows one's Downloads, a Mac's
]
# Files the game folder takes from testrun, by hardlink. ddraw.ini is copied
# instead, with a section for Hyperion.exe added (cnc-ddraw reads per-game
# settings from a section named after the exe). Only the oracle loads
# cnc-ddraw. Its window goes off the desktop: cnc-ddraw hands the game the mouse
# whenever the pointer crosses the window, and a mouse at work on the machine
# would move the oracle's cursor and its snapshots with it.
LINKED = ["storm.dll", "Local.dll", "Smackw32.dll", "battle.snp", "standard.snp",
          "StarDat.mpq", "BrooDat.mpq", "Starcraft.mpq", "BroodWar.mpq", "ddraw.dll"]
DDRAW_SECTION = ("\n; Hyperion (the oracle exe under the harness's name)\n[Hyperion]\n"
                 "game_handles_close=true\nposX=-12000\nposY=-12000\n")
# Hyperion: the x64 build's own folder and files. Its
# folder takes the MPQs and Local.dll, whose resources the engine reads as data.
ENGINE_BUILDS = {"hx64", "lx64", "la64", "ma64"}
HX64_GAME = TESTRUN / "hx64"
HX64_EXE = HX64_GAME / "Hyperion.exe"
# Hyperion on a StarCraft: Remastered install alone (--data remastered): a folder with
# Hyperion and SDL3 and none of 1.17's files, so the game reads the install it finds.
HXR_GAME = TESTRUN / "hxr"
# The same for the Linux x64 build, in WSL, reading the install through /mnt.
LXR_GAME = TESTRUN / "lxr"
# The kinds of game files an engine build is given (HYPERION_DATA_SOURCE, port_remastered.cpp):
# Remastered's install alone, 1.17's MPQs, or the install searched before the MPQs.
DATA_SOURCES = ("remastered", "mpq", "both")
DEFAULT_DATA = "remastered,mpq"
DATA_WORDS = {"remastered": "Remastered's files", "mpq": "the MPQs", "both": "Remastered's files over the MPQs"}
# Battle.net's entry for StarCraft: Remastered, which names its folder (as tools/assets/scasc.py
# finds it).
UNINSTALL_KEY = r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\StarCraft"
# The logs HYPERION_FILE_LOG has the engine write in its data folder: every name asked for and
# where it came from (port_stormfile.cpp), and every name asked of the install (port_casc.cpp).
FILE_LOGS = ("hyperion_files.log", "hyperion_casc.log")
HX64_BUILT = BUILD / "x64" / "Hyperion.exe"
HX64_OBSERVER = BUILD / "x64" / "rt_observer.dll"
SDL3_DLL_X64 = ROOT / "third_party" / "SDL3-3.4.16" / "lib" / "x64" / "SDL3.dll"
# The Linux builds and the Mac's: each one's folder, build output and program; the
# program, SDL and the observer are copied into the folder.
LINUX_BUILDS = {
    "lx64": (TESTRUN / "lx64", BUILD / "linux-x64", "Hyperion.x86_64", "x86_64"),
    "la64": (TESTRUN / "la64", BUILD / "linux-arm64", "Hyperion.aarch64", "aarch64"),
    "ma64": (TESTRUN / "ma64", BUILD / "mac-arm64", "Hyperion.arm64", "arm64"),
}
WSL_DISTRO = os.environ.get("HX_WSL_DISTRO", "Ubuntu")
ARM64_IMAGE = "hyperion-linux-arm64"
STAGED_NAME = "~oc_run.rep"  # sorts after every other entry, so {DOWN*n} lands on it

# The suite's replays, named with their sha1 and source in corpus.json (the files are kept in
# testrun, gitignored). Its sets: quick on every commit (a version-59 map played as a scenario,
# the 8-player game, a replay on which the original loses commands, and the game with computers,
# whose AI plays again in playback); full at a merge; archive, the rest of the SSCAIT 2016
# archive, with gate --merge.
REPLAYS = TESTRUN / "rt_replays"
CORPUS = Path(__file__).resolve().parent / "corpus.json"


def corpus() -> list[dict]:
    return json.loads(CORPUS.read_text(encoding="utf-8"))["replays"]


def corpus_set(name: str) -> list[Path]:
    return [REPLAYS / r["path"] for r in corpus() if name in r["sets"]]


def corpus_known(replay: str | Path | None) -> dict | None:
    """The early end corpus.json gives the oracle on this replay, by the replay's sha1, or None."""
    if not replay or not Path(replay).exists():
        return None
    s = sha1(Path(replay))
    return next((r.get("known") for r in corpus() if r["sha1"] == s), None)
# Oracle runs, kept by what their log depends on (oracle_cache_key): the oracle plays a
# replay the same way every time, so a suite reuses its run instead of repeating it.
ORACLE_CACHE = TESTRUN / "rt_cache" / "oracle"
# Hyperion's own replays (hxRS: a map past 256 tiles; players past 7), which the
# original cannot play. Each is held to a kept run of Hyperion's own, made by a build that
# was known to play it right (hxsuite --keep-ref), in place of the oracle's.
HX_SET = REPLAYS / "hyperion"
HX_REF = TESTRUN / "rt_cache" / "hx_ref"
# What the Hyperion set is made of: one game an entry, which hxrecord plays and records
# (Hyperion's --record, src117/port/replays/port_record.cpp) on the maps of HX_MAPS.
HX_MANIFEST = Path(__file__).resolve().parent / "hxsuite.json"
HX_MAPS = TESTRUN / "play" / "maps" / "Hyperion"
HX_SYNC_EVERY = 24  # the H lines' period, the same as the replay's own checks
# An entry's checkpoint: the live game is saved at that frame, and the save is kept beside
# the replay as <name>.sav; hxsuite loads it and holds the loaded game to the live one.

# The menu path to a replay, found by walking the menus with the observer:
#   screen 0  main menu: S (Single Player), E (Expansion), Enter (the ID screen, 5)
#   screen 22 the expansion campaign screen. Its result 11 opens the replay
#             browser (do_xcampaign_dlg), but no control on it has index 11, so
#             the observer ends the dialog with 11 as a button would.
#   screen 12 do_create_dlg(1), the replay browser on maps\replays: Down to the
#             staged file, which sorts last, then O (Ok).
MENU = [
    "4,0,1500,S{WAIT1500}E{WAIT1500}{ENTER}",
    "4,22,1500,{RETURN11}",
    "4,12,2000,{DOWN*80}{WAIT800}O",
]
# A saved game (--load): past the profile to the Brood War campaign screen, where the
# Load screen's Ok is taken for the file ({LOAD}) and the screen ended.
MENU_LOAD = [
    "4,0,1500,S{WAIT1500}E{WAIT1500}{ENTER}",
    "4,22,1500,{LOAD}{RETURN0}",
]


def sha1(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pe_sha1(path: Path) -> str:
    """sha1 of a DLL or exe with the fields a relink always changes set to 0: the COFF
    and export directory time stamps and the checksum. A rebuild of the same source
    then hashes the same (checked on rt_observer.dll, 2026-09-28), so rebuilding the
    observer does not throw away the kept oracle runs."""
    b = bytearray(path.read_bytes())
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    opt = pe + 24
    b[pe + 8:pe + 12] = bytes(4)
    b[opt + 64:opt + 68] = bytes(4)
    exp_rva = struct.unpack_from("<I", b, opt + 96)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    sec0 = opt + struct.unpack_from("<H", b, pe + 20)[0]
    for i in range(nsec):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", b, sec0 + i * 40 + 8)
        if exp_rva and va <= exp_rva < va + max(vsize, rsize):
            o = exp_rva - va + raw
            b[o + 4:o + 8] = bytes(4)
    return hashlib.sha1(b).hexdigest()


def oracle_exe() -> Path:
    for c in ORACLE_CANDIDATES:
        if c.exists():
            return c
    sys.exit("oracle exe not found: " + ", ".join(map(str, ORACLE_CANDIDATES)))


def game_running() -> bool:
    """True while a Hyperion.exe runs. Other copies of the game do not count.
    By name, because the game's InitACL denies other
    processes the rights needed to read its exe path. None off Windows, where the oracle
    does not run and its runs come from the cache."""
    if sys.platform != "win32":
        return False
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_EXE.name}", "/NH"],
                         capture_output=True, text=True).stdout
    return GAME_EXE.name.lower() in out.lower()


# ---------------------------------------------------------------------------
# what a run ran on

def data_sources(args) -> list[str]:
    """The kinds of game files --data names, in its order."""
    names = [s.strip() for s in (getattr(args, "data", None) or DEFAULT_DATA).split(",") if s.strip()]
    bad = [s for s in names if s not in DATA_SOURCES]
    if bad or not names:
        sys.exit(f"--data: {', '.join(bad) or 'nothing'} is not one of {', '.join(DATA_SOURCES)}")
    return names


def engine_tag(build: str, data: str) -> str:
    """A run's tag: the build's name on the MPQs, as the suite has always named it; with an r
    for Remastered's files alone (hxr, lxr) and a b for both (hxb)."""
    if data == "mpq":
        return build
    return build[:2] + {"remastered": "r", "both": "b"}[data]


def source_ok(build: str, data: str) -> bool:
    """Whether the harness can give this build these files: Remastered's to Windows and to the
    Linux x64 build in WSL; the ARM64 image and the Mac read the MPQs only."""
    return data == "mpq" or build in ("hx64", "lx64")


def engine_folder(build: str, data: str) -> Path:
    """The folder an engine build runs from with these files: one with the MPQs, or for
    Remastered's files alone one with none of 1.17's."""
    if build in LINUX_BUILDS:
        return LXR_GAME if data == "remastered" else LINUX_BUILDS[build][0]
    return HXR_GAME if data == "remastered" else HX64_GAME


_INSTALL: list = []


def remastered_install(args=None) -> Path:
    """The StarCraft: Remastered install a run reads: --remastered, HYPERION_REMASTERED, or the
    folder Battle.net's uninstall entry names. The harness hands it to the engine as
    HYPERION_REMASTERED, so a run reads this install whatever the settings say."""
    if not _INSTALL:
        given = getattr(args, "remastered", None) or os.environ.get("HYPERION_REMASTERED")
        path = Path(given) if given else None
        if path is None and sys.platform == "win32":
            import winreg
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, UNINSTALL_KEY) as k:
                    path = Path(winreg.QueryValueEx(k, "InstallLocation")[0])
            except OSError:
                pass
        if path is None:
            sys.exit("no StarCraft: Remastered install found: give --remastered <folder>, or --data mpq")
        _INSTALL.append(path)
    return _INSTALL[0]


def casc_build(install: Path) -> str | None:
    """The install's version, from its .build.info (the active row's Version), or None."""
    try:
        lines = (install / ".build.info").read_text(errors="replace").splitlines()
    except OSError:
        return None
    head = [h.split("!")[0] for h in lines[0].split("|")] if lines else []
    for line in lines[1:]:
        cells = line.split("|")
        if len(cells) == len(head) and cells[head.index("Active")] == "1":
            return cells[head.index("Version")]
    return None


def git_state() -> tuple[str, bool]:
    """The commit and whether the engine's tracked files differ from it (the repository's
    other folders, a design session's, left out)."""
    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                          cwd=ROOT).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no", "--", "."],
                           capture_output=True, text=True, cwd=ROOT).stdout.strip() != ""
    return head, dirty


def git_head() -> str:
    head, dirty = git_state()
    return head + ("+dirty" if dirty else "")


FILE_SHA1_CACHE = TESTRUN / "rt_cache" / "file_sha1.json"


def file_sha1(path: Path) -> str | None:
    """sha1 of a large file (an MPQ), kept by its path, size and time so it is read once."""
    if not path.exists():
        return None
    st = path.stat()
    key = str(path.resolve())
    try:
        kept = json.loads(FILE_SHA1_CACHE.read_text())
    except (OSError, ValueError):
        kept = {}
    if kept.get(key, [None, None])[:2] == [st.st_size, st.st_mtime_ns]:
        return kept[key][2]
    h = sha1(path)
    kept[key] = [st.st_size, st.st_mtime_ns, h]
    FILE_SHA1_CACHE.parent.mkdir(parents=True, exist_ok=True)
    FILE_SHA1_CACHE.write_text(json.dumps(kept, indent=1))
    return h


def mpq_sha1s(game: Path) -> dict:
    """sha1 of each MPQ and DLL in a game folder that has them."""
    names = [n for n in LINKED if n.lower().endswith((".mpq", ".dll"))] + ["Patch_rt.mpq"]
    return {n: file_sha1(game / n) for n in names if (game / n).exists()}


def provenance(args, sources: list[str]) -> dict:
    """What a suite ran on, for its summary.json and report."""
    head, dirty = git_state()
    p = {"commit": head, "dirty": dirty, "data": sources,
         "oracle_sha1": sha1(GAME_EXE) if GAME_EXE.exists() else None,
         "hyperion_sha1": sha1(HX64_BUILT) if HX64_BUILT.exists() else None,
         "mpqs": mpq_sha1s(GAME)}
    if any(s != "mpq" for s in sources):
        install = remastered_install(args)
        p["remastered"] = str(install)
        p["casc_build"] = casc_build(install)
        # kept for the install alone, so a folder that is no install (a test of the harness) is
        # neither compared nor remembered
        last = TESTRUN / "rt_cache" / "casc_build.txt"
        before = last.read_text().strip() if last.exists() else None
        if p["casc_build"]:
            if before and before != "None" and before != p["casc_build"]:
                print(f"  WARNING: the install is now {p['casc_build']}, {before} at the last run "
                      "(Battle.net updated it); a difference may be the install's")
            last.parent.mkdir(parents=True, exist_ok=True)
            last.write_text(f"{p['casc_build']}\n")
    return p


def source_check(meta: dict, data: str, install: str | None) -> str | None:
    """Why an engine run did not read the files it was given, from its hyperion_init.log
    (hyperion.cpp, WriteInitLog), or None."""
    text = (meta.get("engine_init") or "").lower().replace("/", "\\")
    if not text:
        return "no hyperion_init.log, so which files it read is not known"
    over = "1.17's mpqs under starcraft: remastered at "
    if data == "mpq":
        ok = "the game's files from 1.17's mpqs;" in text
    else:
        at = "starcraft: remastered at " + (install or "").lower().replace("/", "\\").rstrip("\\")
        ok = at in text and ((over in text) == (data == "both"))
    if ok:
        return None
    said = (meta.get("engine_init") or "").split("; ")
    return (f"read other files than {DATA_WORDS[data]}: "
            + next((s for s in said if s.startswith("the game's files")), "its init log names none"))


def logs_aside(game: Path) -> None:
    """The file logs an earlier run left in the folder, which the engine appends to, moved into
    old_logs/<stamp>/ (kept, not removed)."""
    old = [game / n for n in FILE_LOGS if (game / n).exists()]
    if old:
        dst = game / "old_logs" / _dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        dst.mkdir(parents=True, exist_ok=True)
        for f in old:
            shutil.move(str(f), str(dst / f.name))


def casc_key(name: str) -> str:
    """A name the install was asked for, without the folders the lookup tries in turn (SD\\,
    the locale's) and with a Smacker video as the WebM that stands in for it
    (port_remastered.cpp, Find; port_webm.cpp)."""
    n = name.lower().replace("/", "\\")
    for prefix in ("locales\\enus\\assets\\", "sd\\"):
        if n.startswith(prefix):
            n = n[len(prefix):]
    return n[:-4] + ".webm" if n.endswith(".smk") else n


def files_check(out_dir: Path, tag: str, data: str) -> tuple[str | None, list[str]]:
    """Why a run on Remastered's files failed its file logs, or None, and the names found
    nowhere: alone, no file may come from an MPQ; alone or over the MPQs, no name the game asked
    of the install may be refused (Remastered's paid content, port_casc.cpp) unless its SD
    form was read in its place. The lookup tries the install's top folders after SD\\, so an
    HD portrait's .smk is refused on the way to the SD portrait's .webm. A name whose SD form
    was looked up and is not in the install is listed with the names found nowhere, not
    failed: the game asked and was told there is none, as 1.17's MPQs would tell it of a
    file they lack, and the game goes on without it (the Ultralisk's fourth fidget,
    portrait\\ZUltra\\ZUlFid03, is HD only; sub_4A6B40 then plays the third)."""
    files, casc = out_dir / f"{tag}_files.log", out_dir / f"{tag}_casc.log"
    if not files.exists():
        return "no hyperion_files.log (HYPERION_FILE_LOG)", []
    rows = [line.split("\t") for line in files.read_text(errors="replace").splitlines() if line]
    nowhere = [r[0] for r in rows if r[-1] == "-"]
    why = []
    from_mpq = [r[0] for r in rows if r[-1].lower().endswith(".mpq")]
    if data == "remastered" and from_mpq:
        why.append(f"{len(from_mpq)} files from an MPQ, the first {from_mpq[0]}")
    asked = [line.split("\t") for line in (casc.read_text(errors="replace").splitlines()
                                            if casc.exists() else []) if "\t" in line]
    served = {casc_key(n) for n, what in asked if what == "read"}
    no_sd = {casc_key(n) for n, what in asked if what == "missing" and n.lower().startswith("sd\\")}
    refused = [n for n, what in asked if what == "refused" and casc_key(n) not in served]
    hd_only = sorted({casc_key(n) for n in refused if casc_key(n) in no_sd})
    nowhere += [f"{n} (HD only)" for n in hd_only]
    refused = [n for n in refused if casc_key(n) not in no_sd]
    if refused:
        why.append(f"{len(refused)} names refused by the install with no SD file in their place, "
                   f"the first {refused[0]}")
    return ("; ".join(why) or None), nowhere


# ---------------------------------------------------------------------------
# setup

def _link(src: Path, dst: Path) -> None:
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def player_data_folder() -> Path | None:
    """The folder vanilla 1.17 keeps the player's profiles in: Documents/Starcraft."""
    import ctypes
    buf = ctypes.create_unicode_buffer(260)
    if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf) != 0:  # CSIDL_PERSONAL
        return None
    return Path(buf.value) / "Starcraft"


def seed_profiles(game: Path) -> None:
    """The engine keeps profiles in its own data folder, the exe's (port_paths.cpp),
    which is the game folder here, while the oracle keeps them in Documents/Starcraft.
    The menu path takes the profile the ID screen shows, so a game folder with none gets
    copies of the player's."""
    dst = game / "characters"
    if any(dst.glob("*.spc")):
        return
    src = player_data_folder()
    found = sorted(src.glob("characters/*.[sm]pc")) if src else []
    if not found:
        print(f"  WARNING: no 1.17 profile in {src / 'characters'}; the engine builds stop at the ID screen")
        return
    dst.mkdir(parents=True, exist_ok=True)
    for f in found:
        shutil.copy2(f, dst / f.name)
    print(f"  {dst} <- {', '.join(f.name for f in found)}")


def fill_game_folder(game: Path, x64: bool = False) -> None:
    """The data, DLLs, Patch_rt.mpq and ddraw.ini the oracle's folder takes; Hyperion's
    takes only the data, Local.dll and the x64 SDL3.dll, which the engine presents
    through (docs/renderer.md)."""
    game.mkdir(parents=True, exist_ok=True)
    for name in LINKED:
        src = TESTRUN / name
        if x64 and not (name.lower().endswith(".mpq") or name == "Local.dll"):
            continue
        if not src.exists():
            print(f"  skip {name} (not in testrun)")
            continue
        _link(src, game / name)
    if x64 and SDL3_DLL_X64.exists():
        _link(SDL3_DLL_X64, game / "SDL3.dll")
    elif x64:
        print(f"  WARNING: no {SDL3_DLL_X64} (python tools/fetch_sdl3.py); Hyperion will not start")
    patch = next((p for p in PATCH_RT_CANDIDATES if p.exists()), None)
    if patch:
        _link(patch, game / "Patch_rt.mpq")
        print(f"  Patch_rt.mpq <- {patch} (sha1 {sha1(patch)[:12]})")
    else:
        print("  WARNING: no Patch_rt.mpq; replays stop on game\\nconsole.pcx")

    ini = TESTRUN / "ddraw.ini"
    if ini.exists() and not x64:
        text = ini.read_text(errors="replace")
        (game / "ddraw.ini").unlink(missing_ok=True)  # an old setup hardlinked it
        (game / "ddraw.ini").write_text(text.rstrip("\n") + "\n" + DDRAW_SECTION)
    (game / "maps" / "replays").mkdir(parents=True, exist_ok=True)
    seed_profiles(game)


def setup_hxr() -> None:
    """testrun/hxr/: the x64 Hyperion.exe and SDL3.dll, with no MPQ and no Local.dll."""
    HXR_GAME.mkdir(parents=True, exist_ok=True)
    for f in list(HXR_GAME.glob("*.mpq")) + list(HXR_GAME.glob("*.MPQ")) + [HXR_GAME / "Local.dll",
                                                                          HXR_GAME / "StarCraft.exe"]:
        f.unlink(missing_ok=True)
    if SDL3_DLL_X64.exists():
        _link(SDL3_DLL_X64, HXR_GAME / "SDL3.dll")
    shutil.copyfile(HX64_BUILT, HXR_GAME / "Hyperion.exe")
    (HXR_GAME / "maps" / "replays").mkdir(parents=True, exist_ok=True)
    seed_profiles(HXR_GAME)


def setup_hx64() -> None:
    """testrun/hx64/: the x64 Hyperion.exe with the game's files, and no 1.17 exe: a
    copy left from an older set-up goes, so a run shows that none is read."""
    fill_game_folder(HX64_GAME, x64=True)
    (HX64_GAME / "StarCraft.exe").unlink(missing_ok=True)
    shutil.copyfile(HX64_BUILT, HX64_EXE)
    print(f"  {HX64_EXE.name} <- {HX64_BUILT} (sha1 {sha1(HX64_BUILT)[:12]}), no StarCraft.exe")
    print(f"game folder ready: {HX64_GAME}")


def setup_linux(build: str) -> None:
    """testrun/lx64/ or la64/: the Linux Hyperion with the game's files, and no 1.17 exe."""
    game = LINUX_BUILDS[build][0]
    fill_game_folder(game, x64=True)
    (game / "SDL3.dll").unlink(missing_ok=True)
    (game / "StarCraft.exe").unlink(missing_ok=True)
    refresh_linux(build)
    print(f"game folder ready: {game}")


def setup_lxr() -> None:
    """testrun/lxr/: the Linux x64 Hyperion and its libraries, with no MPQ and no Local.dll, so
    it reads the Remastered install it is given."""
    LXR_GAME.mkdir(parents=True, exist_ok=True)
    for f in list(LXR_GAME.glob("*.mpq")) + list(LXR_GAME.glob("*.MPQ")) + [LXR_GAME / "Local.dll"]:
        f.unlink(missing_ok=True)
    refresh_linux("lx64", LXR_GAME)
    (LXR_GAME / "maps" / "replays").mkdir(parents=True, exist_ok=True)
    seed_profiles(LXR_GAME)


def refresh_linux(build: str, game: Path | None = None) -> None:
    """The Linux build's files copied in when they changed (a copy, so a build can go on
    while a run uses the last one)."""
    folder, built, program, _ = LINUX_BUILDS[build]
    game = game or folder
    for name in (program,) + posix_libs(build):
        src, dst = built / name, game / name
        if not src.exists():
            sys.exit(f"the Linux build is not there: {src} (python tools/build_engine_linux.py)")
        if not dst.exists() or sha1(dst) != sha1(src):
            shutil.copyfile(src, dst)
            os.chmod(dst, 0o755)


def posix_libs(build: str) -> tuple:
    """SDL and the observer as a Linux or Mac build has them, in that order."""
    return ("libSDL3.0.dylib", "rt_observer.dylib") if build == "ma64" else ("libSDL3.so.0", "rt_observer.so")


def linux_runs_here(build: str) -> bool:
    """Whether this machine runs the build itself: Linux on the build's processor, or for
    ma64 an Apple-silicon Mac."""
    import platform
    if build == "ma64":
        return sys.platform == "darwin" and platform.machine().lower() == "arm64"
    return sys.platform.startswith("linux") and platform.machine().lower() in (
        LINUX_BUILDS[build][3], "arm64" if LINUX_BUILDS[build][3] == "aarch64" else "amd64")


def linux_path(p: Path, build: str = "lx64") -> str:
    """A path as the Linux program sees it: itself where it runs natively, under /mnt in WSL,
    under /src in the Docker image (the repository mounted there)."""
    if linux_runs_here(build):
        return str(p)
    if build == "la64":
        return "/src/" + Path(p).resolve().relative_to(ROOT.parent).as_posix()
    s = str(Path(p).resolve())
    return "/mnt/" + s[0].lower() + s[2:].replace("\\", "/")


def cmd_setup(args) -> int:
    fill_game_folder(GAME)
    src = oracle_exe()
    if sha1(src) != ORACLE_SHA1:
        sys.exit(f"{src} is not the 1.17 oracle (sha1 mismatch)")
    data = bytearray(src.read_bytes())
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    off = pe + 24 + 70
    dc = struct.unpack_from("<H", data, off)[0]
    struct.pack_into("<H", data, off, dc & ~0x40)
    GAME_EXE.write_bytes(data)
    (GAME / "Starcraft.exe").unlink(missing_ok=True)  # the name before Hyperion.exe
    print(f"  {GAME_EXE.name} <- {src.name}, DYNAMICBASE cleared ({dc:#06x} -> {dc & ~0x40:#06x})")
    print(f"game folder ready: {GAME}")
    if HX64_BUILT.exists():
        setup_hx64()
    else:
        print(f"  no {HX64_BUILT} (python tools/build_engine_x64.py); testrun/hx64 not set up")
    for b, (_, built, program, _) in LINUX_BUILDS.items():
        if (built / program).exists():
            setup_linux(b)
    return 0


# ---------------------------------------------------------------------------
# run

def write_cfg(path: Path, out_csv: Path, args, snap_dir: Path | None, menus: bool = True,
              view_every: int = 0, check_observe: bool = False, load_file: Path | None = None,
              linux: str = "") -> None:
    # a Linux build reads its paths as the Linux program sees them
    place = (lambda p: linux_path(p, linux)) if linux else str
    lines = [
        f"out={place(out_csv)}",
        f"clock={args.clock}",
        f"clock_step={args.clock_step}",
        f"updates={args.updates}",
        f"max_frames={args.max_frames}",
        f"stats_every={args.stats_every}",
        f"stall_secs={args.stall_secs}",
        f"menu_timeout_secs={args.menu_timeout}",
        f"keep_active={1 if args.keep_active else 0}",
        f"snap_every={args.snap_every}",
        f"snap_screen={1 if args.snap_screen else 0}",
        f"snap_move={1 if args.snap_move else 0}",
        f"dump_frame={args.dump_frame}",
    ]
    if getattr(args, "save_frame", 0):
        lines += [f"save_frame={args.save_frame}", f"save_name={args.save_name}"]
    if load_file:
        lines += [f"load_file={place(load_file)}", "pin_seed=1"]
    if snap_dir:
        lines.append(f"snap_dir={place(snap_dir)}")
    if view_every:
        lines.append(f"view_every={view_every}")
    # An engine build's observer also reads 1.17's memory and compares it with what
    # the engine hands over (oc_observe); the oracle's config leaves it out.
    if check_observe:
        lines.append("check_observe=1")
    # The state's hash a domain at a time, from an engine build (H lines).
    if getattr(args, "sync_every", 0):
        lines.append(f"sync_every={args.sync_every}")
    # The game goes on past the replay's end, into the score screen, until the
    # timeout ends the run (the score screen's drawing, which no replay run reached).
    if getattr(args, "stay", False):
        lines.append("exit_at_end=0")
    if menus:
        lines += [f"menu={m}" for m in (MENU_LOAD if load_file else MENU)]
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


# Hyperion keeps 1.17's settings in a file of its own in the game folder
# (src117/port/platform/port_settings.cpp), not in the registry key the oracle reads. Every engine
# run starts from the same one: 1.17's defaults, with the intro movies marked as seen, as
# main.cpp plays them on a first run, before the replay starts, and Settings' View shape at the
# original's 4:3, as the oracle's, since without it a game fits the screen (2026-10-19).
ENGINE_SETTINGS = ('{\n  "Starcraft": {\n    "intro": 0,\n    "introX": 0\n  },\n'
                   '  "hyperion": {\n    "view": 0\n  }\n}\n')


def seed_settings(game: Path) -> None:
    (game / "settings.json").write_text(ENGINE_SETTINGS, encoding="ascii")


def run_one(build: str, replay: Path, out_dir: Path, args, live: list[str] | None = None) -> dict:
    """A replay played by a build, or with live, an engine build's game started from
    those arguments (--map and the rest) with no replay, which records replay itself. An
    engine build reads the one kind of files args.data names (the callers give each in turn)."""
    engine = build in ENGINE_BUILDS
    linux = build if build in LINUX_BUILDS else ""
    if live and not engine:
        sys.exit("only an engine build plays a live game")
    data = data_sources(args)[0] if engine else "mpq"
    if engine and len(data_sources(args)) > 1:
        sys.exit("run_one plays one kind of files at a time")
    if not source_ok(build, data):
        sys.exit(f"{build} reads the MPQs only here, not {DATA_WORDS[data]}")
    install = str(remastered_install(args)) if data != "mpq" else None
    if not GAME_EXE.exists() and not linux:
        cmd_setup(args)
    if linux == "ma64" and not linux_runs_here(linux):
        sys.exit("ma64 runs on an Apple-silicon Mac only")
    if linux:
        if not (LINUX_BUILDS[linux][0] / LINUX_BUILDS[linux][2]).exists():
            setup_linux(linux)
        refresh_linux(linux)
        (LINUX_BUILDS[linux][0] / "StarCraft.exe").unlink(missing_ok=True)
    elif engine:
        if not HX64_BUILT.exists():
            sys.exit(f"the x64 Hyperion.exe not built: {HX64_BUILT} (python tools/build_engine_x64.py)")
        if not HX64_EXE.exists():
            setup_hx64()
        elif sha1(HX64_EXE) != sha1(HX64_BUILT):
            shutil.copyfile(HX64_BUILT, HX64_EXE)
        # no 1.17 exe beside Hyperion, one an older set-up left included
        (HX64_GAME / "StarCraft.exe").unlink(missing_ok=True)
    game, exe = ((LINUX_BUILDS[linux][0], LINUX_BUILDS[linux][0] / LINUX_BUILDS[linux][2]) if linux
                 else (HX64_GAME, HX64_EXE) if engine else (GAME, GAME_EXE))
    if engine and not linux and data == "remastered":
        setup_hxr()
        game, exe = HXR_GAME, HXR_GAME / "Hyperion.exe"
    elif linux and data == "remastered":
        setup_lxr()
        game, exe = LXR_GAME, LXR_GAME / LINUX_BUILDS[linux][2]
    seed_profiles(game)
    if engine:
        seed_settings(game)
    if not linux and game_running():
        sys.exit(f"a run is already going (a {GAME_EXE.name} is running); it shares the staged replay")
    out_dir = out_dir.resolve()  # the game's working directory is its folder, not ours
    out_dir.mkdir(parents=True, exist_ok=True)
    tag = args.tag or build
    csv = out_dir / f"{tag}.csv"
    cfg = out_dir / f"{tag}.cfg"
    snap_dir = out_dir / f"{tag}_snaps" if args.snap_every else None
    # Hyperion starts the replay itself (--replay, src117/port/gameplay/port_launch.cpp); the
    # oracle goes through the menus.
    launch = engine and not args.menus
    # Switching the view while a game runs is the engine's (--view-every); the oracle's
    # config leaves it out, so its cache key is the same with it or without.
    # A saved game to start from in place of the replay (--load): an engine build is
    # given it on its command line, the oracle takes it
    # through the menus. Nothing is given as input from there on.
    load_file = Path(args.load).resolve() if args.load else None
    write_cfg(cfg, csv, args, snap_dir, menus=not launch,
              view_every=args.view_every if engine and args.view else 0,
              check_observe=engine and args.check_observe, load_file=load_file, linux=linux)

    cache = None
    # A run that saves or loads the game is not cached: that is its point.
    if (build == "oracle" and args.cache and not getattr(args, "extra_dll", None) and not args.save_frame
            and not load_file):
        cache = ORACLE_CACHE / oracle_cache_key(replay, cfg, args)
        if (cache / "oracle.json").exists():
            return use_cached_oracle(cache, out_dir, tag, replay)

    staged = game / "maps" / "replays" / STAGED_NAME
    if not live:
        shutil.copy2(replay, staged)
    # The save of --save-frame lands in the game folder's save\<player>\ (MakeSaveDir,
    # saveload.cpp); an older one of the same name is taken away first.
    saves = []
    started = time.time()
    if args.save_frame:
        for old in list((game / "save").glob(f"*/{args.save_name}.*")) + list(
                (game / "maps" / "save").glob(f"{args.save_name}.*")):
            old.unlink()

    observer = game / posix_libs(linux)[1] if linux else HX64_OBSERVER if engine else BUILD / "rt_observer.dll"
    # Extra DLLs (--dll) go last, so the observer's import hooks are in place first.
    dlls = [f"{observer}@rt_init"] + list(getattr(args, "extra_dll", None) or [])
    cmd = [str(BUILD / "rt_launcher.exe"), str(exe)]
    # How the engine shows its window (port_present.cpp); the oracle ignores it.
    env = dict(os.environ, HYPERION_WINDOW=args.window)
    # Where the engine reads the game's files (port_remastered.cpp): 1.17's MPQs, or the
    # StarCraft: Remastered install the harness names, alone or over the MPQs. With the install
    # the engine logs every file it reads and where from (HYPERION_FILE_LOG), for files_check.
    if engine:
        env["HYPERION_DATA_SOURCE"] = data
        if install:
            env["HYPERION_REMASTERED"] = linux_path(Path(install), linux) if linux else install
            env["HYPERION_FILE_LOG"] = "1"
            logs_aside(game)
    # Its window on the right-hand screen unless told otherwise, off the screen the user works on
    # (HYPERION_DISPLAY, port_present.cpp: left, right or a display's number).
    env.setdefault("HYPERION_DISPLAY", "right")
    # Which presenter the engine draws through, and the GPU presenter's check
    # (port_present.cpp, port_gpu.cpp; docs/renderer.md, R4a).
    # SDL's renderer and no GPU scene unless asked: the engine's own default is the GPU
    # presenter and scene since 2026-10-02, and a run stays as the suite has always made it.
    env["HYPERION_RENDERER"] = getattr(args, "renderer", None) or "sdl"
    if getattr(args, "gpu_check", False):
        env["HYPERION_GPU_CHECK"] = "1"
    # The frame description drawn on the GPU, and the stage it is checked at
    # (port_scene.cpp, docs/renderer.md, R4b).
    env["HYPERION_GPU_SCENE"] = getattr(args, "scene", None) or "off"
    if engine:
        # Hyperion.exe loads them itself, after 1.17's data (hyperion/hyperion.cpp).
        env["HYPERION_DLLS"] = ";".join(dlls)
    else:
        for d in dlls:
            cmd += ["--dll", d]
    cmd += ["--cfg", str(cfg), "--timeout", str(args.timeout)]
    for a in args.game_arg:
        cmd += ["--arg", a]
    # Hyperion keeps its data in Hyperion\ beside the exe unless told (port_paths.cpp); the
    # harness keeps it in the game folder itself, where it reads the replays, saves and logs.
    if engine:
        cmd += ["--arg", f'--data="{game}"']
    # The view's width is the engine's (docs/renderer.md, R3); the oracle has only
    # 640x480, so it is left out of the oracle's command line and cache key.
    if args.view and engine:
        cmd += ["--arg", f"--view={args.view}"]
    if live:
        cmd += [x for a in live for x in ("--arg", a)]
    elif launch:
        cmd += ["--arg", f'--load="{load_file}"' if load_file else f'--replay="{staged}"']

    if linux:
        # no launcher: the program itself, with the observer's settings in its environment,
        # under timeout; from Windows in WSL (x64) or in the Docker image (ARM64), with the
        # paths as it sees them
        lp = lambda p: linux_path(p, linux)
        lx_env = {"HYPERION_DLLS": ";".join([f"{lp(observer)}@rt_init"] + list(dlls[1:])),
                  "OC_RT_CFG": lp(cfg)}
        for k in ("HYPERION_WINDOW", "HYPERION_RENDERER", "HYPERION_GPU_CHECK", "HYPERION_GPU_SCENE",
                  "HYPERION_DATA_SOURCE", "HYPERION_REMASTERED", "HYPERION_FILE_LOG"):
            if k in env:
                lx_env[k] = env[k]
        docker = linux == "la64" and not linux_runs_here(linux)
        if docker:
            # no display and no sound device in the image, and SDL's software renderer, as its
            # OpenGL would be Mesa's software one, emulated too
            lx_env.update({"SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy",
                           "SDL_RENDER_DRIVER": "software"})
        lx_args = [f"--data={lp(game)}"] + list(args.game_arg)
        if args.view:
            lx_args.append(f"--view={args.view}")
        if live:
            lx_args += live
        elif launch:
            lx_args.append(f"--load={lp(load_file)}" if load_file else f"--replay={lp(staged)}")
        # a Mac has no timeout(1): Python ends its run at the limit instead (below)
        killer = [] if linux == "ma64" else ["timeout", "-s", "KILL", str(args.timeout)]
        run = ["env"] + [f"{k}={v}" for k, v in lx_env.items()] + killer + ["./" + exe.name] + lx_args
        if linux_runs_here(linux):
            cmd = run
        elif docker:
            cmd = ["docker", "run", "--rm", "--platform", "linux/arm64", "-v", f"{ROOT.parent.as_posix()}:/src",
                   "-w", lp(game), ARM64_IMAGE] + run
        else:
            cmd = ["wsl.exe", "-d", WSL_DISTRO, "--cd", str(game), "-e"] + run
    init_log = game / "hyperion_init.log" if engine else None
    if init_log and init_log.exists():
        init_log.unlink()
    render_log = game / "hyperion_render.log" if engine else None
    if render_log and render_log.exists():
        render_log.unlink()
    # Hyperion's crash report (src117/port/tools/port_crash.cpp) writes hyperion_crash.log
    # and, unless this flag is in the game's working directory, holds the process on
    # a message box until the stall watchdog ends it. With the flag the run ends at
    # once.
    (game / "hyperion_quiet.flag").touch()
    crash_log = game / "hyperion_crash.log"
    crash_log.unlink(missing_ok=True)
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=game if linux else None,
                              timeout=args.timeout if linux == "ma64" else None)
    except subprocess.TimeoutExpired as e:
        # killed at the limit, as timeout -s KILL ends a Linux run (128 + SIGKILL)
        text = lambda b: b.decode(errors="replace") if isinstance(b, bytes) else (b or "")
        proc = subprocess.CompletedProcess(cmd, 137, text(e.stdout), text(e.stderr))
    wall = time.time() - t0
    staged.unlink(missing_ok=True)
    crash = crash_log.read_text(errors="replace") if crash_log.exists() else None
    if crash:
        (out_dir / f"{tag}_crash.log").write_text(crash)

    if args.save_frame:
        # a multiplayer save (a replay's) goes to maps\save\ instead. The oracle's
        # Storm puts its user data in Documents\StarCraft, so its save is looked for
        # there and moved out, and a directory the save made there is removed again.
        roots = [game, ORACLE_USER_DATA]
        found = sorted([q for r in roots for q in list((r / "save").glob(f"*/{args.save_name}.*"))
                        + list((r / "maps" / "save").glob(f"{args.save_name}.*"))], key=lambda q: q.stat().st_mtime)
        for q in found[-1:]:
            dst = out_dir / f"{tag}{q.suffix}"
            shutil.copy2(q, dst)
            saves.append(str(dst))
        for q in found:
            if ORACLE_USER_DATA in q.parents:
                q.unlink()
        for d in (ORACLE_USER_DATA / "save").glob("*"):
            if d.is_dir() and not any(d.iterdir()) and d.stat().st_ctime >= started:
                d.rmdir()

    summary = parse_csv(csv) if csv.exists() else {}
    meta = {
        "build": build,
        "tag": tag,
        "replay": str(replay),
        "replay_sha1": sha1(replay) if replay.exists() else None,
        "live": live,
        "engine": engine,
        "dlls": dlls,
        "exe": str(exe),
        "exe_sha1": sha1(exe),
        "x64": engine,
        "engine_init": init_log.read_text(errors="replace").strip() if init_log and init_log.exists() else None,
        "engine_crash": bool(crash),
        "engine_render": render_log.read_text(errors="replace").strip()
        if render_log and render_log.exists() else None,
        "launcher_rc": proc.returncode,
        "launcher_out": proc.stdout.strip().splitlines(),
        "wall_s": round(wall, 1),
        "end": summary.get("end"),
        "frames": summary.get("frames"),
        "sim_fps": summary.get("sim_fps"),
        "total_frames": summary.get("total_frames"),
        "saves": saves,
        "data": data,
        "argv": {k: v for k, v in vars(args).items() if k not in ("func",)},
    }
    if engine:
        # the files the run was given, and what shows it read them
        shown = linux_path(Path(install), linux) if install and linux else install
        meta["source_check"] = source_check(meta, data, shown)
        if install:
            meta["remastered"], meta["casc_build"] = install, casc_build(Path(install))
            for n in FILE_LOGS:
                if (game / n).exists():
                    shutil.move(str(game / n), str(out_dir / f"{tag}_{n.split('_', 1)[1]}"))
            meta["files_check"], meta["files_nowhere"] = files_check(out_dir, tag, data)
        for k in ("source_check", "files_check"):
            if meta.get(k):
                print(f"  FAILED {tag} {replay.name}: {meta[k]}")
    (out_dir / f"{tag}.json").write_text(json.dumps(meta, indent=2, default=str))
    # Only a run that played the whole replay is kept; a menu timeout is run again next time.
    if cache and meta["end"] == "replay_end":
        cache.mkdir(parents=True, exist_ok=True)
        shutil.copy2(csv, cache / "oracle.csv")
        shutil.copy2(out_dir / f"{tag}.json", cache / "oracle.json")
        if snap_dir and snap_dir.is_dir():
            shutil.copytree(snap_dir, cache / "oracle_snaps", dirs_exist_ok=True)
    fps = meta["sim_fps"]
    print(f"  {tag:10s} {replay.name}: end={meta['end']} frames={meta['frames']}"
          f"/{meta['total_frames']} wall={meta['wall_s']}s"
          + (f" sim={fps:.0f} fps ({fps * 0.042:.1f}x real time)" if fps else ""))
    return meta


def oracle_cache_key(replay: Path, cfg: Path, args) -> str:
    """What an oracle run's log depends on: the replay, the exe, the observer, the game's
    arguments and the run's settings (its config without the output paths). Three runs of
    each suite replay gave the same frame and player lines (2026-09-27). The MPQs and DLLs it
    reads count too, so a run kept from other files is not reused."""
    h = hashlib.sha1()
    for p in (replay, GAME_EXE):
        h.update(sha1(p).encode())
    h.update(pe_sha1(BUILD / "rt_observer.dll").encode())
    h.update(repr(sorted(mpq_sha1s(GAME).items())).encode())
    h.update(repr(args.game_arg).encode())
    for line in cfg.read_text().splitlines():
        if not line.startswith(("out=", "snap_dir=")):
            h.update(line.encode() + b"\n")
    return h.hexdigest()[:16]


def use_cached_oracle(cache: Path, out_dir: Path, tag: str, replay: Path) -> dict:
    shutil.copy2(cache / "oracle.csv", out_dir / f"{tag}.csv")
    shutil.copy2(cache / "oracle.json", out_dir / f"{tag}.json")
    if (cache / "oracle_snaps").is_dir():
        shutil.copytree(cache / "oracle_snaps", out_dir / f"{tag}_snaps", dirs_exist_ok=True)
    meta = json.loads((cache / "oracle.json").read_text())
    print(f"  {tag:10s} {replay.name}: end={meta['end']} frames={meta['frames']}"
          f"/{meta['total_frames']} (cached, {cache.name})")
    return meta


def each_source(args, build: str) -> list:
    """The run's settings once for each kind of files the build is given, each with its tag;
    the oracle's once, on the MPQs."""
    if build not in ENGINE_BUILDS:
        a = argparse.Namespace(**vars(args))
        a.data, a.tag = "mpq", build
        return [a]
    runs = []
    for s in data_sources(args):
        if not source_ok(build, s):
            print(f"  {build} reads the MPQs only here; {DATA_WORDS[s]} left out")
            continue
        a = argparse.Namespace(**vars(args))
        a.data, a.tag = s, engine_tag(build, s)
        runs.append(a)
    return runs


def cmd_run(args) -> int:
    out = Path(args.out) if args.out else RUNS / _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    if args.tag and args.build in ENGINE_BUILDS and len(data_sources(args)) > 1:
        sys.exit("--tag names one run: give --data one kind of files")
    for a in each_source(args, args.build):
        a.tag = args.tag or a.tag
        run_one(args.build, Path(args.replay), out / Path(args.replay).stem, a)
    print(f"output: {out}")
    return 0


def suite_replays(args) -> list[tuple[str, Path]]:
    """The replays a suite plays, each with its folder's name: --replays, or a set of
    corpus.json by its row names (the archive reuses file names across rounds)."""
    if args.replays:
        reps = []
        for p in map(Path, args.replays):
            reps += sorted(p.glob("*.rep")) if p.is_dir() else [p]
        names, used = [], set()
        for rep in reps:
            name, n = rep.stem, 2
            while name.lower() in used:
                name, n = f"{rep.stem}_{n}", n + 1
            used.add(name.lower())
            names.append((name, rep))
        return names
    which = args.set or ("quick" if args.quick else "full")
    rows = [r for r in corpus() if which in r["sets"]]
    missing = [r for r in rows if not (REPLAYS / r["path"]).exists()]
    if missing:
        sys.exit(f"{len(missing)} replays of the {which} set are not in {REPLAYS}, the first "
                 f"{missing[0]['path']} (from {missing[0]['source']}); replaytest.py corpus lists them")
    return [(r["name"], REPLAYS / r["path"]) for r in rows]


def cmd_suite(args) -> int:
    reps = suite_replays(args)
    out = Path(args.out) if args.out else RUNS / _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    builds = [b.strip() for b in args.builds.split(",") if b.strip()]
    sources = data_sources(args)
    prov = provenance(args, sources)
    print(f"suite: {len(reps)} replays x {builds} on {' and '.join(DATA_WORDS[s] for s in sources)} -> {out}")
    for name, rep in reps:
        for b in builds:
            for a in each_source(args, b):
                run_one(b, rep, out / name, a)
    rows = write_report(out, builds[0], prov)
    return suite_verdict(rows, out, prov, "suite")


def suite_verdict(rows: list[dict], out: Path, prov: dict, what: str, failed: list[str] | None = None) -> int:
    """Each run's verdict printed, the run's summary.json written; 1 when any run failed."""
    failed = list(failed or []) + [f"{r['replay']} {r['tag']}: {r['why']}" for r in rows if r["why"]]
    for r in rows:
        if not r["reference"]:
            print(f"  {'ok    ' if not r['why'] else 'FAILED'} {r['replay']} {r['tag']}: {r['why'] or r['verdict']}")
    (out / "summary.json").write_text(json.dumps({**prov, "what": what, "passed": not failed, "failed": failed,
                                                  "runs": rows}, indent=1, default=str))
    print(f"report: {out / 'report.html'}")
    print(f"{what}: " + (f"{sum(not r['reference'] for r in rows)} runs pass" if not failed
                         else f"{len(failed)} failed"))
    return 1 if failed else 0


def hx_check(meta: dict, csv: Path) -> str | None:
    """Why a Hyperion replay's run fails its own check, or None: the replay refused, a
    check that differs from the game it recorded, or checks left unmade."""
    sy = parse_csv(csv).get("sync") if csv.exists() else None
    if sy and sy["state"] == "refused":
        return f"refused: {sy['reason']}"
    if meta.get("end") != "replay_end":
        return f"ended {meta.get('end')}"
    if not sy:
        return "no replay check reported"
    if sy["recorded"] == 0:
        return "no checks in the replay (none recorded, or of another hash version)"
    if sy["state"] == "mismatch":
        return f"differs from its game at frame {sy['first_bad']} (domains {sy['domains']})"
    if sy["checked"] != sy["recorded"]:
        return f"{sy['checked']} of {sy['recorded']} checks made"
    return None


def hx_checkpoint(ref_csv: Path, load_csv: Path, at: int) -> str | None:
    """Why a game loaded from a checkpoint's save fails, or None: every state hash after the
    frame saved, and every F, P and Q line, must be the live game's."""
    ref, got = parse_csv(ref_csv), parse_csv(load_csv)
    common = sorted(f for f in set(ref.get("H", {})) & set(got.get("H", {})) if f > at)
    if not common:
        return "no state hashes after the save"
    bad = next((f for f in common if ref["H"][f] != got["H"][f]), None)
    if bad is not None:
        return f"differs from the live game at frame {bad}"
    # the live game's hash at the frame saved is taken before the save changes the game
    after = lambda run: {**run, "F": {k: v for k, v in run["F"].items() if k > at},
                         "H": {k: v for k, v in run.get("H", {}).items() if k > at}}
    cmp = compare(after(ref), after(got), None, False)
    if cmp["first_divergence"] is not None:
        return f"its log differs from the live game's at frame {cmp['first_divergence']}"
    if max(common) < max(ref["H"]):
        return f"stopped at frame {max(common)} of {max(ref['H'])}"
    return None


def hx_expect(entry: dict, csv: Path) -> str | None:
    """Why a game's end fails what its entry expects, or None: with expect_minerals, each
    slot's minerals in the last P line logged for it (the trigger test map)."""
    want = entry.get("expect_minerals")
    if not want:
        return None
    players = parse_csv(csv).get("P", {}) if csv.exists() else {}
    bad = []
    for slot, value in sorted(want.items(), key=lambda kv: int(kv[0])):
        seen = players.get(int(slot), {})
        got = seen[max(seen)][0] if seen else None
        if got != value:
            bad.append(f"slot {slot} has {got}, not {value}")
    return "; ".join(bad) if bad else None


def hx_entries() -> dict:
    """The manifest's games by name."""
    return {e["name"]: e for e in json.loads(HX_MANIFEST.read_text())["games"]}


def cmd_hxsuite(args) -> int:
    """The Hyperion replays, each run by hx64: every check the replay holds must match
    (the game it recorded), and its log must match its kept reference, the log
    of the game it recorded (hxrecord) or a first run (--keep-ref). A replay with a save
    beside it (<name>.sav, an entry's checkpoint) is also loaded from the save, which must
    go on as the live game did. Each is played on every kind of files --data names, all
    held to the one reference. The set is the manifest's (hxsuite.json): an entry with no
    replay fails, as does a replay with no kept reference unless --keep-ref makes one."""
    entries = hx_entries()
    failed = []
    if args.replays:
        reps: list[Path] = []
        for p in map(Path, args.replays):
            reps += sorted(p.glob("*.rep")) if p.is_dir() else [p]
    else:
        reps = [HX_SET / f"{name}.rep" for name in entries]
        failed += [f"{r.name}: not recorded (replaytest.py hxrecord --only {r.stem})" for r in reps if not r.exists()]
        reps = [r for r in reps if r.exists()]
        for extra in sorted(set(HX_SET.glob("*.rep")) - set(reps)):
            if extra.stem not in entries:
                print(f"  WARNING: {extra.name} is in {HX_SET.name}\\ but not in {HX_MANIFEST.name}; not run")
    if not reps:
        print(f"no Hyperion replays in {HX_SET}; hxrecord makes them")
        return 1
    out = Path(args.out) if args.out else RUNS / ("hx_" + _dt.datetime.now().strftime("%Y%m%d_%H%M%S"))
    head = git_head()
    args.sync_every = HX_SYNC_EVERY
    sources = data_sources(args)
    prov = provenance(args, sources)
    print(f"hxsuite: {len(reps)} replays on {' and '.join(DATA_WORDS[s] for s in sources)} -> {out}")
    for rep in reps:
        entry = entries.get(rep.stem, {})
        run_dir = out / rep.stem
        ref = HX_REF / rep.stem
        kept = False
        for a in each_source(args, "hx64"):
            tag = a.tag
            a.game_arg = list(args.game_arg) + hx_mod_args(entry, engine_folder("hx64", a.data))
            meta = run_one("hx64", rep, run_dir, a)
            csv = run_dir / f"{tag}.csv"
            for why in (meta.get("source_check"), meta.get("files_check"), hx_check(meta, csv), hx_expect(entry, csv)):
                if why:
                    failed.append(f"{rep.name} {tag}: {why}")
            if args.keep_ref and not kept:  # the first kind of files' run; the others are held to it
                keep_ref(csv, run_dir / f"{tag}.json", ref, head)
                kept = True
                print(f"  reference kept for {rep.name}, from {head} ({tag})")
            if not (ref / "hxref.csv").exists():
                failed.append(f"{rep.name}: no kept reference (hxrecord makes one; --keep-ref keeps this run's)")
                continue
            shutil.copy2(ref / "hxref.csv", run_dir / "hxref.csv")
            shutil.copy2(ref / "hxref.json", run_dir / "hxref.json")
            cmp = compare(parse_csv(ref / "hxref.csv"), parse_csv(csv), None, False)
            if cmp["first_divergence"] is not None:
                failed.append(f"{rep.name} {tag}: differs from its reference at frame {cmp['first_divergence']}")
            save = rep.with_suffix(".sav")
            ref_json = json.loads((ref / "hxref.json").read_text())
            at = ref_json.get("checkpoint")
            if save.exists() and at and str(ref_json.get("made_by", "")).startswith("live"):
                load_args = argparse.Namespace(**vars(a))
                load_tag = "load" if tag == "hx64" else f"{tag}_load"
                load_args.load, load_args.max_frames, load_args.tag = str(save), meta.get("frames") or 0, load_tag
                # A game that gave orders gives them again from the save (--record-orders).
                if entry.get("orders"):
                    load_args.game_arg = list(a.game_arg) + [f"--record-orders={orders_arg(entry)}"]
                lmeta = run_one("hx64", rep, run_dir, load_args)
                why = lmeta.get("source_check") or lmeta.get("files_check")
                if why:
                    failed.append(f"{save.name} {tag}: {why}")
                why = hx_checkpoint(ref / "hxref.csv", run_dir / f"{load_tag}.csv", at)
                if why:
                    failed.append(f"{save.name} {tag}, saved at {at}: {why}")
                else:
                    print(f"  {save.name} {tag}: loaded at {at}, goes on as the live game did")
            elif save.exists():
                failed.append(f"{save.name}: no live reference with its checkpoint (hxrecord makes one)")
    rows = write_report(out, "hxref", prov)
    for f in failed:
        print(f"  FAILED {f}")
    (out / "summary.json").write_text(json.dumps({**prov, "what": "hxsuite", "passed": not failed,
                                                  "failed": failed, "runs": rows}, indent=1, default=str))
    print(f"report: {out / 'report.html'}")
    print("hxsuite: " + ("every replay plays the game it recorded" if not failed else f"{len(failed)} failed"))
    return 1 if failed else 0


def orders_arg(entry: dict) -> str:
    """--record-orders' value for an hxsuite entry: every n frames, queued with "queue": true."""
    return f"{entry['orders']}" + (",queue" if entry.get("queue") else "")


def keep_ref(csv: Path, meta_json: Path, ref: Path, made_by: str, checkpoint: int = 0) -> None:
    ref.mkdir(parents=True, exist_ok=True)
    shutil.copy2(csv, ref / "hxref.csv")
    meta = json.loads(meta_json.read_text())
    meta["tag"] = "hxref"
    meta["made_by"] = made_by
    if checkpoint:
        meta["checkpoint"] = checkpoint
    (ref / "hxref.json").write_text(json.dumps(meta, indent=1))


def hx_mod_args(e: dict, game: Path) -> list[str]:
    """An entry's mod: its folder copied from engine/mods/, or from the local test
    mods in testrun/test_mods/ (wide_test_mod.py), into the data folder of the game
    folder the run uses, and --mod, which the live game, the replay and the save's load all
    take."""
    mod = e.get("mod")
    if not mod:
        return []
    src = ROOT / "mods" / mod
    if not src.is_dir():
        src = TESTRUN / "test_mods" / mod
    dst = game / "mods" / mod  # the data folder the harness gives the game (--data)
    for f in src.rglob("*"):
        if f.is_file():
            t = dst / f.relative_to(src)
            t.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, t)
    return [f"--mod={mod}"]


def cmd_hxrecord(args) -> int:
    """Each game of the manifest (hxsuite.json) played live by hx64 with --record, its
    replay put in the Hyperion set and the live game's log kept as its reference. The game is
    played once, on the first kind of files --data names (Remastered's by default); hxsuite
    then holds every kind to it."""
    entries = json.loads(HX_MANIFEST.read_text())["games"]
    if args.only:
        entries = [e for e in entries if e["name"] in args.only]
    out = Path(args.out) if args.out else RUNS / ("hxrec_" + _dt.datetime.now().strftime("%Y%m%d_%H%M%S"))
    head = git_head()
    args.sync_every = HX_SYNC_EVERY
    args.data = data_sources(args)[0]
    tag = engine_tag("hx64", args.data)
    game = engine_folder("hx64", args.data)
    HX_SET.mkdir(parents=True, exist_ok=True)
    print(f"hxrecord: {len(entries)} games on {DATA_WORDS[args.data]} -> {out}")
    failed = []
    for e in entries:
        mp = HX_MAPS / e["map"]
        if not mp.exists():
            failed.append(f"{e['name']}: no map {mp}")
            continue
        rep = (HX_SET / f"{e['name']}.rep").resolve()
        rep.unlink(missing_ok=True)
        save = rep.with_suffix(".sav")
        args.save_frame = e.get("checkpoint", 0)
        args.save_name = f"hx_{e['name']}"
        live = [f'--map="{mp}"', f"--race={e.get('race', 'terran')}", f"--seed={e['seed']}",
                f'--record="{e["frames"]},{rep}"']  # quoted: the path has spaces
        if e.get("computers"):
            live.append(f"--computers={e['computers']}")
        if e.get("computer_race"):
            live.append(f"--computer-race={e['computer_race']}")
        if e.get("ums"):
            live.append("--ums")
        if e.get("rules"):
            live.append(f"--rules={e['rules']}")
        if e.get("ai_levels"):
            live.append(f"--ai-levels={e['ai_levels']}")
        if e.get("handicaps"):
            live.append(f"--handicaps={e['handicaps']}")
        if e.get("mutators"):
            live.append(f"--mutators={e['mutators']}")
        live += hx_mod_args(e, game)
        if e.get("orders"):
            live.append(f"--record-orders={orders_arg(e)}")
        args.tag = tag
        run_dir = out / e["name"]
        # The observer ends the process once the game is left, before Hyperion copies the
        # replay to the path, so it is taken from where every game leaves it.
        last = game / "maps" / "replays" / "LastReplay.rep"
        last.unlink(missing_ok=True)
        meta = run_one("hx64", rep, run_dir, args, live=live)
        if not rep.exists() and last.exists():
            shutil.copy2(last, rep)
        why = meta.get("source_check") or meta.get("files_check")
        if why:
            failed.append(f"{e['name']}: {why}; no reference kept")
            continue
        info = parse_csv(run_dir / f"{tag}.csv") if (run_dir / f"{tag}.csv").exists() else {}
        sy = info.get("sync")
        logged = len(info.get("F", {}))
        if logged == 0 or logged < info.get("frames", 0) - 1:  # one line a frame
            failed.append(f"{e['name']}: the observer logged {logged} frames of {info.get('frames', 0)}; "
                          "no reference kept")
            continue
        if not rep.exists():
            failed.append(f"{e['name']}: no replay written (end {meta.get('end')})")
            continue
        why = hx_expect(e, run_dir / f"{tag}.csv")
        if why:
            failed.append(f"{e['name']}: {why}; no reference kept")
            continue
        if args.save_frame:
            if not meta.get("saves"):
                failed.append(f"{e['name']}: no save written at frame {args.save_frame}")
                continue
            shutil.copy2(meta["saves"][0], save)
        keep_ref(run_dir / f"{tag}.csv", run_dir / f"{tag}.json", HX_REF / e["name"], f"live {head}",
                 checkpoint=args.save_frame)
        print(f"  {e['name']}: {rep.stat().st_size:,} bytes, {sy['recorded'] if sy else '?'} checks, "
              f"reference kept" + (f", saved at {args.save_frame}" if args.save_frame else ""))
    for f in failed:
        print(f"  FAILED {f}")
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# log parsing and comparison

def parse_csv(path: Path) -> dict:
    frames: dict[int, tuple] = {}
    players: dict[int, dict] = {}
    scores: dict[int, dict] = {}
    perf: list[tuple[int, float]] = []
    hashes: dict[int, tuple] = {}
    info: dict = {"events": []}
    for line in path.read_text(errors="replace").splitlines():
        if not line or line[0] == "#":
            continue
        parts = line.split(",")
        k = parts[0]
        if k == "F" and len(parts) >= 7:
            f = int(parts[1])
            # h_stat is the eighth field; a log from before it has none
            frames[f] = (parts[2], int(parts[3]), int(parts[4]), parts[5], parts[6],
                         parts[7] if len(parts) >= 8 else None)
        elif k == "P" and len(parts) >= 7:
            f, p = int(parts[1]), int(parts[2])
            players.setdefault(p, {})[f] = (int(parts[3]), int(parts[4]), int(parts[5]), int(parts[6]))
        elif k == "Q" and len(parts) >= 5:
            f, p = int(parts[1]), int(parts[2])
            scores.setdefault(p, {})[f] = tuple(int(x) for x in parts[3:])
        elif k == "T":
            perf.append((int(parts[1]), float(parts[2])))
        elif k == "G":
            for kv in parts[3:]:
                if "=" in kv:
                    a, b = kv.split("=", 1)
                    info[a] = b
        elif k == "S":
            info["end"] = parts[1]
            info["frames"] = int(parts[2])
            info["game_ms"] = float(parts[4])
            info["sim_fps"] = float(parts[5])
        elif k == "V" and len(parts) >= 7 and parts[1] == "sync":
            info["sync"] = {"state": parts[2], "recorded": int(parts[3]), "checked": int(parts[4]),
                            "first_bad": int(parts[5]), "domains": parts[6],
                            "reason": parts[7] if len(parts) > 7 else ""}
        elif k == "H" and len(parts) >= 9:
            hashes[int(parts[1])] = tuple(parts[2:])  # the random state and count, then each domain
        elif k == "V" and len(parts) >= 5 and parts[1] == "presenter":
            info["presenter"] = parts[2]
            info["gpu_checked"] = int(parts[3])
            info["gpu_mismatched"] = int(parts[4])
        elif k in ("E", "W", "X"):
            info["events"].append(line)
            if k == "X":  # X,frame,code,eip,address,ecx=,stack=: a fault, logged before the game's handler
                info.setdefault("fault", line)
    if "end" not in info and "fault" in info:
        # The game died on the fault before the observer could end the run.
        info["end"] = "crashed"
        info["frames"] = max(frames) if frames else 0
    info["F"] = frames
    info["P"] = players
    info["Q"] = scores
    info["T"] = perf
    info["H"] = hashes
    if "total_frames" in info:
        info["total_frames"] = int(info["total_frames"])
    return info


FIELDS = ["rand_state", "rand_count", "units", "h_sem", "h_raw", "h_stat"]

# The numbers of a Q line: the two resources gathered, then glGameCounts_g's 28 rows
# (rt_observer.cpp, log_scores). The names follow the score screen (gluScore.cpp: produced,
# killed and lost are rows 2, 4 and 3; constructed, razed and lost 9, 11 and 10), the
# triggers' scores (TrigEnum.cpp: 5, 12, 6, 13 and 27) and count.cpp; rows 0 and 1, 7 and 8,
# 14 and 15 are what a player has, the first with what is still being made. Rows 18 to 26 are
# supply, available, used and the cap, for Zerg, Terran and Protoss.
SCORE_NAMES = (["minerals gathered", "gas gathered"]
               + ["units total", "units owned", "units produced", "units lost", "units killed",
                  "unit score", "kill score", "buildings total", "buildings owned",
                  "buildings constructed", "buildings lost", "buildings razed", "building score",
                  "razing score", "factories total", "factories owned", "factories lost",
                  "factories razed"]
               + [f"{r} supply {w}" for r in ("zerg", "terran", "protoss") for w in ("available", "used", "max")]
               + ["custom score"])


def _per_player_diff(ref: dict, other: dict, frames) -> int | None:
    """First of frames where a player's line differs, or is in one run only."""
    a: dict[int, dict] = {}
    b: dict[int, dict] = {}
    for src, dst in ((ref, a), (other, b)):
        for p, pts in src.items():
            for f, v in pts.items():
                dst.setdefault(f, {})[p] = v
    for f in frames:
        if (f in a or f in b) and a.get(f, {}) != b.get(f, {}):
            return f
    return None


def compare(ref: dict, other: dict, limit: int | None = None, raw: bool = True) -> dict:
    """First frame where the two runs disagree, per field and overall, over the
    frames up to limit when there is one; h_raw only when raw is set. Besides the
    F fields, the per-player lines count toward the overall first difference: P
    (minerals, gas, units, hit points) and Q (the score screen's numbers)."""
    common = sorted(f for f in set(ref["F"]) & set(other["F"]) if limit is None or f <= limit)
    first = {f: None for f in FIELDS}
    first_any = None
    for fr in common:
        a, b = ref["F"][fr], other["F"][fr]
        for i, name in enumerate(FIELDS):
            if name == "h_stat" and (a[i] is None or b[i] is None):
                continue
            if a[i] != b[i] and first[name] is None:
                first[name] = fr
        stat_differs = a[5] is not None and b[5] is not None and a[5] != b[5]
        if first_any is None and (a[:4] != b[:4] or stat_differs):
            first_any = fr
    # The engine's state hash (H lines), where both runs logged one.
    ha, hb = ref.get("H", {}), other.get("H", {})
    if ha and hb:
        first["state_hash"] = next((f for f in sorted(set(ha) & set(hb)) if (limit is None or f <= limit)
                                    and ha[f] != hb[f]), None)
        if first["state_hash"] is not None and (first_any is None or first["state_hash"] < first_any):
            first_any = first["state_hash"]
    first["players"] = _per_player_diff(ref.get("P", {}), other.get("P", {}), common)
    first["scores"] = _per_player_diff(ref.get("Q", {}), other.get("Q", {}), common)
    for k in ("players", "scores"):
        if first[k] is not None and (first_any is None or first[k] < first_any):
            first_any = first[k]
    return {
        "common_frames": len(common),
        "ref_frames": len([f for f in ref["F"] if limit is None or f <= limit]),
        "other_frames": len([f for f in other["F"] if limit is None or f <= limit]),
        "limit": limit,
        "first_divergence": first_any,  # over all but h_raw, which is reported alone
        "first_by_field": first if raw else {k: v for k, v in first.items() if k != "h_raw"},
    }


def final_scores(ref: dict, other: dict) -> dict | None:
    """The score screen's numbers on each run's last Q frame: the counters that
    differ, as (player, name, reference, other)."""
    def last(run):
        q = {p: pts for p, pts in run.get("Q", {}).items() if pts}
        if not q:
            return None, {}
        f = max(max(pts) for pts in q.values())
        return f, {p: pts[f] for p, pts in q.items() if f in pts}
    fr, a = last(ref)
    fo, b = last(other)
    if fr is None or fo is None:
        return None
    diffs = []
    for p in sorted(set(a) | set(b)):
        va, vb = a.get(p, ()), b.get(p, ())
        for k in range(max(len(va), len(vb))):
            x = va[k] if k < len(va) else None
            y = vb[k] if k < len(vb) else None
            if x != y:
                diffs.append((p, SCORE_NAMES[k] if k < len(SCORE_NAMES) else f"counter {k}", x, y))
    return {"frame": fr, "other_frame": fo, "diffs": diffs}


# ---------------------------------------------------------------------------
# report

PALETTE = ["#2f6fdf", "#e0561b", "#2a9d58", "#b33fb3", "#c89b12", "#1aa3a3", "#d6336c", "#6c757d"]


def svg_lines(series: list[tuple[str, list[tuple[float, float]], str, bool]], w=760, h=200,
              ylabel="", xmax=None) -> str:
    pts = [p for _, s, _, _ in series for p in s]
    if not pts:
        return "<p class='muted'>no data</p>"
    x1 = xmax or max(p[0] for p in pts) or 1
    y1 = max(max(p[1] for p in pts), 4)
    pad_l, pad_b = 48, 22
    def sx(x): return pad_l + (w - pad_l - 28) * x / x1
    def sy(y): return h - pad_b - (h - pad_b - 8) * y / y1
    out = [f"<svg viewBox='0 0 {w} {h}' class='chart' role='img'>"]
    for i in range(5):
        y = y1 * i / 4
        out.append(f"<line x1='{pad_l}' x2='{w - 8}' y1='{sy(y):.1f}' y2='{sy(y):.1f}' class='grid'/>")
        out.append(f"<text x='{pad_l - 4}' y='{sy(y) + 4:.1f}' class='ax' text-anchor='end'>{y:,.0f}</text>")
    for i in range(5):
        x = x1 * i / 4
        out.append(f"<text x='{sx(x):.1f}' y='{h - 6}' class='ax' text-anchor='middle'>{x:,.0f}</text>")
    for name, s, color, dashed in series:
        if not s:
            continue
        if len(s) > 400:  # thin long series; the page stays small and the shape stays
            step = len(s) / 400.0
            s = [s[int(i * step)] for i in range(400)] + [s[-1]]
        d = " ".join(f"{sx(x):.1f},{sy(y):.1f}" for x, y in s)
        dash = " stroke-dasharray='5 4'" if dashed else ""
        out.append(f"<polyline points='{d}' fill='none' stroke='{color}' stroke-width='1.6'{dash}>"
                   f"<title>{name}</title></polyline>")
    out.append("</svg>")
    return "".join(out)


def agreement_strip(ref: dict, other: dict, total: int, w=760, h=18) -> str:
    """Green where the frame's record matches the reference, red where it does not,
    grey where one run has no record."""
    if total <= 0:
        return ""
    buckets = 380
    cells = []
    for i in range(buckets):
        lo, hi = total * i // buckets + 1, total * (i + 1) // buckets + 1
        state = "none"
        for fr in range(lo, hi):
            a, b = ref["F"].get(fr), other["F"].get(fr)
            if a is None or b is None:
                continue
            if a[:4] != b[:4] or (a[5] is not None and b[5] is not None and a[5] != b[5]):
                state = "bad"
                break
            state = "ok"
        cells.append(state)
    cw = w / buckets
    rects = "".join(f"<rect x='{i * cw:.2f}' y='0' width='{cw + 0.3:.2f}' height='{h}' class='s-{s}'/>"
                    for i, s in enumerate(cells))
    return f"<svg viewBox='0 0 {w} {h}' class='strip'>{rects}</svg>"


def lost_frames(replay: str | None) -> list[int]:
    """The frames the original plays from stale memory (repfile.lost_frames). A
    build under test plays the recorded commands there, so it is compared with the
    oracle only through the first of them."""
    if not replay:
        return []
    import repfile
    global _CMD_SIZES
    if _CMD_SIZES is None:
        _CMD_SIZES = repfile.command_sizes(oracle_exe())
    path = Path(replay) if Path(replay).is_absolute() else ROOT / replay
    parts = replay.replace("\\", "/").split("/")
    if not path.exists() and "testrun" in parts:
        # a run kept on another machine, the oracle's from a Windows one: the same replay here
        path = TESTRUN.joinpath(*parts[parts.index("testrun") + 1:])
    return repfile.lost_frames(path, _CMD_SIZES)


_CMD_SIZES = None


def compare_snaps(ref_dir: Path, other_dir: Path, limit: int | None) -> dict | None:
    """Byte-compares the frame snapshots (f<frame>.bmp) two runs both took, up to
    the frame limit: how many match, and the first that does not with its count
    of differing pixels and whether the palette differs too. A pair of different
    sizes (a wide view against 640x480) is counted apart and not compared."""
    if not ref_dir.is_dir() or not other_dir.is_dir():
        return None
    names = sorted({p.name for p in ref_dir.glob("f*.bmp")} & {p.name for p in other_dir.glob("f*.bmp")})
    if limit:
        names = [n for n in names if int(n[1:-4]) < limit]
    same, first, sized = 0, None, 0
    for n in names:
        a, b = (ref_dir / n).read_bytes(), (other_dir / n).read_bytes()
        if a[18:26] != b[18:26]:
            sized += 1
            continue
        if a == b:
            same += 1
        elif first is None:
            off = int.from_bytes(a[10:14], "little")
            pa, pb = a[off:], b[off:]
            px = sum(x != y for x, y in zip(pa, pb)) + abs(len(pa) - len(pb))
            first = (int(n[1:-4]), px, a[:off] != b[:off])
    return {"count": len(names) - sized, "same": same, "first": first, "sized": sized}


def ref_failure(ref: dict, limit: int | None, known: dict | None) -> str | None:
    """Why a reference run tells nothing, or None: it must reach the replay's end, or reach the
    frame from which the original loses commands (it is compared only that far), or end as
    corpus.json says the oracle is known to."""
    end, last = ref.get("end"), max(ref["F"] or [0])
    if end == "replay_end" or (limit and last >= limit):
        return None
    if known and known.get("oracle") == end:
        return None
    return f"the reference ended {end or 'without an end'} at frame {last:,}"


def run_failure(m: dict, lg: dict, ref: dict, cmp: dict | None, lim: int | None, known: dict | None) -> str | None:
    """Why an engine run fails against its reference, or None. It must have read the files it
    was given, play the replay to its end, and match the reference on every frame the
    reference has (through lim, where the original loses commands), its GPU frames and its
    replay's own checks too."""
    if known and known.get("oracle") == "load_failed":
        return None  # nothing to hold it to (corpus.json)
    for k in ("source_check", "files_check"):
        if m.get(k):
            return m[k]
    if not lg["F"]:
        return f"played no frames (end: {lg.get('end') or 'none'})"
    if cmp is None:
        return None
    if cmp["first_divergence"] is not None:
        return f"diverges at frame {cmp['first_divergence']:,}"
    if cmp["common_frames"] != cmp["ref_frames"]:
        return f"identical over {cmp['common_frames']:,} of the reference's {cmp['ref_frames']:,} frames"
    if m.get("engine") and lg.get("end") != "replay_end":
        return f"ended {lg.get('end') or 'without an end'} at frame {max(lg['F']):,}, not at the replay's end"
    if lg.get("gpu_mismatched"):
        return f"{lg['gpu_mismatched']:,} GPU frames differ from the screen ({lg.get('presenter')})"
    sy = lg.get("sync")
    if sy and sy["state"] in ("refused", "mismatch"):
        return f"its replay's check {sy['state']}" + (f": {sy['reason']}" if sy["reason"] else "")
    return None


def write_report(run_dir: Path, ref_tag: str = "oracle", prov: dict | None = None) -> list[dict]:
    """report.html for a run's folder; returns each run's verdict, with why it fails or None."""
    groups = sorted(p for p in run_dir.iterdir() if p.is_dir())
    rows, sections, results = [], [], []
    for g in groups:
        metas = {}
        for j in sorted(g.glob("*.json")):
            m = json.loads(j.read_text())
            metas[m["tag"]] = m
        logs = {t: parse_csv(g / f"{t}.csv") for t in metas if (g / f"{t}.csv").exists()}
        # A run that wrote no frames is a failure of its own, not one left out of the verdict;
        # the oracle only where corpus.json does not know it to fail to load the replay.
        for t in (t for t in metas if t not in logs):
            m = metas[t]
            known = corpus_known(m.get("replay"))
            if (m.get("argv") or {}).get("load") or (t == ref_tag and known and known.get("oracle") == "load_failed"):
                continue
            why = (m.get("source_check") or m.get("files_check")
                   or f"wrote no frames (launcher rc {m.get('launcher_rc')}, crash {bool(m.get('engine_crash'))})")
            results.append({"replay": g.name, "tag": t, "reference": False, "data": m.get("data"), "end": None,
                            "frames": 0, "of": None, "wall_s": m.get("wall_s"), "verdict": "no frames", "why": why})
            sections.append(f"<section><h2>{g.name}</h2><p class='bad'>{t}: {why}</p></section>")
        if not logs:
            continue
        ref_t = ref_tag if ref_tag in logs else next(iter(logs))
        ref = logs[ref_t]
        total = int(ref.get("total_frames") or max(ref["F"] or [0]))
        lost = lost_frames(metas[ref_t].get("replay"))
        limit = lost[0] if lost else None
        known = corpus_known(metas[ref_t].get("replay"))
        sec = [f"<section><h2>{g.name}</h2>",
               f"<p class='muted'>{total:,} frames in the replay "
               f"({total * 0.042 / 60:.1f} min at Fastest). Reference: <b>{ref_t}</b>.</p>"]
        if lost:
            sec.append(f"<p class='muted'>The original loses recorded commands on {len(lost)} frames of "
                       f"this replay, from frame {limit:,} (docs/replay-test.md). Hyperion "
                       f"plays them, so they are compared through frame {limit:,}; the others on every "
                       f"frame.</p>")
        for t, lg in logs.items():
            m = metas[t]
            # Hyperion plays the commands the original loses (src117/port/replays/port_replay.cpp);
            # an older run names its engine as a payload.
            lim = limit if m.get("engine") or m.get("payload") else None
            # An x64 run's observer hashes no raw unit bytes (rt_observer.cpp, raw_hash).
            raw = not m.get("x64") and not metas[ref_t].get("x64")
            cmp = compare(ref, lg, lim, raw) if t != ref_t else None
            if not lg["F"] or not ref["F"]:
                verdict = f"no frames to compare (end: {lg.get('end') or 'none'})"
                cmp = None if t == ref_t else cmp
            elif cmp is None:
                verdict = "reference"
            elif cmp["first_divergence"] is None and cmp["common_frames"] == cmp["ref_frames"] and lim:
                verdict = f"identical through frame {lim:,}, where the original loses commands"
            elif cmp["first_divergence"] is None and cmp["common_frames"] == cmp["ref_frames"]:
                verdict = "identical"
            elif cmp["first_divergence"] is None:
                verdict = f"identical over {cmp['common_frames']:,} shared frames"
            else:
                verdict = f"diverges at frame {cmp['first_divergence']:,}"
            # A Hyperion replay's check against the game it recorded.
            if lg.get("sync") and lg["sync"]["state"] != "none":
                sy = lg["sync"]
                verdict += ("; replay check: " + (
                    f"refused, {sy['reason']}" if sy["state"] == "refused" else
                    f"differs from its game at frame {sy['first_bad']:,} (domains {sy['domains']})"
                    if sy["state"] == "mismatch" else f"{sy['checked']} of {sy['recorded']} match its game"))
            # The GPU presenter's read-back check (--gpu-check), beside the simulation's.
            if lg.get("gpu_checked") or lg.get("gpu_mismatched"):
                verdict += (f"; GPU frames {lg['gpu_checked']:,} checked, "
                            f"{lg['gpu_mismatched']:,} differ ({lg['presenter']})")
            why = ref_failure(ref, limit, known) if t == ref_t else run_failure(m, lg, ref, cmp, lim, known)
            if (m.get("argv") or {}).get("load"):
                why = None  # a game loaded from a save starts part way in; hxsuite's hx_checkpoint holds it
            if known and t != ref_t:
                verdict += f"; the oracle is known to end {known['oracle']} here (corpus.json)"
            fps = lg.get("sim_fps") or 0
            rows.append((g.name, t, lg.get("end"), len(lg["F"]), total, m.get("wall_s"), fps, verdict, cmp, why))
            results.append({"replay": g.name, "tag": t, "reference": t == ref_t, "data": m.get("data"),
                            "end": lg.get("end"), "frames": len(lg["F"]), "of": total, "wall_s": m.get("wall_s"),
                            "verdict": verdict, "why": why})
            if cmp:
                sec.append(f"<h3>{t} against {ref_t}: {verdict}</h3>")
                if why:
                    sec.append(f"<p class='bad'>fails: {why}</p>")
                ff = cmp["first_by_field"]
                sec.append("<p class='muted'>first difference by field: " + ", ".join(
                    f"{k} {('frame ' + format(v, ',')) if v else 'none'}" for k, v in ff.items()) + "</p>")
                fs = final_scores(ref, lg)
                if fs:
                    where = (f"frame {fs['frame']:,}" if fs["frame"] == fs["other_frame"]
                             else f"frames {fs['frame']:,} and {fs['other_frame']:,}")
                    if not fs["diffs"]:
                        sec.append(f"<p class='muted'>final scores ({where}): identical, every player's "
                                   "score screen numbers</p>")
                    else:
                        shown = "; ".join(f"player {p} {n} {x} against {y}" for p, n, x, y in fs["diffs"][:8])
                        more = f" and {len(fs['diffs']) - 8} more" if len(fs["diffs"]) > 8 else ""
                        after = (f", after frame {lim:,}, where the original loses commands"
                                 if lim and fs["frame"] > lim else "")
                        sec.append(f"<p class='muted'>final scores ({where}) differ{after}: {shown}{more}</p>")
                sec.append(agreement_strip(ref, lg, total))
                snaps = compare_snaps(g / f"{ref_t}_snaps", g / f"{t}_snaps", lim)
                if snaps and snaps["sized"]:
                    sec.append(f"<p class='muted'>snapshots: {snaps['sized']:,} at another screen size, "
                               "not compared</p>")
                if snaps and snaps["count"]:
                    fs = snaps["first"]
                    sec.append(f"<p class='muted'>snapshots: {snaps['same']:,} of {snaps['count']:,} identical"
                               + (f"; the first difference is at frame {fs[0]:,}, {fs[1]:,} pixels"
                                  + (" and the palette" if fs[2] else "") if fs else "") + "</p>")
        # per-player curves: minerals and units, reference solid, others dashed
        # An empty slot still reports its starting 50 minerals; plot only players
        # that ever own a unit.
        active = {p for lg in logs.values() for p, pts in lg["P"].items() if any(v[2] for v in pts.values())}
        for label, idx in (("Minerals", 0), ("Gas", 1), ("Units", 2)):
            series = []
            for ti, (t, lg) in enumerate(logs.items()):
                for p, pts in sorted(lg["P"].items()):
                    if p not in active:
                        continue
                    s = [(f, v[idx]) for f, v in sorted(pts.items())]
                    series.append((f"{t} player {p}", s, PALETTE[p % len(PALETTE)], t != ref_t))
            players = sorted(active)
            key = " &middot; ".join(f"<span style='color:{PALETTE[p % len(PALETTE)]}'>&#9632;</span> player {p}"
                                    for p in players)
            sec.append(f"<h3>{label} per player</h3>" + svg_lines(series, xmax=total)
                       + f"<p class='muted'>{key}; {ref_t} solid, other builds dashed over it</p>")
        perf = []
        for ti, (t, lg) in enumerate(logs.items()):
            perf.append((t, [(f, ms / 1000.0) for f, ms in lg["T"]], PALETTE[ti % len(PALETTE)], False))
        sec.append("<h3>Wall-clock seconds by frame</h3>" + svg_lines(perf, xmax=total, h=160))
        sec.append("<p class='muted'>" + " &middot; ".join(
            f"<span style='color:{PALETTE[i % len(PALETTE)]}'>&#9632;</span> {t}"
            for i, t in enumerate(logs)) + "</p>")
        sec.append("</section>")
        sections.append("\n".join(sec))

    trs = "".join(
        f"<tr><td>{r[0]}</td><td>{r[1]}</td><td>{r[2]}</td><td class='n'>{r[3]:,}</td>"
        f"<td class='n'>{r[4]:,}</td><td class='n'>{r[5]}</td><td class='n'>{r[6]:,.0f}</td>"
        f"<td class='n'>{r[6] * 0.042:,.1f}x</td>"
        f"<td class='{'bad' if r[9] else 'ok'}'>{r[7]}{'; fails: ' + r[9] if r[9] else ''}</td></tr>"
        for r in rows)
    ran_on = ""
    if prov:
        ran_on = (f"<p class='muted'>Commit {prov['commit']}{' with changes not committed' if prov['dirty'] else ''}; "
                  f"Hyperion on {' and '.join(DATA_WORDS[s] for s in prov['data'])}"
                  + (f", the install {prov['remastered']} at {prov.get('casc_build')}" if prov.get("remastered") else "")
                  + f"; the oracle on the MPQs ({', '.join(f'{k} {v[:10]}' for k, v in prov['mpqs'].items())}).</p>")
    html = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Replay Test Report</title>
<style>
:root {{ --bg:#fbfbfa; --fg:#1d1f21; --muted:#6b7075; --line:#e2e2de; --ok:#2a9d58; --bad:#d23c3c; --none:#c9c9c4; --card:#fff; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#16181b; --fg:#e6e6e3; --muted:#9aa0a6; --line:#2c3035; --card:#1d2024; --none:#3a3e44; }} }}
:root[data-theme="dark"] {{ --bg:#16181b; --fg:#e6e6e3; --muted:#9aa0a6; --line:#2c3035; --card:#1d2024; --none:#3a3e44; }}
body {{ background:var(--bg); color:var(--fg); font:14px/1.5 system-ui,sans-serif; margin:0; padding:24px 16px; }}
main {{ max-width:820px; margin:0 auto; }}
h1 {{ font-size:22px; margin:0 0 4px; }} h2 {{ font-size:17px; margin:28px 0 4px; }} h3 {{ font-size:14px; margin:18px 0 6px; }}
.muted {{ color:var(--muted); margin:2px 0 8px; }}
table {{ border-collapse:collapse; width:100%; font-size:13px; }} th,td {{ padding:5px 8px; border-bottom:1px solid var(--line); text-align:left; }}
td.n {{ text-align:right; font-variant-numeric:tabular-nums; }} .ok {{ color:var(--ok); }} .bad {{ color:var(--bad); }}
.wrap {{ overflow-x:auto; }} section {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:4px 16px 12px; margin-top:16px; }}
svg.chart {{ width:100%; height:auto; }} svg.strip {{ width:100%; height:18px; display:block; }}
.grid {{ stroke:var(--line); }} .ax {{ fill:var(--muted); font-size:10px; }}
.s-ok {{ fill:var(--ok); }} .s-bad {{ fill:var(--bad); }} .s-none {{ fill:var(--none); }}
</style></head><body><main>
<h1>Replay test report</h1>
<p class="muted">{run_dir.name}. Each run carries the same observer: the 1.17 oracle exe has it injected, Hyperion loads it.
A frame matches when the random state, the random call count, the unit count, the unit-field hash and the stats hash
(resources, score counters, counts per unit type, research and upgrades) all agree with the reference; every 24 frames
each player's resources, units and score screen numbers must agree too.</p>
{ran_on}
<div class="wrap"><table><tr><th>Replay</th><th>Build</th><th>End</th><th>Frames</th><th>Of</th><th>Wall s</th><th>Sim fps</th><th>Speed</th><th>Against reference</th></tr>{trs}</table></div>
{''.join(sections)}
</main></body></html>"""
    (run_dir / "report.html").write_text(html, encoding="utf-8")
    return results


def cmd_report(args) -> int:
    run_dir = Path(args.run_dir)
    kept = run_dir / "summary.json"
    prov = json.loads(kept.read_text()) if kept.exists() else None
    rows = write_report(run_dir, args.ref, prov)
    print(run_dir / "report.html")
    return 1 if any(r["why"] for r in rows) else 0


# ---------------------------------------------------------------------------

def add_run_args(p):
    p.add_argument("--data", default=DEFAULT_DATA,
                   help="the kinds of files an engine build plays each replay on, in turn, a comma "
                        "between: remastered (a StarCraft: Remastered install alone, testrun/hxr/), mpq "
                        f"(1.17's MPQs), both (Remastered's over the MPQs); default {DEFAULT_DATA}")
    p.add_argument("--remastered", help="the StarCraft: Remastered install to read (default: "
                                        "HYPERION_REMASTERED, or the one Battle.net's uninstall entry names)")
    p.add_argument("--clock", default="virtual", choices=["virtual", "real"])
    p.add_argument("--clock-step", type=int, default=50, dest="clock_step")
    p.add_argument("--updates", type=int, default=0,
                   help="frames per repaint while a game runs (0 leaves the game's own value)")
    p.add_argument("--max-frames", type=int, default=0, dest="max_frames")
    p.add_argument("--stats-every", type=int, default=24, dest="stats_every")
    p.add_argument("--stall-secs", type=int, default=30, dest="stall_secs")
    p.add_argument("--menu-timeout", type=int, default=60, dest="menu_timeout")
    p.add_argument("--no-keep-active", action="store_false", dest="keep_active")
    p.add_argument("--snap-every", type=int, default=0, dest="snap_every")
    p.add_argument("--snap-screen", action="store_true", dest="snap_screen",
                   help="snapshots are the whole screen (map, console, dialogs, cursor), not the "
                        "draw buffer; the report compares them")
    p.add_argument("--snap-move", action="store_true", dest="snap_move",
                   help="halfway to each snapshot, centre the view on a unit (the same one in every "
                        "build), so the snapshots cover the map and not only the replay's first view")
    p.add_argument("--dump-frame", type=int, default=0, dest="dump_frame",
                   help="log every used unit at this frame: the fields h_sem hashes (Z lines), and on "
                        "x86 its raw bytes (Y lines)")
    p.add_argument("--save-frame", type=int, default=0, dest="save_frame",
                   help="save the game at this frame, as the save menu does but with a fixed time; the "
                        "save is copied into the run folder as <tag>.<ext>")
    p.add_argument("--save-name", default="rt_save", dest="save_name")
    p.add_argument("--load", help="start from this saved game in place of the replay, with the game's "
                                  "seed pinned and no input, and play it on to --max-frames "
                                  "")
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--stay", action="store_true",
                   help="the game goes on past the replay's end, into the score screen, until "
                        "--timeout ends the run")
    p.add_argument("--menus", action="store_true",
                   help="an engine build reaches the replay through the menus, as the oracle does, "
                        "instead of starting it with --replay")
    p.add_argument("--no-cache", action="store_false", dest="cache",
                   help="run the oracle even when a cached run of the same replay and settings exists")
    p.add_argument("--game-arg", action="append", default=["nosound"], dest="game_arg")
    p.add_argument("--view", help="an engine build plays at this view width or shape (Hyperion's "
                                  "--view, e.g. 16:9); the oracle stays 640x480")
    p.add_argument("--view-every", type=int, default=0, dest="view_every",
                   help="an engine build switches its view between 640x480 and --view's every N "
                        "frames, as F5 does, starting a quarter of the way into the first N; with "
                        "--snap-every N every other snapshot is at 640x480, after a switch there "
                        "and back, and compares with the oracle's")
    p.add_argument("--window", default="background",
                   choices=["background", "hidden", "windowed", "fullscreen"],
                   help="how an engine build shows its window (HYPERION_WINDOW): background is a "
                        "640x480 window that takes neither the focus nor the mouse; hidden draws "
                        "every frame but shows none")
    p.add_argument("--renderer", choices=["sdl", "direct3d12", "vulkan", "gpu"],
                   help="the presenter an engine build draws through (HYPERION_RENDERER); "
                        "sdl when not given, though the engine's own default is vulkan")
    p.add_argument("--gpu-check", action="store_true", dest="gpu_check",
                   help="the GPU presenter reads every frame back and compares it with the screen "
                        "through the palette (HYPERION_GPU_CHECK); the counts are in each run's JSON")
    p.add_argument("--scene", choices=["terrain", "sprites", "map", "screen", "gpu"],
                   help="an engine build draws the map on the GPU from its frame description as well, "
                        "to this stage (HYPERION_GPU_SCENE, needs a GPU --renderer); with --gpu-check "
                        "it is compared with the game's map every frame")
    p.add_argument("--dll", action="append", default=[], dest="extra_dll",
                   help="another DLL, path[@export], loaded after the observer")
    p.add_argument("--check-observe", action="store_true", dest="check_observe",
                   help="an engine build's observer compares what oc_observe hands over with 1.17's memory")
    p.add_argument("--out")
    p.add_argument("--tag")


def cmd_gate(args) -> int:
    """The gates in turn (docs/modules/build.md, "Gates"): on every commit the quick suite on
    each kind of files, with --hx hxsuite too; at a merge the full set, the archive, hxsuite and
    the Linux builds' quick suites. Prints one line for the commit message, and exits 1 if any
    gate failed."""
    stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    data = ["--data", args.data] + (["--remastered", args.remastered] if args.remastered else [])
    if args.merge:
        steps = [("full suite", ["suite"] + data), ("archive", ["suite", "--set", "archive"] + data),
                 ("hxsuite", ["hxsuite"] + data)]
        if not args.no_linux:
            steps += [("Linux x64 quick suite", ["suite", "--quick", "--builds", "oracle,lx64"] + data),
                      ("Linux ARM64 quick suite", ["suite", "--quick", "--builds", "oracle,la64", "--data", "mpq"])]
    else:
        steps = [("quick suite", ["suite", "--quick"] + data)] + ([("hxsuite", ["hxsuite"] + data)] if args.hx else [])
    gate_dir = RUNS / f"gate_{stamp}"
    said = []
    for label, argv in steps:
        out = gate_dir / label.replace(" ", "_").lower()
        print(f"\ngate: {label} -> {out}")
        step = build_parser().parse_args(argv + ["--out", str(out)])
        rc = step.func(step)
        kept = json.loads((out / "summary.json").read_text()) if (out / "summary.json").exists() else {}
        runs = sum(not r["reference"] for r in kept.get("runs", []))
        where = out.relative_to(TESTRUN).as_posix()
        on = " and ".join(DATA_WORDS[s] for s in kept.get("data", []))
        said.append(f"{label} on {on}: " + (f"{runs} runs pass" if rc == 0 else
                                             f"FAILED ({len(kept.get('failed', [])) or 'see the log'})") + f" ({where})")
    failed = any("FAILED" in s for s in said)
    print("\n" + ("gate FAILED: " if failed else "gate: ") + "; ".join(said))
    return 1 if failed else 0


def cmd_corpus(args) -> int:
    """Every replay corpus.json names, here with its sha1; a missing one is listed with where it
    comes from."""
    rows, bad = corpus(), []
    for r in rows:
        p = REPLAYS / r["path"]
        if not p.exists():
            bad.append(f"missing {r['path']}, from {r['source']}")
        elif sha1(p) != r["sha1"]:
            bad.append(f"{r['path']} is another file (sha1 {sha1(p)[:12]}, not {r['sha1'][:12]})")
    sets = {}
    for r in rows:
        for s in r["sets"]:
            sets[s] = sets.get(s, 0) + 1
    print(f"corpus: {len(rows)} replays, " + ", ".join(f"{n} in {s}" for s, n in sets.items()))
    for b in bad:
        print(f"  {b}")
    print("corpus: " + ("every replay here" if not bad else f"{len(bad)} not as corpus.json has them"))
    return 1 if bad else 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("setup")
    s.set_defaults(func=cmd_setup)
    r = sub.add_parser("run")
    r.add_argument("--build", required=True, choices=["oracle", *ENGINE_BUILDS])
    r.add_argument("--replay", required=True)
    add_run_args(r)
    r.set_defaults(func=cmd_run)
    su = sub.add_parser("suite")
    su.add_argument("--builds", default="oracle,hx64")
    su.add_argument("--replays", nargs="+",
                    help="replays or folders of them (default: a set of corpus.json, the full one)")
    su.add_argument("--quick", action="store_true", help="the quick set, as --set quick")
    su.add_argument("--set", choices=["quick", "full", "archive"], help="the set of corpus.json to play")
    add_run_args(su)
    su.set_defaults(updates=32)
    su.set_defaults(func=cmd_suite)
    hx = sub.add_parser("hxsuite", help="Hyperion's own replays against their kept references")
    hx.add_argument("--replays", nargs="+", help="replays or folders of them (default: the Hyperion set)")
    hx.add_argument("--keep-ref", action="store_true", dest="keep_ref",
                    help="keep this build's runs as the references")
    add_run_args(hx)
    hx.set_defaults(updates=32)
    hx.set_defaults(func=cmd_hxsuite)
    hr = sub.add_parser("hxrecord", help="record the Hyperion set's games (hxsuite.json) with this build")
    hr.add_argument("--only", nargs="+", help="these games of the manifest, by name")
    add_run_args(hr)
    hr.set_defaults(updates=32)
    hr.set_defaults(func=cmd_hxrecord)
    rp = sub.add_parser("report")
    rp.add_argument("run_dir")
    rp.add_argument("--ref", default="oracle")
    rp.set_defaults(func=cmd_report)
    g = sub.add_parser("gate", help="the gates of docs/modules/build.md, with one line for the commit message")
    g.add_argument("--hx", action="store_true", help="hxsuite too, for a change Hyperion's games can reach")
    g.add_argument("--merge", action="store_true",
                   help="the merge gate: the full and archive sets, hxsuite, and the Linux builds' quick suites")
    g.add_argument("--no-linux", action="store_true", dest="no_linux", help="the merge gate without the Linux builds")
    g.add_argument("--data", default=DEFAULT_DATA, help=f"the kinds of files (default {DEFAULT_DATA})")
    g.add_argument("--remastered", help="the StarCraft: Remastered install to read")
    g.set_defaults(func=cmd_gate)
    c = sub.add_parser("corpus", help="check that every replay corpus.json names is here, by its sha1")
    c.set_defaults(func=cmd_corpus)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
