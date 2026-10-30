#!/usr/bin/env python3
"""Play replays on the original 1.17 and on Hyperion, and show that both play the same game.

    python tools/replaytest/compare.py <replay or folder> [...]
    python tools/replaytest/compare.py my.rep --every 10s     a snapshot every 10 seconds (default 1m)
    python tools/replaytest/compare.py my_replays --brief     the hashes only, no player lines
    python tools/replaytest/compare.py my.rep --open          open the page when it is written

Each replay is played by both through replaytest.py's run_one, as the suite plays it, so
1.17's run of a replay is kept and reused (rt_cache/oracle). For each snapshot the
terminal shows the time, the frame, the state hash of each build, the running hash of each,
and each player's minerals, gas, supply, units and buildings; at the end, every player's
score screen numbers from both builds, and the game's result.

The state hash is a snapshot frame's whole record in the observer's log (docs/replay-test.md,
"The log"): the random number state and its call count, the unit count, the hash of every
live unit's fields (h_sem), the hash of the stats (h_stat: resources, the score counters,
the counts per unit type, research and upgrades), and each player's lines of that frame. The
running hash folds in every frame's record from the first one, so equal running hashes say
every frame up to the snapshot was equal too, not only the snapshot's.

The result comes from the replay's quit commands (repfile.quits) and from each build's own
end of the game: a side is out when its players have left or have no buildings. A replay on
which 1.17 loses recorded commands (repfile.lost_frames) is compared through the first such
frame, as the suite compares it, since from there Hyperion plays the commands and 1.17 does
not.

The run goes to testrun/rt_runs/compare_<stamp>/: per replay, both builds' logs and
snapshots.csv (both builds' numbers every second, side by side); for the run, compare.html.
The exit code is 0 when every replay matched and 1 otherwise.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import datetime as _dt
import hashlib
import html
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import replaytest as rt  # noqa: E402
import repfile  # noqa: E402

MS_PER_FRAME = 42  # a frame at Fastest
STATS_EVERY = 24  # the P and Q lines' period (replaytest's --stats-every)
DEFAULT_BUILD = {"win32": "hx64", "darwin": "ma64"}.get(sys.platform, "lx64")
RACE_INDEX = {"Zerg": 0, "Terran": 1, "Protoss": 2}

# The Q line's numbers shown at the end, by their names in it (replaytest.SCORE_NAMES): the
# two gathered, then glGameCounts_g's rows as the score screen counts them.
Q_INDEX = {n: i for i, n in enumerate(rt.SCORE_NAMES)}
FINAL_ROWS = [(n, Q_INDEX[n]) for n in (
    "minerals gathered", "gas gathered", "units produced", "units owned", "units lost", "units killed",
    "buildings constructed", "buildings owned", "buildings lost", "buildings razed", "unit score",
    "building score", "kill score", "razing score")]
Q_UNITS, Q_BUILDINGS = Q_INDEX["units owned"], Q_INDEX["buildings owned"]
Q_SUPPLY = Q_INDEX["zerg supply available"]  # then used and the cap, then Terran's and Protoss's
SNAP_MEASURES = ["minerals", "gas", "supply", "units", "buildings"]

FIELD_WORDS = {"rand_state": "the random number state", "rand_count": "the random number count",
               "units": "the unit count", "h_sem": "the units' fields", "h_stat": "the stats",
               "state_hash": "the engine's state hash", "players": "a player's minerals, gas or units",
               "scores": "a player's score screen numbers"}


# ---------------------------------------------------------------------------
# terminal

class Ink:
    def __init__(self, on: bool):
        if on and sys.platform == "win32":
            os.system("")  # turns on the console's escape codes
        self.on = on
        try:
            "\u2713\u2717".encode(sys.stdout.encoding or "ascii")
            self.yes, self.no = "\u2713", "\u2717"
        except UnicodeEncodeError:  # output to a file or pipe in a code page without them
            self.yes, self.no = "ok", "DIFFERS"

    def _w(self, code, s):
        return f"\x1b[{code}m{s}\x1b[0m" if self.on else s

    def ok(self, s): return self._w("32", s)
    def bad(self, s): return self._w("31", s)
    def dim(self, s): return self._w("2", s)
    def bold(self, s): return self._w("1", s)


def clock(frame: int) -> str:
    s = round(frame * MS_PER_FRAME / 1000)
    h, s = divmod(s, 3600)
    m, s = divmod(s, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def parse_every(text: str):
    """--every: 10s, 1m, 2.5m or a number of frames as 360f."""
    t = text.strip().lower()
    try:
        if t.endswith("f"):
            n = int(t[:-1])
            return ("frames", max(STATS_EVERY, -(-n // STATS_EVERY) * STATS_EVERY))
        secs = float(t[:-1]) * 60 if t.endswith("m") else float(t.rstrip("s"))
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a period: {text} (10s, 1m, 360f)")
    if secs <= 0:
        raise argparse.ArgumentTypeError("the period must be more than 0")
    return ("secs", secs)


def snapshot_frames(last: int, every) -> list[int]:
    """The frames of the snapshots up to last, last included: each on a frame of the P and Q
    lines, the nearest to its time, so they do not drift."""
    kind, n = every
    out = []
    k = 1
    while True:
        if kind == "frames":
            f = k * n
        else:
            f = max(STATS_EVERY, round(k * n * 1000 / MS_PER_FRAME / STATS_EVERY) * STATS_EVERY)
        if f >= last:
            break
        if not out or f > out[-1]:
            out.append(f)
        k += 1
    return out + [last]


# ---------------------------------------------------------------------------
# reading the two runs

class Run:
    """One build's log of a replay, with lookups by frame."""

    def __init__(self, log: dict):
        self.log = log
        self.F = log["F"]
        self.P = log["P"]
        self.Q = log["Q"]
        self.last = max(self.F) if self.F else 0
        self._pk = {p: sorted(v) for p, v in self.P.items()}
        self._qk = {p: sorted(v) for p, v in self.Q.items()}

    @staticmethod
    def _at(series: dict, keys: list, frame: int):
        i = bisect.bisect_right(keys, frame)
        return series[keys[i - 1]] if i else None

    def p(self, player: int, frame: int):
        return self._at(self.P[player], self._pk[player], frame) if player in self.P else None

    def q(self, player: int, frame: int):
        return self._at(self.Q[player], self._qk[player], frame) if player in self.Q else None

    def record(self, frame: int) -> str | None:
        """The frame's record as compared: the F fields but h_raw, then each player's lines."""
        f = self.F.get(frame)
        if f is None:
            return None
        parts = [f[0], str(f[1]), str(f[2]), f[3], f[5] or ""]
        for p in sorted(self.P):
            v = self.P[p].get(frame)
            if v is not None:
                parts.append(f"P{p}={v}")
        for p in sorted(self.Q):
            v = self.Q[p].get(frame)
            if v is not None:
                parts.append(f"Q{p}={v}")
        return "|".join(parts)

    def state_hash(self, frame: int) -> str | None:
        r = self.record(frame)
        return hashlib.sha1(r.encode()).hexdigest()[:8] if r is not None else None

    def running_hashes(self, frames: list[int]) -> dict[int, str]:
        want = set(frames)
        h = hashlib.sha1()
        out = {}
        for f in sorted(self.F):
            h.update(self.record(f).encode())
            if f in want:
                out[f] = h.copy().hexdigest()[:8]
        return out

    def race(self, player: dict) -> str:
        """The race played: the header's, or for a random slot the race whose supply it has."""
        if player["race"] in RACE_INDEX:
            return player["race"]
        for f in self._qk.get(player["slot"], []):
            q = self.Q[player["slot"]][f]
            avail = [q[Q_SUPPLY + 3 * r] for r in range(3)]
            if any(avail):
                return ["Zerg", "Terran", "Protoss"][avail.index(max(avail))]
        return player["race"]

    def values(self, player: dict, race: str, frame: int) -> dict | None:
        """A player's numbers at a frame: minerals and gas on hand, supply used and given (as
        the game shows it, halves rounded up), units and buildings owned."""
        p, q = self.p(player["slot"], frame), self.q(player["slot"], frame)
        if p is None and q is None:
            return None
        out = {"minerals": p[0] if p else None, "gas": p[1] if p else None}
        if q:
            r = RACE_INDEX.get(race, 0)
            avail, used, cap = q[Q_SUPPLY + 3 * r:Q_SUPPLY + 3 * r + 3]
            out["supply"] = f"{(used + 1) // 2}/{min(avail, cap) // 2}"
            out["units"], out["buildings"] = q[Q_UNITS], q[Q_BUILDINGS]
        else:
            out.update(supply=None, units=None, buildings=None)
        return out


