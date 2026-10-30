"""Hyperion's release notes (docs/versioning.md): engine/notes/release-notes.json, checked and
turned into what reads it.

    python tools/release_notes.py check       the file is well formed and its newest release is
                                              hyperion/version.h's version
    python tools/release_notes.py md          writes docs/release-notes.md, the notes to read
    python tools/release_notes.py header OUT  writes OUT, the notes as bytes for Hyperion.exe

build_engine_x64.py writes the header for each build (release_notes.h in the build's folder,
port_front_notes.cpp includes it); package.py runs the check and puts the Markdown in docs\\.
"""
import json
import pathlib
import re
import sys

ENGINE = pathlib.Path(__file__).resolve().parent.parent
NOTES = ENGINE / "notes" / "release-notes.json"
VERSION_H = ENGINE / "hyperion" / "version.h"
MD = ENGINE.parent / "docs" / "release-notes.md"

KINDS = ("release", "announcement")
SECTIONS = (("new", "New"), ("improved", "Improved"), ("changed", "Changed"), ("fixed", "Fixed"))
# what a media item can be (a live view of the engine is planned, docs/versioning.md)
MEDIA = {"image": ("file",), "frames": ("file", "frames"), "clip": ("file",)}


def load():
    return json.loads(NOTES.read_text(encoding="utf-8"))


def version_h():
    """hyperion/version.h's version, as "0.3.0-beta", and its other spellings for the check."""
    text = VERSION_H.read_text(encoding="utf-8")
    def define(name):
        m = re.search(r"#define %s (.+)" % name, text)
        return m.group(1).strip() if m else None
    major, minor, patch = (define("HYPERION_VERSION_" + n) for n in ("MAJOR", "MINOR", "PATCH"))
    channel = (define("HYPERION_VERSION_CHANNEL") or "").strip('"')
    version = "%s.%s.%s" % (major, minor, patch) + ("-" + channel if channel else "")
    return version, {
        "HYPERION_VERSION_TEXT": '"Hyperion-v%s"' % version,
        "HYPERION_FILE_VERSION_STR": '"%s"' % version,
        "HYPERION_FILE_VERSION": "%s,%s,%s,0" % (major, minor, patch),
    }, define


def numbers(version):
    """A version's MAJOR, MINOR and PATCH, to compare: "0.6.29-beta" is (0, 6, 29)."""
    m = re.match(r"(\d+)\.(\d+)\.(\d+)", version or "")
    return tuple(int(n) for n in m.groups()) if m else (0, 0, 0)


def check():
    problems = []
    try:
        notes = load()
    except (OSError, ValueError) as e:
        return ["%s: %s" % (NOTES, e)]
    if notes.get("format") != 1:
        problems.append("format is not 1")
    entries = notes.get("entries", [])
    ids = set()
    for i, e in enumerate(entries):
        where = "entry %d (%s)" % (i, e.get("id", "no id"))
        for key in ("id", "kind", "date", "title", "summary"):
            if not isinstance(e.get(key), str) or not e.get(key):
                problems.append("%s: no %s" % (where, key))
        if e.get("id") in ids:
            problems.append("%s: the id is used twice" % where)
        ids.add(e.get("id"))
        if e.get("kind") not in KINDS:
            problems.append("%s: kind is %r, not one of %s" % (where, e.get("kind"), ", ".join(KINDS)))
        if len(e.get("line", "")) > 110:
            problems.append("%s: the notification's line is over 110 characters" % where)
        if e.get("kind") == "release" and not e.get("version"):
            problems.append("%s: a release names its version" % where)
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", e.get("date", "")):
            problems.append("%s: date is not YYYY-MM-DD" % where)
        for key, _ in SECTIONS:
            for j, item in enumerate(e.get(key, [])):
                if not isinstance(item, dict) or not item.get("text"):
                    problems.append("%s: %s item %d has no text" % (where, key, j))
        for j, m in enumerate(e.get("media", [])):
            kind = m.get("kind")
            if kind not in MEDIA:
                problems.append("%s: media %d is %r, not one of %s" % (where, j, kind, ", ".join(MEDIA)))
                continue
            for key in MEDIA[kind]:
                if key not in m:
                    problems.append("%s: media %d (%s) needs %s" % (where, j, kind, key))
            if kind in ("image", "frames") and not (NOTES.parent / m.get("file", "")).is_file():
                problems.append("%s: media %d's file %s is not in notes\\" % (where, j, m.get("file")))
    releases = [e for e in entries if e.get("kind") == "release"]
    version, want, define = version_h()
    if not releases:
        problems.append("no release")
    elif numbers(releases[0].get("version")) > numbers(version):
        # notes are written for public releases only, so a build may be past the newest; never behind it
        problems.append("the newest release is %s, past hyperion/version.h's %s" % (releases[0].get("version"), version))
    for name, value in want.items():
        if define(name) != value:
            problems.append("hyperion/version.h: %s is %s, not %s" % (name, define(name), value))
    return problems


def markdown():
    notes = load()
    out = ["# Hyperion release notes", "",
           "What changed in each version of Hyperion, newest first. The game shows the same under Menu,",
           "Patch Notes. Versions are numbered as `docs/versioning.md` says; this page is written from",
           "`engine/notes/release-notes.json` by `engine/tools/release_notes.py md`.", ""]
    for e in notes["entries"]:
        if e["kind"] == "release":
            out.append("## v%s, %s" % (e["version"], e["date"]))
        else:
            out.append("## %s" % e["date"])
        out += ["", "**%s.** %s" % (e["title"].rstrip("."), e["summary"]), ""]
        for para in e.get("body", []):
            out += [para, ""]
        for key, head in SECTIONS:
            items = e.get(key, [])
            if not items:
                continue
            out += ["### " + head, ""]
            for item in items:
                title = item.get("title")
                out.append("- " + ("**%s.** " % title.rstrip(".") if title else "") + item["text"])
            out.append("")
    return "\n".join(out)


def header(path):
    data = NOTES.read_bytes()
    lines = ["// release_notes.h -- engine/notes/release-notes.json as bytes, written by",
             "// tools/release_notes.py for each build (port_front_notes.cpp).",
             "#pragma once",
             "static const unsigned char kReleaseNotesJson[] = {"]
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    lines += ["  0x00};", ""]
    # open's newline, as Path.write_text takes one only from Python 3.10 (a Mac's is 3.9)
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    if cmd == "check":
        problems = check()
        for p in problems:
            print("release notes:", p)
        if problems:
            sys.exit(1)
        entries = load()["entries"]
        newest = next((e["version"] for e in entries if e.get("kind") == "release"), "none")
        print("release notes: %d entries, the newest release %s, the build %s" % (len(entries), newest, version_h()[0]))
    elif cmd == "md":
        with open(MD, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(markdown())
        print("wrote", MD)
    elif cmd == "header" and len(sys.argv) > 2:
        header(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
