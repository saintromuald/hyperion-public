"""The game's own declarations of C functions against their definitions, at 64 bits
.

    python tools/decl_widths.py

Needs the x64 build (tools/build_engine_x64.py). Many of the game's TUs declare the
functions they call themselves, with 1.17's 32-bit types: Windows' (a window
procedure's `long lParam`, a MSG as a struct of 32-bit fields), since the byte match
kept <windows.h> out of them (src117/h/event_Win.h), and Storm's (a file handle as
an `int`), which the port now defines. A C name is only its name, so the linker
cannot see the difference, and the x64 build would pass a pointer cut to 32 bits,
or a buffer too small for what is written into it. A C++ name carries its argument
types, so the link already catches those.

The compiler is asked, on x64. For each lang TU, a probe includes the TU and records,
for every C function the TU's object calls, the number of its arguments, the size of
each and of its result, and the size of what each pointer argument points at. The
same is recorded from the function's definition: the probe of the engine TU that
defines it (a port or lang TU), or, for Windows and the C runtime, a probe of their
headers. Writes src117/build/decls/report.md and lists every declaration that
differs from its definition: a size, a count, or a pointer argument to a structure
that the declaration gives as `void *` or as a structure of another size (to review:
what the game passes must be that structure; REVIEWED lists those looked at). Exit 0
when nothing is left.

The Windows callbacks the game defines (its window and dialog procedures, timer and
enumeration callbacks) reach Windows through casts, which this does not see; they
were reviewed by hand on 2026-09-29.
"""
import os
import re
import struct
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, "tools"))
import coff  # noqa: E402
import build_engine_x64 as b64  # noqa: E402
from port_files import tu_path  # noqa: E402

SRC = os.path.join(ENGINE, "src117")
X64 = os.path.join(SRC, "build", "x64")
OUT = os.path.join(SRC, "build", "decls")
NARGS = 14

HEAD = r"""
#include <type_traits>
namespace oc_iw {
template <class T, class = void> struct complete { enum { size = 0 }; };
template <class T> struct complete<T, decltype(void(sizeof(T)))> { enum { size = sizeof(T) }; };
// a value's size, with 0x100 added for floating point
template <class T> constexpr unsigned rsz() {
  return std::is_void<T>::value ? 0u
                                : (unsigned)complete<T>::size | (std::is_floating_point<T>::value ? 0x100u : 0u);
}
// what a pointer argument points at: 0 for void, a function or an incomplete type
template <class T> constexpr unsigned psz() {
  typedef typename std::remove_cv<typename std::remove_pointer<T>::type>::type E;
  return std::is_pointer<T>::value && !std::is_void<E>::value && !std::is_function<E>::value
             ? (unsigned)complete<E>::size : 0u;
}
struct row { unsigned idx, n, var, ret, a[%(n)d], p[%(n)d]; };
template <class F> struct sig;
template <class R, class... A> struct sig<R (*)(A...)> {
  static constexpr row make(unsigned i) { return row{i, sizeof...(A), 0, rsz<R>(), {rsz<A>()...}, {psz<A>()...}}; }
};
template <class R, class... A> struct sig<R (*)(A..., ...)> {
  static constexpr row make(unsigned i) { return row{i, sizeof...(A), 1, rsz<R>(), {rsz<A>()...}, {psz<A>()...}}; }
};
template <class R, class... A> struct sig<R (*)(A...) noexcept> : sig<R (*)(A...)> {};
template <class R, class... A> struct sig<R (*)(A..., ...) noexcept> : sig<R (*)(A..., ...)> {};
}
extern "C" const oc_iw::row oc_iw_rows[] = {
""" % {"n": NARGS}
ROW = 4 + 2 * NARGS
SYSTEM_HEADERS = ["windows.h", "imm.h", "mmsystem.h", "shellapi.h", "shlobj.h", "wininet.h",
                  "wincrypt.h", "wintrust.h", "softpub.h", "winver.h", "ddraw.h", "dsound.h",
                  "stdlib.h", "stdio.h", "string.h", "stdarg.h", "process.h", "time.h", "math.h",
                  "mbstring.h", "ctype.h", "io.h", "direct.h", "malloc.h", "locale.h", "errno.h"]

