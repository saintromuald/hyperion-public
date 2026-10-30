"""The AI lab's checks (docs/modules/lab.md).

    python tools/labcheck.py worker [--folder <game folder>] [--map <map>] [--players 2]
                                    [--levels 7,5] [--seed 7] [--frames 20000] [--keep]

worker: this script plays the lab's part. It listens on this machine, starts one worker
(Hyperion.exe --lab-worker) on a game of computers, and checks what it says: HELLO with its
token, GAME_START, samples of its units, its status, GAME_END. Then the game's folder: its
replay, settings.json and summary.json, whose end state must be the one GAME_END gave; and the
replay played again in a fresh process (--bench-replay) must end in the same state. It reports
the worker's start-up and end, its game frames a second and its memory.

    python tools/labcheck.py farm [--folder <game folder>] [--map <map>] [--frames 20000]
                                  [--games 8] [--port 27166] [--keep]

farm: the lab itself (--mod lab, its screen open, its live channel on --port) with a project of
its own (testrun\\lab_check\\lab\\lab\\projects\\Check): lab_start plays its games four at once;
each must end at its length or with one player left, with its folder and its line in
games.jsonl, the summary's end state the line's. At x16 each game must run at about 381 game
frames a second, paused hold still and resumed go on. Then lab_stop must end every worker, and
the lab killed (itself alone, not its tree) must take its workers with it.

    python tools/labcheck.py soak [--folder <game folder>] [--map <map>] [--games 20]
                                  [--frames 6000] [--check <n>] [--keep]

soak: one worker plays game after game in its one process, as the farm's do. This script plays
the lab's part: it starts a worker without --bench, gives it --games games (JOB) one after
another, the levels and seeds varying, and reads each one's end; then each of the first --check
of them (all, unless it says) is played again in a fresh process and must end in the same state.
It reports each game's time from its JOB to its end, and the worker's memory after each game.

    python tools/labcheck.py rate [--folder <game folder>] [--map <map>] [--at-once 1,2,4,8,12,16,24]
                                  [--warm 10] [--seconds 60] [--port 27167] [--keep]

rate: games an hour against games at once. The lab, its wall shown, plays the first project's
games (the map, Very Hard and Elite against each other, Free For All, an hour of game at the
most) at the most pace, at each --at-once in turn: --warm seconds to fill its workers, then the
games that end in the next --seconds, counted. It reports games an hour, hours of game an hour,
the games' length, how they ended, and the workers' memory at the end of each count.

The build is copied into the game folder (testrun\\front_w2 unless --folder names another),
and the worker's data folder and the game's folder are under testrun\\lab_check\\, made fresh.
Every process is stopped by its own id, never by name.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import time

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ENGINE, "src117", "build", "x64")
RUNS = os.path.join(ENGINE, "testrun", "lab_check")

# port_labproto.h
HELLO, GAME_START, SAMPLE, STATUS, HASH, GAME_END, LOG = 1, 2, 3, 4, 5, 6, 7
WELCOME, JOB, PACE, SAMPLING, WATCHED, STOP = 0x20, 0x21, 0x22, 0x23, 0x24, 0x25
PROTOCOL = 1


class Reader:
    def __init__(self, data):
        self.d, self.at = data, 0

    def take(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.at)
        self.at += struct.calcsize("<" + fmt)
        return v[0] if len(v) == 1 else v

    def s(self):
        end = self.d.index(b"\0", self.at)
        v = self.d[self.at:end].decode("latin-1")
        self.at = end + 1
        return v


class Link:
    """One worker's connection: LanNet's frames, a 32-bit length (the kind's byte and the rest)."""

    def __init__(self, sock):
        self.sock, self.buf = sock, b""
        sock.setblocking(False)

    def send(self, kind, payload=b""):
        self.sock.sendall(struct.pack("<IB", len(payload) + 1, kind) + payload)

    def frames(self):
        try:
            got = self.sock.recv(1 << 20)
            if got == b"":
                return None
            self.buf += got
        except BlockingIOError:
            pass
        except (ConnectionResetError, ConnectionAbortedError):
            return None
        out = []
        while len(self.buf) >= 5:
            n = struct.unpack_from("<I", self.buf)[0]
            if len(self.buf) < 4 + n:
                break
            out.append((self.buf[4], self.buf[5:4 + n]))
            self.buf = self.buf[4 + n:]
        return out


def decode(kind, data):
    r = Reader(data)
    if kind == HELLO:
        return {"version": r.take("H"), "worker": r.take("I"), "token": r.s(), "pid": r.take("I"), "build": r.s()}
    if kind == GAME_START:
        m = {"game": r.take("I"), "map": r.s(), "seed": r.take("I"), "frames": r.take("I"), "speed": r.take("B"),
             "mul": r.take("B"), "w": r.take("H"), "h": r.take("H")}
        m["players"] = [{"slot": r.take("B"), "race": r.take("B"), "team": r.take("B"), "level": r.take("B"),
                         "name": r.s()} for _ in range(r.take("B"))]
        return m
    if kind == SAMPLE:
        m = {"game": r.take("I"), "frame": r.take("I")}
        m["players"] = [dict(zip(("slot", "minerals", "gas", "supply", "supply_most", "units", "out"),
                                 r.take("BIIHHHB"))) for _ in range(r.take("B"))]
        m["units"] = [r.take("IHHBB") for _ in range(r.take("H"))]
        return m
    if kind == STATUS:
        return dict(zip(("game", "frame", "fps10", "units"), r.take("IIII")))
    if kind == HASH:
        g, f, lo, hi = r.take("IIII")
        return {"game": g, "frame": f, "state": "%016X" % (hi << 32 | lo)}
    if kind == GAME_END:
        m = {"game": r.take("I"), "reason": r.s(), "frame": r.take("I")}
        lo, hi = r.take("II")
        m["state"] = "%016X" % (hi << 32 | lo)
        m["seconds"] = r.take("I") / 10.0
        m["fps"] = r.take("I") / 10.0
        m["winners"] = [r.take("B") for _ in range(r.take("B"))]
        m["players"] = [dict(zip(("slot", "units", "minerals", "gas"), r.take("BIII"))) for _ in range(r.take("B"))]
        return m
    if kind == LOG:
        return {"text": r.s()}
    return {"bytes": len(data)}


class Memory:
    """A process's largest working set and private bytes, read while it runs."""

    class Counters(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]

    def __init__(self, pid):
        # PROCESS_QUERY_INFORMATION alone: once 1.17's InitACL has run, a Hyperion process lets
        # its user have only that, SYNCHRONIZE, PROCESS_TERMINATE and two more (main.cpp)
        self.h = ctypes.windll.kernel32.OpenProcess(0x0400, False, pid)
        self.peak_ws = self.peak_private = 0

    def read(self):
        if not self.h:
            return
        c = Memory.Counters()
        c.cb = ctypes.sizeof(c)
        if ctypes.windll.psapi.GetProcessMemoryInfo(self.h, ctypes.byref(c), c.cb):
            self.peak_ws = max(self.peak_ws, c.PeakWorkingSetSize)
            self.peak_private = max(self.peak_private, c.PrivateUsage)
            self.ws, self.private = c.WorkingSetSize, c.PrivateUsage


def stop(p):
    if p.poll() is None:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)


