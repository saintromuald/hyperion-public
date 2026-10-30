"""List the code that reads or writes a map position through a pointer of another type
(stage E; src117/h/pos.h).

1.17 moves a point as one DWORD, `*(DWORD *)&wOrderTargetX`, and reads a pair of fields
as a point16_t or an order_target_t by casting the first one's address. Once a field is
an spos_t or upos_t (4 bytes on x64) such a cast still compiles but reads x alone, or x
and half of y. This finds them: every cast of the address of a field the headers declare
as a position type, and every memcpy or memcmp that takes one. Each site it lists is to
be rewritten through the fields, PosPack or the point types; it prints nothing when there
are none and exits 1 when there are.

Usage: python tools/check_packed.py [--names]
"""
import collections
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src117")
POS_TYPES = ("spos_t", "upos_t", "mpoint_t", "mrect_t", "order_target_t")
DIRS = ("lang", "port", "h")


def sources():
    for d in DIRS:
        for root, dirs, names in os.walk(os.path.join(ROOT, d)):
            dirs.sort()
            for name in sorted(names):
                if name.endswith((".cpp", ".h")):
                    yield os.path.relpath(os.path.join(root, name), ROOT)


def strip_comments(text):
    """The text with comments blanked, lines kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def position_names(texts):
    """The members and globals declared with one of the position types."""
    decl = re.compile(r"\b(?:%s)\s+(\w+)\s*(?:\[[^\]]*\]\s*)?[;,=]" % "|".join(POS_TYPES))
    names = set()
    for path, text in texts.items():
        if os.path.basename(path) != "pos.h":   # the types' own header: their members only
            names.update(decl.findall(text))
    return names | {"x", "y", "left", "top", "right", "bottom"}


# Types that hold no map position of ours: a name declared as one of these is a local or a
# parameter that shares a position's name (a point16_t pt, a RECT's left) and is left alone.
NOT_POSITIONS = ("point16_t", "rect16_t", "RECT", "POINT", "SIZE", "short", "WORD", "int",
                 "DWORD", "BYTE", "char", "long")


def struct_members(texts):
    """{struct or class name: the names of its members that hold a position in place}:
    a member of a position type, or of a struct that holds one in place, not a pointer
    to one (an anonymous union's members are the class's)."""
    head = re.compile(r"\b(?:struct|class)\s+(\w+)[^;{()]*\{")
    decl = re.compile(r"\b(\w+)\s*(\*?)\s*&?\s*(\w+)\s*(?:\[[^\]]*\]\s*)?[;,=]")
    members = {}  # struct -> {member: (type, through a pointer)}
    for text in texts.values():
        for m in head.finditer(text):
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            for t, ptr, name in decl.findall(text[m.end():i]):
                members.setdefault(m.group(1), {})[name] = (t, bool(ptr))
    holds = {t: any(mt in POS_TYPES and not p for mt, p in ms.values()) for t, ms in members.items()}
    changed = True
    while changed:
        changed = False
        for t, ms in members.items():
            if not holds[t] and any(holds.get(mt) and not p for mt, p in ms.values()):
                holds[t] = changed = True
    return {t: {n for n, (mt, p) in ms.items() if not p and (mt in POS_TYPES or holds.get(mt))}
            for t, ms in members.items()}


def of_another_type(lines, at, cast, name, holds):
    """True when the address the cast takes is not a position: it starts at a local or a
    parameter whose nearest declaration above is of another type. A name taken alone is a
    position when it is declared as one or not declared above (a member); one reached
    through another (a.b) is unless the first is declared as a type in NOT_POSITIONS, or
    as a struct that holds no position."""
    m = re.search(r"&\s*\(?\s*(?:this\s*->\s*)?(\w+)\s*(?:\[[^\]]*\]\s*)?(?:(?:\.|->)\s*(\w+))?", cast)
    if not m:
        return False
    root, first = m.group(1), m.group(2)
    decl = re.compile(r"\b(\w+)\s*[*&]?\s*\b%s\b\s*(?:\[[^\]]*\]\s*)?[;=,)\[{]" % re.escape(root))
    for i in range(at, max(-1, at - 400), -1):
        for d in decl.finditer(lines[i]):
            t = d.group(1)
            if t in ("return", "sizeof", "case", "goto", "delete", "struct", "class"):
                continue
            if root == name:
                return t not in POS_TYPES
            return t in NOT_POSITIONS or (t in holds and first not in holds[t])
    return False


def main():
    texts = {p: strip_comments(open(os.path.join(ROOT, p), encoding="utf-8", errors="replace").read())
             for p in sources()}
    names = position_names(texts)
    holds = struct_members(texts)
    if "--names" in sys.argv:
        print(" ".join(sorted(names)))
        return 0
    # the name ends the address taken: not followed by a member, a call or arithmetic; the
    # way to it may hold casts, `((const ep_glob_t *)epGlob)->aRegions[i].nMinX`
    field = (r"&\s*\(?\s*[\w.\->\[\]()\s*]*?\b(%s)\b(?:\s*\[[^\]]*\])?(?!\s*(?:\.|->|\(|[+\-*/%%]))"
             % "|".join(sorted(names)))
    casts = [
        re.compile(r"reinterpret_cast\s*<[^>]*\*\s*>\s*\(\s*" + field),
        # an address handed on as an integer, which the callee casts back to 1.17's type
        re.compile(r"reinterpret_cast\s*<\s*(?:intptr_t|uintptr_t|INT_PTR|UINT_PTR|DWORD_PTR|DWORD|int)\s*>\s*\(\s*"
                   + field),
        re.compile(r"\(\s*(?:const\s+)?\w+(?:\s+const)?\s*\*\s*\)\s*\(?\s*" + field),
        re.compile(r"\(\s*(?:intptr_t|uintptr_t|INT_PTR|UINT_PTR|DWORD_PTR|DWORD|int|long)\s*\)\s*\(?\s*" + field),
        re.compile(r"\b(?:memcpy|memcmp|memmove|memset)\s*\(\s*" + field),
        re.compile(r"\b(?:memcpy|memcmp|memmove)\s*\([^;]*?,\s*" + field),
    ]
    found = 0
    for path, text in texts.items():
        lines = text.split("\n")
        for no, line in enumerate(lines, 1):
            for c in casts:
                m = c.search(line)
                # the address flagged is the one whose & is nearest before the name
                amp = line.rfind("&", 0, m.start(1)) if m else -1
                if m and not of_another_type(lines, no - 1, line[amp:], m.group(1), holds):
                    found += 1
                    print("%s:%d: %s" % (path.replace("\\", "/"), no, line.strip()))
                    break
    found += untyped_addresses(texts)
    if found:
        print("check_packed: %d sites read a position through another type" % found)
    return 1 if found else 0


UNTYPED = re.compile(r"\bvoid\s*\*|\b(?:intptr_t|uintptr_t|INT_PTR|UINT_PTR|DWORD_PTR|DWORD|int|long)\s*\w*\s*$")


def split_args(s):
    """The arguments of a call whose text starts after its '(' -> (list, end index)."""
    args, depth, cur = [], 0, ""
    for i, ch in enumerate(s):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                args.append(cur)
                return args, i
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(cur)
            cur = ""
            continue
        cur += ch
    return args, len(s)


def untyped_addresses(texts):
    """The address of a local map point or box (mpoint_t, mrect_t) passed where a
    declaration of the callee takes a void * or an integer: it compiles silently, and a
    callee that casts it back to 1.17's point or box reads the wrong bytes."""
    decls = collections.defaultdict(list)  # function name -> [parameter type strings]
    head = re.compile(r"\b(\w+)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*(?:const\s*)?[;{]")
    for text in texts.values():
        for m in head.finditer(text):
            decls[m.group(1)].append(split_args(m.group(2) + ")")[0])
    local = re.compile(r"^\s*(?:const\s+)?(?:mpoint_t|mrect_t)\s+(\w+)\b[^()]*;")
    call = re.compile(r"\b(\w+)\s*\(")
    found = 0
    for path, text in texts.items():
        lines = text.split("\n")
        for i, line in enumerate(lines):
            m = local.match(line)
            if not m:
                continue
            addr = re.compile(r"^\s*&\s*%s\s*$" % re.escape(m.group(1)))
            for j in range(i + 1, min(len(lines), i + 400)):
                if lines[j].startswith("}"):
                    break
                for c in call.finditer(lines[j]):
                    args, _ = split_args(lines[j][c.end():])
                    for k, a in enumerate(args):
                        if not addr.match(a):
                            continue
                        params = [p[k] for p in decls.get(c.group(1), []) if len(p) > k]
                        if any(UNTYPED.search(p.strip()) for p in params):
                            found += 1
                            print("%s:%d: %s  (the address of %s, a map type, goes to an untyped parameter)"
                                  % (path.replace("\\", "/"), j + 1, lines[j].strip(), m.group(1)))
    return found


if __name__ == "__main__":
    sys.exit(main())
