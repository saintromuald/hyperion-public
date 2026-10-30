"""Put OC_VA around every literal address of 1.17's data the engine compiles.

The engine keeps 1.17's .rdata and .data as a block of its own (src117/h/oc_data.h),
so a global the source reaches through a literal address,
`reinterpret_cast<T *>(0x6662B8u)` or `(T *)0x4EEB80`, has to reach it at its place in
the block instead: `reinterpret_cast<T *>(OC_VA(0x6662B8u))`. This finds each literal in
0x4EB000..0x6DF000 that is the operand of a pointer cast, outside comments and strings,
and wraps it. Other literals in that range are constants, not addresses (a save tag, an
array's size worked out from two addresses), and are left alone. An address already
wrapped is left alone too, so a rerun changes nothing.

    python tools/data_block.py            rewrite src117's lang, h, port and gen/globals117.h
    python tools/data_block.py --check    report what a rewrite would change; 2 if anything

tools/regen_globals.py runs it over the generated src117/gen/globals117.h, which also gets
the include of oc_data.h.
"""
import argparse
import glob
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LO, HI = 0x4EB000, 0x6DF000
LIT = re.compile(r"0x([0-9A-Fa-f]{6,8})u?\b")
CSTYLE = re.compile(r"\(\s*(?:const\s+)?[\w:]+(?:\s+const)?\s*\*+\s*\)\s*$")
INCLUDE = '#include "oc_data.h"  // Hyperion: OC_VA'


def code_mask(text):
    """True for each character outside comments, string and character literals."""
    mask = [True] * len(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j += 1
        else:
            i += 1
            continue
        for k in range(i, min(j, n)):
            mask[k] = False
        i = j
    return mask


def is_cast_operand(text, start):
    """Is the literal at start the operand of reinterpret_cast<...>( or of (T *)?"""
    i = start - 1
    while i >= 0 and text[i] in " \t":
        i -= 1
    if i >= 0 and text[i] == "(":
        j = i - 1
        while j >= 0 and text[j] in " \t":
            j -= 1
        if j >= 0 and text[j] == ">":
            depth, k = 0, j
            while k >= 0:
                if text[k] == ">":
                    depth += 1
                elif text[k] == "<":
                    depth -= 1
                    if depth == 0:
                        break
                k -= 1
            return k > 0 and text[:k].rstrip().endswith("reinterpret_cast")
        return False
    # a C-style cast, directly before the literal
    line_start = text.rfind("\n", 0, start) + 1
    return bool(CSTYLE.search(text[line_start:start]))


def rewrite(text):
    """-> (new text, number of literals wrapped)."""
    mask = code_mask(text)
    out, last, n = [], 0, 0
    for m in LIT.finditer(text):
        if not mask[m.start()] or not (LO <= int(m.group(1), 16) < HI):
            continue
        if text[:m.start()].rstrip().endswith("OC_VA("):
            continue
        if not is_cast_operand(text, m.start()):
            continue
        out.append(text[last:m.start()])
        out.append("OC_VA(%s)" % m.group(0))
        last = m.end()
        n += 1
    out.append(text[last:])
    return "".join(out), n


def add_include(text):
    """The include of oc_data.h: after a header's #pragma once, else before the
    file's first #include, else at the top."""
    if "oc_data.h" in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    anchor = "#pragma once" + nl
    if anchor in text:
        return text.replace(anchor, anchor + INCLUDE + nl, 1)
    m = re.search(r"^#include\b", text, re.M)
    at = m.start() if m else 0
    return text[:at] + INCLUDE + nl + text[at:]


def process(path, write=True, include=False):
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8", errors="surrogateescape")
    new, n = rewrite(text)
    if include or "OC_VA(" in new:
        new = add_include(new)
    if new != text and write:
        with open(path, "wb") as f:
            f.write(new.encode("utf-8", errors="surrogateescape"))
    return n, new != text


def sources():
    src = os.path.join(ENGINE, "src117")
    files = (glob.glob(os.path.join(src, "lang", "*.cpp"))
             + glob.glob(os.path.join(src, "h", "**", "*.h*"), recursive=True)
             + glob.glob(os.path.join(src, "port", "*", "*.cpp"))
             + glob.glob(os.path.join(src, "port", "*", "*.h")))
    return sorted(f for f in files if os.path.basename(f) != "oc_data.h")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    total = changed = 0
    globals_h = os.path.join(ENGINE, "src117", "gen", "globals117.h")
    for f in sources() + [globals_h]:
        n, diff = process(f, write=not a.check, include=f == globals_h)
        if diff:
            changed += 1
            total += n
            print("  %4d  %s" % (n, os.path.relpath(f, ENGINE)))
    print("data_block: %d literals in %d files %s" % (total, changed, "to wrap" if a.check else "wrapped"))
    return 2 if a.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
