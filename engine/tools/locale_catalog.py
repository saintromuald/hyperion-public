"""Hyperion's own text in other languages (docs/translating.md): the English in the code is the
key, engine/locale/<locale>.po holds a language's translations, gettext's format.

    python tools/locale_catalog.py extract      reads the sources for the text to translate,
                                                writes engine/locale/hyperion.pot and brings
                                                each .po up to date with it
    python tools/locale_catalog.py check        the .pot matches the sources and every .po is
                                                well formed, its slots and choices as the English
    python tools/locale_catalog.py report       what each language lacks
    python tools/locale_catalog.py header OUT   writes OUT, the catalogs for Hyperion.exe
    python tools/locale_catalog.py todo LOC [N] the next N texts LOC lacks, as JSON
    python tools/locale_catalog.py fill LOC F   puts the translations in F (JSON) into LOC's .po

The text to translate is a string literal in one of these, in src117/port/:
    PortText("...")  PortTextF("...", ...)  PortTextC("context", "...")
    PortTextN("one", "other", n)  PortTextNF("one", "other", n, ...)
    PT_("...")  PTC_("context", "...")      (a table's text, translated where it is shown)
build_engine_x64.py and build_engine_linux.py write the header for each build
(locale_catalogs.h in the build's folder, port_text.cpp includes it).
"""
import json
import pathlib
import re
import sys

ENGINE = pathlib.Path(__file__).resolve().parent.parent
SRC = ENGINE / "src117" / "port"
LOCALE = ENGINE / "locale"
POT = LOCALE / "hyperion.pot"

# Remastered's languages: locale, the language's own name, gettext's code, its plural rule
LANGUAGES = [
    ("deDE", "Deutsch", "de", 1),
    ("esES", "Espa\u00f1ol (Espa\u00f1a)", "es", 1),
    ("esMX", "Espa\u00f1ol (M\u00e9xico)", "es_MX", 1),
    ("frFR", "Fran\u00e7ais", "fr", 2),
    ("itIT", "Italiano", "it", 1),
    ("jaJP", "\u65e5\u672c\u8a9e", "ja", 0),
    ("koKR", "\ud55c\uad6d\uc5b4", "ko", 0),
    ("plPL", "Polski", "pl", 4),
    ("ptBR", "Portugu\u00eas (Brasil)", "pt_BR", 2),
    ("ruRU", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "ru", 3),
    ("zhCN", "\u7b80\u4f53\u4e2d\u6587", "zh_CN", 0),
    ("zhTW", "\u7e41\u9ad4\u4e2d\u6587", "zh_TW", 0),
]
# gettext's Plural-Forms for each rule; port_text.cpp computes the same (PluralIndex)
PLURALS = {
    0: "nplurals=1; plural=0;",
    1: "nplurals=2; plural=(n != 1);",
    2: "nplurals=2; plural=(n > 1);",
    3: "nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);",
    4: "nplurals=3; plural=(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);",
}
FORMS = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3}

# each marker's leading string arguments, in order
MARKERS = {
    "PortText": ("id",),
    "PortTextF": ("id",),
    "PortTextC": ("ctx", "id"),
    "PortTextN": ("id", "plural"),
    "PortTextNF": ("id", "plural"),
    "PT_": ("id",),
    "PTC_": ("ctx", "id"),
}
MARKER_RE = re.compile(r"\b(" + "|".join(sorted(MARKERS, key=len, reverse=True)) + r")\s*\(")


# --- C++ sources ---------------------------------------------------------------------------

def blank_comments(text):
    """The source with its comments as spaces (newlines kept, so lines still count) and its
    string and character literals as they are."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


SIMPLE = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11, "\\": 92, "'": 39, '"': 34, "?": 63}


def c_bytes(body):
    """A C string literal's body as its bytes."""
    out = bytearray()
    i, n = 0, len(body)
    while i < n:
        c = body[i]
        if c != "\\":
            out += c.encode("utf-8")
            i += 1
            continue
        e = body[i + 1]
        if e == "x":
            m = re.match(r"[0-9A-Fa-f]+", body[i + 2:])
            out.append(int(m.group(0), 16) & 0xFF)
            i += 2 + len(m.group(0))
        elif e in "01234567":
            m = re.match(r"[0-7]{1,3}", body[i + 1:])
            out.append(int(m.group(0), 8) & 0xFF)
            i += 1 + len(m.group(0))
        elif e in "uU":
            k = 4 if e == "u" else 8
            out += chr(int(body[i + 2:i + 2 + k], 16)).encode("utf-8")
            i += 2 + k
        else:
            out.append(SIMPLE[e])
            i += 2
    return bytes(out)


