"""Compares two saves item by item.

    python tools/porttest/savecompare.py A.snx B.snx [--dir OUT] [--limit N] [--expect]

Runs savedump on both (src117/build/x64/porttest/savedump.exe, built by
build_porttest_x64.bat), then reports each item that differs: a raw item by its
bytes, a chunk by its size and its differing byte ranges. The banner is left out,
since it holds the save's path, and so are a Hyperion save's marker and section
headers, which are listed instead, so that a Hyperion save compares with a 1.17 one.

With --expect, the differences two builds' saves of one game are expected to have
are listed as such and do not count: the map's path in the flat block
(MapFileName), and pointers that 1.17 writes and a load does not read, the path
glob's contour table and its lists, and each AI banker's demand heap, which the
load clears (sai_read_banker_data).
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
SAVEDUMP = ENGINE / "src117" / "build" / "x64" / "porttest" / "savedump.exe"


def dump(save, out, verbose):
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(SAVEDUMP), str(Path(save).resolve()), str(out.resolve())], check=True, stdout=subprocess.DEVNULL)
    items = []
    for line in (out / "items.txt").read_text().splitlines():
        f = line.split()
        if f[0] == "raw":
            # a section's header splits what 1.17 writes as one run
            if items and items[-1][0] == "raw":
                items[-1] = ("raw", items[-1][1] + bytes.fromhex(f[2]))
            else:
                items.append(("raw", bytes.fromhex(f[2])))
        elif f[0] in ("hyperion", "section"):
            if verbose:
                print("  %s: %s" % (Path(save).name, line))
        elif f[0] == "chunk":
            items.append(("chunk", (out / f"{int(f[1]):03d}.bin").read_bytes()))
    return items


def ranges(a, b, limit):
    """The differing byte ranges of two equal-length buffers, merged across gaps of 16 bytes."""
    out, start, last = [], None, None
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            if start is None:
                start = i
            elif i - last > 16:
                out.append((start, last + 1))
                start = i
            last = i
    if start is not None:
        out.append((start, last + 1))
    return out[:limit], len(out)


MAP_FILE_NAME = (0xC4C, 0xC4C + 260)   # MapFileName, 0x52701C, in the flat block
GLOB_CONTOURS = (8 + 0x97A1C, 8 + 0x97A20 + 16)  # after the chunk's 8-byte header
BANKER, BANKERS = 0x4E8, 8
DEMANDS = (0x18, 0x210)                 # AI_Banker::aDemands


def expected(n, size, s, e):
    """Whether bytes s..e of chunk n, of size bytes, differ as expected (--expect)."""
    def inside(lo, hi):
        return lo <= s and e <= hi
    if n == 2:
        return inside(*MAP_FILE_NAME)
    if size >= GLOB_CONTOURS[1]:        # only the path chunk is this large
        return inside(*GLOB_CONTOURS)
    if size == BANKER * BANKERS:
        i = s // BANKER
        return inside(i * BANKER + DEMANDS[0], i * BANKER + DEMANDS[1])
    return False


def main():
    p = argparse.ArgumentParser()
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--dir", help="where to keep the dumps (default: a temporary folder)")
    p.add_argument("--limit", type=int, default=8, help="differing ranges shown per chunk")
    p.add_argument("--sections", action="store_true", help="list a Hyperion save's sections")
    p.add_argument("--expect", action="store_true", help="the known differences between builds do not count")
    args = p.parse_args()
    base = Path(args.dir) if args.dir else Path(tempfile.mkdtemp(prefix="savecmp_"))
    a = dump(args.a, base / "a", args.sections)
    b = dump(args.b, base / "b", args.sections)
    same = True
    if len(a) != len(b):
        print(f"items: {len(a)} against {len(b)}")
        same = False
    n = -1
    nExpected = 0
    for i, ((ka, da), (kb, db)) in enumerate(zip(a, b)):
        if ka == "chunk":
            n += 1
        label = f"item {i} ({ka}" + (f" {n})" if ka == "chunk" else ")")
        if ka != kb or len(da) != len(db):
            print(f"{label}: {ka} {len(da)} against {kb} {len(db)}")
            same = False
            continue
        if da == db:
            continue
        rs, total = ranges(da, db, 1 << 30)
        if args.expect and ka == "chunk":
            known = [r for r in rs if expected(n, len(da), *r)]
            nExpected += len(known)
            rs = [r for r in rs if r not in known]
            if not rs:
                continue
        same = False
        print(f"{label}, {len(da)} bytes: {len(rs)} differing ranges")
        for s, e in rs[:args.limit]:
            print(f"  +{s:#x}..{e:#x}  {da[s:e][:24].hex()}  {db[s:e][:24].hex()}")
    if args.expect:
        print(f"{nExpected} expected differences")
    print("same" if same else "different")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
