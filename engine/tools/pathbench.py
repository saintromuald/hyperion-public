"""pathbench.py -- how well units find their way on a map (stages F0 and F4).

Starts Hyperion on a map as a Use Map Settings game (--ums), so no computer plays and no
melee rule ends the game, with the live channel open. Then for each route and each unit
type it spawns a group for the local player beside its start location and moves it to a
point across the map, stepping the game and reading where each unit is, and removes the
group after (the channel's clear). It reports, per route and unit type, how many arrived
and when, how long their paths were against the straight line, where the others ended,
and what the pathing counted meanwhile (h/port_pathstats.h): the region searches and local
searches, and those that ran out of room or settled for the closest place; then the run's
totals. The run is written to testrun/rt_runs/pathbench_<stamp>/pathbench.json after each
group.

    python tools/pathbench.py "testrun/play/maps/Hyperion/Big Game Hunters 4x4.hxm"
    python tools/pathbench.py <map> --units zergling,dragoon,tank,scv --count 12 --routes 4

The routes go from the local player's start to the points that mirror it across the
map's middle, both axes and each, and to the middle itself, so on a symmetric map they end
at another base; a point a ground unit cannot reach moves to the nearest one it can (the
channel's region). A group ends when every unit has arrived, died or run out of time, a
time from its type's speed. A unit that ends within 256 pixels of the target is counted
as arrived, in the crowd there; one that ends within 256 pixels of where it was made, as
boxed in at the spawn; any other, as stuck on the way. The game is not a replay, and it is run for the numbers, not
for an exact result.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ENGINE / "tools" / "replaytest"))
sys.path.insert(0, str(ENGINE / "tools" / "control"))
import replaytest  # noqa: E402
from hyperion_ctl import Client  # noqa: E402

# unit ids and top speeds in pixels a frame (flingy.dat's, x256)
UNITS = {"zergling": (37, 1402 / 256), "dragoon": (66, 1280 / 256), "mutalisk": (43, 1707 / 256),
         "marine": (0, 1024 / 256), "ultralisk": (39, 1314 / 256), "scv": (7, 1280 / 256),
         "tank": (5, 1024 / 256)}
STEP = 48            # frames between readings
ARRIVED = 128        # pixels from the target that count as there
CROWD = 256          # ... and that count as in the crowd there, held up by the ones that arrived
STILL = 8            # pixels a unit must move in STILL_FOR frames not to count as standing
STILL_FOR = 480
KEYS = ("area_searches", "area_capped", "area_short", "local_searches", "local_capped", "local_short",
        "local_nodes")


class GameOver(Exception):
    pass


def wait_game(c, seconds=180):
    end = time.time() + seconds
    while time.time() < end:
        try:
            s = c.call("status")
            if s.get("state") == "game":
                return s
        except Exception:
            pass
        time.sleep(1)
    sys.exit("pathbench: no game within %d seconds" % seconds)


def past_objectives(c, seconds=30):
    """Runs the game past the mission objectives a Use Map Settings game shows early on,
    which pause it: the box stays up, the game goes on, and the channel's steps reach their
    frame."""
    call(c, "resume")
    end = time.time() + seconds
    while time.time() < end:
        s = call(c, "status")
        if s["paused"] and s["frame"] > 0:
            break
        time.sleep(0.5)
    call(c, "resume")
    time.sleep(1)
    call(c, "pause")


def connect(port, seconds=60):
    end = time.time() + seconds
    while time.time() < end:
        try:
            return Client(port=port, timeout=5.0)
        except Exception:
            time.sleep(1)
    sys.exit("pathbench: the channel did not open on port %d" % port)


def call(c, cmd, **args):
    """A command that needs the game: GameOver when the game has ended."""
    a = c.call(cmd, **args)
    if not a.get("ok"):
        raise GameOver(a.get("error"))
    return a


def step(c):
    """STEP frames on, back to back (step's fast). A click or key in Hyperion's window ends the channel's stepping
    (PortControlPlayerInput), and the step then never answers: the game is paused again
    where it got to, which serves as well, the readings carrying their own frame."""
    try:
        c.call("step", timeout=30.0, frames=STEP, fast=True)
    except TimeoutError:
        call(c, "pause")


def routes(start, width, height, n, avoid):
    sx, sy = start
    pts = [(width - sx, height - sy), (width - sx, sy), (sx, height - sy), (width // 2, height // 2)]
    out = []
    for tx, ty in pts:
        tx = min(max(tx, 64), width - 64)
        ty = min(max(ty, 64), height - 64)
        far = math.hypot(tx - sx, ty - sy) > 256
        clear = all(math.hypot(tx - ax, ty - ay) > 1500 for ax, ay in avoid)
        if far and clear and (tx, ty) not in out:
            out.append((tx, ty))
    return out[:n]


def reachable(c, start, target):
    """The target, or the nearest point to it a ground unit at start can reach: one in the
    start's area, on a walkable region (the channel's region). None when there is none
    within 1,024 pixels."""
    home = call(c, "region", x=start[0], y=start[1])["area"]
    st = call(c, "status")["map"]
    tx, ty = target
    for r in range(0, 1025, 32):
        ring = [(0, 0)] if r == 0 else [(round(r * math.cos(a * math.pi / 16)), round(r * math.sin(a * math.pi / 16)))
                                        for a in range(32)]
        for dx, dy in ring:
            x, y = tx + dx, ty + dy
            if 32 <= x < st["width"] - 32 and 32 <= y < st["height"] - 32:
                g = call(c, "region", x=x, y=y)
                if g["area"] == home and g["group"] != 0x1FFD:
                    return (x, y)
    return None


def run_route(c, units, count, start, target, player):
    sx, sy = start
    tx, ty = target
    straight = math.hypot(tx - sx, ty - sy)
    before = call(c, "pathstats")
    groups = {}
    for unit in units:
        a = call(c, "spawn", unit=UNITS[unit][0], x=sx, y=sy, count=count, move=[tx, ty], player=player)
        groups[unit] = a["ids"]
    frame0 = a["frame"]
    limit = int(straight / min(UNITS[u][1] for u in units) * 1.5) + 1500
    seen = {i: {"length": 0.0, "last": None, "arrived": None, "dead": False, "track": []}
            for ids in groups.values() for i in ids}
    ids = list(seen)
    frame = frame0
    while frame - frame0 < limit:
        step(c)
        u = call(c, "units", ids=ids)
        frame = u["frame"]
        for r in u["units"]:
            s = seen[r["id"]]
            if not r["alive"]:
                s["dead"] = True
                continue
            p = (r["x"], r["y"])
            if s["last"] is not None:
                s["length"] += math.hypot(p[0] - s["last"][0], p[1] - s["last"][1])
            s["last"] = p
            s["track"].append((frame, p))
            if s["arrived"] is None and math.hypot(p[0] - tx, p[1] - ty) <= ARRIVED:
                s["arrived"] = frame - frame0
        if all(s["arrived"] is not None or s["dead"] for s in seen.values()):
            break
    after = call(c, "pathstats")
    out = {"from": [sx, sy], "to": [tx, ty], "straight": round(straight), "frames": frame - frame0,
           "counts": {k: after.get(k, 0) - before.get(k, 0) for k in KEYS}, "units": {}}
    for unit, uids in groups.items():
        g = [seen[i] for i in uids]
        arrived = sorted(s["arrived"] for s in g if s["arrived"] is not None)
        # where the others ended: in the crowd at the target, still where they were made
        # (the spawn can put a unit in a pocket beside a base), or on the way
        ends = [s["last"] for s in g if s["arrived"] is None and not s["dead"] and s["last"] is not None]
        crowd = [e for e in ends if math.hypot(e[0] - tx, e[1] - ty) <= CROWD]
        boxed = [e for e in ends if e not in crowd and math.hypot(e[0] - sx, e[1] - sy) <= CROWD]
        stuck = [e for e in ends if e not in crowd and e not in boxed]
        still = 0
        for s in g:
            if s["arrived"] is None and not s["dead"] and s["track"]:
                back = [p for f, p in s["track"] if f <= frame - STILL_FOR]
                if back and math.hypot(s["last"][0] - back[-1][0], s["last"][1] - back[-1][1]) < STILL:
                    still += 1
        ratios = sorted(s["length"] / straight for s in g if s["arrived"] is not None)
        out["units"][unit] = {
            "count": len(uids), "arrived": len(arrived), "dead": sum(s["dead"] for s in g), "still": still,
            "median": arrived[len(arrived) // 2] if arrived else None,
            "ratio": round(ratios[len(ratios) // 2], 3) if ratios else None,
            "crowd": len(crowd), "boxed": len(boxed), "stuck": len(stuck),
            "stuck_at": [list(e) for e in stuck], "boxed_at": [list(e) for e in boxed],
        }
    return out


def total(result):
    """The run's totals, printed and kept in the result."""
    n = dict.fromkeys(("count", "arrived", "crowd", "stuck", "boxed", "dead"), 0)
    k = dict.fromkeys(KEYS, 0)
    ratios = []
    for r in result["routes"]:
        for key in KEYS:
            k[key] += r["counts"].get(key, 0)
        for g in r["units"].values():
            for key in n:
                n[key] += g[key]
            if g["ratio"]:
                ratios.append(g["ratio"])
    ratios.sort()
    t = {"units": n, "counts": k, "ratio": ratios[len(ratios) // 2] if ratios else None,
         "area_capped": round(k["area_capped"] / max(1, k["area_searches"]), 4),
         "local_capped": round(k["local_capped"] / max(1, k["local_searches"]), 4)}
    result["total"] = t
    print("  total: %d/%d arrived (%d in the crowd), %d stuck on the way, %d boxed in at the spawn, %d dead; "
          "region searches %d, %.1f%% capped; local searches %d, %.1f%% capped; median path x%s" % (
              n["arrived"] + n["crowd"], n["count"], n["crowd"], n["stuck"], n["boxed"], n["dead"],
              k["area_searches"], 100 * t["area_capped"], k["local_searches"], 100 * t["local_capped"], t["ratio"]),
          flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map")
    ap.add_argument("--units", default="zergling,dragoon,tank,scv")
    ap.add_argument("--count", type=int, default=12)
    ap.add_argument("--routes", type=int, default=4)
    ap.add_argument("--race", default="zerg")
    ap.add_argument("--seed", type=int, default=7001)
    ap.add_argument("--port", type=int, default=27119)
    ap.add_argument("--out", help="the run's folder (default testrun/rt_runs/pathbench_<stamp>)")
    ap.add_argument("--window", default="hidden", help="HYPERION_WINDOW: hidden (default) or windowed")
    ap.add_argument("--budget", choices=("map", "117"), default="map",
                    help="the region search's budget on a map past 256 tiles: the map's (default), or "
                         "1.17's, for a baseline (HYPERION_PATH_BUDGET)")
    args = ap.parse_args()
    units = args.units.split(",")
    for unit in units:
        if unit not in UNITS:
            sys.exit("pathbench: no unit %s; one of %s" % (unit, ", ".join(UNITS)))

    replaytest.setup_hx64()
    mp = Path(args.map).resolve()
    out = Path(args.out) if args.out else ENGINE / "testrun" / "rt_runs" / time.strftime("pathbench_%Y%m%d_%H%M%S")
    out.mkdir(parents=True, exist_ok=False)
    exe = replaytest.HX64_EXE
    # the harness's game folder is its data folder too (M1a), and the window stays off the
    # screen, where a click would end the channel's stepping
    proc = subprocess.Popen([str(exe), "--data=%s" % exe.parent, "--map=%s" % mp, "--ums",
                             "--seed=%d" % args.seed, "--control=%d" % args.port],
                            cwd=str(exe.parent), env=dict(os.environ, HYPERION_WINDOW=args.window,
                                                          HYPERION_PATH_BUDGET=args.budget))
    result = {"map": str(mp), "units": units, "count": args.count, "budget": args.budget, "routes": []}

    def save():
        (out / "pathbench.json").write_text(json.dumps(result, indent=1))

    try:
        c = connect(args.port)
        wait_game(c)
        past_objectives(c)
        call(c, "reveal")
        players = call(c, "players")["players"]
        # the local player, the human: a map with random starts may seat it in any slot
        local = next(p for p in players if p["type"] == 2)
        start = tuple(local["start"])
        others = [tuple(p["start"]) for p in players if p["slot"] not in (local["slot"], 11) and p["type"] != 0]
        st = c.call("status")
        width, height = st["map"]["width"], st["map"]["height"]
        result["pathstats"] = call(c, "pathstats")
        result["start"] = list(start)
        result["slot"] = local["slot"]
        save()
        ps = result["pathstats"]
        print("pathbench: %s, %d x %d pixels, %d regions, %d areas, build %.0f ms" % (
            mp.name, width, height, ps["regions"], ps["areas"], ps["build_ms"]), flush=True)
        # beside the start, clear of where a base would stand
        origin = (start[0], start[1] + 160)
        for aim in routes(start, width, height, args.routes, others):
            target = reachable(c, origin, aim)
            if target is None:
                print("  to %s: no point near it can be reached, left out" % (tuple(aim),), flush=True)
                continue
            print("  to %s%s, %d px" % (tuple(target), "" if target == aim else " (for %s)" % (tuple(aim),),
                                         math.hypot(target[0] - origin[0], target[1] - origin[1])), flush=True)
            for unit in units:
                r = run_route(c, [unit], args.count, origin, target, local["slot"])
                r["aim"] = list(aim)
                call(c, "clear", player=local["slot"])
                result["routes"].append(r)
                save()
                k = r["counts"]
                g = r["units"][unit]
                print("    %-9s %2d/%d arrived (%d in the crowd), %d stuck, %d boxed in, %d dead, median %5s frames, "
                      "path x%s; area %d (%d capped, %d short), local %d (%d capped, %d short)%s" % (
                          unit, g["arrived"] + g["crowd"], g["count"], g["crowd"], g["stuck"], g["boxed"], g["dead"],
                          g["median"], g["ratio"], k["area_searches"], k["area_capped"], k["area_short"],
                          k["local_searches"], k["local_capped"], k["local_short"],
                          " stuck at %s" % g["stuck_at"] if g["stuck"] else ""), flush=True)
        total(result)
        c.close()
    except GameOver as e:
        result["ended"] = str(e)
        print("pathbench: the game ended (%s)" % e, flush=True)
    finally:
        save()
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/F"], capture_output=True)
    print("pathbench: %s" % (out / "pathbench.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