def result(hdr: dict, quits: list, run: Run) -> str:
    """Who won, as far as the replay and the end of a build's game say: a side is out when
    every player of it has left or has no buildings at the end."""
    players = hdr["players"]
    by_storm = {p["storm"]: p for p in players}
    left = {}
    for frame, storm in quits:
        p = by_storm.get(storm)
        if p and p["slot"] not in left and frame <= run.last:
            left[p["slot"]] = frame
    team_game = len({p["team"] for p in players}) > 1
    sides: dict = {}
    for p in players:
        sides.setdefault(p["team"] if team_game else p["slot"], []).append(p)

    def out(p):
        q = run.q(p["slot"], run.last)
        return p["slot"] in left or (q is not None and q[Q_BUILDINGS] == 0)

    def why(p):
        return (f"{p['name']} left at {clock(left[p['slot']])}" if p["slot"] in left
                else f"{p['name']} had no buildings left")

    def name(k):
        return (f"team {k} ({', '.join(p['name'] for p in sides[k])})" if team_game else sides[k][0]["name"])

    standing = [k for k, ps in sides.items() if not all(out(p) for p in ps)]
    fallen = [k for k in sides if k not in standing]
    reasons = "; ".join(why(p) for k in fallen for p in sides[k])
    if len(standing) == 1 and fallen:
        return f"{name(standing[0])} won: {reasons}"
    if not fallen:
        return "no result: the replay ends with every side still in the game"
    if not standing:
        return f"every side is out at the end: {reasons}"
    return f"still in at the end: {', '.join(name(k) for k in standing)}; out: {reasons}"


