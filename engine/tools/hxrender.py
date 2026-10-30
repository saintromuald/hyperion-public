"""Large games measured frame by frame: a benchmark of computer players, then its replay
played again under a camera that holds, zooms and pans, with a row for every game frame and
every frame shown (HYPERION_FRAME_LOG, src117/port/tools/port_framelog.cpp), and a report.

    python tools/hxrender.py setup                     the game folder testrun/scale/ and its maps
    python tools/hxrender.py bench [--players 32] [--map "Big Game Hunters 4x4"] [--frames 60000]
                                   [--army N] [--pools K]   a stress run: N units a computer, pools x K
    python tools/hxrender.py sweep <bench folder | run.rep> [--art sd|hd] [--view 1136] [--at FRAME]
    python tools/hxrender.py report <sweep folder> [--against <sweep folder>]
    python tools/hxrender.py shots <bench folder | run.rep> [--art hd] [--zooms 1,4,16] [--label x] [--env V=1]
    python tools/hxrender.py compare <shots folder> <shots folder>   the pixels that differ
    python tools/hxrender.py profile <bench folder | run.rep> [--start F] [--end F]   the game alone, sampled
    python tools/hxrender.py all [--players 32] [--art sd,hd]   bench, a sweep for each art, the reports

The game folder is testrun/scale/, made as tools/play.py makes its own: hardlinks to testrun's
MPQs and Local.dll, the x64 SDL3.dll, and the built Hyperion.exe copied in each run; the maps
are copied from testrun/play/maps/Hyperion (read only), or made by tools/hxmap/remake.py. Its
data folder is testrun/scale/data. A run's outputs go under testrun/scale/runs/<stamp> <name>/:
the bench's folder, a folder per sweep with game.csv, drawn.csv, marks.csv, sweep.json, and
report.md / report.json.

A sweep plays the replay from the frame given (the bench's most units, by default), in a
background window on the right display, at the game's own speed, every setting that paces
frames off (no frame limit, no vertical sync), so a frame's cost shows as the time to the next.
With --display both are left as a player has them (matched to the display, vertical sync on),
to see what the game keeps of its own speed when frames are shown as a player sees them.
The camera then goes through its steps, each marked in the log:

  hold-z1          the game's view over the busiest part of the map
  ramp-out         the zoom eased out a step at a time to the whole map
  hold-zN          held at each zoom on the way (2, 4, 8 and the whole map)
  pan-zN           moved across the map at a zoom
  ramp-in          back to 1
  pan-z1           moved across at the game's view

The report gives, a step at a time: frames shown a second, the time between frames (median,
95th, 99th, worst), each part of a drawn frame, what the scene held (primitives, images, HD
draws, units in the camera's sight); the game frames' parts and their spikes; and how the cost
grows with what is drawn.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ENGINE / "tools"))
sys.path.insert(0, str(ENGINE / "tools" / "replaytest"))
import replaytest as rt  # noqa: E402  (the harness's folder helpers)
from hxlive import ws_call  # noqa: E402

SCALE = ENGINE / "testrun" / "scale"
DATA = SCALE / "data"
RUNS = SCALE / "runs"
MAPS_FROM = ENGINE / "testrun" / "play" / "maps" / "Hyperion"
PORT = 27317  # the live channel of the instance this starts, apart from hxbench's 27217
BUILT = [(rt.HX64_BUILT, "Hyperion.exe"), (rt.HX64_BUILT.with_suffix(".pdb"), "Hyperion.pdb"),
         (rt.SDL3_DLL_X64, "SDL3.dll")]


# --- the game folder -------------------------------------------------------------------------

def setup(quiet: bool = False) -> None:
    rt.fill_game_folder(SCALE, x64=True)
    for src, name in BUILT:
        if src.exists():
            dst = SCALE / name
            if not dst.exists() or src.stat().st_mtime != dst.stat().st_mtime or src.stat().st_size != dst.stat().st_size:
                if dst.exists():
                    dst.unlink()
                shutil.copy2(src, dst)
    mods = ENGINE / "mods"
    if mods.exists():
        shutil.copytree(mods, SCALE / "mods", dirs_exist_ok=True)
    maps = SCALE / "maps" / "Hyperion"
    maps.mkdir(parents=True, exist_ok=True)
    if MAPS_FROM.exists():
        for m in MAPS_FROM.glob("Big Game Hunters*.hxm"):
            if not (maps / m.name).exists():
                shutil.copy2(m, maps / m.name)
    if not any(maps.glob("Big Game Hunters 4x4.hxm")):
        subprocess.run([sys.executable, "-m", "tools.hxmap.remake", "set", str(maps)], cwd=ENGINE.parent, check=True)
    DATA.mkdir(parents=True, exist_ok=True)
    (SCALE / "hyperion_quiet.flag").write_text("")
    if not quiet:
        print(f"game folder ready: {SCALE}")


def env_for(art: str | None = None, renderer: str = "vulkan") -> dict:
    env = dict(os.environ)
    for k in list(env):
        if k.startswith("HYPERION_"):
            del env[k]
    env.update(HYPERION_WINDOW="background", HYPERION_DISPLAY="right", HYPERION_RENDERER=renderer,
               HYPERION_QUIET="1")
    return env


def map_path(name: str) -> Path:
    p = Path(name)
    if p.exists():
        return p.resolve()
    p = SCALE / "maps" / "Hyperion" / (name if name.endswith(".hxm") else name + ".hxm")
    if not p.exists():
        sys.exit(f"no map {p}")
    return p


# --- the benchmark ---------------------------------------------------------------------------

def bench(players: int, map_name: str, frames: int, out: Path, army: int = 0, pools: int = 0) -> Path:
    setup(quiet=True)
    before = set((DATA / "benchmarks").glob("*")) if (DATA / "benchmarks").exists() else set()
    cmd = [str(SCALE / "Hyperion.exe"), f"--data={DATA}", "--bench", "--map", str(map_path(map_name)),
           "--players", str(players), "--bench-speed", "max", "--bench-shown", "none",
           "--bench-frames", str(frames)]
    # a stress run: each computer an army at the first frame, the pools past the players'
    if army:
        cmd += ["--bench-army", str(army)]
    if pools:
        cmd += ["--bench-pools", str(pools)]
    print("bench:", " ".join(cmd[1:]))
    t0 = time.time()
    p = subprocess.run(cmd, cwd=SCALE, env=env_for())
    after = set((DATA / "benchmarks").glob("*")) - before
    if not after:
        sys.exit(f"the bench wrote no folder (exit {p.returncode})")
    folder = max(after, key=lambda q: q.stat().st_mtime)
    dst = out / "bench"
    shutil.copytree(folder, dst)
    s = json.loads((dst / "summary.json").read_text())
    print(f"bench: {time.time() - t0:.0f} s, {s.get('game_frames')} game frames at {s.get('game_frames_per_s')}/s, "
          f"most units {s.get('units_most')} at frame {s.get('units_most_at')}, ended {s.get('ended')}")
    return dst


class _Done(Exception):
    """A quick sweep's early end."""