# Pointer arguments reviewed (2026-09-29): what the game passes is that structure, at
# its size on both widths. A difference of size or count is never reviewed away.
_MSG = "win_msg_t, asserted to be MSG's size"
REVIEWED = {
    ("BeginPaint", "event_Win"): "WndProc's buffer is a PAINTSTRUCT's size",
    ("EndPaint", "event_Win"): "WndProc's buffer is a PAINTSTRUCT's size",
    ("DispatchMessageA", "event_Win"): _MSG,
    ("GetMessageA", "event_Win"): _MSG,
    ("PeekMessageA", "event_Win"): _MSG,
    ("TranslateAcceleratorA", "event_Win"): _MSG,
    ("TranslateMessage", "event_Win"): _MSG,
    ("GetLocalTime", "event_Win"): "systemtime_t, asserted to be SYSTEMTIME's size",
    ("EnterCriticalSection", "snd"): "snd_crit_t, asserted to be CRITICAL_SECTION's size",
    ("LeaveCriticalSection", "snd"): "snd_crit_t, asserted to be CRITICAL_SECTION's size",
    ("CreateEventA", "snd"): "passes NULL",
    ("CreateDIBSection", "gds_font"): "a BITMAPINFO with its 256 colours",
    ("SVidPlayBegin", "dlg_flc"): "a 16-byte rectangle, or 0",
    ("SVidPlayBegin", "gluBrfPort"): "a 16-byte rectangle, or 0",
    ("SVidPlayBegin", "playflic"): "a 16-byte rectangle, or 0",
    ("SVidPlayBegin", "statport"): "a 16-byte rectangle, or 0",
}


def run(env, args, cwd):
    exe = b64.shutil.which(args[0], path=env["PATH"])
    return subprocess.run([exe] + args[1:], env=env, cwd=cwd, capture_output=True, text=True)


def c_functions(obj):
    """-> (C functions the object defines, C functions it calls)."""
    defined, called = set(), set()
    for s in coff.ObjFile(obj).symbols:
        if s is None or s["class"] != 2:
            continue
        n = s["name"]
        imp = n.startswith("__imp_")
        n = n[6:] if imp else n
        if n.startswith("?") or n.startswith("$") or n.startswith("."):
            continue
        if s["sec"] > 0 and (s["type"] >> 4) & 0xF == 2:
            defined.add(n)
        elif s["sec"] == 0 and (imp or (s["type"] >> 4) & 0xF == 2):
            called.add(n)
    return defined, called


def write_probe(path, include, names):
    with open(path, "w", encoding="utf-8") as f:
        f.write(include)
        f.write(HEAD)
        for i, n in enumerate(names):
            f.write("  oc_iw::sig<decltype(&::%s)>::make(%d),\n" % (n, i))
        f.write("  {0xFFFFFFFFu}\n};\n")


def read_rows(obj):
    of = coff.ObjFile(obj)
    for s in of.symbols:
        if s and s["name"] == "oc_iw_rows" and s["sec"] > 0:
            sec = of.sections[s["sec"] - 1]
            d = of.data[sec["rawptr"] + s["value"]:sec["rawptr"] + sec["rawsz"]]
            rows = {}
            for i in range(0, len(d) - 4 * ROW + 1, 4 * ROW):
                v = struct.unpack_from("<%dI" % ROW, d, i)
                if v[0] == 0xFFFFFFFF:
                    break
                rows[v[0]] = {"n": v[1], "var": v[2], "ret": v[3], "a": list(v[4:4 + NARGS]),
                              "p": list(v[4 + NARGS:])}
            return rows
    raise SystemExit("[decl_widths] no rows in %s" % obj)


def undeclared(out):
    bad = set()
    for a, b in re.findall(r"'::(\w+)'|'(\w+)': is not a member of '`global namespace''", out):
        bad.add(a or b)
    bad |= set(re.findall(r"error C2039: '(\w+)'", out))
    bad |= set(re.findall(r"error C2065: '(\w+)'", out))
    # overloaded, as C++ makes strchr and its kin: no one type to compare
    bad |= set(re.findall(r"error C3556: '(\w+)'", out))
    return bad


def probe(env, flags, name, include, names):
    """{name: row} for the names the include declares; the others are dropped."""
    path = os.path.join(OUT, "probe_%s.cpp" % name)
    names = list(names)
    for _ in range(8):
        write_probe(path, include, names)
        r = run(env, ["cl"] + flags + ["/std:c++17", "/wd4005", "/Fo" + OUT + "\\", path], OUT)
        if r.returncode == 0:
            rows = read_rows(os.path.join(OUT, "probe_%s.obj" % name))
            return {n: rows[i] for i, n in enumerate(names)}
        bad = undeclared(r.stdout) & set(names)
        if not bad:
            print("\n".join(ln for ln in r.stdout.splitlines() if "error" in ln)[:3000])
            raise SystemExit("[decl_widths] the probe %s does not compile" % name)
        names = [n for n in names if n not in bad]
    raise SystemExit("[decl_widths] the probe %s does not settle" % name)


def size_text(v):
    return "%d bytes%s" % (v & 0xFF, " of floating point" if v & 0x100 else "")