# ---------------------------------------------------------------------------
# comparing

def compare_replay(rep: Path, group: Path, build: str, every, hdr: dict, sizes: list[int]) -> dict:
    ref_csv, hyp_csv = group / "oracle.csv", group / f"{build}.csv"
    ref = Run(rt.parse_csv(ref_csv)) if ref_csv.exists() else None
    hyp = Run(rt.parse_csv(hyp_csv)) if hyp_csv.exists() else None
    lost = rt.lost_frames(str(rep))
    limit = lost[0] if lost else None
    quits = repfile.quits(rep, sizes)
    out = {"replay": rep, "group": group, "build": build, "header": hdr, "limit": limit, "lost": len(lost),
           "ref": ref, "hyp": hyp, "quits": quits, "rows": [], "final": [], "players": []}

    if ref is None or not ref.F:
        end = ref.log.get("end") if ref else "no log"
        out.update(status="not_compared", verdict=f"1.17 could not play this replay (it ended: {end})")
        return out
    if hyp is None or not hyp.F:
        end = hyp.log.get("end") if hyp else "no log"
        out.update(status="differs", verdict=f"Hyperion did not play this replay (it ended: {end})")
        return out

    cmp = rt.compare(ref.log, hyp.log, limit, raw=False)
    fd = cmp["first_divergence"]
    races = {p["slot"]: ref.race(p) for p in hdr["players"]}
    out["players"] = [dict(p, played=races[p["slot"]]) for p in hdr["players"]]
    last = max(ref.last, hyp.last)
    frames = snapshot_frames(last, every)
    if fd is not None and fd not in frames:  # the first difference gets a row of its own
        frames = sorted(frames + [fd])
    run_ref, run_hyp = ref.running_hashes(frames), hyp.running_hashes(frames)
    for f in frames:
        row = {"frame": f, "after": limit is not None and f > limit, "first": f == fd,
               "hash": (ref.state_hash(f), hyp.state_hash(f)), "running": (run_ref.get(f), run_hyp.get(f)),
               "players": []}
        for p in out["players"]:
            row["players"].append((ref.values(p, p["played"], f), hyp.values(p, p["played"], f)))
        row["same"] = (row["hash"][0] is not None and row["hash"][0] == row["hash"][1]
                       and row["running"][0] == row["running"][1] and all(a == b for a, b in row["players"]))
        out["rows"].append(row)

    # the end: each build's last line of numbers
    for p in out["players"]:
        a, b = ref.q(p["slot"], ref.last), hyp.q(p["slot"], hyp.last)
        va, vb = ref.values(p, p["played"], ref.last), hyp.values(p, p["played"], hyp.last)
        cells = [("minerals", va and va["minerals"], vb and vb["minerals"]),
                 ("gas", va and va["gas"], vb and vb["gas"]),
                 ("supply", va and va["supply"], vb and vb["supply"])]
        cells += [(n, a[i] if a else None, b[i] if b else None) for n, i in FINAL_ROWS]
        out["final"].append(cells)
    out["result"] = (result(hdr, quits, ref), result(hdr, quits, hyp))

    compared = [r for r in out["rows"] if not r["after"] and not r["first"]]
    out["snapshots"] = (sum(r["same"] for r in compared), len(compared))
    out["first_detail"] = first_detail(ref, hyp, out["players"], fd) if fd is not None else []
    whole = cmp["common_frames"] == cmp["ref_frames"]
    ends = (ref.log.get("end"), hyp.log.get("end"))
    # a game that did not reach the replay's end, said with any verdict
    early = "".join(f"; {who}'s game ended at frame {run.last:,} ({clock(run.last)}): {run.log.get('end')}"
                    for who, run in (("1.17", ref), ("Hyperion", hyp)) if run.log.get("end") != "replay_end")
    if fd is None and whole and limit and ends[1] != "replay_end":
        out.update(status="differs", verdict=(
            f"identical through frame {limit:,} ({clock(limit)}), where 1.17 starts losing recorded commands, "
            f"but Hyperion's game did not reach the replay's end{early}"))
    elif fd is None and whole and limit:
        # past the limit the two may part, and may come together again
        both = sorted(f for f in set(ref.F) & set(hyp.F) if f > limit)
        after = [f for f in both if ref.record(f) != hyp.record(f)]
        n = len(lost)
        then = ("and every frame after it is the same too" if not after else
                f"after it {len(after):,} frames differ, the last at {after[-1]:,} ({clock(after[-1])}), "
                "and from there to the end the games are the same again" if after[-1] < both[-1] else
                f"after it {len(after):,} of the {len(both):,} frames both played differ")
        out.update(status="identical_through", verdict=(
            f"identical through frame {limit:,} ({clock(limit)}), where 1.17 starts losing recorded commands "
            f"({n} frame{'s' if n != 1 else ''} of this replay) and Hyperion plays them; {then}{early}"))
    elif fd is None and whole and ref.last == hyp.last and ends[0] == ends[1]:
        out.update(status="identical", verdict=f"identical on all {cmp['common_frames']:,} frames, "
                                               "and at the end")
    elif fd is None:
        out.update(status="differs", verdict=(
            f"Hyperion's game ended at frame {hyp.last:,} ({ends[1]}), 1.17's at {ref.last:,} ({ends[0]}); "
            f"the {cmp['common_frames']:,} frames both played are identical"))
    else:
        what = [FIELD_WORDS.get(k, k) for k, v in cmp["first_by_field"].items() if v == fd]
        out.update(status="differs", verdict=f"differs from frame {fd:,} ({clock(fd)}): {', '.join(what)}{early}")
    out["first"] = fd
    return out


