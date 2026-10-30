"""Spell the engine's C++ `long` as LONG and `unsigned long` as ULONG.

    python tools/fixed_widths.py [--check] [<file or folder> ...]   default: src117's h, lang, port, gen

1.17's code was written where `long` is 32 bits, and the engine keeps that width in its
structs, its copy of 1.17's data, saves and replays. Windows x64 keeps `long` at 32 bits;
Linux and Mac make it 64. h/platform.h defines LONG and ULONG as `long` and `unsigned long`
on Windows, so this changes nothing there (tools/objsig.py shows the code is the same), and
as 32-bit types elsewhere.

Only code is changed, never a comment, a string or a character literal. `long long` and
`long double` are left as they are. `signed long` and `long int` become LONG, `unsigned
long` and `unsigned long int` ULONG. --check lists what would change and exits 1 if
anything would; run it on code that comes in from upstream. A line
that names a C library function of `long` (ftell, strtol and the like) keeps its `long`.
"""
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ENGINE, "src117")
# h/platform.h defines LONG and ULONG from the native types itself
# port_iscript.cpp parses text with strtol, whose `long` is the C library's.
SKIP = {os.path.normcase(os.path.join(SRC, "h", "platform.h")),
        os.path.normcase(os.path.join(SRC, "port", "data", "port_iscript.cpp"))}
# A line that names one of the C library's functions of `long` keeps its `long`: the game's
# TUs restate some of their declarations (ftell, fseek, strtoul), which off Windows have to
# match the C library's own, and a value from one of them is the library's width.
CRT_LONG = re.compile(r"\b(?:strtol|strtoul|wcstol|wcstoul|atol|labs|ftell|fseek|lround|lroundf"
                      r"|lrint|lrintf|lseek|_lseek)\s*\(")

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SPACE = re.compile(r"(?:[ \t]|\\\r?\n)+")


def segments(text):
    """Split C++ text into (is_code, piece), with comments, strings and character literals
    as the pieces that are not code."""
    i, n, start = 0, len(text), 0
    out = []
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            while j < n and text[j - 1] == "\\":  # a line comment continued
                k = text.find("\n", j + 1)
                j = n if k < 0 else k
        elif c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            d = text.find("(", i + 2)
            delim = ")" + text[i + 2:d] + '"'
            j = text.find(delim, d)
            j = n if j < 0 else j + len(delim)
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
        else:
            i += 1
            continue
        if start < i:
            out.append((True, text[start:i]))
        out.append((False, text[i:j]))
        i = start = j
    if start < n:
        out.append((True, text[start:]))
    return out


def convert_code(code):
    """Rewrite the long types in a piece of code; -> (new code, count)."""
    toks = []  # (start, end, word) of each identifier
    for m in IDENT.finditer(code):
        # skip identifiers that are the tail of a number (0x1L) or part of a literal prefix
        if m.start() and (code[m.start() - 1].isdigit() or code[m.start() - 1] == "."):
            continue
        toks.append((m.start(), m.end(), m.group()))
    edits = []
    k = 0
    while k < len(toks):
        s, e, w = toks[k]

        def nxt(idx):
            """The next token if only blanks lie between, else None."""
            if idx + 1 < len(toks) and SPACE.fullmatch(code[toks[idx][1]:toks[idx + 1][0]] or " "):
                return toks[idx + 1]
            return None
        if w in ("unsigned", "signed"):
            t = nxt(k)
            if t and t[2] == "long":
                t2 = nxt(k + 1)
                if t2 and t2[2] in ("long", "double"):
                    k += 3
                    continue
                end, skip = t[1], 2
                if t2 and t2[2] == "int":
                    end, skip = t2[1], 3
                edits.append((s, end, "ULONG" if w == "unsigned" else "LONG"))
                k += skip
                continue
        elif w == "long":
            t = nxt(k)
            if t and t[2] in ("long", "double"):
                k += 2
                continue
            # `long unsigned` / `long signed`, rare spellings
            if t and t[2] in ("unsigned", "signed"):
                t2 = nxt(k + 1)
                end, skip = t[1], 2
                if t2 and t2[2] == "int":
                    end, skip = t2[1], 3
                edits.append((s, end, "ULONG" if t[2] == "unsigned" else "LONG"))
                k += skip
                continue
            end, skip = e, 1
            if t and t[2] == "int":
                end, skip = t[1], 2
            edits.append((s, end, "LONG"))
            k += skip
            continue
        k += 1
    for s, e, r in reversed(edits):
        code = code[:s] + r + code[e:]
    return code, len(edits)


def convert(text):
    out, total = [], 0
    for is_code, piece in segments(text):
        if is_code:
            piece, n = convert_code(piece)
            total += n
        out.append(piece)
    new = "".join(out)
    if new == text:
        return text, 0
    old_lines, new_lines = text.split("\n"), new.split("\n")
    for i, line in enumerate(old_lines):
        if new_lines[i] != line and CRT_LONG.search(line):
            new_lines[i] = line
            total -= 1  # approximately: such a line rarely holds two
    new = "\n".join(new_lines)
    return new, (total if new != text else 0)


def files(args):
    roots = args or [os.path.join(SRC, d) for d in ("h", "lang", "port", "gen")]
    for r in roots:
        if os.path.isfile(r):
            yield r
            continue
        for d, _, fs in os.walk(r):
            for f in sorted(fs):
                if f.endswith((".h", ".cpp", ".inl")):
                    yield os.path.join(d, f)


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    changed = 0
    for p in files(args):
        if os.path.normcase(os.path.abspath(p)) in SKIP:
            continue
        raw = open(p, "rb").read()
        text = raw.decode("latin-1")  # byte for byte, whatever the file's encoding
        new, n = convert(text)
        if n:
            changed += 1
            print("%4d  %s" % (n, os.path.relpath(p, ENGINE)))
            if not check:
                open(p, "wb").write(new.encode("latin-1"))
    print("%d files %s" % (changed, "would change" if check else "changed"))
    return 1 if check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