def differences(ours, theirs):
    """What in a declaration can go wrong at 64 bits against its definition. Safe, and
    not listed: a result the caller declares void, or reads narrower than it is; an
    integer argument the caller passes wider than the callee reads (the callee reads
    its low bytes); a pointer to more than the callee writes."""
    out = []
    if ours["n"] != theirs["n"] or ours["var"] != theirs["var"]:
        out.append("%d arguments%s, the definition %d%s" % (ours["n"], " and ..." if ours["var"] else "",
                                                          theirs["n"], " and ..." if theirs["var"] else ""))
    r, t = ours["ret"], theirs["ret"]
    if r and r != t and ((r ^ t) & 0x100 or not t or r > t):
        out.append("returns %s, the definition %s" % (size_text(r), size_text(t) if t else "nothing"))
    for i in range(min(ours["n"], theirs["n"], NARGS)):
        a, d = ours["a"][i], theirs["a"][i]
        if a != d and ((a ^ d) & 0x100 or a < d):
            out.append("argument %d is %s, the definition's %s" % (i + 1, size_text(a), size_text(d)))
        elif a == d == 8 and ours["p"][i] and theirs["p"][i] and ours["p"][i] < theirs["p"][i]:
            out.append("argument %d points at %d bytes, the definition's at %d"
                       % (i + 1, ours["p"][i], theirs["p"][i]))
        elif a == d == 8 and not ours["p"][i] and theirs["p"][i] > 8:
            out.append("argument %d points at void, the definition's at %d bytes (review)"
                       % (i + 1, theirs["p"][i]))
    return out


def include_of(grp, tu):
    return '#include "%s"\n' % tu_path(grp, tu).replace("\\", "/")


def main():
    os.makedirs(OUT, exist_ok=True)
    env = b64.vs_env()
    tus = [("lang", t, b64.LANG) for t in b64.tus("lang")] + [("port", t, b64.PORT) for t in b64.tus("port")]
    definer, calls = {}, {}
    for grp, tu, flags in tus:
        obj = os.path.join(X64, tu + ".obj")
        if not os.path.exists(obj):
            raise SystemExit("[decl_widths] no x64 object for %s: run tools/build_engine_x64.py" % tu)
        defined, called = c_functions(obj)
        for n in defined:
            definer.setdefault(n, (grp, tu, flags))
        if grp == "lang":
            calls[tu] = called
    # the declarations, in each lang TU that calls
    ours = {}
    for grp, tu, flags in tus:
        if grp != "lang" or not calls.get(tu):
            continue
        rows = probe(env, flags, tu, include_of(grp, tu), sorted(calls[tu]))
        for n, row in rows.items():
            ours[(tu, n)] = row
    # the definitions: the engine TU that defines each, or the system's headers
    theirs, where = {}, {}
    by_definer = {}
    for (_tu, n) in ours:
        if n in definer:
            by_definer.setdefault(definer[n][:2], set()).add(n)
    flags_of = {(g, t): f for g, t, f in tus}
    for (grp, tu), names in sorted(by_definer.items(), key=lambda x: x[0][1]):
        for n, row in probe(env, flags_of[(grp, tu)], "def_" + tu, include_of(grp, tu), sorted(names)).items():
            theirs[n], where[n] = row, tu
    rest = sorted({n for (_tu, n) in ours if n not in definer})
    inc = "".join("#include <%s>\n" % h for h in SYSTEM_HEADERS)
    for n, row in probe(env, b64.COMMON + ["/TP"], "system", inc, rest).items():
        theirs[n], where[n] = row, "system"
    lines = ["# The game's declarations of C functions against their definitions, at 64 bits", ""]
    found, unknown, reviewed = 0, set(), []
    for (tu, n), row in sorted(ours.items(), key=lambda x: (x[0][1], x[0][0])):
        if n not in theirs:
            unknown.add(n)
            continue
        if where[n] == tu:
            continue
        diff = differences(row, theirs[n])
        if diff and (n, tu) in REVIEWED and all(d.endswith("(review)") for d in diff):
            reviewed.append("- %s in %s: %s" % (n, tu, REVIEWED[(n, tu)]))
        elif diff:
            found += 1
            lines.append("- %s in %s (defined in %s): %s" % (n, tu, where[n], "; ".join(diff)))
    checked = sum(1 for (tu, n) in ours if n in theirs and where[n] != tu)
    lines += ["", "%d declarations checked in %d TUs; %d differ, %d reviewed; no definition found for: %s"
              % (checked, len(calls), found, len(reviewed), ", ".join(sorted(unknown)) or "none"),
              "", "Reviewed:"] + reviewed
    open(os.path.join(OUT, "report.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print("\n".join(lines[2:]))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