def first_detail(ref: Run, hyp: Run, players: list[dict], f: int, most: int = 10) -> list[str]:
    """What differs at frame f: the frame's fields, then each player's numbers, both builds'."""
    lines = []
    a, b = ref.F.get(f), hyp.F.get(f)
    if a is None or b is None:
        return [f"{'1.17' if a is None else 'Hyperion'} has no record of the frame"]
    for i, name in ((0, "the random number state"), (1, "the random number count"), (2, "the unit count"),
                    (3, "the units' fields (h_sem)"), (5, "the stats (h_stat)")):
        if a[i] != b[i]:
            lines.append(f"{name}: 1.17 {a[i]}, Hyperion {b[i]}")
    for p in players:
        s = p["slot"]
        for kind, names in (("P", ["minerals", "gas", "units, all of them", "hit points, all units'"]),
                            ("Q", rt.SCORE_NAMES)):
            va = getattr(ref, kind).get(s, {}).get(f)
            vb = getattr(hyp, kind).get(s, {}).get(f)
            if va == vb:
                continue
            for i in range(max(len(va or ()), len(vb or ()))):
                x = va[i] if va and i < len(va) else None
                y = vb[i] if vb and i < len(vb) else None
                if x != y:
                    n = names[i] if i < len(names) else f"counter {i}"
                    lines.append(f"{p['name']}, {n}: 1.17 {fmt(x)}, Hyperion {fmt(y)}")
    return lines[:most] + ([f"and {len(lines) - most} more"] if len(lines) > most else [])


# ---------------------------------------------------------------------------
# writing it out

def mark(ink: Ink, same: bool | None) -> str:
    return ink.dim("-") if same is None else ink.ok(ink.yes) if same else ink.bad(ink.no)


def cell(a, b) -> str:
    fa = f"{a:,}" if isinstance(a, int) else ("-" if a is None else str(a))
    if a == b:
        return fa
    fb = f"{b:,}" if isinstance(b, int) else ("-" if b is None else str(b))
    return f"{fa} | {fb}"


