"""What a StarCraft: Remastered install gives for each file 1.17 opens.

    python tools/remastered_map.py [--log <hyperion_files.log> ...] [--out <file.json>]

The names come from two places: what Hyperion asked Storm for in runs with HYPERION_FILE_LOG=1
(port_stormfile.cpp), and every name the game can ask for that its data says: the file names
among 1.17's starting values (src117/gen/data117.cpp's pieces), the files the arr tables list
(images.tbl's GRPs, sfxdata.tbl's sounds, portdata.tbl's portraits, mapdata.tbl's campaign
maps). Each is read from 1.17's MPQs (engine/testrun, tools/assets/scmpq.py) and looked for in
the Remastered install (tools/assets/scasc.py, read only): at its own name, under SD/, or by
its last part anywhere with the same bytes. The kinds:

    same      the same bytes at the name or under SD/
    moved     the same bytes under another path
    differs   a file at the name or under SD/ with other bytes
    missing   nothing of that name
    absent    not in 1.17's MPQs either (asked for and not found, as 1.17 does)

Nothing here is written into the repository but the --out file a caller names; the install
and the MPQs are only read.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ENGINE)
sys.path.insert(0, os.path.join(REPO, "tools", "assets"))
import scasc  # noqa: E402
import scmpq  # noqa: E402

DATA117 = os.path.join(ENGINE, "src117", "gen", "data117.cpp")
EXTENSIONS = ("grp", "pcx", "wav", "smk", "bin", "tbl", "dat", "fnt", "chk", "spk", "wpe", "cv5", "vf4",
              "vx4", "vr4", "lo", "loa", "lob", "lod", "lof", "loo", "los", "lou", "lol", "trg", "txt", "pal",
              "cur", "ogg", "dbf", "spr", "mpq", "scm", "scx", "rep")
NAME = re.compile(rb"[A-Za-z0-9_\\/ .\-]{3,120}\.(?:%s)" % "|".join(EXTENSIONS).encode(), re.I)


def static_names():
    """File names among 1.17's starting values."""
    src = open(DATA117, encoding="utf-8").read()
    blob = bytearray()
    for arr in re.findall(r"static const unsigned char k[0-9A-F]{6}\[\] = \{(.*?)\};", src, re.S):
        blob += bytes(int(h, 16) for h in re.findall(r"0x([0-9A-F]{2})", arr))
        blob += b"\0"
    out = set()
    for m in NAME.finditer(bytes(blob)):
        s = m.group(0).decode("latin-1").strip()
        if "\\" in s or "/" in s or s.count(".") == 1:
            out.add(s.replace("/", "\\"))
    return out


def tbl_strings(data):
    n = int.from_bytes(data[0:2], "little")
    offs = [int.from_bytes(data[2 + 2 * i:4 + 2 * i], "little") for i in range(n)]
    out = []
    for o in offs:
        e = data.index(b"\0", o) if b"\0" in data[o:] else len(data)
        out.append(data[o:e].decode("latin-1"))
    return out


def table_names(mpq):
    """The files the arr tables list."""
    out = set()
    for tbl, prefix in (("arr\\images.tbl", "unit\\"), ("arr\\sfxdata.tbl", "sound\\")):
        for s in tbl_strings(mpq.read(tbl)):
            if s:
                out.add(prefix + s)
    for s in tbl_strings(mpq.read("arr\\portdata.tbl")):
        if s:
            for suffix in ("fid00.smk", "fid01.smk", "fid02.smk", "fid03.smk", "tlk00.smk", "tlk01.smk", "tlk02.smk"):
                out.add("portrait\\" + s + suffix)
    for s in tbl_strings(mpq.read("arr\\mapdata.tbl")):
        if s:
            out.add(s + "\\staredit\\scenario.chk")
    return out


def log_names(paths):
    out = {}
    for p in paths:
        for line in open(p, encoding="latin-1"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3:
                out[parts[0]] = parts[2]
    return out


def classify(name, mpq, casc, by_leaf):
    try:
        want = mpq.read(name)
    except FileNotFoundError:
        return "absent", None, None
    except OSError:
        return "absent", None, None
    h = hashlib.sha1(want).hexdigest()
    key = name.replace("\\", "/")
    seen_other = None
    for cand in (key, "SD/" + key):
        if casc.exists(cand):
            got = casc.read(cand)
            if got == want:
                return "same", casc.names()[cand.lower()][0], h
            seen_other = casc.names()[cand.lower()][0]
    leaf = key.rsplit("/", 1)[-1].lower()
    for stored, size in by_leaf.get(leaf, []):
        if size == len(want) and casc.read(stored) == want:
            return "moved", stored, h
    if seen_other:
        return "differs", seen_other, h
    return "missing", None, h


def build_version(root):
    """The install's version, from .build.info's Version column."""
    lines = open(os.path.join(root, ".build.info"), encoding="latin-1").read().splitlines()
    head = [h.split("!")[0] for h in lines[0].split("|")]
    for line in lines[1:]:
        row = dict(zip(head, line.split("|")))
        if row.get("Active") == "1":
            return row.get("Version", "")
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--log", action="append", default=[], help="a hyperion_files.log")
    ap.add_argument("--out", help="write the table as JSON")
    ap.add_argument("--install", help="the Remastered install (else Battle.net's)")
    a = ap.parse_args()
    mpq = scmpq.MpqChain()
    casc = scasc.CascStorage(a.install)
    by_leaf = collections.defaultdict(list)
    for low, (stored, size) in casc.names().items():
        by_leaf[low.rsplit("/", 1)[-1]].append((stored, size))
    logged = log_names(a.log)
    names = {}
    for n in static_names():
        names.setdefault(n.lower(), ["data", n])
    for n in table_names(mpq):
        names.setdefault(n.lower(), ["table", n])
    for n, src in logged.items():
        names[n.lower().replace("/", "\\")] = ["run", n]
    rows = []
    for low, (why, name) in sorted(names.items()):
        kind, where, h = classify(name, mpq, casc, by_leaf)
        rows.append({"name": name, "from": why, "kind": kind, "remastered": where,
                     "found_by_hyperion": logged.get(name)})
    count = collections.Counter(r["kind"] for r in rows)
    print("[remastered_map] %d names (%d from runs): %s" % (len(rows), len(logged),
          ", ".join("%s %d" % kv for kv in sorted(count.items()))))
    for kind in ("differs", "missing"):
        exts = collections.Counter(r["name"].rsplit(".", 1)[-1].lower() for r in rows if r["kind"] == kind)
        print("  %s by kind of file: %s" % (kind, ", ".join("%s %d" % kv for kv in exts.most_common())))
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump({"build": build_version(casc.root), "rows": rows}, f, indent=1)
        print("[remastered_map] wrote", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