STRING_RE = re.compile(r'\s*(?:u8)?"((?:[^"\\\n]|\\.)*)"')


def literal_at(text, i):
    """The string argument starting at i (adjacent literals joined) and where it ends, or None
    when the argument is not a literal."""
    parts = []
    while True:
        m = STRING_RE.match(text, i)
        if not m:
            break
        parts.append(c_bytes(m.group(1)))
        i = m.end()
    if not parts:
        return None
    return b"".join(parts).decode("utf-8"), i


def scan():
    """Every marked text in the sources: {(ctx, id): {"plural": ..., "refs": [...]}}."""
    found = {}
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path.name == "port_text.h":
            continue
        text = blank_comments(path.read_text(encoding="utf-8", errors="replace"))
        rel = path.relative_to(ENGINE).as_posix()
        for m in MARKER_RE.finditer(text):
            args = {}
            i = m.end()
            for k, role in enumerate(MARKERS[m.group(1)]):
                got = literal_at(text, i)
                if got is None:
                    break
                args[role], i = got
                m2 = re.match(r"\s*[,)]", text[i:])
                if not m2:
                    args = {}
                    break
                i += m2.end()
            if "id" not in args or args["id"] == "":
                continue
            key = (args.get("ctx"), args["id"])
            e = found.setdefault(key, {"plural": None, "refs": []})
            if "plural" in args:
                e["plural"] = args["plural"]
            e["refs"].append("%s:%d" % (rel, text.count("\n", 0, m.start()) + 1))
    return found


# --- .po files -----------------------------------------------------------------------------

def po_quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'


def po_unquote(s):
    s = s.strip()
    assert s.startswith('"') and s.endswith('"'), s
    return c_bytes(s[1:-1]).decode("utf-8")


def read_po(path):
    """A .po file's header text and entries: [{"ctx", "id", "plural", "strs", "comments",
    "fuzzy"}], the header the entry whose id is empty."""
    entries = []
    cur = None
    field = None
    comments = []

    def finish():
        nonlocal cur
        if cur is not None:
            entries.append(cur)
        cur = None

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            finish()
            field = None
            continue
        if line.startswith("#~"):
            continue
        if line.startswith("#"):
            if cur is not None and field is not None:
                finish()
                field = None
            comments.append(raw)
            continue
        if cur is None:
            cur = {"ctx": None, "id": None, "plural": None, "strs": {}, "comments": comments,
                   "fuzzy": any(c.startswith("#,") and "fuzzy" in c for c in comments)}
            comments = []
        m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr(?:\[(\d+)\])?)\s+(\".*\")$", line)
        if m:
            word, idx, val = m.group(1), m.group(2), po_unquote(m.group(3))
            if word == "msgctxt":
                if cur["id"] is not None:
                    finish()
                    cur = {"ctx": None, "id": None, "plural": None, "strs": {}, "comments": [],
                           "fuzzy": False}
                cur["ctx"] = val
                field = ("ctx",)
            elif word == "msgid":
                if cur["id"] is not None:
                    finish()
                    cur = {"ctx": None, "id": None, "plural": None, "strs": {}, "comments": [],
                           "fuzzy": False}
                cur["id"] = val
                field = ("id",)
            elif word == "msgid_plural":
                cur["plural"] = val
                field = ("plural",)
            else:
                k = int(idx) if idx is not None else 0
                cur["strs"][k] = val
                field = ("strs", k)
        elif line.startswith('"') and field is not None:
            val = po_unquote(line)
            if field[0] == "strs":
                cur["strs"][field[1]] += val
            else:
                cur[field[0]] += val
        else:
            raise SystemExit("%s: cannot read: %s" % (path, raw))
    finish()
    return entries


def write_entry(out, e, refs=None):
    for c in e.get("comments", []):
        if not c.startswith("#:"):
            out.append(c)
    for r in refs or []:
        out.append("#: " + r)
    if e.get("ctx") is not None:
        out.append("msgctxt " + po_quote(e["ctx"]))
    out.append("msgid " + po_quote(e["id"]))
    if e.get("plural") is not None:
        out.append("msgid_plural " + po_quote(e["plural"]))
        for k in sorted(e["strs"]):
            out.append("msgstr[%d] %s" % (k, po_quote(e["strs"][k])))
    else:
        out.append("msgstr " + po_quote(e["strs"].get(0, "")))
    out.append("")