def print_replay(c: dict, ink: Ink, brief: bool) -> None:
    hdr = c["header"]
    total = hdr["frames"]
    print()
    print(ink.bold(f"{c['replay'].name}") + ink.dim(f"  in {c['replay'].resolve().parent}")
          + f"  1.17 against Hyperion ({c['build']})")
    print(f"  {hdr['map'] or 'unnamed map'}, {hdr['size'][0]}x{hdr['size'][1]}, {total:,} frames "
          f"({clock(total)} at Fastest)")
    left = {}
    by_storm = {p["storm"]: p for p in hdr["players"]}
    for frame, storm in c["quits"]:
        if storm in by_storm:
            left.setdefault(by_storm[storm]["slot"], frame)
    team_game = len({p["team"] for p in hdr["players"]}) > 1
    players = c["players"] or hdr["players"]
    w = min(16, max([len(p["name"]) for p in players] + [6]))
    for p in players:
        extra = [p.get("played", p["race"])] + ([f"team {p['team']}"] if team_game else [])
        extra += [p["type"]] if p["type"] != "human" else []
        extra += [f"left at {clock(left[p['slot']])}"] if p["slot"] in left else []
        print(f"    {p['name'][:w]:<{w}}  " + ", ".join(extra))
    if c["status"] == "not_compared" or not c["rows"]:
        print("  " + ink.bad(c["verdict"]))
        return
    if c["limit"]:
        print(ink.dim(f"  1.17 loses recorded commands from frame {c['limit']:,} ({clock(c['limit'])}); "
                      "the snapshots after it are shown, not counted"))
    print(ink.dim("  a number is shown once where both builds have it, as 1.17 | Hyperion where they do not"))
    print()
    print(ink.dim("  time      frame   state hash          running hash"))
    print(ink.dim("                    1.17      Hyperion  1.17      Hyperion"))
    for r in c["rows"]:
        same = None if r["after"] else r["same"]
        h, rn = r["hash"], r["running"]
        print(f"  {clock(r['frame']):>7} {r['frame']:>9,}   {h[0] or '-':8}  {h[1] or '-':8}  "
              f"{rn[0] or '-':8}  {rn[1] or '-':8}  {mark(ink, same)}"
              + (ink.bad("  the first difference") if r["first"] else ""))
        if brief:
            continue
        for p, (a, b) in zip(players, r["players"]):
            if a is None and b is None:
                continue
            a, b = a or {}, b or {}
            parts = [f"{m} {cell(a.get(m), b.get(m))}" for m in SNAP_MEASURES]
            same = None if r["after"] else a == b
            print(ink.dim(f"      {p['name'][:w]:<{w}}  ") + "  ".join(parts) + "  " + mark(ink, same))
    # the end
    print()
    names = [p["name"][:16] for p in players]
    cw = max([len(n) for n in names] + [14])
    print(ink.dim("  the end of the game"))
    print("  " + " " * 22 + "".join(f"{n:>{cw}}  " for n in names))
    for i, (label, _, _) in enumerate(c["final"][0] if c["final"] else []):
        vals = [c["final"][k][i] for k in range(len(players))]
        all_same = all(x[1] == x[2] for x in vals)
        print(f"  {label:<22}" + "".join(f"{cell(x[1], x[2]):>{cw}}  " for x in vals) + mark(ink, all_same))
    ra, rb = c["result"]
    print()
    print("  result: " + (f"{ra} (the same in both)" if ra == rb else f"1.17: {ra}; Hyperion: {rb}"))
    sn = c["snapshots"]
    head = {"identical": ink.ok("IDENTICAL"), "differs": ink.bad("DIFFERS"),
            "identical_through": ink.ok(f"IDENTICAL TO {clock(c['limit'] or 0)}")}.get(c["status"],
                                                                                    ink.bad("NOT COMPARED"))
    print(f"  {head}: {c['verdict']}; {sn[0]} of {sn[1]} snapshots the same")
    if c["first_detail"]:
        print(f"  at frame {c['first']:,}:")
        for line in c["first_detail"]:
            print("    " + line)


def write_csv(c: dict) -> Path | None:
    """Both builds' numbers for every player every second (each P and Q frame), side by side."""
    if not c["rows"]:
        return None
    ref, hyp = c["ref"], c["hyp"]
    path = c["group"] / ("snapshots.csv" if c["build"] in rt.ENGINE_BUILDS else f"snapshots_{c['build']}.csv")
    frames = sorted({f for p in ref.P.values() for f in p} | {f for p in hyp.P.values() for f in p})
    with open(path, "w", newline="", encoding="utf-8") as fh:
        wr = csv.writer(fh)
        wr.writerow(["frame", "time", "player", "name"] + [f"{m} {b}" for m in SNAP_MEASURES
                                                           for b in ("1.17", "Hyperion")]
                    + ["state hash 1.17", "state hash Hyperion", "same"])
        for f in frames:
            ha, hb = ref.state_hash(f), hyp.state_hash(f)
            for p in c["players"]:
                a = ref.values(p, p["played"], f) or {}
                b = hyp.values(p, p["played"], f) or {}
                wr.writerow([f, clock(f), p["slot"] + 1, p["name"]]
                            + [x for m in SNAP_MEASURES for x in (a.get(m), b.get(m))]
                            + [ha, hb, "yes" if ha == hb and a == b else "no"])
    return path