def copy_build(folder):
    for f in ("Hyperion.exe", "Hyperion.pdb"):
        shutil.copy2(os.path.join(BUILD, f), os.path.join(folder, f))


def fresh(path):
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path)
    return path


def worker(a):
    folder = os.path.abspath(a.folder)
    copy_build(folder)
    data = fresh(os.path.join(RUNS, "w01"))
    out = os.path.join(fresh(os.path.join(RUNS, "games")), "000001")
    os.makedirs(out)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    port = listener.getsockname()[1]
    token = secrets.token_hex(8)
    cmd = [os.path.join(folder, "Hyperion.exe"), "--data", data, "--bench", "--map", a.map,
           "--players", str(a.players), "--ai-levels", a.levels, "--seed", str(a.seed),
           "--bench-frames", str(a.frames), "--bench-speed", "max", "--bench-shown", "none",
           "--lab-worker", "%d:%s:1" % (port, token), "--lab-game", "1", "--lab-out", out]
    env = dict(os.environ)
    for k in ("HYPERION_FRONT_OPEN", "HYPERION_FRONT_SNAP", "HYPERION_DLLS"):
        env.pop(k, None)
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=folder, env=env)
    mem = Memory(p.pid)
    got = {k: [] for k in (HELLO, GAME_START, SAMPLE, STATUS, HASH, GAME_END, LOG)}
    times = {}
    problems = []
    link = None
    try:
        listener.settimeout(30)
        conn, _ = listener.accept()
        times["connected"] = time.time() - t0
        link = Link(conn)
        while time.time() - t0 < a.timeout:
            mem.read()
            frames = link.frames()
            if frames is None:
                break
            for kind, data in frames:
                m = decode(kind, data)
                got.setdefault(kind, []).append(m)
                if kind == HELLO:
                    if m["token"] != token:
                        problems.append("HELLO's token is %r, not ours" % m["token"])
                    link.send(WELCOME, struct.pack("<I", m["worker"]))
                    link.send(SAMPLING, struct.pack("<HH", 30, 6000))
                elif kind == GAME_START:
                    times["game_start"] = time.time() - t0
                elif kind == GAME_END:
                    times["game_end"] = time.time() - t0
            if got[GAME_END]:
                link.sock.close()  # the lab has read the end, and says so by closing
                break
            if p.poll() is not None and not frames:
                break
            time.sleep(0.005)
        p.wait(timeout=30)
        times["exited"] = time.time() - t0
    finally:
        stop(p)
        listener.close()

    hello, start, samples, end = got[HELLO], got[GAME_START], got[SAMPLE], got[GAME_END]
    if len(hello) != 1:
        problems.append("%d HELLOs" % len(hello))
    if len(start) != 1:
        problems.append("%d GAME_STARTs" % len(start))
    if len(end) != 1:
        problems.append("%d GAME_ENDs" % len(end))
    if len(samples) < 10:
        problems.append("only %d samples" % len(samples))
    if samples and max(len(s["units"]) for s in samples) == 0:
        problems.append("no units in any sample")
    if samples and any(t["frame"] < s["frame"] for s, t in zip(samples, samples[1:])):
        problems.append("samples' frames go back")
    files = {f: os.path.isfile(os.path.join(out, f)) for f in ("run.rep", "settings.json", "summary.json")}
    for f, ok in files.items():
        if not ok:
            problems.append("no %s in the game's folder" % f)
    summary = {}
    if files["summary.json"]:
        summary = json.load(open(os.path.join(out, "summary.json"), encoding="utf-8"))
        if end and summary.get("end_state") != end[0]["state"]:
            problems.append("summary's end state %s, GAME_END's %s" % (summary.get("end_state"), end[0]["state"]))

    # the replay played again in a fresh process ends in the same state
    again = None
    if files["run.rep"] and end:
        copy = os.path.join(RUNS, "games", "000001-again")
        shutil.copytree(out, copy)
        os.remove(os.path.join(copy, "summary.json"))
        data2 = fresh(os.path.join(RUNS, "w02"))
        cmd2 = [os.path.join(folder, "Hyperion.exe"), "--data", data2, "--replay", os.path.join(copy, "run.rep"),
                "--bench-replay", copy, "--bench-frames", str(a.frames), "--bench-speed", "max",
                "--bench-shown", "none"]
        env2 = dict(env, HYPERION_WINDOW="hidden", SDL_AUDIO_DRIVER="dummy", HYPERION_RENDERER="sdl")
        p2 = subprocess.Popen(cmd2, cwd=folder, env=env2)
        try:
            p2.wait(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            problems.append("the replay played again did not end")
        finally:
            stop(p2)
        s2 = os.path.join(copy, "summary.json")
        if os.path.isfile(s2):
            again = json.load(open(s2, encoding="utf-8"))
            if again.get("end_state") != end[0]["state"]:
                problems.append("played again it ends in %s, the worker's in %s" % (again.get("end_state"),
                                                                                  end[0]["state"]))
        else:
            problems.append("the replay played again wrote no summary")

    print("worker: game of %d players on %s, seed %d" % (a.players, os.path.basename(a.map), a.seed))
    if start:
        s = start[0]
        print("  started: %dx%d tiles, %s" % (s["w"], s["h"], ", ".join(
            "slot %d race %d level %d" % (q["slot"], q["race"], q["level"]) for q in s["players"])))
    if end:
        e = end[0]
        print("  ended: %s at frame %d, %.1f s, %.0f game frames a second, winners %s, state %s" % (
            e["reason"], e["frame"], e["seconds"], e["fps"], e["winners"] or "none", e["state"]))
        print("  players: %s" % ", ".join("slot %d %d units %d minerals" % (q["slot"], q["units"], q["minerals"])
                                           for q in e["players"]))
    if samples:
        print("  samples: %d, units in one up to %d, %d status messages" % (
            len(samples), max(len(s["units"]) for s in samples), len(got[STATUS])))
    print("  times: connected %.2f s, game start %.2f s, game end %.2f s, exited %.2f s" % tuple(
        times.get(k, -1) for k in ("connected", "game_start", "game_end", "exited")))
    print("  memory: working set at most %.0f MB, private bytes %.0f MB" % (mem.peak_ws / 1e6, mem.peak_private / 1e6))
    if again:
        print("  played again: ended %s at frame %s, state %s" % (again.get("ended"), again.get("end_frame"),
                                                                  again.get("end_state")))
    if not a.keep:
        shutil.rmtree(RUNS, ignore_errors=True)
    for q in problems:
        print("PROBLEM:", q)
    print("worker: %s" % ("passed" if not problems else "FAILED"))
    return 0 if not problems else 1


def cstr(s):
    return s.encode("utf-8") + b"\0"


def play_again(folder, out, frames, timeout, env):
    """A game's replay played in a fresh process: the end state its summary gives, or None."""
    copy = out + "-again"
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(out, copy)
    if os.path.isfile(os.path.join(copy, "summary.json")):
        os.remove(os.path.join(copy, "summary.json"))
    data = fresh(os.path.join(RUNS, "again"))
    cmd = [os.path.join(folder, "Hyperion.exe"), "--data", data, "--replay", os.path.join(copy, "run.rep"),
           "--bench-replay", copy, "--bench-frames", str(frames), "--bench-speed", "max", "--bench-shown", "none"]
    p = subprocess.Popen(cmd, cwd=folder, env=dict(env, HYPERION_WINDOW="hidden", SDL_AUDIO_DRIVER="dummy",
                                                   HYPERION_RENDERER="sdl"))
    try:
        p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    finally:
        stop(p)
    s = os.path.join(copy, "summary.json")
    return json.load(open(s, encoding="utf-8")).get("end_state") if os.path.isfile(s) else None


def soak(a):
    folder = os.path.abspath(a.folder)
    copy_build(folder)
    data = fresh(os.path.join(RUNS, "w01"))
    games = fresh(os.path.join(RUNS, "games"))
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    port = listener.getsockname()[1]
    token = secrets.token_hex(8)
    env = dict(os.environ)
    for k in ("HYPERION_FRONT_OPEN", "HYPERION_FRONT_SNAP", "HYPERION_DLLS"):
        env.pop(k, None)
    levels = ["7,7", "7,5", "6,6", "7,1", "5,5", "7,6"]
    t0 = time.time()
    p = subprocess.Popen([os.path.join(folder, "Hyperion.exe"), "--data", data, "--lab-worker", "%d:%s:1" % (port, token)],
                         cwd=folder, env=env)
    mem = Memory(p.pid)
    played = []  # each game: number, levels, reason, end frame, state, seconds from its JOB, memory
    problems = []
    given, t_job, t_start, starts = 0, 0.0, 0.0, 0

    def give(link):
        nonlocal given, t_job, t_start
        given += 1
        out = os.path.join(games, "%06d" % given)
        os.makedirs(out)
        link.send(JOB, struct.pack("<I", given) + cstr(a.map) + struct.pack("<B", 2) +
                  cstr(levels[(given - 1) % len(levels)]) + cstr("") + cstr("ffa") +
                  struct.pack("<II", a.seed + given, a.frames) + cstr(out))
        t_job = time.time()

    try:
        listener.settimeout(30)
        conn, _ = listener.accept()
        link = Link(conn)
        while time.time() - t0 < a.timeout:
            mem.read()
            frames = link.frames()
            if frames is None:
                break
            for kind, body in frames:
                m = decode(kind, body)
                if kind == HELLO:
                    if m["token"] != token:
                        problems.append("HELLO's token is %r, not ours" % m["token"])
                    link.send(WELCOME, struct.pack("<I", m["worker"]))
                    link.send(SAMPLING, struct.pack("<HH", 0, 6000))
                    give(link)
                elif kind == GAME_START:
                    starts += 1
                    t_start = time.time()
                    if m["game"] != given:
                        problems.append("GAME_START for game %d, given %d" % (m["game"], given))
                elif kind == LOG:
                    print("  worker says:", m["text"])
                elif kind == GAME_END:
                    mem.read()
                    played.append({"game": m["game"], "levels": levels[(m["game"] - 1) % len(levels)],
                                   "reason": m["reason"], "frame": m["frame"], "state": m["state"],
                                   "seconds": time.time() - t_job, "to_start": t_start - t_job,
                                   "ws": mem.ws, "private": mem.private})
                    if given < a.games:
                        give(link)
                    else:
                        link.send(STOP, struct.pack("<B", 1))
            if p.poll() is not None and not frames:
                break
            time.sleep(0.002)
        try:
            p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            problems.append("the worker did not end after STOP")
    finally:
        stop(p)
        listener.close()
    took = time.time() - t0
    if len(played) != a.games:
        problems.append("%d games ended of %d given" % (len(played), a.games))
    if starts != len(played):
        problems.append("%d GAME_STARTs, %d GAME_ENDs" % (starts, len(played)))

    # each played again in a fresh process
    checked = 0
    for g in played[:a.check if a.check else len(played)]:
        out = os.path.join(games, "%06d" % g["game"])
        again = play_again(folder, out, a.frames, a.timeout, env)
        checked += 1
        g["again"] = again
        if again != g["state"]:
            problems.append("game %d: played again it ends in %s, the worker's in %s" % (g["game"], again, g["state"]))
        if not a.keep:
            shutil.rmtree(out + "-again", ignore_errors=True)

    print("soak: %d games of %d frames on %s in one worker, %.0f s" % (len(played), a.frames, os.path.basename(a.map), took))
    for g in played if len(played) <= 30 else played[:10] + played[-10:]:
        print("  game %4d  levels %s  %-8s at %6d  %s  %5.2f s  working set %4.0f MB  private %4.0f MB%s" % (
            g["game"], g["levels"], g["reason"], g["frame"], g["state"], g["seconds"], g["ws"] / 1e6, g["private"] / 1e6,
            "" if "again" not in g else ("  again: same" if g["again"] == g["state"] else "  again: %s" % g["again"])))
    if played:
        secs = sorted(g["seconds"] for g in played[1:]) or [played[0]["seconds"]]
        starts_s = sorted(g["to_start"] for g in played[1:]) or [played[0]["to_start"]]
        print("  a game from its JOB to its end: first %.2f s, then %.2f s at the median, %.2f s of it to its "
              "first frame" % (played[0]["seconds"], secs[len(secs) // 2], starts_s[len(starts_s) // 2]))
        early = played[min(9, len(played) - 1)]
        print("  memory: private %.0f MB after game %d, %.0f MB after game %d; working set at most %.0f MB" % (
            early["private"] / 1e6, early["game"], played[-1]["private"] / 1e6, played[-1]["game"], mem.peak_ws / 1e6))
        print("  played again in a fresh process: %d of %d the same" % (
            sum(1 for g in played if g.get("again") == g["state"]), checked))
    if not a.keep:
        shutil.rmtree(RUNS, ignore_errors=True)
    for q in problems:
        print("PROBLEM:", q)
    print("soak: %s" % ("passed" if not problems else "FAILED"))
    return 0 if not problems else 1


def alive(pid):
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h = k.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE, which InitACL leaves its user (Memory)
    if not h:
        return ctypes.get_last_error() == 5  # there, but not ours to open: taken as alive
    try:
        return k.WaitForSingleObject(h, 0) == 0x102  # WAIT_TIMEOUT
    finally:
        k.CloseHandle(h)


def farm(a):
    """The lab's farm: a project's games played by workers the lab owns, and the lab's ends."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hxlive

    folder = os.path.abspath(a.folder)
    copy_build(folder)
    data = fresh(os.path.join(RUNS, "lab"))
    shutil.copytree(os.path.join(ENGINE, "mods", "lab"), os.path.join(data, "mods", "lab"))
    project = os.path.join(data, "lab", "projects", "Check")
    os.makedirs(project)
    with open(os.path.join(project, "project.json"), "w", encoding="utf-8") as f:
        json.dump({"name": "Check", "maps": [a.map], "players": 2, "levels": ["7,1", "7,7", "6,5", "5,5"],
                   "type": "ffa", "first_seed": 1, "frames": a.frames}, f, indent=2)

    def call(msg, timeout=15):
        try:
            r = hxlive.ws_call(a.port, msg, timeout=timeout)
            return r if isinstance(r, dict) else json.loads(r)
        except Exception as e:
            return {"ok": False, "error": str(e)}

    env = dict(os.environ, HYPERION_DISPLAY="right", HYPERION_WINDOW="background", HYPERION_FRONT_OPEN="lab")
    env.pop("HYPERION_DLLS", None)
    cmd = [os.path.join(folder, "Hyperion.exe"), "--data", data, "--control=%d" % a.port, "--mod", "lab"]
    problems = []
    lab = subprocess.Popen(cmd, cwd=folder, env=env)
    pids = set()

    def wait_status(until, seconds):
        t0 = time.time()
        while time.time() - t0 < seconds:
            s = call({"id": 1, "cmd": "lab_status"})
            if s.get("ok"):
                for w in s.get("workers", []):
                    if w.get("pid"):
                        pids.add(w["pid"])
                if until(s):
                    return s
            time.sleep(0.25)
        return None

    try:
        if not wait_status(lambda s: True, 60):
            problems.append("the lab's channel did not answer")
            raise RuntimeError
        # 1. eight games, four at once
        t0 = time.time()
        r = call({"id": 2, "cmd": "lab_start", "project": "Check", "at_once": 4, "games": a.games})
        if not r.get("ok"):
            problems.append("lab_start: %s" % r.get("error"))
            raise RuntimeError
        most = [0]

        def done(s):
            most[0] = max(most[0], len(s.get("workers", [])))
            return not s["running"] and not s["workers"]
        s = wait_status(done, a.timeout)
        took = time.time() - t0
        if s is None:
            problems.append("the %d games did not end in %d s" % (a.games, a.timeout))
        snap = call({"id": 3, "cmd": "snapshot", "name": "lab_farm"}, timeout=20)
        g = call({"id": 4, "cmd": "lab_games"})
        games = g.get("games", [])
        if len(games) != a.games:
            problems.append("%d games in the index, not %d" % (len(games), a.games))
        ended = {}
        for line in games:
            ended[line["ended"]] = ended.get(line["ended"], 0) + 1
            if line["ended"] not in ("finished", "last"):
                problems.append("game %d ended %s: %s" % (line["game"], line["ended"], line.get("why", "")))
                continue
            gf = os.path.join(project, "games", "%06d" % line["game"])
            sm = os.path.join(gf, "summary.json")
            if not os.path.isfile(os.path.join(gf, "run.rep")) or not os.path.isfile(sm):
                problems.append("game %d's folder lacks run.rep or summary.json" % line["game"])
                continue
            if json.load(open(sm, encoding="utf-8")).get("end_state") != line["state"]:
                problems.append("game %d: its summary's end state is not the index's" % line["game"])
        print("farm: %d games, %d at once at the most, in %.1f s (%s)" % (
            len(games), most[0], took, ", ".join("%d %s" % (n, k) for k, n in sorted(ended.items()))))
        for line in games:
            print("  game %d: %s, levels %s, seed %d, %s at %s in %.1f s, winners %s" % (
                line["game"], line["map"], line["levels"], line["seed"], line["ended"], line.get("end_frame"),
                line.get("seconds", 0), line.get("winners")))
        if snap.get("path"):
            print("  screen: %s" % snap["path"])
        if most[0] > 4:
            problems.append("%d workers at once, asked for 4" % most[0])

        # 2. the pace: x16 holds each game to 16 times Fastest; paused, it holds still
        pids.clear()
        call({"id": 5, "cmd": "lab_pace", "multiple": 16, "paused": False})
        call({"id": 5, "cmd": "lab_start", "project": "Check", "at_once": 4})
        if not wait_status(lambda s: len([w for w in s["workers"] if w["state"] == "playing"]) >= 4, 30):
            problems.append("four workers did not start again")
        time.sleep(2.0)
        s1 = call({"id": 5, "cmd": "lab_status"})
        fps = [w["game_fps"] for w in s1.get("workers", []) if w["state"] == "playing"]
        if not fps or any(abs(f - 381) > 20 for f in fps):
            problems.append("at x16 the workers ran at %s game frames a second, not about 381" % fps)
        call({"id": 5, "cmd": "lab_pace", "paused": True})
        time.sleep(1.0)
        f1 = {w["worker"]: w["frame"] for w in call({"id": 5, "cmd": "lab_status"}).get("workers", [])}
        time.sleep(1.5)
        f2 = {w["worker"]: w["frame"] for w in call({"id": 5, "cmd": "lab_status"}).get("workers", [])}
        call({"id": 5, "cmd": "lab_pace", "paused": False})
        time.sleep(1.5)
        f3 = {w["worker"]: w["frame"] for w in call({"id": 5, "cmd": "lab_status"}).get("workers", [])}
        if f1 != f2:
            problems.append("paused, the games went on: %s then %s" % (f1, f2))
        if not f3 or any(f3.get(k, 0) <= v for k, v in f2.items()):
            problems.append("resumed, the games did not go on: %s then %s" % (f2, f3))
        print("  pace: x16 at %s game frames a second; paused held %d games still, resumed they went on" % (
            fps, len(f2)))
        call({"id": 5, "cmd": "lab_pace", "multiple": 0, "paused": False})

        # 3. Stop ends every worker
        r = call({"id": 6, "cmd": "lab_stop"})
        time.sleep(0.5)
        left = [p for p in pids if alive(p)]
        if left or r.get("workers"):
            problems.append("after lab_stop, workers %s still run" % left)
        print("  stop: %d workers ended" % len(pids))

        # 4. the lab killed: the Job ends its workers
        pids.clear()
        call({"id": 7, "cmd": "lab_start", "project": "Check", "at_once": 4})
        if not wait_status(lambda s: len([w for w in s["workers"] if w["pid"]]) >= 4, 30):
            problems.append("four workers did not start a third time")
        subprocess.run(["taskkill", "/PID", str(lab.pid), "/F"], capture_output=True)  # the lab alone
        t1 = time.time()
        while time.time() - t1 < 5 and any(alive(p) for p in pids):
            time.sleep(0.1)
        left = [p for p in pids if alive(p)]
        if left:
            problems.append("the lab killed, workers %s still run" % left)
        else:
            print("  lab killed: its %d workers gone in %.1f s" % (len(pids), time.time() - t1))
    except RuntimeError:
        pass
    finally:
        stop(lab)
        for p in pids:
            if alive(p):
                subprocess.run(["taskkill", "/PID", str(p), "/T", "/F"], capture_output=True)
    if not a.keep:
        shutil.rmtree(RUNS, ignore_errors=True)
    for q in problems:
        print("PROBLEM:", q)
    print("farm: %s" % ("passed" if not problems else "FAILED"))
    return 0 if not problems else 1


def rate(a):
    """Games an hour against games at once, the first project's games at the most pace."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hxlive

    folder = os.path.abspath(a.folder)
    copy_build(folder)
    data = fresh(os.path.join(RUNS, "rate"))
    shutil.copytree(os.path.join(ENGINE, "mods", "lab"), os.path.join(data, "mods", "lab"))

    def call(msg, timeout=15):
        try:
            r = hxlive.ws_call(a.port, msg, timeout=timeout)
            return r if isinstance(r, dict) else json.loads(r)
        except Exception as e:
            return {"ok": False, "error": str(e)}

    env = dict(os.environ, HYPERION_DISPLAY="right", HYPERION_WINDOW="background", HYPERION_FRONT_OPEN="lab")
    env.pop("HYPERION_DLLS", None)
    cmd = [os.path.join(folder, "Hyperion.exe"), "--data", data, "--control=%d" % a.port, "--mod", "lab"]
    lab = subprocess.Popen(cmd, cwd=folder, env=env)
    pids = set()
    rows, problems = [], []
    try:
        t0 = time.time()
        while not call({"id": 1, "cmd": "status"}).get("ok"):
            if time.time() - t0 > 60:
                problems.append("the lab's channel did not answer")
                raise RuntimeError
            time.sleep(0.5)
        for n in [int(x) for x in a.at_once.split(",") if x]:
            name = "Rate %d" % n
            project = os.path.join(data, "lab", "projects", name)
            os.makedirs(project)
            with open(os.path.join(project, "project.json"), "w", encoding="utf-8") as f:
                json.dump({"name": name, "maps": [a.map], "players": 2, "levels": ["7,7", "7,6", "6,6", "7,5"],
                           "type": "ffa", "first_seed": 1, "frames": 86400}, f, indent=2)
            r = call({"id": 2, "cmd": "lab_start", "project": name, "at_once": n})
            if not r.get("ok"):
                problems.append("lab_start %d: %s" % (n, r.get("error")))
                break
            time.sleep(a.warm)
            first = call({"id": 3, "cmd": "lab_games", "max": 0}).get("count", 0)
            t1 = time.time()
            time.sleep(a.seconds)
            took = time.time() - t1
            games = call({"id": 4, "cmd": "lab_games", "since": first, "max": 100000}).get("games", [])
            s = call({"id": 5, "cmd": "lab_status"})
            playing = [w for w in s.get("workers", []) if w.get("pid")]
            memory = []
            for w in playing:
                pids.add(w["pid"])
                m = Memory(w["pid"])
                m.read()
                memory.append(getattr(m, "private", 0))
            call({"id": 6, "cmd": "lab_stop"})
            frames = sum(g.get("end_frame", 0) for g in games)
            ended = {}
            for g in games:
                ended[g["ended"]] = ended.get(g["ended"], 0) + 1
            lost = sum(c for k, c in ended.items() if k not in ("finished", "last"))
            if lost:
                problems.append("%d at once: %d games neither finished nor won" % (n, lost))
            row = {"at_once": n, "games": len(games), "per_hour": len(games) * 3600.0 / took,
                   "game_hours": frames / (24.0 * 3600) * 3600.0 / took,
                   "minutes": frames / (24.0 * 60) / len(games) if games else 0,
                   "ended": ended, "workers": len(playing),
                   "memory": sum(memory) / len(memory) / 2 ** 20 if memory else 0}
            rows.append(row)
            print("%2d at once: %4d games in %.0f s, %6.0f an hour, %5.0f hours of game an hour, %4.1f min a game, "
                  "%s; %d workers, %.0f MB each" % (n, row["games"], took, row["per_hour"], row["game_hours"],
                                                   row["minutes"], ", ".join("%d %s" % (c, k) for k, c in
                                                                             sorted(ended.items())),
                                                   row["workers"], row["memory"]), flush=True)
            time.sleep(2.0)
    except RuntimeError:
        pass
    finally:
        stop(lab)
        for p in pids:
            if alive(p):
                subprocess.run(["taskkill", "/PID", str(p), "/T", "/F"], capture_output=True)
    if rows:
        print("cores: %d" % os.cpu_count())
    if not a.keep:
        shutil.rmtree(RUNS, ignore_errors=True)
    for q in problems:
        print("PROBLEM:", q)
    return 0 if rows and not problems else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="what", required=True)
    w = sub.add_parser("worker")
    w.add_argument("--folder", default=os.path.join(ENGINE, "testrun", "front_w2"))
    w.add_argument("--map", default=None)
    w.add_argument("--players", type=int, default=2)
    w.add_argument("--levels", default="7,5")
    w.add_argument("--seed", type=int, default=7)
    w.add_argument("--frames", type=int, default=20000)
    w.add_argument("--timeout", type=float, default=300)
    w.add_argument("--keep", action="store_true", help="keep testrun\\lab_check\\ afterwards")
    f = sub.add_parser("farm")
    f.add_argument("--folder", default=os.path.join(ENGINE, "testrun", "front_w2"))
    f.add_argument("--map", default=None)
    f.add_argument("--frames", type=int, default=20000)
    f.add_argument("--games", type=int, default=8)
    f.add_argument("--port", type=int, default=27166, help="the lab's live channel")
    f.add_argument("--timeout", type=float, default=300)
    f.add_argument("--keep", action="store_true", help="keep testrun\\lab_check\\ afterwards")
    s = sub.add_parser("soak")
    s.add_argument("--folder", default=os.path.join(ENGINE, "testrun", "front_w2"))
    s.add_argument("--map", default=None)
    s.add_argument("--games", type=int, default=20)
    s.add_argument("--frames", type=int, default=6000)
    s.add_argument("--seed", type=int, default=100)
    s.add_argument("--check", type=int, default=0, help="games played again in a fresh process (0 for all)")
    s.add_argument("--timeout", type=float, default=3600)
    s.add_argument("--keep", action="store_true", help="keep testrun\\lab_check\\ afterwards")
    r = sub.add_parser("rate")
    r.add_argument("--folder", default=os.path.join(ENGINE, "testrun", "front_w2"))
    r.add_argument("--map", default=None)
    r.add_argument("--at-once", default="1,2,4,8,12,16,24", help="games at once, a count for each")
    r.add_argument("--warm", type=float, default=10, help="seconds before each count")
    r.add_argument("--seconds", type=float, default=60, help="seconds each count lasts")
    r.add_argument("--port", type=int, default=27167, help="the lab's live channel")
    r.add_argument("--keep", action="store_true", help="keep testrun\\lab_check\\ afterwards")
    a = ap.parse_args()
    if a.map is None:
        a.map = os.path.join(os.path.abspath(a.folder), "maps", "classic", "(4)Lost Temple.scm")
    return {"worker": worker, "farm": farm, "soak": soak, "rate": rate}[a.what](a)


if __name__ == "__main__":
    sys.exit(main())