# --- the sweep -------------------------------------------------------------------------------

def call(msg: dict, timeout: float = 30.0) -> dict:
    msg = dict(msg)
    msg.setdefault("id", int(time.time() * 1000) % 1000000)
    return ws_call(PORT, msg, timeout=timeout)


def wait_for_game(seconds: float = 90.0) -> dict:
    end = time.time() + seconds
    last = None
    while time.time() < end:
        try:
            st = call({"cmd": "status"}, timeout=5)
            last = st
            if st.get("ok") and st.get("state") == "game":
                return st
        except Exception as e:  # not listening yet
            last = str(e)
        time.sleep(1.0)
    raise RuntimeError(f"the replay did not start: {last}")


def settings_rows() -> list:
    return call({"cmd": "settings"}).get("settings", [])


def set_setting(rows: list, keys: tuple, value: float, log: list) -> None:
    """The row whose name or key holds one of keys, set to value."""
    for r in rows:
        hay = " ".join(str(r.get(k, "")) for k in ("name", "key", "id", "label")).lower()
        if any(k in hay for k in keys):
            a = call({"cmd": "setting", "name": r.get("name"), "value": float(value)})
            log.append({"setting": r.get("name"), "value": value, "ok": a.get("ok"), "error": a.get("error")})
            return
    log.append({"setting": "/".join(keys), "value": value, "ok": False, "error": "no such row"})


