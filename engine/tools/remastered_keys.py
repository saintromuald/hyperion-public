"""Writes src117/port/data/port_remastered_keys.h: 1.17's gluAll.tbl, network.tbl, tips.tbl,
gluHist.tbl and help_txt.tbl, string by string in 1.17's order, as the keys StarCraft: Remastered's rez\\<table>.xml files the same
strings under (milestone B).

    python tools/remastered_keys.py [--check] [--install <Remastered folder>]

Remastered keeps those tables only as locale files of keyed strings, not in 1.17's order,
so a Remastered install gives Hyperion the text and this header gives it 1.17's order
(port_remastered.cpp builds the .tbl from both). The keys are found by lining up 1.17's
strings (read from 1.17's MPQs, engine/testrun) with the XML's in order, comparing them with
line ends, tabs, trailing spaces and case set aside; the spaces 1.17 has at a string's end and
Remastered dropped are kept with its key. Strings still unmatched between two matched ones
take the keys left between those, in order, when there are as many (gluHist's titles
Remastered corrected: "Gettysburg", and a plain apostrophe for 1.17's 0x92). A string
Remastered has no key for is
Hyperion's own text, written below (OWN). Only keys and that text are written: no string of
the game's goes into the header.

--check reports, for each table, how many strings the header gives exactly as 1.17 has them
and lists the rest, without writing.
"""
import argparse
import difflib
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ENGINE)
sys.path.insert(0, os.path.join(REPO, "tools", "assets"))
import scasc  # noqa: E402
import scmpq  # noqa: E402

OUT = os.path.join(ENGINE, "src117", "port", "data", "port_remastered_keys.h")
TABLES = ("gluAll", "network", "tips", "gluHist", "help_txt")

# Hyperion's own text for a string 1.17 has and Remastered does not, or has naming a key Hyperion
# does not use (Remastered's Help Menu is on F11, Hyperion's on F1 as in 1.17): the table,
# 1.17's index.
OWN = {
    ("gluAll", 102): "Requirements: TCP/IP networking, with every computer on the same local network.",
    ("gluAll", 177): "Replay",
    ("tips", 79): "Press F5 to switch between the classic graphics and HD.",
    ("help_txt", 7): "F1 - Help Menu",
}


def tbl_strings(data):
    n = int.from_bytes(data[0:2], "little")
    out = []
    for i in range(n):
        o = int.from_bytes(data[2 + 2 * i:4 + 2 * i], "little")
        out.append(data[o:data.index(b"\0", o)].decode("latin-1"))
    return out


ENTITY = re.compile(r"&(#x[0-9A-Fa-f]+|#[0-9]+|lt|gt|amp|quot|apos);")


def unescape(s):
    def one(m):
        e = m.group(1)
        if e.startswith("#x"):
            return chr(int(e[2:], 16))
        if e.startswith("#"):
            return chr(int(e[1:]))
        return {"lt": "<", "gt": ">", "amp": "&", "quot": '"', "apos": "'"}[e]
    return ENTITY.sub(one, s)


def xml_strings(data):
    """The XML's (key, value) pairs in its order, each value as port_remastered.cpp reads it:
    entities resolved and CR LF taken as LF."""
    text = data.decode("utf-8-sig")
    out = []
    for m in re.finditer(r"<id>(.*?)</id>\s*<value>(.*?)</value>", text, re.S):
        out.append((m.group(1), unescape(m.group(2)).replace("\r\n", "\n")))
    return out


def loose(s):
    return re.sub(r"\s+", " ", s).strip().lower()


def keys_for(want, have):
    """1.17's index -> the XML's key, by lining the two up in order, loosely compared."""
    a = [loose(s) for s in want]
    b = [loose(v) for k, v in have]
    keys = [None] * len(want)
    for i, j, n in difflib.SequenceMatcher(None, a, b, autojunk=False).get_matching_blocks():
        for d in range(n):
            keys[i + d] = have[j + d][0]
    # a string the order missed, matched by its text alone when only one key has it
    for i, k in enumerate(keys):
        if k is None:
            hits = [kk for kk, v in have if loose(v) == a[i]]
            if len(hits) == 1:
                keys[i] = hits[0]
    # a run of strings between two matched ones, where as many keys are left between theirs
    where = {k: j for j, (k, v) in enumerate(have)}
    taken = set(k for k in keys if k)
    i = 1
    while i < len(want):
        if keys[i] is None and keys[i - 1]:
            end = i
            while end < len(want) and keys[end] is None:
                end += 1
            if end < len(want):
                lo, hi = where[keys[i - 1]], where[keys[end]]
                gap = [k for k, v in have[lo + 1:hi]]
                if len(gap) == end - i and not taken.intersection(gap):
                    keys[i:end] = gap
                    taken.update(gap)
            i = end
        i += 1
    return keys


def c_string(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif 32 <= o < 127:
            out.append(ch)
        else:
            out.append("\\x%02X\"\"" % o)
    return '"' + "".join(out) + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--install")
    a = ap.parse_args()
    mpq = scmpq.MpqChain()
    casc = scasc.CascStorage(a.install)
    lines = ["// port_remastered_keys.h -- written by tools/remastered_keys.py; do not edit.",
             "//",
             "// 1.17's gluAll.tbl, network.tbl, tips.tbl, gluHist.tbl and help_txt.tbl in 1.17's order: for each string, the key a",
             "// StarCraft: Remastered install's rez\\<table>.xml files it under, or, where Remastered has",
             "// none, Hyperion's own text (port_remastered.cpp builds each .tbl from these).",
             "",
             "struct PortRemasteredKey {",
             "  const char *pszKey;  // the XML's <id>, or NULL",
             "  const char *pszOwn;  // Hyperion's text where pszKey is NULL",
             "  const char *pszTail; // what 1.17 has after Remastered's text (the spaces it drops), or NULL",
             "};",
             ""]
    bad = 0
    for name in TABLES:
        want = tbl_strings(mpq.read("rez\\%s.tbl" % name))
        have = xml_strings(casc.read("SD/rez/%s.xml" % name))
        values = dict(have)
        keys = keys_for(want, have)
        exact = 0
        array = "".join(w[0].upper() + w[1:] for w in name.split("_"))
        lines.append("static const PortRemasteredKey kRemastered%s[%d] = {" % (array, len(want)))
        for i, k in enumerate(keys):
            own = OWN.get((name, i))
            if own is not None:
                lines.append("    {NULL, %s, NULL},  // %d" % (c_string(own), i))
                continue
            if k is None:
                print("%s %d: no key and no text of Hyperion's for %r" % (name, i, want[i][:70]))
                bad += 1
                lines.append("    {NULL, \"\", NULL},  // %d" % i)
                continue
            tail = None
            if values[k] != want[i] and values[k] == want[i].rstrip(" "):
                tail = want[i][len(values[k]):]
            lines.append("    {%s, NULL, %s},  // %d" % (c_string(k), c_string(tail) if tail else "NULL", i))
            if values[k] + (tail or "") == want[i]:
                exact += 1
            elif a.check:
                print("  %s %d %s: %r -> %r" % (name, i, k, want[i][:60], values[k][:60]))
        lines.append("};")
        lines.append("")
        print("%s: %d strings, %d keyed, %d exactly as 1.17's, %d Hyperion's own" % (
            name, len(want), sum(1 for k in keys if k), exact, sum(1 for t, _ in OWN if t == name)))
    if bad:
        sys.exit("remastered_keys: %d strings with neither a key nor Hyperion's text" % bad)
    if a.check:
        return
    with open(OUT, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