def page(results: list[dict], out_dir: Path, build: str) -> Path:
    e = html.escape

    def status_word(c):
        return {"identical": "identical", "differs": "differs", "not_compared": "1.17 could not play it",
                "identical_through": f"identical to {clock(c['limit'] or 0)}, where 1.17 loses commands"}[c["status"]]
    summary = "".join(
        f"<tr><td>{e(c['replay'].name)}</td><td>{e(c['header']['map'])}</td>"
        f"<td class='n'>{clock(c['header']['frames'])}</td>"
        f"<td class='{'ok' if c['status'].startswith('identical') else 'bad'}'>{status_word(c)}</td></tr>"
        for c in results)
    sections = []
    for c in results:
        hdr = c["header"]
        players = c["players"] or hdr["players"]
        team_game = len({p["team"] for p in hdr["players"]}) > 1
        left = {}
        by_storm = {p["storm"]: p for p in hdr["players"]}
        for frame, storm in c["quits"]:
            if storm in by_storm:
                left.setdefault(by_storm[storm]["slot"], frame)
        s = [f"<section><h2>{e(c['replay'].name)}</h2>",
             f"<p class='muted'>{e(str(c['replay'].resolve().parent))}</p>",
             f"<p class='muted'>{e(hdr['map'])}, {hdr['size'][0]}x{hdr['size'][1]}, {hdr['frames']:,} frames "
             f"({clock(hdr['frames'])} at Fastest). 1.17 against Hyperion ({e(build)}).</p>",
             f"<p class='verdict {'ok' if c['status'].startswith('identical') else 'bad'}'>"
             f"{e(c['verdict'][0].upper() + c['verdict'][1:])}.</p>"]
        if c.get("first_detail"):
            s.append(f"<p>At frame {c['first']:,} ({clock(c['first'])}):</p><ul>"
                     + "".join(f"<li>{e(x)}</li>" for x in c["first_detail"]) + "</ul>")
        s.append("<div class='wrap'><table><tr><th>Player</th><th>Name</th><th>Race</th>"
                 + ("<th>Team</th>" if team_game else "") + "<th>Kind</th><th>Left</th></tr>" + "".join(
                     f"<tr><td>{p['slot'] + 1}</td><td>{e(p['name'])}</td>"
                     f"<td>{e(p.get('played', p['race']))}</td>"
                     + (f"<td class='n'>{p['team']}</td>" if team_game else "")
                     + f"<td>{p['type']}</td><td>{clock(left[p['slot']]) if p['slot'] in left else ''}</td></tr>"
                     for p in players) + "</table></div>")
        if not c["rows"]:
            s.append("</section>")
            sections.append("".join(s))
            continue
        ra, rb = c["result"]
        s.append("<p><b>Result:</b> " + (e(ra) + " <span class='muted'>(the same in both)</span>" if ra == rb
                                         else f"1.17: {e(ra)}; Hyperion: {e(rb)}") + "</p>")
        sn = c["snapshots"]
        s.append(f"<p class='muted'>{sn[0]} of {sn[1]} snapshots the same"
                 + (f"; 1.17 loses recorded commands from frame {c['limit']:,} ({clock(c['limit'])}), "
                    "so the snapshots after it are shown and not counted" if c["limit"] else "") + ".</p>")
        # the end, side by side
        s.append("<h3>The end of the game</h3><div class='wrap'><table><tr><th></th>" + "".join(
            f"<th colspan='2'>{e(p['name'])}</th>" for p in players) + "</tr><tr><th></th>"
            + "<th class='n'>1.17</th><th class='n'>Hyperion</th>" * len(players) + "</tr>")
        for i, (label, _, _) in enumerate(c["final"][0] if c["final"] else []):
            tds = []
            for k in range(len(players)):
                _, a, b = c["final"][k][i]
                cls = "n" if a == b else "n bad"
                tds.append(f"<td class='{cls}'>{fmt(a)}</td><td class='{cls}'>{fmt(b)}</td>")
            s.append(f"<tr><td>{label}</td>{''.join(tds)}</tr>")
        s.append("</table></div>")
        # curves: 1.17 solid, Hyperion dashed over it
        total = max(c["ref"].last, c["hyp"].last)
        for label, idx in (("Minerals", 0), ("Units, buildings included,", 2)):
            series = []
            for run, dashed in ((c["ref"], False), (c["hyp"], True)):
                for p in players:
                    pts = run.P.get(p["slot"], {})
                    series.append((f"{'Hyperion' if dashed else '1.17'} {p['name']}",
                                   [(f, v[idx]) for f, v in sorted(pts.items())],
                                   rt.PALETTE[p["slot"] % len(rt.PALETTE)], dashed))
            key = " &middot; ".join(f"<span style='color:{rt.PALETTE[p['slot'] % len(rt.PALETTE)]}'>&#9632;</span> "
                                    f"{e(p['name'])}" for p in players)
            s.append(f"<h3>{label} per player</h3>" + rt.svg_lines(series, xmax=total)
                     + f"<p class='muted'>{key}; 1.17 solid, Hyperion dashed over it, by frame</p>")
        # the snapshots
        s.append("<h3>Snapshots</h3><div class='wrap'><table><tr><th>Time</th><th class='n'>Frame</th>"
                 "<th>State hash 1.17</th><th>Hyperion</th><th>Running hash 1.17</th><th>Hyperion</th>"
                 "<th></th></tr>")
        for r in c["rows"]:
            same = None if r["after"] else r["same"]
            sym = "&ndash;" if same is None else "&#10003;" if same else "&#10007;"
            cls = "muted" if same is None else "ok" if same else "bad"
            first = " <span class='bad'>first difference</span>" if r["first"] else ""
            s.append(f"<tr><td>{clock(r['frame'])}{first}</td><td class='n'>{r['frame']:,}</td>"
                     f"<td><code>{r['hash'][0] or '-'}</code></td><td><code>{r['hash'][1] or '-'}</code></td>"
                     f"<td><code>{r['running'][0] or '-'}</code></td><td><code>{r['running'][1] or '-'}</code></td>"
                     f"<td class='{cls}'>{sym}</td></tr>")
        s.append("</table></div>")
        for k, p in enumerate(players):
            s.append(f"<details{' open' if len(players) <= 2 else ''}><summary>{e(p['name'])}: "
                     "each snapshot, both builds</summary><div class='wrap'><table><tr><th>Time</th>"
                     + "".join(f"<th colspan='2'>{m}</th>" for m in SNAP_MEASURES) + "<th></th></tr><tr><th></th>"
                     + "<th class='n'>1.17</th><th class='n'>Hyperion</th>" * len(SNAP_MEASURES) + "<th></th></tr>")
            for r in c["rows"]:
                a, b = r["players"][k]
                a, b = a or {}, b or {}
                same = None if r["after"] else a == b
                tds = "".join(f"<td class='n{'' if a.get(m) == b.get(m) else ' bad'}'>{fmt(a.get(m))}</td>"
                              f"<td class='n{'' if a.get(m) == b.get(m) else ' bad'}'>{fmt(b.get(m))}</td>"
                              for m in SNAP_MEASURES)
                sym = "&ndash;" if same is None else "&#10003;" if same else "&#10007;"
                s.append(f"<tr><td>{clock(r['frame'])}</td>{tds}"
                         f"<td class='{'muted' if same is None else 'ok' if same else 'bad'}'>{sym}</td></tr>")
            s.append("</table></div></details>")
        s.append("</section>")
        sections.append("".join(s))

    doc = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>1.17 against Hyperion</title>