def header_text(lang):
    loc, name, code, rule = lang
    return ("Project-Id-Version: Hyperion\n"
            "Language: %s\n"
            "MIME-Version: 1.0\n"
            "Content-Type: text/plain; charset=UTF-8\n"
            "Content-Transfer-Encoding: 8bit\n"
            "Plural-Forms: %s\n" % (code, PLURALS[rule]))


def write_po(path, lang, header_comments, entries, found):
    out = list(header_comments)
    write_entry(out, {"ctx": None, "id": "", "plural": None, "strs": {0: header_text(lang)}})
    have = {(e["ctx"], e["id"]): e for e in entries if e["id"]}
    forms = FORMS[lang[3]]
    for key in sorted(found, key=lambda k: found[k]["refs"][0]):
        f = found[key]
        e = have.get(key) or {"ctx": key[0], "id": key[1], "strs": {}, "comments": []}
        e = dict(e, ctx=key[0], id=key[1], plural=f["plural"])
        if f["plural"] is not None:
            e["strs"] = {k: e["strs"].get(k, "") for k in range(forms)}
        else:
            e["strs"] = {0: e["strs"].get(0, "")}
        write_entry(out, e, f["refs"])
    path.write_bytes(("\n".join(out)).replace("\n", "\r\n").encode("utf-8"))


# --- commands ------------------------------------------------------------------------------

DRAFT = ["# Hyperion's text in %s.",
         "# A first draft, written for Hyperion and not by a native speaker: corrections are welcome",
         "# (docs/translating.md). The English is the key; a line left empty shows the English."]


def extract():
    found = scan()
    LOCALE.mkdir(exist_ok=True)
    out = ["# Hyperion's text to translate, read from the sources by tools/locale_catalog.py extract.", ""]
    for key in sorted(found, key=lambda k: found[k]["refs"][0]):
        f = found[key]
        e = {"ctx": key[0], "id": key[1], "plural": f["plural"],
             "strs": {0: "", 1: ""} if f["plural"] is not None else {0: ""}}
        write_entry(out, e, f["refs"])
    POT.write_bytes(("\n".join(out)).replace("\n", "\r\n").encode("utf-8"))
    for lang in LANGUAGES:
        path = LOCALE / (lang[0] + ".po")
        if path.exists():
            entries = read_po(path)
            comments = entries[0]["comments"] if entries and entries[0]["id"] == "" else []
        else:
            entries, comments = [], [c % lang[1] if "%s" in c else c for c in DRAFT]
        write_po(path, lang, comments, entries, found)
    print("locale: %d texts to translate" % len(found))
    report()


SLOT_RE = re.compile(r"\{\d+\}")


def problems(e):
    """What is wrong with a translated entry: its slots or choices differ."""
    out = []
    want = set(SLOT_RE.findall(e["id"])) | set(SLOT_RE.findall(e["plural"] or ""))
    for s in e["strs"].values():
        if not s:
            continue
        got = set(SLOT_RE.findall(s))
        # a plural's form may leave the count out ("one" said as a word)
        if got - want or (e["plural"] is None and got != want):
            out.append("its slots {n} differ from the English")
        if e["id"].count("|") != s.count("|"):
            out.append("its choices (|) differ in number from the English")
    return sorted(set(out))


def load_all():
    cats = {}
    for lang in LANGUAGES:
        path = LOCALE / (lang[0] + ".po")
        if path.exists():
            cats[lang[0]] = [e for e in read_po(path) if e["id"]]
    return cats


def report():
    found = scan()
    for loc, entries in load_all().items():
        done = sum(1 for e in entries if (e["ctx"], e["id"]) in found and all(e["strs"].values()) and not e["fuzzy"])
        print("  %s: %d of %d translated" % (loc, done, len(found)))


def check():
    bad = 0
    found = scan()
    if POT.exists():
        have = {(e["ctx"], e["id"]) for e in read_po(POT) if e["id"]}
        if have != set(found):
            print("locale: hyperion.pot is not up to date with the sources (%d new, %d gone): "
                  "run tools/locale_catalog.py extract" % (len(set(found) - have), len(have - set(found))))
            bad += 1
    for loc, entries in load_all().items():
        for e in entries:
            for p in problems(e):
                print("locale: %s.po, %r: %s" % (loc, e["id"][:60], p))
                bad += 1
    if bad:
        raise SystemExit(1)
    print("locale: %d texts; %d languages well formed" % (len(found), len(load_all())))