def busiest(units: list, map_w: int, map_h: int, cell=(1136, 400)) -> tuple:
    """The middle of the game-view-sized rectangle holding the most units."""
    cw, ch = cell
    best, at = -1, (map_w // 2, map_h // 2)
    grid = {}
    for u in units:
        k = (u["x"] // (cw // 2), u["y"] // (ch // 2))
        grid[k] = grid.get(k, 0) + 1
    for (gx, gy) in grid:
        n = sum(grid.get((gx + dx, gy + dy), 0) for dx in (0, 1) for dy in (0, 1))
        if n > best:
            best, at = n, ((gx + 1) * (cw // 2), (gy + 1) * (ch // 2))
    return at, best


def sweep(replay: Path, art: str, view: int, at: int | None, out: Path, renderer: str, hold: float,
          quick: bool = False, label: str = "", env_extra: dict | None = None, profile: bool = False,
          holds: list[float] | None = None, display: bool = False) -> Path:
    setup(quiet=True)
    replay, out = replay.resolve(), out.resolve()
    folder = out / f"sweep_{art}{('_' + label) if label else ''}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    rep = folder / "run.rep"
    shutil.copy2(replay, rep)
    cmd = [str(SCALE / "Hyperion.exe"), f"--data={DATA}", f"--control={PORT}", f"--view={view}", "--replay", str(rep)]
    env = env_for(art, renderer)
    env.update(env_extra or {})
    if profile:
        env["HYPERION_PROFILE"] = "1"
    p = subprocess.Popen(cmd, cwd=SCALE, env=env)
    meta = {"replay": str(replay), "art": art, "view": view, "renderer": renderer, "steps": [], "settings": [],
            "started": dt.datetime.now().isoformat(timespec="seconds")}
    try:
        st = wait_for_game()
        meta["status_start"] = st
        # the replay's speed is its own; held while it seeks
        call({"cmd": "pause"})
        if at:
            frame = call({"cmd": "status"}).get("frame", 0)
            print(f"sweep {art}: seeking to frame {at} from {frame}")
            while frame < at:
                a = call({"cmd": "step", "frames": min(10000, at - frame), "fast": True}, timeout=600)
                if not a.get("ok"):
                    raise RuntimeError(f"step: {a}")
                frame = a.get("frame", call({"cmd": "status"}).get("frame", 0))
        rows = settings_rows()
        (folder / "settings_rows.json").write_text(json.dumps(rows, indent=1))
        set_setting(rows, ("frame rate", "framerate"), 2 if display else 5, meta["settings"])  # the display's, or no limit
        set_setting(rows, ("vertical sync", "vsync"), 1 if display else 0, meta["settings"])
        set_setting(rows, ("art",), 1 if art == "hd" else 0, meta["settings"])
        units = call({"cmd": "units", "max": 100000}, timeout=60)
        us = units.get("units", [])
        size = st.get("map") or {}
        map_w = int(size.get("width") or max([u["x"] for u in us] + [4096]) + 64)
        map_h = int(size.get("height") or max([u["y"] for u in us] + [4096]) + 64)
        (cx, cy), n = busiest(us, map_w, map_h)
        meta.update(units=units.get("count"), busiest={"x": cx, "y": cy, "units": n}, map_px=[map_w, map_h])
        print(f"sweep {art}: frame {units.get('frame')}, {units.get('count')} units, busiest ({cx},{cy}) with {n}")
        call({"cmd": "camera", "x": cx, "y": cy, "zoom": 1})
        time.sleep(1.5)
        log = call({"cmd": "framelog", "on": True, "folder": str(folder)})
        if not log.get("ok"):
            raise RuntimeError(f"framelog: {log}")
        call({"cmd": "resume"})

        def step(name: str, seconds: float, moves=()):
            call({"cmd": "framelog", "mark": name})
            meta["steps"].append({"name": name, "at": time.time()})
            if not moves:
                # with --profile, each hold sampled by itself: profile_<step>.log
                if profile and seconds > 0:
                    call({"cmd": "profile", "reset": True})
                time.sleep(seconds)
                if profile and seconds > 0:
                    call({"cmd": "profile", "write": str(folder / f"profile_{name}.log")}, timeout=120)
                return
            dt_ = seconds / len(moves)
            for (x, y, z) in moves:
                call({"cmd": "camera", "x": x, "y": y, "zoom": z})
                time.sleep(dt_)

        step("hold-z1", hold)
        zooms = [1.25, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10, 12, 16]
        holds = holds or [2, 4, 8, 16]
        if quick:
            for z in holds:
                call({"cmd": "camera", "x": cx, "y": cy, "zoom": z})
                time.sleep(1.5)
                step(f"hold-z{z:g}", hold)
            step("end", 0.5)
            meta["log"] = call({"cmd": "framelog", "on": False})
            meta["status_end"] = call({"cmd": "status"})
            return_folder = True
        else:
            return_folder = False
        if return_folder:
            raise _Done()
        step("ramp-out", 6.0, [(cx, cy, z) for z in zooms])
        for z in holds:
            call({"cmd": "camera", "x": cx, "y": cy, "zoom": z})
            time.sleep(1.5)
            step(f"hold-z{z:g}", hold)
            # a pan across the map's middle row and back at this zoom
            xs = [int(map_w * f) for f in (0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 0.6, 0.3)]
            step(f"pan-z{z:g}", 6.0, [(x, map_h // 2, z) for x in xs])
        step("ramp-in", 6.0, [(cx, cy, z) for z in reversed([1] + zooms)])
        time.sleep(1.0)
        xs = [cx + d for d in (-1600, -800, 0, 800, 1600, 800, 0)]
        step("pan-z1", 6.0, [(max(400, x), cy, 1) for x in xs])
        step("end", 0.5)
        final = call({"cmd": "framelog", "on": False})
        meta["log"] = final
        meta["status_end"] = call({"cmd": "status"})
    except _Done:
        pass
    finally:
        p.kill()
        p.wait(15)
    (folder / "sweep.json").write_text(json.dumps(meta, indent=1, default=str))
    return folder


# --- snapshots, to compare two ways of drawing the same frame --------------------------------

def shots(replay: Path, art: str, view: int, at: int | None, out: Path, renderer: str, zooms: list,
          label: str, env_extra: dict | None = None) -> Path:
    """The replay held at frame at, the camera over its busiest spot at each zoom, a snapshot of
    each: the same frames drawn by two builds or settings (--env) compare pixel for pixel."""
    setup(quiet=True)
    replay, out = replay.resolve(), out.resolve()
    folder = out / f"shots_{art}{('_' + label) if label else ''}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    cmd = [str(SCALE / "Hyperion.exe"), f"--data={DATA}", f"--control={PORT}", f"--view={view}", "--replay",
           str(replay)]
    env = env_for(art, renderer)
    env.update(env_extra or {})
    p = subprocess.Popen(cmd, cwd=SCALE, env=env)
    meta = {"replay": str(replay), "art": art, "view": view, "env": env_extra or {}, "shots": []}
    try:
        st = wait_for_game()
        call({"cmd": "pause"})
        frame = call({"cmd": "status"}).get("frame", 0)
        while at and frame < at:
            a = call({"cmd": "step", "frames": min(10000, at - frame), "fast": True}, timeout=600)
            if not a.get("ok"):
                raise RuntimeError(f"step: {a}")
            frame = a.get("frame", call({"cmd": "status"}).get("frame", 0))
        rows = settings_rows()
        set_setting(rows, ("frame rate", "framerate"), 5, [])
        set_setting(rows, ("vertical sync", "vsync"), 0, [])
        set_setting(rows, ("art",), 1 if art == "hd" else 0, [])
        call({"cmd": "set", "name": "hd.water", "value": 0})  # its ripple moves with the clock
        units = call({"cmd": "units", "max": 100000}, timeout=60).get("units", [])
        size = st.get("map") or {}
        (cx, cy), n = busiest(units, int(size.get("width") or 4096), int(size.get("height") or 4096))
        meta.update(frame=frame, busiest={"x": cx, "y": cy, "units": n})
        for z in zooms:
            call({"cmd": "camera", "x": cx, "y": cy, "zoom": z})
            time.sleep(4.0)  # the art each frame may bring up, and frames enough to settle
            name = f"z{z:g}"
            a = call({"cmd": "snapshot", "name": f"shots_{name}"}, timeout=60)
            if not a.get("ok"):
                raise RuntimeError(f"snapshot: {a}")
            shutil.copy2(a["path"], folder / f"{name}.png")
            meta["shots"].append(name)
            print(f"shots {art}{(' ' + label) if label else ''}: {name} at frame {frame}")
    finally:
        p.kill()
        p.wait(15)
    (folder / "shots.json").write_text(json.dumps(meta, indent=1))
    return folder


def profile(replay: Path, start: int, end: int, out: Path, env_extra: dict | None = None) -> Path:
    """The game alone sampled (HYPERION_PROFILE) from frame start to end of the replay, stepped as
    fast as it goes, nothing drawn between: profile_<start>_<end>.log in out."""
    setup(quiet=True)
    replay, out = replay.resolve(), out.resolve()
    cmd = [str(SCALE / "Hyperion.exe"), f"--data={DATA}", f"--control={PORT}", "--replay", str(replay)]
    env = env_for()
    env.update(env_extra or {})
    env["HYPERION_PROFILE"] = "1"
    p = subprocess.Popen(cmd, cwd=SCALE, env=env)
    path = out / f"profile_{start}_{end}.log"
    try:
        wait_for_game()
        call({"cmd": "pause"})

        def seek(to: int) -> int:
            frame = call({"cmd": "status"}).get("frame", 0)
            while frame < to:
                a = call({"cmd": "step", "frames": min(10000, to - frame), "fast": True}, timeout=900)
                if not a.get("ok"):
                    raise RuntimeError(f"step: {a}")
                frame = a.get("frame", call({"cmd": "status"}).get("frame", 0))
            return frame

        seek(start)
        call({"cmd": "profile", "reset": True})
        t0 = time.time()
        seek(end)
        secs = time.time() - t0
        a = call({"cmd": "profile", "write": str(path)}, timeout=120)
        print(f"profile: frames {start} to {end} in {secs:.1f} s ({(end - start) / secs:.0f} a second), "
              f"{a.get('samples')} samples -> {path}")
        # the replay's own check (a Hyperion replay holds the game's hash every 24 frames)
        sync = call({"cmd": "status"}).get("sync", {})
        print(f"sync: {sync.get('state')}, {sync.get('checked')} checked of {sync.get('recorded')}, "
              f"first bad {sync.get('first_bad')}")
    finally:
        p.kill()
        p.wait(15)
    return path


def compare(a: Path, b: Path) -> list:
    """Each snapshot of a against b's: the pixels that differ, the largest difference, and a
    picture of where (diff_<name>.png in b's folder, white where they differ)."""
    from PIL import Image, ImageChops
    out = []
    for pa in sorted(a.glob("z*.png")):
        pb = b / pa.name
        if not pb.exists():
            continue
        ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
        if ia.size != ib.size:
            out.append({"shot": pa.stem, "error": f"sizes {ia.size} and {ib.size}"})
            continue
        d = ImageChops.difference(ia, ib)
        px = list(d.getdata())
        bad = sum(1 for q in px if q != (0, 0, 0))
        big = max((max(q) for q in px), default=0)
        d.point(lambda v: 255 if v else 0).save(b / f"diff_{pa.stem}.png")
        out.append({"shot": pa.stem, "pixels": len(px), "differ": bad, "most": big})
    return out


# --- the report ------------------------------------------------------------------------------

def read_csv(path: Path) -> list:
    with open(path, newline="") as f:
        return [{k: float(v) if k != "name" else v for k, v in r.items()} for r in csv.DictReader(f)]


def pct(xs: list, q: float) -> float:
    if not xs:
        return float("nan")
    xs = sorted(xs)
    i = min(len(xs) - 1, max(0, int(math.ceil(q / 100.0 * len(xs))) - 1))
    return xs[i]


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def fit(xs, ys):
    """Least squares y = a + b x: (a, b, r)."""
    if len(xs) < 3:
        return float("nan"), float("nan"), float("nan")
    mx, my = mean(xs), mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    if sxx == 0:
        return my, 0.0, 0.0
    b = sxy / sxx
    r = sxy / math.sqrt(sxx * syy) if syy > 0 else 0.0
    return my - b * mx, b, r


GAME_PARTS = ["creep", "ai", "fog", "units_ms", "movers", "triggers", "check", "minimap"]
DRAWN_PARTS = ["events_ms", "ui_ms", "draw_ms", "pass_ms", "scene_ms", "present_ms", "gpu_wait_ms"]


def summarize_drawn(rows: list) -> dict:
    iv = [r["interval_ms"] for r in rows]
    d = {"frames": len(rows), "shown_per_s": round(1000.0 / mean(iv), 1) if rows else 0,
         "interval_ms": {"p50": pct(iv, 50), "p95": pct(iv, 95), "p99": pct(iv, 99), "max": max(iv) if iv else 0},
         "over_16_7": sum(1 for x in iv if x > 16.7), "over_33_3": sum(1 for x in iv if x > 33.3)}
    for k in DRAWN_PARTS + ["prims", "image_prims", "hd_draws", "units_in_sight", "zoom", "game_frames", "distorts",
                            "layer_px"]:
        xs = [r[k] for r in rows]
        d[k] = {"mean": round(mean(xs), 3), "p95": round(pct(xs, 95), 3), "max": round(max(xs), 3) if xs else 0}
    return d


def report(folder: Path, against: Path | None = None) -> dict:
    game = read_csv(folder / "game.csv")
    drawn = read_csv(folder / "drawn.csv")
    with open(folder / "marks.csv", newline="") as f:
        marks = [{"t_ms": float(r["t_ms"]), "frame": int(r["frame"]), "name": r["name"]} for r in csv.DictReader(f)]
    meta = json.loads((folder / "sweep.json").read_text()) if (folder / "sweep.json").exists() else {}
    # the steps, from one mark to the next
    steps = []
    for i, m in enumerate(marks):
        end = marks[i + 1]["t_ms"] if i + 1 < len(marks) else float("inf")
        rows = [r for r in drawn if m["t_ms"] <= r["t_ms"] < end]
        g = [r for r in game if m["t_ms"] <= r["t_ms"] < end]
        if m["name"] == "end" or not rows:
            continue
        s = summarize_drawn(rows)
        s["name"] = m["name"]
        s["seconds"] = round((min(end, drawn[-1]["t_ms"]) - m["t_ms"]) / 1000.0, 2)
        s["game_frames_run"] = len(g)
        s["game_per_s"] = round(len(g) / s["seconds"], 1) if s["seconds"] > 0 else 0
        steps.append(s)
    # the game frames
    gms = [r["game_ms"] for r in game]
    med = statistics.median(gms) if gms else 0.0
    spikes = [r for r in game if r["game_ms"] > max(2.0 * med, med + 2.0)]
    def lead(r):
        return max(GAME_PARTS, key=lambda k: r[k])
    by_lead = {}
    for r in spikes:
        by_lead.setdefault(lead(r), []).append(r["game_ms"])
    game_s = {"frames": len(game), "units_mean": round(mean([r["units"] for r in game]), 0) if game else 0,
              "game_ms": {"mean": round(mean(gms), 3), "p50": round(med, 3), "p95": round(pct(gms, 95), 3),
                          "p99": round(pct(gms, 99), 3), "max": round(max(gms), 3) if gms else 0},
              "parts_mean_ms": {k: round(mean([r[k] for r in game]), 4) for k in GAME_PARTS},
              "parts_max_ms": {k: round(max([r[k] for r in game]), 3) if game else 0 for k in GAME_PARTS},
              "spikes": {"threshold_ms": round(max(2.0 * med, med + 2.0), 3), "count": len(spikes),
                         "by_part": {k: {"count": len(v), "mean_ms": round(mean(v), 2), "max_ms": round(max(v), 2)}
                                     for k, v in sorted(by_lead.items(), key=lambda kv: -len(kv[1]))}},
              "worst": [{"frame": int(r["frame"]), "game_ms": round(r["game_ms"], 2), "lead": lead(r),
                         **{k: round(r[k], 2) for k in GAME_PARTS if r[k] > 0.05}}
                        for r in sorted(game, key=lambda r: -r["game_ms"])[:12]]}
    # what a drawn frame costs against what it draws
    by_zoom = {}
    for r in drawn:
        z = max(r["zoom"], 0.5)
        key = min([1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32], key=lambda k: abs(math.log(z) - math.log(k)))
        by_zoom.setdefault(key, []).append(r)
    zoom_s = []
    for k in sorted(by_zoom):
        rows = by_zoom[k]
        zoom_s.append({"zoom": k, "frames": len(rows), **{n: round(mean([r[n] for r in rows]), 3)
                       for n in ("interval_ms", "draw_ms", "pass_ms", "scene_ms", "present_ms", "gpu_wait_ms", "distorts", "layer_px", "prims",
                                 "image_prims", "hd_draws", "units_in_sight")},
                       "interval_p95": round(pct([r["interval_ms"] for r in rows], 95), 2)})
    fits = {}
    for x, y in (("image_prims", "pass_ms"), ("prims", "draw_ms"), ("hd_draws", "present_ms"),
                 ("units_in_sight", "draw_ms"), ("prims", "scene_ms")):
        a, b, rr = fit([r[x] for r in drawn], [r[y] for r in drawn])
        fits[f"{y}~{x}"] = {"base_ms": round(a, 4), "us_per_item": round(b * 1000.0, 4), "r": round(rr, 3)}
    # the frames shown while a game frame ran in the same pass, against those without one
    with_game = [r["interval_ms"] for r in drawn if r["game_frames"] > 0]
    without = [r["interval_ms"] for r in drawn if r["game_frames"] == 0]
    res = {"folder": str(folder), "meta": {k: meta.get(k) for k in ("art", "view", "renderer", "units", "busiest",
                                                                   "map_px", "settings")},
           "overall": summarize_drawn(drawn), "steps": steps, "game": game_s, "by_zoom": zoom_s, "fits": fits,
           "interval_with_game_frame": {"frames": len(with_game), "p50": pct(with_game, 50), "p99": pct(with_game, 99)},
           "interval_without": {"frames": len(without), "p50": pct(without, 50), "p99": pct(without, 99)}}
    (folder / "report.json").write_text(json.dumps(res, indent=1))
    (folder / "report.md").write_text(render_md(res, json.loads((against / "report.json").read_text())
                                                if against and (against / "report.json").exists() else None))
    return res


def render_md(r: dict, base: dict | None) -> str:
    m = r["meta"]
    L = [f"# Frame report: {m.get('art', '?').upper()} art, view {m.get('view')}, {m.get('renderer')}", "",
         f"Units in the game: {m.get('units')}; the camera's start over ({(m.get('busiest') or {}).get('x')}, "
         f"{(m.get('busiest') or {}).get('y')}) with {(m.get('busiest') or {}).get('units')} units near.", ""]
    o = r["overall"]
    L += ["## Overall", "",
          f"{o['frames']} frames shown, {o['shown_per_s']} a second; between frames p50 {o['interval_ms']['p50']:.2f} ms, "
          f"p95 {o['interval_ms']['p95']:.2f}, p99 {o['interval_ms']['p99']:.2f}, worst {o['interval_ms']['max']:.2f}; "
          f"{o['over_16_7']} over 16.7 ms and {o['over_33_3']} over 33.3 ms.", ""]
    L += ["## By step", "",
          "| step | s | shown/s | p50 ms | p95 | p99 | worst | >33 ms | draw ms | pass | scene | present | gpu wait | prims | images | HD draws | units seen | game/s |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in r["steps"]:
        i = s["interval_ms"]
        L.append(f"| {s['name']} | {s['seconds']} | {s['shown_per_s']} | {i['p50']:.2f} | {i['p95']:.2f} | {i['p99']:.2f} | "
                 f"{i['max']:.1f} | {s['over_33_3']} | {s['draw_ms']['mean']:.2f} | {s['pass_ms']['mean']:.2f} | "
                 f"{s['scene_ms']['mean']:.2f} | {s['present_ms']['mean']:.2f} | {s['gpu_wait_ms']['mean']:.2f} | {s['prims']['mean']:.0f} | "
                 f"{s['image_prims']['mean']:.0f} | {s['hd_draws']['mean']:.0f} | {s['units_in_sight']['mean']:.0f} | "
                 f"{s['game_per_s']} |")
    L += ["", "## By zoom", "", "| zoom | frames | interval ms | p95 | draw | pass | scene | present | gpu wait | distorts | layer Mpx | prims | images | HD draws | units seen |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for z in r["by_zoom"]:
        L.append(f"| {z['zoom']} | {z['frames']} | {z['interval_ms']:.2f} | {z['interval_p95']:.2f} | {z['draw_ms']:.2f} | "
                 f"{z['pass_ms']:.2f} | {z['scene_ms']:.2f} | {z['present_ms']:.2f} | {z['gpu_wait_ms']:.2f} | {z['distorts']:.1f} | "
                 f"{z['layer_px'] / 1e6:.2f} | {z['prims']:.0f} | "
                 f"{z['image_prims']:.0f} | {z['hd_draws']:.0f} | {z['units_in_sight']:.0f} |")
    g = r["game"]
    L += ["", "## Game frames", "",
          f"{g['frames']} game frames, {g['units_mean']:.0f} units on average; a frame's parts: mean {g['game_ms']['mean']:.2f} ms, "
          f"p50 {g['game_ms']['p50']:.2f}, p95 {g['game_ms']['p95']:.2f}, p99 {g['game_ms']['p99']:.2f}, worst {g['game_ms']['max']:.2f}.", "",
          "| part | mean ms | worst ms |", "|---|---|---|"]
    for k in GAME_PARTS:
        L.append(f"| {k.replace('_ms', '')} | {g['parts_mean_ms'][k]:.4f} | {g['parts_max_ms'][k]:.3f} |")
    sp = g["spikes"]
    L += ["", f"Spikes (over {sp['threshold_ms']} ms): {sp['count']}, by the part that led them:", ""]
    for k, v in sp["by_part"].items():
        L.append(f"- {k.replace('_ms', '')}: {v['count']}, mean {v['mean_ms']} ms, worst {v['max_ms']} ms")
    L += ["", "Worst frames:", ""]
    for w in g["worst"]:
        L.append(f"- frame {w['frame']}: {w['game_ms']} ms, led by {w['lead'].replace('_ms', '')}")
    L += ["", "## Cost against what is drawn (least squares over every frame shown)", ""]
    for k, v in r["fits"].items():
        L.append(f"- {k}: {v['base_ms']} ms + {v['us_per_item']} us an item, r = {v['r']}")
    a, b = r["interval_with_game_frame"], r["interval_without"]
    L += ["", f"Frames shown in a pass that also ran a game frame: {a['frames']}, p50 {a['p50']:.2f} ms, p99 {a['p99']:.2f}; "
          f"without one: {b['frames']}, p50 {b['p50']:.2f} ms, p99 {b['p99']:.2f}.", ""]
    if base:
        L += ["## Against the earlier run", "", "| step | shown/s before | after | p99 before | after | draw ms before | after |",
              "|---|---|---|---|---|---|---|"]
        bs = {s["name"]: s for s in base["steps"]}
        for s in r["steps"]:
            if s["name"] in bs:
                t = bs[s["name"]]
                L.append(f"| {s['name']} | {t['shown_per_s']} | {s['shown_per_s']} | {t['interval_ms']['p99']:.2f} | "
                         f"{s['interval_ms']['p99']:.2f} | {t['draw_ms']['mean']:.2f} | {s['draw_ms']['mean']:.2f} |")
        L.append("")
    return "\n".join(L) + "\n"


# --- the command line ------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("setup")
    for name in ("bench", "all"):
        b = sub.add_parser(name)
        b.add_argument("--players", type=int, default=32)
        b.add_argument("--map", default="Big Game Hunters 4x4")
        b.add_argument("--frames", type=int, default=60000)
        b.add_argument("--name", default="")
        b.add_argument("--army", type=int, default=0, help="a stress run: units given each computer at the first frame")
        b.add_argument("--pools", type=int, default=0, help="a stress run: the pools 1.17's times this, up to 13")
        if name == "all":
            b.add_argument("--art", default="sd,hd")
            b.add_argument("--view", type=int, default=1136)
            b.add_argument("--renderer", default="vulkan")
            b.add_argument("--hold", type=float, default=6.0)
    s = sub.add_parser("sweep")
    s.add_argument("source", help="a bench folder (its run.rep) or a replay")
    s.add_argument("--art", default="sd")
    s.add_argument("--view", type=int, default=1136)
    s.add_argument("--at", type=int, default=None, help="the frame to start at (default: the bench's most units)")
    s.add_argument("--renderer", default="vulkan")
    s.add_argument("--hold", type=float, default=6.0)
    s.add_argument("--out", default=None)
    s.add_argument("--quick", action="store_true", help="holds at zoom 1, 2, 4, 8 and 16 only")
    s.add_argument("--label", default="", help="a name for the sweep's folder (sweep_<art>_<label>)")
    s.add_argument("--env", action="append", default=[], help="VAR=VALUE for the run")
    s.add_argument("--profile", action="store_true", help="HYPERION_PROFILE's sampler, a log for each hold")
    s.add_argument("--display", action="store_true", help="frames paced as a player has them: matched to the display, vertical sync on")
    s.add_argument("--zooms", default="2,4,8,16", help="the zooms held after zoom 1 (a replay goes out to its whole map)")
    h = sub.add_parser("shots")
    h.add_argument("source", help="a bench folder (its run.rep) or a replay")
    h.add_argument("--art", default="hd")
    h.add_argument("--view", type=int, default=1136)
    h.add_argument("--at", type=int, default=None)
    h.add_argument("--renderer", default="vulkan")
    h.add_argument("--zooms", default="1,2,4,8,16")
    h.add_argument("--label", default="")
    h.add_argument("--env", action="append", default=[], help="VAR=VALUE for the run")
    q = sub.add_parser("profile")
    q.add_argument("source", help="a bench folder (its run.rep) or a replay")
    q.add_argument("--start", type=int, default=None, help="default: 6,000 frames before the bench's most units")
    q.add_argument("--end", type=int, default=None, help="default: the bench's most units")
    q.add_argument("--env", action="append", default=[], help="VAR=VALUE for the run")
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    r = sub.add_parser("report")
    r.add_argument("folder")
    r.add_argument("--against", default=None)
    a = ap.parse_args()

    if a.cmd == "setup":
        setup()
        return
    if a.cmd == "report":
        res = report(Path(a.folder), Path(a.against) if a.against else None)
        print((Path(a.folder) / "report.md").read_text())
        return
    if a.cmd == "compare":
        for row in compare(Path(a.a), Path(a.b)):
            print(row)
        return
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    if a.cmd in ("bench", "all"):
        out = RUNS / f"{stamp} {a.players}p {Path(a.map).stem}{(' ' + a.name) if a.name else ''}"
        out.mkdir(parents=True, exist_ok=True)
        b = bench(a.players, a.map, a.frames, out, a.army, a.pools)
        if a.cmd == "bench":
            print(f"bench folder: {b}")
            return
        s = json.loads((b / "summary.json").read_text())
        at = max(0, int(s.get("units_most_at", 0)) - 600)
        for art in a.art.split(","):
            f = sweep(b / "run.rep", art, a.view, at, out, a.renderer, a.hold)
            report(f)
            print((f / "report.md").read_text())
        return
    if a.cmd == "profile":
        src = Path(a.source)
        rep = src / "run.rep" if src.is_dir() else src
        most = int(json.loads((src / "summary.json").read_text()).get("units_most_at", 0)) if src.is_dir() else 0
        end = a.end if a.end is not None else most
        start = a.start if a.start is not None else max(0, end - 6000)
        out = src.parent if src.is_dir() else RUNS / f"{stamp} profile"
        out.mkdir(parents=True, exist_ok=True)
        profile(rep, start, end, out, dict(e.split("=", 1) for e in a.env))
        return
    if a.cmd == "shots":
        src = Path(a.source)
        rep = src / "run.rep" if src.is_dir() else src
        at = a.at
        if at is None and src.is_dir() and (src / "summary.json").exists():
            at = max(0, int(json.loads((src / "summary.json").read_text()).get("units_most_at", 0)) - 600)
        out = src.parent if src.is_dir() else RUNS / f"{stamp} shots"
        out.mkdir(parents=True, exist_ok=True)
        env = dict(e.split("=", 1) for e in a.env)
        f = shots(rep, a.art, a.view, at, out, a.renderer, [float(z) for z in a.zooms.split(",")], a.label, env)
        print(f"shots: {f}")
        return
    if a.cmd == "sweep":
        src = Path(a.source)
        rep = src / "run.rep" if src.is_dir() else src
        at = a.at
        if at is None and src.is_dir() and (src / "summary.json").exists():
            at = max(0, int(json.loads((src / "summary.json").read_text()).get("units_most_at", 0)) - 600)
        out = Path(a.out) if a.out else (src.parent if src.is_dir() else RUNS / f"{stamp} sweep")
        out.mkdir(parents=True, exist_ok=True)
        env = dict(e.split("=", 1) for e in a.env)
        f = sweep(rep, a.art, a.view, at, out, a.renderer, a.hold, a.quick, a.label, env, a.profile,
                  [float(z) for z in a.zooms.split(",")], a.display)
        report(f)
        print((f / "report.md").read_text())


if __name__ == "__main__":
    main()
