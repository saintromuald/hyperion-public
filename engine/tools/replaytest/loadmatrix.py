"""Saves at one frame of a replay, loaded by every build.

    python tools/replaytest/loadmatrix.py --replay <file> --frame N --until M --out <dir>
                                          [--builds oracle,hx64]

Each build saves the replay's game at frame N. Then each engine build starts from
each save, the oracle's and every engine build's, and the oracle from its own (a
Hyperion save need not load in 1.17), and plays on to frame M with no input and the
game's seed pinned in place of the clock's. Every run is compared with the oracle's, frame by frame:
the random state, the unit count, h_sem and h_stat, and h_raw between 32-bit builds.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
RT = ENGINE / "tools" / "replaytest" / "replaytest.py"
X64 = {"hx64"}


def run(args):
    print("  " + " ".join(args[2:]))
    r = subprocess.run([sys.executable, str(RT)] + args[1:], cwd=ENGINE, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-2000:] + r.stderr[-2000:])
    return r


def frames(csv):
    out = {}
    for line in open(csv):
        if line.startswith("F,"):
            p = line.strip().split(",")
            out[int(p[1])] = p[2:]
    return out


def compare(ref, got, raw):
    """-> (frames compared, the first difference or None)."""
    names = ["rand", "count", "units", "h_sem", "h_raw", "h_stat"]
    n = 0
    for fr in sorted(ref):
        if fr not in got:
            return n, (fr, "missing", "", "")
        n += 1
        for i, name in enumerate(names):
            if name == "h_raw" and not raw:
                continue
            if ref[fr][i] != got[fr][i]:
                return n, (fr, name, ref[fr][i], got[fr][i])
    return n, None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--replay", required=True)
    p.add_argument("--frame", type=int, required=True)
    p.add_argument("--until", type=int, required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--builds", default="oracle,hx64")
    p.add_argument("--data", default="mpq", help="the one kind of files the engine builds read (replaytest.py --data)")
    args = p.parse_args()
    builds = args.builds.split(",")
    out = Path(args.out).resolve()
    stem = Path(args.replay).stem
    common = ["--updates", "32", "--data", args.data]

    print("saving at frame %d" % args.frame)
    run(["x", "suite", "--builds", ",".join(builds), "--replays", args.replay, "--save-frame", str(args.frame),
         "--max-frames", str(args.frame + 1), "--out", str(out / "save")] + common)
    saves = {}
    for b in builds:
        found = sorted((out / "save" / stem).glob(b + ".*"))
        found = [f for f in found if f.suffix in (".mlx", ".snx", ".mlt", ".sng")]
        if found:
            saves[b] = found[0]
    print("saves: " + ", ".join("%s %d bytes" % (b, f.stat().st_size) for b, f in saves.items()))

    print("loading")
    runs = []
    for b in builds:
        for s in saves:
            if b == "oracle" and s != "oracle":
                continue
            tag = "%s_from_%s" % (b, s)
            run(["x", "run", "--build", b, "--replay", args.replay, "--load", str(saves[s]), "--tag", tag,
                 "--max-frames", str(args.until), "--out", str(out / "load")] + common)
            runs.append((b, s, tag))

    ref = frames(out / "load" / stem / "oracle_from_oracle.csv") if "oracle" in saves else None
    ok = True
    for b, s, tag in runs:
        csv = out / "load" / stem / (tag + ".csv")
        if not csv.exists():
            print("%-24s no log" % tag)
            ok = False
            continue
        got = frames(csv)
        if ref is None or tag == "oracle_from_oracle":
            print("%-24s %d frames, to %d" % (tag, len(got), max(got) if got else 0))
            continue
        n, diff = compare(ref, got, b not in X64 and "oracle" not in X64)
        ok = ok and diff is None
        print("%-24s %5d frames compared with the oracle's: %s"
              % (tag, n, "identical" if diff is None else "first difference at %d, %s %s against %s" % diff))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