def todo(loc, limit):
    """The next untranslated entries of a language, as JSON: id, context, plural and where."""
    lang = next(l for l in LANGUAGES if l[0] == loc)
    found = scan()
    out = []
    for e in read_po(LOCALE / (loc + ".po")):
        if not e["id"] or (e["ctx"], e["id"]) not in found:
            continue
        if all(e["strs"].values()) and not problems(e):
            continue
        item = {"id": e["id"]}
        if e["ctx"] is not None:
            item["ctx"] = e["ctx"]
        if e["plural"] is not None:
            item["plural"] = e["plural"]
            item["forms"] = FORMS[lang[3]]
        item["where"] = found[(e["ctx"], e["id"])]["refs"][0]
        out.append(item)
        if len(out) >= limit:
            break
    print(json.dumps(out, ensure_ascii=False, indent=1))


def fill(loc, path):
    """Translations from a JSON file into a language's .po: a list of {"id", "ctx"?, "str"} or,
    for a plural, {"id", "ctx"?, "strs": [...]}, as many forms as the language has."""
    lang = next(l for l in LANGUAGES if l[0] == loc)
    rows = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    want = {(r.get("ctx"), r["id"]): r for r in rows}
    po = LOCALE / (loc + ".po")
    entries = read_po(po)
    header = entries[0]["comments"] if entries and entries[0]["id"] == "" else []
    done, bad = 0, 0
    for e in entries:
        r = want.get((e["ctx"], e["id"]))
        if r is None or not e["id"]:
            continue
        strs = r.get("strs") or [r.get("str", "")]
        if e["plural"] is not None:
            strs = (strs + [strs[-1]] * FORMS[lang[3]])[:FORMS[lang[3]]]
            new = dict(e, strs={k: s for k, s in enumerate(strs)})
        else:
            new = dict(e, strs={0: strs[0]})
        p = problems(new)
        if p:
            print("locale: %s %r: %s" % (loc, e["id"][:60], "; ".join(p)))
            bad += 1
            continue
        e["strs"] = new["strs"]
        done += 1
    write_po(po, lang, header, entries, scan())
    print("locale: %s: %d filled, %d refused, %d not found" % (loc, done, bad, len(want) - done - bad))


def c_literal(s):
    """s as a C string literal, every byte past ASCII as an octal escape."""
    out = ['"']
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch == "\\" or ch == '"':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif 0x20 <= b < 0x7F:
            out.append(ch)
        else:
            out.append("\\%03o" % b)
    out.append('"')
    return "".join(out)


def header(out_path):
    found = scan()
    lines = ["// locale_catalogs.h -- written by tools/locale_catalog.py from engine/locale/*.po for each build.",
             "#pragma once", ""]
    cats = []
    for lang in LANGUAGES:
        path = LOCALE / (lang[0] + ".po")
        if not path.exists():
            continue
        rows = []
        for e in read_po(path):
            if not e["id"] or e["fuzzy"] or (e["ctx"], e["id"]) not in found:
                continue
            strs = [e["strs"][k] for k in sorted(e["strs"])]
            if not all(strs) or problems(e):
                continue
            key = e["id"] if e["ctx"] is None else e["ctx"] + "\x04" + e["id"]
            forms = ", ".join(c_literal(s) for s in strs) + ", NULL" * (3 - len(strs))
            rows.append("    {%s, {%s}}," % (c_literal(key), forms))
        if not rows:
            continue
        lines.append("static const PortTextEntry kPortText_%s[] = {" % lang[0])
        lines += rows
        lines.append("};")
        cats.append("    {\"%s\", %d, kPortText_%s, %d}," % (lang[0], lang[3], lang[0], len(rows)))
    lines.append("static const PortTextCatalog kPortTextCatalogs[] = {")
    lines += cats or ["    {NULL, 0, NULL, 0},"]
    lines.append("};")
    lines.append("static const int kPortTextCatalogCount = %d;" % len(cats))
    pathlib.Path(out_path).write_bytes(("\n".join(lines) + "\n").replace("\n", "\r\n").encode("utf-8"))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "extract":
        extract()
    elif cmd == "check":
        check()
    elif cmd == "report":
        report()
    elif cmd == "header" and len(sys.argv) > 2:
        header(sys.argv[2])
    elif cmd == "todo" and len(sys.argv) > 2:
        todo(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 100)
    elif cmd == "fill" and len(sys.argv) > 3:
        fill(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