<style>
:root {{ --bg:#fbfbfa; --fg:#1d1f21; --muted:#6b7075; --line:#e2e2de; --ok:#2a8a4f; --bad:#d23c3c; --card:#fff; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#16181b; --fg:#e6e6e3; --muted:#9aa0a6; --line:#2c3035; --card:#1d2024; --ok:#4cc27a; --bad:#f06262; }} }}
:root[data-theme="dark"] {{ --bg:#16181b; --fg:#e6e6e3; --muted:#9aa0a6; --line:#2c3035; --card:#1d2024; --ok:#4cc27a; --bad:#f06262; }}
body {{ background:var(--bg); color:var(--fg); font:14px/1.5 system-ui,sans-serif; margin:0; padding:24px 16px; }}
main {{ max-width:980px; margin:0 auto; }}
h1 {{ font-size:22px; margin:0 0 4px; }} h2 {{ font-size:17px; margin:12px 0 4px; }} h3 {{ font-size:14px; margin:18px 0 6px; }}
.muted {{ color:var(--muted); }} p {{ margin:4px 0 8px; }}
.verdict {{ font-weight:600; }} .ok {{ color:var(--ok); }} .bad {{ color:var(--bad); }}
table {{ border-collapse:collapse; width:100%; font-size:13px; }}
th,td {{ padding:4px 8px; border-bottom:1px solid var(--line); text-align:left; white-space:nowrap; }}
.n {{ text-align:right; font-variant-numeric:tabular-nums; }} code {{ font-size:12px; }}
.wrap {{ overflow-x:auto; }} details {{ margin:10px 0; }} summary {{ cursor:pointer; }}
section {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:4px 16px 12px; margin-top:16px; }}
svg.chart {{ width:100%; height:auto; }} .grid {{ stroke:var(--line); }} .ax {{ fill:var(--muted); font-size:10px; }}
</style></head><body><main>
<h1>1.17 against Hyperion</h1>
<p class="muted">{e(out_dir.name)}. Each replay played on the original 1.17 and on Hyperion, the same observer in both.
A snapshot's state hash covers its frame's random number state and count, the unit count, every live unit's fields,
the stats (resources, score counters, counts per unit type, research and upgrades) and each player's numbers; its running
hash covers every frame from the first one to it.</p>
<div class="wrap"><table><tr><th>Replay</th><th>Map</th><th class="n">Length</th><th>Against 1.17</th></tr>{summary}</table></div>
{''.join(sections)}
</main></body></html>"""
    path = out_dir / "compare.html"
    path.write_text(doc, encoding="utf-8")
    return path


def fmt(v) -> str:
    return f"{v:,}" if isinstance(v, int) else ("-" if v is None else html.escape(str(v)))


# ---------------------------------------------------------------------------

def gather(paths: list[str]) -> list[Path]:
    reps = []
    for s in paths:
        p = Path(s)
        if p.is_dir():
            reps += sorted(q for q in p.rglob("*") if q.suffix.lower() == ".rep")
        elif p.exists():
            reps.append(p)
        else:
            sys.exit(f"no such replay or folder: {s}")
    return reps


def main() -> int:
    ap = argparse.ArgumentParser(description="Play replays on 1.17 and on Hyperion and show the two games match.")
    ap.add_argument("replays", nargs="+", help="replays, or folders of them (searched with their subfolders)")
    ap.add_argument("--build", default=DEFAULT_BUILD, choices=sorted(rt.ENGINE_BUILDS),
                    help=f"Hyperion's build to hold to 1.17 (default {DEFAULT_BUILD})")
    ap.add_argument("--every", type=parse_every, default=parse_every("1m"),
                    help="the snapshots' period: 10s, 1m, 360f (default 1m)")
    ap.add_argument("--brief", action="store_true", help="the hashes only, without each player's numbers")
    ap.add_argument("--open", action="store_true", help="open compare.html when it is written")
    rt.add_run_args(ap)  # the suite's run settings, so 1.17's kept runs are found
    ap.set_defaults(updates=32)  # as the suite has it
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    ink = Ink(sys.stdout.isatty() and not os.environ.get("NO_COLOR"))

    reps = gather(args.replays)
    if not reps:
        sys.exit("no replays found")
    out = Path(args.out) if args.out else rt.RUNS / ("compare_" + _dt.datetime.now().strftime("%Y%m%d_%H%M%S"))
    sizes = repfile.command_sizes(rt.oracle_exe())
    print(f"{len(reps)} replay{'s' if len(reps) != 1 else ''}, 1.17 and Hyperion ({args.build}) -> {out}")
    results, skipped, used = [], [], set()
    for rep in reps:
        try:
            hdr = repfile.header(rep)
        except Exception as ex:  # anything unreadable: a Remastered replay, another file
            skipped.append((rep, f"not a replay 1.17 can read ({ex}); StarCraft: Remastered's replays, "
                                 "from 1.21 on, are another format"))
            continue
        if hdr["magic"] == "hxRS":
            skipped.append((rep, "a Hyperion replay (a map or players past 1.17's), which 1.17 cannot play; "
                                 "replaytest.py hxsuite holds those to their own recorded hashes"))
            continue
        name = rep.stem
        n = 2
        while name.lower() in used:  # two replays of one name in different folders
            name = f"{rep.stem}_{n}"
            n += 1
        used.add(name.lower())
        group = out / name
        # 1.17 once, then Hyperion on each kind of files --data names (Remastered's alone and the
        # MPQs by default), each held to that one run
        for a in rt.each_source(args, "oracle"):
            rt.run_one("oracle", rep, group, a)
        for a in rt.each_source(args, args.build):
            rt.run_one(args.build, rep, group, a)
            results.append(compare_replay(rep, group, a.tag, args.every, hdr, sizes))
            print_replay(results[-1], ink, args.brief)
            write_csv(results[-1])

    for rep, why in skipped:
        print(f"\n{rep.name}: skipped, {why}")
    if results:
        rt.write_report(out)  # the suite's own report too, with its per-field detail
        path = page(results, out, args.build)
        counts = {k: sum(c["status"] == k for c in results) for k in
                  ("identical", "identical_through", "differs", "not_compared")}
        words = [f"{counts['identical']} identical to the end"]
        words += [f"{counts['identical_through']} identical up to where 1.17 loses commands"] * bool(
            counts["identical_through"])
        words += [f"{counts['differs']} differ"] * bool(counts["differs"])
        words += [f"{counts['not_compared']} 1.17 could not play"] * bool(counts["not_compared"])
        words += [f"{len(skipped)} skipped"] * bool(skipped)
        print()
        print(ink.bold(f"{len(results) + len(skipped)} replays: " + ", ".join(words)))
        print(f"page: {path.as_uri()}")
        if args.open:
            if sys.platform == "win32":
                os.startfile(path)
            else:
                import webbrowser
                webbrowser.open(path.as_uri())
    return 0 if results and all(c["status"].startswith("identical") for c in results) and not skipped else 1


if __name__ == "__main__":
    sys.exit(main())
