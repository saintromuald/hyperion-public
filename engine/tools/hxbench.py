r"""hxbench.py -- Hyperion's before-and-after benchmark.

Runs Hyperion builds on replays under named configurations and records, per run, how fast the
game goes and what it takes from the processor and the graphics card, as JSON lines.

  python tools/hxbench.py fast --replays h64_bgh8x8 h64_bgh4x4 --config auto --config off:HYPERION_WORKERS=1
  python tools/hxbench.py pace --replays m1_IronManLadder_1v1_LostTemple_2003 --config rest --config spin:HYPERION_LOOP_REST=0
  python tools/hxbench.py fast --build base=testrun/bench/base --build now=src117/build/x64 ...

fast: under the replay harness (replaytest.py run), as fast as playback goes, in a background
window on the right display: the game frames a second, HYPERION_PERF's parts and checks, the
processor cores the game's process used and its share of the GPU's 3D engine while it ran flat
out (the half seconds it used most of a core, not its start-up and menus).

pace: Hyperion started on its own with --replay, at the replay's own speed, kept running
unfocused by the live channel (--control on a port of its own): over a window after a warm-up,
the processor cores and GPU share, the frames drawn and game frames a second, and how evenly
they came (Debug Tools' game clock, the live channel's "clock").

A configuration is NAME or NAME:VAR=VALUE,VAR=VALUE, its variables set for Hyperion (for
example HYPERION_WORKERS, HYPERION_LOOP_REST); --setting changes one of Settings' rows for a
pace run through the live channel and puts it back after. A build is NAME=FOLDER holding a
Hyperion.exe (with its .pdb, and its rt_observer.dll for fast runs); for its runs the exe is put
in testrun/hx64, and in src117/build/x64 with the observer, which the harness copies the exe
from and loads the observer from, and what was there is put back after. Without --build the
exe in testrun/hx64 runs as "current". GPU shares come from Windows' "GPU
Engine" counters for the process alone (pywin32's win32pdh); the whole card's power from
nvidia-smi where there is one. Results append to --out (testrun/bench/results.jsonl).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from collections import defaultdict

import psutil

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ENGINE, "testrun", "hx64")
BUILT = os.path.join(ENGINE, "src117", "build", "x64")
REPLAYS = os.path.join(ENGINE, "testrun", "rt_replays")
PERF_LOG = os.path.join(GAME, "hyperion_perf.log")
CONTROL_PORT = 27217
# PresentMon's console build, local only (tools/local is not committed): what reaches the display.
# Unelevated it traces a process given by its id, not by its name.
PRESENTMON = os.path.join(os.path.dirname(ENGINE), "tools", "local", "presentmon", "PresentMon-2.6.0-x64.exe")


def find_replay(name):
    if os.path.isabs(name) and os.path.exists(name):
        return name
    for root, _, files in os.walk(REPLAYS):
        if name + ".rep" in files:
            return os.path.join(root, name + ".rep")
    sys.exit("hxbench: no replay %s under %s" % (name, REPLAYS))


class Sampler(threading.Thread):
    """The process's processor time, and its GPU engines' use every half second."""

    def __init__(self, find_pid):
        super().__init__(daemon=True)
        self.find_pid = find_pid
        self.stop = threading.Event()
        self.gpu3d, self.gpuall, self.power = [], [], []
        self.cpu0 = self.cpu1 = None
        self.t0 = self.t1 = None
        self.pid = None
        self.samples = []  # (time, processor seconds, GPU 3D share or None, busiest engine or None)

    def run(self):
        import win32pdh
        query, handles, kinds = None, [], []
        proc = None
        while not self.stop.is_set():
            if proc is None:
                pid = self.find_pid()
                if pid:
                    try:
                        proc = psutil.Process(pid)
                        self.pid = pid
                    except psutil.Error:
                        proc = None
            if proc is not None:
                try:
                    c = proc.cpu_times()
                    now = time.perf_counter()
                    if self.cpu0 is None:
                        self.cpu0, self.t0 = c, now
                    self.cpu1, self.t1 = c, now
                    self.samples.append([now, c.user + c.system, None, None])
                except psutil.Error:
                    break
                if query is None:
                    try:
                        paths = win32pdh.ExpandCounterPath(r"\GPU Engine(pid_%d*)\Utilization Percentage" % self.pid)
                    except Exception:
                        paths = []
                    if paths:
                        try:
                            query = win32pdh.OpenQuery()
                            for path in paths:
                                handles.append(win32pdh.AddCounter(query, path))
                                kinds.append(path.split("engtype_")[-1].split(")")[0])
                            win32pdh.CollectQueryData(query)
                        except Exception:
                            query, handles, kinds = None, [], []
                else:
                    try:
                        win32pdh.CollectQueryData(query)
                    except Exception:
                        break  # the process has gone, and its counters with it
                    per = defaultdict(float)
                    for h, k in zip(handles, kinds):
                        try:
                            per[k] += win32pdh.GetFormattedCounterValue(h, win32pdh.PDH_FMT_DOUBLE)[1]
                        except Exception:
                            pass
                    self.gpu3d.append(per.get("3D", 0.0))
                    self.gpuall.append(max(per.values()) if per else 0.0)
                    if self.samples:
                        self.samples[-1][2:] = [self.gpu3d[-1], self.gpuall[-1]]
            self.stop.wait(0.5)

    def result(self, busy=False):
        """Over the whole time the process was followed, or with busy over the half seconds it
        used most of a core: a run as fast as playback goes, without its start-up and menus,
        which in a short run were most of the time."""
        r = {}
        if busy and len(self.samples) > 1:
            used = span = 0.0
            gpu3d, gpuall = [], []
            for (t0, c0, _, _), (t1, c1, g3, ga) in zip(self.samples, self.samples[1:]):
                if t1 > t0 and (c1 - c0) / (t1 - t0) >= 0.9:
                    used += c1 - c0
                    span += t1 - t0
                    if g3 is not None:
                        gpu3d.append(g3)
                        gpuall.append(ga)
            if span > 0:
                r["cpu_cores"] = round(used / span, 3)
                r["busy_s"] = round(span, 1)
            if gpu3d:
                r["gpu_3d_pct"] = round(sum(gpu3d) / len(gpu3d), 2)
                r["gpu_busiest_pct"] = round(sum(gpuall) / len(gpuall), 2)
            return r
        if self.cpu0 is not None and self.t1 > self.t0:
            used = (self.cpu1.user - self.cpu0.user) + (self.cpu1.system - self.cpu0.system)
            r["cpu_cores"] = round(used / (self.t1 - self.t0), 3)
        if self.gpu3d:
            r["gpu_3d_pct"] = round(sum(self.gpu3d) / len(self.gpu3d), 2)
            r["gpu_busiest_pct"] = round(sum(self.gpuall) / len(self.gpuall), 2)
        return r


def gpu_power():
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=power.draw", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=5).stdout
        return float(out.strip().splitlines()[0])
    except Exception:
        return None


def perf_parts():
    """HYPERION_PERF's log averaged over its lines: the parts of a game frame and a check's."""
    try:
        lines = open(PERF_LOG, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return {}
    sums, counts = defaultdict(float), defaultdict(int)
    for line in lines:
        if "per_game_frame_ms" not in line:
            continue
        head, _, rest = line.partition("per_game_frame_ms:")
        frame, _, check = rest.partition("per_check_ms:")
        for k, v in re.findall(r"([\w/]+)=([\d.]+)", head):
            sums["line." + k] += float(v)
            counts["line." + k] += 1
        for k, v in re.findall(r" (\w+)=([\d.]+)", frame):
            sums["frame." + k] += float(v)
            counts["frame." + k] += 1
        for k, v in re.findall(r" (\w)=([\d.]+)/", check) + re.findall(r" (wait)=([\d.]+)", check):
            sums["check." + k] += float(v)
            counts["check." + k] += 1
    return {k: round(sums[k] / counts[k], 3) for k in sums}


def env_for(config):
    env = dict(os.environ, HYPERION_DISPLAY="right", HYPERION_PERF="1")
    env.pop("HYPERION_DLLS", None)
    env.update(config["env"])
    return env


def hyperions_after(t):
    for p in psutil.process_iter(["name", "create_time"]):
        if (p.info["name"] or "").lower() == "hyperion.exe" and p.info["create_time"] >= t - 1:
            try:
                if os.path.normcase(p.exe()).startswith(os.path.normcase(GAME)):
                    return p.pid
            except psutil.Error:
                pass
    return None


def run_fast(replay, config, renderer):
    path = find_replay(replay)
    env = env_for(config)
    start = time.time()
    sampler = Sampler(lambda: hyperions_after(start))
    sampler.start()
    p0 = gpu_power()
    r = subprocess.run([sys.executable, os.path.join(ENGINE, "tools", "replaytest", "replaytest.py"), "run",
                        "--build", "hx64", "--replay", path, "--renderer", renderer, "--scene", "gpu",
                        "--window", "background", "--tag", "bench_" + config["name"],
                        "--data", "mpq"],  # one run, on the files results.jsonl was measured on
                       capture_output=True, text=True, timeout=3600, cwd=ENGINE, env=env)
    sampler.stop.set()
    sampler.join(5)
    m = re.search(r"frames=(\d+)/\d+ wall=([\d.]+)s sim=(\d+) fps", r.stdout)
    out = {"game_fps": int(m.group(3)) if m else None, "frames": int(m.group(1)) if m else None,
           "wall_s": float(m.group(2)) if m else None}
    if not m:
        out["error"] = (r.stdout + r.stderr)[-600:]
    out.update(sampler.result(busy=True))
    out["perf"] = perf_parts()
    out["gpu_power_w_before"] = p0
    return out


def presentmon_summary(path):
    """The display's side of a PresentMon capture: how far apart frames reached the screen, how
    long after the game handed them over, and how they were shown."""
    import csv
    import statistics
    with open(path, encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    def nums(k):
        out = []
        for r in rows:
            try:
                out.append(float(r[k]))
            except (KeyError, TypeError, ValueError):
                pass
        return out
    shown = nums("MsBetweenDisplayChange")
    until = nums("MsUntilDisplayed")
    modes = defaultdict(int)
    for r in rows:
        modes[r.get("PresentMode", "?")] += 1
    out = {"pm_presents": len(rows), "pm_mode": max(modes, key=modes.get) if modes else None}
    if shown:
        s = sorted(shown)
        out.update(pm_displayed_per_s=round(1000.0 / statistics.mean(shown), 2),
                   pm_display_ms_sd=round(statistics.pstdev(shown), 3), pm_display_ms_p99=round(s[int(len(s) * 0.99) - 1], 3))
    if until:
        out["pm_until_displayed_ms"] = round(statistics.mean(until), 2)
    return out


def run_pace(replay, config, renderer, seconds, extra, settings=(), camera=None, presentmon=None):
    sys.path.insert(0, os.path.join(ENGINE, "tools"))
    from hxlive import ws_call
    path = find_replay(replay)
    env = env_for(config)
    env.update(HYPERION_WINDOW="background", HYPERION_RENDERER=renderer, HYPERION_GPU_SCENE="gpu")
    p = subprocess.Popen([os.path.join(GAME, "Hyperion.exe"), "--data=" + GAME, "--control=%d" % CONTROL_PORT] +
                         extra + ["--replay", path], cwd=GAME, env=env)
    out = {}
    put_back = []  # the settings changed for the run, with the values they had: a change is saved
    try:
        time.sleep(6.0)
        try:
            ws_call(CONTROL_PORT, {"id": 1, "cmd": "clock", "on": True})
        except Exception as e:
            out["error"] = "clock: %s" % e
        for s in settings:
            name, _, value = s.rpartition("=")
            try:
                rows = ws_call(CONTROL_PORT, {"id": 3, "cmd": "settings"}).get("settings", [])
                old = next((r["value"] for r in rows if r.get("name") == name), None)
                ws_call(CONTROL_PORT, {"id": 4, "cmd": "setting", "name": name, "value": float(value)})
                if old is not None:
                    put_back.append((name, old))
            except Exception as e:
                out["error"] = "setting %s: %s" % (s, e)
        if camera:
            # the replay's camera moved and zoomed; the zoom eases in over a second or so
            x, y, zoom = (float(v) for v in camera.split(","))
            try:
                ws_call(CONTROL_PORT, {"id": 7, "cmd": "camera", "x": x, "y": y, "zoom": zoom})
            except Exception as e:
                out["error"] = "camera: %s" % e
        if settings or camera:
            time.sleep(2.0)
            ws_call(CONTROL_PORT, {"id": 5, "cmd": "clock", "on": True})  # the clock from the new setting on
        sampler = Sampler(lambda: p.pid)
        sampler.start()
        pm = None
        if presentmon:
            pm = subprocess.Popen([PRESENTMON, "--process_id", str(p.pid), "--output_file", presentmon, "--timed",
                                   str(int(seconds)), "--terminate_after_timed", "--stop_existing_session",
                                   "--no_console_stats"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        powers = []
        end = time.time() + seconds
        while time.time() < end:
            w = gpu_power()
            if w is not None:
                powers.append(w)
            time.sleep(1.0)
        sampler.stop.set()
        sampler.join(5)
        if pm is not None:
            try:
                pm.wait(30)
                out.update(presentmon_summary(presentmon))
                out["pm_csv"] = os.path.relpath(presentmon, ENGINE)
            except Exception as e:
                out["error"] = "presentmon: %s" % e
                pm.kill()
        out.update(sampler.result())
        if powers:
            out["gpu_power_w"] = round(sum(powers) / len(powers), 1)
        try:
            c = ws_call(CONTROL_PORT, {"id": 2, "cmd": "clock"})
            g, s = c.get("game_ms", {}), c.get("shown_ms", {})
            out.update(game_per_s=round(1000.0 / g["mean"], 2) if g.get("mean") else None, game_ms_sd=g.get("sd"),
                       game_within_2ms_pct=c.get("within_2ms_pct"), drift_ms=c.get("drift_ms"),
                       shown_per_s=round(1000.0 / s["mean"], 2) if s.get("mean") else None, shown_ms_sd=s.get("sd"),
                       shown_ms_p1=s.get("p1"), shown_ms_p99=s.get("p99"))
        except Exception as e:
            out["error"] = "clock: %s" % e
    finally:
        for name, old in put_back:
            try:
                ws_call(CONTROL_PORT, {"id": 6, "cmd": "setting", "name": name, "value": float(old)})
            except Exception as e:
                out["error"] = "putting back %s: %s" % (name, e)
        p.kill()
        p.wait(10)
    assert not psutil.pid_exists(p.pid) or psutil.Process(p.pid).status() == psutil.STATUS_ZOMBIE
    out["perf"] = perf_parts()
    return out


def place_build(folder):
    """Its exe and pdb in testrun/hx64, and with its observer in src117/build/x64 too, where
    the harness takes the exe from before each run and loads the observer; returns what was
    there, to put back."""
    kept = {}
    for name, places in (("Hyperion.exe", (GAME, BUILT)), ("Hyperion.pdb", (GAME, BUILT)),
                         ("rt_observer.dll", (BUILT,))):
        src = os.path.join(ENGINE, folder, name) if not os.path.isabs(folder) else os.path.join(folder, name)
        if not os.path.exists(src):
            continue
        for place in places:
            dst = os.path.join(place, name)
            keep = dst + ".hxbench-kept"
            if os.path.exists(dst) and not os.path.exists(keep):
                shutil.copy2(dst, keep)
                kept[dst] = keep
            shutil.copy2(src, dst)
    return kept


def restore(kept):
    for dst, keep in kept.items():
        shutil.copy2(keep, dst)
        os.remove(keep)


def parse_config(s):
    name, _, rest = s.partition(":")
    env = {}
    for kv in filter(None, rest.split(",")):
        k, _, v = kv.partition("=")
        env[k] = v
    return {"name": name, "env": env}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["fast", "pace"])
    ap.add_argument("--replays", nargs="+", required=True)
    ap.add_argument("--config", action="append", default=[], help="NAME or NAME:VAR=VALUE,...")
    ap.add_argument("--build", action="append", default=[], help="NAME=FOLDER holding Hyperion.exe")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--seconds", type=float, default=20.0, help="pace: the measured window")
    ap.add_argument("--renderer", default="vulkan", help="the GPU presenter's driver, Hyperion's own default")
    ap.add_argument("--arg", action="append", default=[], help="pace: another argument for Hyperion (--view=5120:2160)")
    ap.add_argument("--setting", action="append", default=[],
                    help="pace: a setting changed for the run through the live channel and put back after, by "
                         "its label and value as the channel's settings list has them (\"Frame rate=5\", no limit)")
    ap.add_argument("--camera", default=None,
                    help="pace: X,Y,ZOOM, the replay's camera centred on map pixel X,Y and zoomed out to ZOOM "
                         "(1 the game's own scale) after the warm-up")
    ap.add_argument("--presentmon", action="store_true",
                    help="pace: PresentMon traces the measured window, what reached the display; each run's CSV in "
                         "testrun/bench/presentmon")
    ap.add_argument("--tag", default="", help="a label kept with every result")
    ap.add_argument("--out", default=os.path.join(ENGINE, "testrun", "bench", "results.jsonl"))
    args = ap.parse_args()
    configs = [parse_config(c) for c in args.config] or [{"name": "default", "env": {}}]
    builds = [b.partition("=")[::2] for b in args.build] or [("current", None)]
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    for build, folder in builds:
        kept = place_build(folder) if folder else {}
        try:
            for rep in range(args.repeat):
                for replay in args.replays:
                    for config in configs:
                        t = time.strftime("%Y-%m-%d %H:%M:%S")
                        if args.mode == "fast":
                            res = run_fast(replay, config, args.renderer)
                        else:
                            pm = None
                            if args.presentmon:
                                pm = os.path.join(ENGINE, "testrun", "bench", "presentmon", "%s_%s_%s_%s_%d_%s.csv" % (
                                    args.tag or "untagged", build, config["name"], replay, rep, time.strftime("%H%M%S")))
                                os.makedirs(os.path.dirname(pm), exist_ok=True)
                            res = run_pace(replay, config, args.renderer, args.seconds, args.arg, args.setting,
                                           args.camera, pm)
                        res.update(mode=args.mode, build=build, config=config["name"], env=config["env"], replay=replay,
                                   repeat=rep, time=t, tag=args.tag, args=args.arg, settings=args.setting,
                                   camera=args.camera)
                        with open(args.out, "a", encoding="utf-8") as f:
                            f.write(json.dumps(res) + "\n")
                        keys = ("game_fps", "game_per_s", "shown_per_s", "cpu_cores", "gpu_3d_pct", "game_ms_sd",
                                "shown_ms_sd", "pm_displayed_per_s", "pm_display_ms_sd", "pm_until_displayed_ms")
                        print("%-5s %-8s %-10s %-38s %s" % (args.mode, build, config["name"], replay,
                              " ".join("%s=%s" % (k, res[k]) for k in keys if res.get(k) is not None)), flush=True)
                        if res.get("error"):
                            print("      error:", res["error"][:300], flush=True)
        finally:
            restore(kept)


if __name__ == "__main__":
    main()
