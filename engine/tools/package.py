"""Package a Hyperion build to hand out: one zip a player unpacks into a folder of its own,
anywhere, test maps included.

    python tools/package.py                packages the last build
    python tools/package.py --build        build Hyperion.exe first
    python tools/package.py --out <folder> where the zips go (default testrun/share/)
    python tools/package.py --linux x64    the Linux build instead (arm64 for ARM), as a .tar.gz
    python3 tools/package.py --mac         on a Mac: the Mac build as Hyperion.app, in a .zip
    python tools/package.py --install <folder>
                                           the zip's files put in a folder, as unzipping it there
    python tools/package.py --install      would, into each folder engine/install_to.txt names

An install writes only what differs and leaves the rest of the folder as it is, the player's
profiles, saves, settings and own mods in Data\\ included. A running Hyperion.exe, which cannot
be written, is renamed Hyperion.exe.old and goes on; the next start is the new one. Each
Windows build installs into the folders engine/install_to.txt names (one a line, # for a
comment, not committed), unless built with --no-install (tools/build_engine_x64.py): the
user's own Hyperion folder (the user, 2026-10-21: "when you build the next windows build going
forward it pushes the data, and any new tools to E:\\Hyperion").

The build zip holds Hyperion.exe, the x64 SDL3.dll, README.txt and docs\\, the documents for
editors (the map format, the live channel and the live guide to read offline), and everything
else in Data\\ (the user, 2026-10-21): Data\\mods\\ with Hyperion's own mods but its test ones
(UNSHIPPED_MODS), tools\\ with the
live channel's clients (hxlive.py, and hxlive_cpp\\ for C++) and
the sample cutscenes, web\\ with the LAN room's page and the browser build, notes\\ with the
release notes' pictures, and maps\\Hyperion\\ with the remade maps in
testrun/play/maps/Hyperion/, which the user allows sharing (PRD 2026-10-13). The zip goes in a
folder of its own, not into a StarCraft folder (PRD 2026-10-21): Hyperion.exe finds the game's
files where they are installed and keeps its own data in the same Data\\
(src117/port/platform/port_paths.cpp), whose maps\\ it looks in beside the install's;
PortPackagedFile finds web\\ and notes\\ there. Nothing of Blizzard's goes in: the player has
the game. It is one zip with no zips inside (the user, 2026-10-15).

Each run writes a new folder, hyperion_<version>_<stamp>/, with the zip and a list of what
went in; earlier ones are kept.

The Linux package holds the same, with the Linux program Hyperion.x86_64 (or
Hyperion.aarch64) and libSDL3.so.0 beside it (tools/build_engine_linux.py), a README for Linux, text with Linux's
line ends, and without the C++ client, which is built on WinHTTP. It goes in a folder of its
own the same way.

The Mac package is made on the Mac, after tools/build_engine_linux.py there
(tools/mac_sync.py sends the tree): Hyperion.app, with the program and libSDL3.0.dylib in
Contents/MacOS/, an Info.plist and the icon made from hyperion.ico (sips and iconutil),
signed ad hoc (codesign -s -), and beside it the same folders as the Linux package's. The
program keeps Data/ in the folder Hyperion.app is in (PortProgramFolder), so the zip's
contents go where the Linux package's go. ditto zips it, keeping the signature.
"""
import argparse
import hashlib
import json
import io
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]
REPO = ENGINE.parent
BUILT = ENGINE / "src117" / "build" / "x64" / "Hyperion.exe"
SDL3 = ENGINE / "third_party" / "SDL3-3.4.16" / "lib" / "x64" / "SDL3.dll"
MODS = ENGINE / "mods"
# Hyperion's mods for its own testing, in no package or install (the user, 2026-10-27: "For
# public release those don't go with the build for now as its testing use"): the Hybrid Test the
# replay suite plays and the AI Lab
UNSHIPPED_MODS = ("hybrid-test", "lab")
# and the maps for Hyperion's own testing: the Hybrid Test's, which places the mod's unit,
# the trigger and selection tests, and the RGB colours' test, named as Big Game Hunters 2x is
# (the user, 2026-10-27)
UNSHIPPED_MAPS = ("Hybrid Test.hxm", "Trigger Test 32.hxm", "Wide Selection Test.hxm",
                  "Big Game Hunters 2x rgb test.hxm")
DOCS = [REPO / "docs" / "hxm-for-editors.md", REPO / "docs" / "hxm-format.md",
        REPO / "docs" / "live-channel-for-editors.md", REPO / "docs" / "release-notes.md"]
# the release notes' pictures, beside Hyperion.exe in notes\ (the notes themselves are in the exe)
NOTES = ENGINE / "notes"
# the page a LAN room serves, beside the program in web\lan, and Hyperion for the
# browser in web\play, with the program tools/build_engine_web.py built (W3)
WEB = ENGINE / "web"
# Hyperion's built-in HUD designs: layouts, styles and their sounds, from engine/huds/
HUDS = ENGINE / "huds"
WEB_PROGRAM = [ENGINE / "src117" / "build" / "web" / "play" / n for n in ("hyperion.js", "hyperion.wasm")]
# the live channel's clients and the sample scenes, under tools\ in the zip, and the live guide
# (served at /guide from the mod) also as docs\live-guide.html to read offline
TOOLS = [ENGINE / "tools" / "hxlive.py"] + sorted((MODS / "debug-tools" / "cutscenes").glob("*.json"))
CPP_CLIENT = [ENGINE / "tools" / "hxlive_cpp" / n for n in ("hxlive.h", "example.cpp", "build.bat")]
GUIDE = MODS / "debug-tools" / "guide.html"
MAPS = ENGINE / "testrun" / "play" / "maps" / "Hyperion"
VERSION_H = ENGINE / "hyperion" / "version.h"
# the folders each Windows build installs into, one a line (not committed)
INSTALL_TO = ENGINE / "install_to.txt"
# each Windows build's Hyperion.exe and PDB, kept for tools/crash_symbols.py; never shipped
SYMBOLS = ENGINE / "testrun" / "share" / "symbols"
SYMBOLS_KEEP = 10  # the builds kept that were installed and not packaged


def font_licenses() -> str:
    """docs/licenses/fonts.txt: the license of every typeface inside the program, which the SIL
    Open Font License asks to travel with the fonts (tools/fetch_fonts.py)."""
    import fetch_fonts
    parts = ["The typefaces inside Hyperion, each under the SIL Open Font License 1.1.\n\n"
             "Saira is a static cut of its variable font. Noto Sans and Noto Sans JP, KR, SC and TC\n"
             "are cut to their regular weight and to the characters Hyperion uses. Every face keeps\n"
             "its name and is not sold on its own.\n"]
    rule = "=" * 72
    for file, faces in fetch_fonts.LICENSES:
        text = (fetch_fonts.DEST / file).read_text(encoding="utf-8").replace("\r\n", "\n").strip()
        parts.append(f"\n{rule}\n{faces}\n{rule}\n\n{text}\n")
    return "".join(parts)


README = """Hyperion {version}

Hyperion plays StarCraft: Brood War with an engine of its own: the game's maps, and its
classic replays and saved games, play as the original plays them, and Hyperion's own maps
go past the original's limits (up to 512 tiles a side and 32 players for now).

Test maps
Data\\maps\\Hyperion\\ holds remade maps for Hyperion. Those past 32 players or 512 tiles
a side, beyond what Hyperion aims at for now, are in Data\\maps\\Hyperion\\Experimental\\.

What it needs
- Windows 10 or 11, 64-bit, and a graphics card with Direct3D 12 or Vulkan.
- StarCraft: Brood War's data files, either:
  - Remastered's: StarCraft from Battle.net (free), installed by Battle.net.
  - Classic's: a StarCraft folder with the game's MPQ files (StarDat.mpq, BrooDat.mpq,
    patch_rt.mpq, and StarCraft.mpq and BroodWar.mpq, the CDs' files) and Local.dll.
    StarCraft.exe is not needed.
  Hyperion reads the game's art, sounds and maps from those files; it holds none of them.

Installing
Unpack this zip into a folder of its own, anywhere, for example C:\\Games\\Hyperion, and start
Hyperion.exe. Not into a StarCraft folder: Hyperion leaves the game's folders as they are.
At its first start Hyperion asks for the folder of each StarCraft you have, filled in with the
ones it finds (StarCraft from Battle.net where Battle.net installed it), and which of them the
game reads; Settings, Data and mods, Game files switches to the other later. The map list shows
the game's own maps from that install beside Hyperion's.

Beside Hyperion.exe are SDL3.dll, this README, docs\\ with the documents (docs\\licenses\\
has the licenses of the typefaces inside Hyperion), and Data\\ with
everything else Hyperion brings: its mods, maps, tools and web pages. Hyperion keeps
everything of its own in Data\\ too: profiles, saved games, replays, settings, logs and its
cache. To remove Hyperion, delete its folder.

From an earlier Hyperion: unpacked into the same folder as before, Hyperion moves what its
folder Hyperion\\ held into Data\\ at its first start, profiles, saves and replays included,
and the Hyperion folder can go once that is done. An earlier Hyperion in a StarCraft
folder can stay there or move out: copy its Hyperion folder into the new folder, then start
the new Hyperion.exe. The folders docs, notes, tools and web and maps\\Hyperion an older
version left beside Hyperion.exe may be deleted: only maps\\Hyperion, not maps, which holds
the game's own maps.

Hyperion.exe is not signed. Windows Defender or SmartScreen may warn about it, or put it
in quarantine, as a false positive; restore it or allow it if so.

HUDs
Settings, HUD, chooses the console you play with: the original's, or one of Hyperion's
designs, across the foot of the screen or as a column at either side, with a picture of how it
looks on your screen. A HUD of your own goes in Data\\huds\\, as a folder or a .hud package;
    Hyperion.exe --hud-check <file, folder or .hud>
checks one without starting a game. The designs need the GPU renderer (Settings, Graphics).

Starting a game from the command line
    Hyperion.exe --map <map file> [--race zerg|terran|protoss|random] [--computers <n>]
    Hyperion.exe --map <map file> --ums      (Use Map Settings: the map's triggers run)
    Hyperion.exe --map <map file> --rules classic|hyperion   (the original game's rules, or Hyperion's, the default)
    Hyperion.exe --replay <replay file>
    Hyperion.exe --load <saved game>
Give full paths. A map can be .scm, .scx or Hyperion's own .hxm.

For map editors
    Hyperion.exe --hxm-check <map.hxm> [<report.txt>]
checks a map an editor wrote, without starting the game: errors and warnings a line each, exit
code 0 when it plays, 1 when it is refused, 2 when it cannot be read. It needs no game files.
docs\\hxm-for-editors.md says what a Hyperion map is and how to support it;
docs\\hxm-format.md is the format field by field.

Testing live
    Hyperion.exe --control
opens Hyperion's live channel, a WebSocket on 127.0.0.1 port 27117, through which an editor,
a script or an AI changes the running game: it pushes a map's tiles at any of the game's sizes and
switches to it with a fade, loads a map file the same way, and plays scripted cutscenes
(camera moves and zooms, fades, cinema bars, captions, transmissions, units made and
ordered). Started with --control, Hyperion goes straight to its menus, with no opening
cinematic. A program connects with the key Hyperion writes as it opens the channel, to
control-27117.key in Hyperion's folder of the user's application data; the clients here
read it themselves.

While it runs, http://127.0.0.1:27117/guide in a browser is the live guide: every capability
explained and tried on the game from there, the sample scenes played on their maps, a scene
builder, and the code for each in C++, Python and JavaScript. docs\\live-guide.html
is the same to read offline, and docs\\live-channel-for-editors.md has the messages
and the cutscene steps in full. http://127.0.0.1:27117/ is Debug Tools' panel: among its
tools, a list of every map Hyperion finds (with a minimap, its size, players and description,
and buttons to play it) and the sample scenes, each played on the map it was made for.

For an AI assistant: Copy for AI, in Debug Tools' panel and in its overlay in a game (F6),
copies a briefing on the live channel with this install's paths and the game's state, to paste
into the assistant's chat; it is also served at http://127.0.0.1:27117/ai-context.

Data\\tools\\hxlive_cpp\\hxlive.h is a client for C++ in one header over Windows' WinHTTP,
with example.cpp and build.bat beside it. Data\\tools\\hxlive.py is one in Python's
standard library alone, for the command line:
    python Data\\tools\\hxlive.py status
    python Data\\tools\\hxlive.py map push --tileset jungle --size 128x128 --scene Data\\tools\\cutscenes\\temple-flyover.json --reveal
    python Data\\tools\\hxlive.py map push --from "maps\\(2)Volcanis.scm" --scene Data\\tools\\cutscenes\\volcanis-last-stand.json --reveal
    python Data\\tools\\hxlive.py map load <map file> --ums --wait
    python Data\\tools\\hxlive.py cutscene play Data\\tools\\cutscenes\\canvas-sample.json
    python Data\\tools\\hxlive.py cutscene play Data\\tools\\cutscenes\\temple-flyover.json --on-map
    python Data\\tools\\hxlive.py map list temple
    python Data\\tools\\hxlive.py map info "(4)Lost Temple.scm"
A map can be given by its file name alone; Hyperion looks for it in every maps folder.


What is new in each version: Patch Notes in the menu, and docs\\release-notes.md.
Settings shows the version and build at the bottom left.

If Hyperion doesn't start
Each start writes Data\\hyperion_startup.log, a line for each step, and the five starts before
it are kept in Data\\logs\\. When a start fails, Hyperion says so and where that log is. When
Data\\ cannot be written to, the log goes to Windows' folder for temporary files,
%TEMP%\\Hyperion\\.
- Hyperion.exe needs SDL3.dll beside it: unpack the whole zip into a folder, then start
  Hyperion.exe from there, not from inside the zip.
- If Windows Defender put Hyperion.exe in quarantine, restore it (above).
- If Hyperion seems to start and nothing comes, the log's last line says the step it stopped
  at.

If Hyperion stops on an error
It saves a report in a folder of its own in Data\\crashes\\, with report.txt, report.json, a
minidump and the session's logs. Data\\hyperion_crash.log is a copy of the last report.

When you ask for help
The window that says a start failed or Hyperion stopped has the details in a box: press Copy
to clipboard and paste it in a GitHub issue or on Discord, with what you were doing. It names
no folder, network address or name of yours. The logs and the report hold more if they are
asked for; they name the folders Hyperion uses, with your user folder as %USERPROFILE%, and
not your computer's or your own name.
"""


README_LINUX = """Hyperion {version} for Linux ({arch})

Hyperion plays StarCraft: Brood War with an engine of its own: the game's maps, and its
classic replays and saved games, play as the original plays them, and Hyperion's own maps
go past the original's limits (up to 512 tiles a side and 32 players for now).

Test maps
Data/maps/Hyperion/ holds remade maps for Hyperion. Those past 32 players or 512 tiles a
side, beyond what Hyperion aims at for now, are in Data/maps/Hyperion/Experimental/.

What it needs
- 64-bit Linux on {cpu}, with glibc 2.35 or newer: Ubuntu 22.04, Debian 12 or Fedora 36,
  a later release of one of them, or another distribution of the same age or newer.
- A graphics driver with Vulkan (Mesa's, or the card maker's).
- Sound through PulseAudio, PipeWire or ALSA, as a desktop has. Without any, the game
  plays silent.
- StarCraft: Brood War's data files, either:
  - Remastered's: StarCraft from Battle.net, installed through Wine or Lutris, or its folder
    copied from a Windows install; Settings, Data and mods, Game files names where it is.
  - Classic's: a StarCraft folder with the game's MPQ files (StarDat.mpq, BrooDat.mpq,
    patch_rt.mpq, and StarCraft.mpq and BroodWar.mpq, the CDs' files) and Local.dll,
    copied from a Windows install. StarCraft.exe is not needed.
  Hyperion reads the game's art, sounds and maps from those files; it holds none of them.

Installing
Unpack this archive into a folder of its own, anywhere, for example ~/Games/Hyperion, and
run ./{program} there. Not into the StarCraft folder: Hyperion leaves the game's folders as
they are. At its first start Hyperion asks for the folder of each StarCraft you have (it
offers Lutris's and Wine's usual places when StarCraft is there), and which of them the game
reads; Settings, Data and mods, Game files switches to the other later. Names in the game's
folder may be in any case, as on Windows. The map list shows the game's own maps from that
folder beside Hyperion's.

Beside {program} are libSDL3.so.0, this README, docs/ with the documents (docs/licenses/
has the licenses of the typefaces inside Hyperion), and Data/ with
everything else Hyperion brings: its mods, maps, tools and web pages. Hyperion keeps
everything of its own in Data/ too: profiles, saved games, replays, settings, logs and its
cache. To remove Hyperion, delete its folder.

From an earlier Hyperion: unpacked into the same folder as before, Hyperion moves what its
folder Hyperion/ held into Data/ at its first start, profiles, saves and replays included, and
the Hyperion folder can go once that is done. An earlier Hyperion in a StarCraft folder
can stay there or move out: copy its Hyperion folder into the new folder, then start the new
{program}. The folders docs, notes, tools and web and maps/Hyperion an older version left
beside {program} may be deleted: only maps/Hyperion, not maps, which holds the game's own
maps.

Starting a game from the command line
    ./{program} --map <map file> [--race zerg|terran|protoss|random] [--computers <n>]
    ./{program} --map <map file> --ums      (Use Map Settings: the map's triggers run)
    ./{program} --map <map file> --rules classic|hyperion   (the original game's rules, or Hyperion's, the default)
    ./{program} --replay <replay file>
    ./{program} --load <saved game>
Give full paths. A map can be .scm, .scx or Hyperion's own .hxm.

For map editors
    ./{program} --hxm-check <map.hxm> [<report.txt>]
checks a map an editor wrote, without starting the game: errors and warnings a line each,
exit code 0 when it plays, 1 when it is refused, 2 when it cannot be read. It needs no
game files. docs/hxm-for-editors.md says what a Hyperion map is and how to support it;
docs/hxm-format.md is the format field by field.

Testing live
    ./{program} --control
opens Hyperion's live channel, a WebSocket on 127.0.0.1 port 27117, through which an editor,
a script or an AI changes the running game: it pushes a map's tiles at any of the game's sizes and
switches to it with a fade, loads a map file the same way, and plays scripted cutscenes
(camera moves and zooms, fades, cinema bars, captions, transmissions, units made and
ordered). Started with --control, Hyperion goes straight to its menus, with no opening
cinematic. A program connects with the key Hyperion writes as it opens the channel, to
control-27117.key in Hyperion's folder of the user's application data; the clients here
read it themselves.

While it runs, http://127.0.0.1:27117/guide in a browser is the live guide: every capability
explained and tried on the game from there, the sample scenes played on their maps, a scene
builder, and the code for each in C++, Python and JavaScript. docs/live-guide.html is
the same to read offline, and docs/live-channel-for-editors.md has the messages and
the cutscene steps in full. http://127.0.0.1:27117/ is Debug Tools' panel: among its tools, a
list of every map Hyperion finds (with a minimap, its size, players and description, and
buttons to play it) and the sample scenes, each played on the map it was made for.

For an AI assistant: Copy for AI, in Debug Tools' panel and in its overlay in a game (F6),
copies a briefing on the live channel with this install's paths and the game's state, to paste
into the assistant's chat; it is also served at http://127.0.0.1:27117/ai-context.

Data/tools/hxlive.py is a client in Python's standard library alone, for the command line:
    python3 Data/tools/hxlive.py status
    python3 Data/tools/hxlive.py map push --tileset jungle --size 128x128 --scene Data/tools/cutscenes/temple-flyover.json --reveal
    python3 Data/tools/hxlive.py map push --from "maps/(2)Volcanis.scm" --scene Data/tools/cutscenes/volcanis-last-stand.json --reveal
    python3 Data/tools/hxlive.py map load <map file> --ums --wait
    python3 Data/tools/hxlive.py cutscene play Data/tools/cutscenes/canvas-sample.json
    python3 Data/tools/hxlive.py cutscene play Data/tools/cutscenes/temple-flyover.json --on-map
    python3 Data/tools/hxlive.py map list temple
    python3 Data/tools/hxlive.py map info "(4)Lost Temple.scm"
A map can be given by its file name alone; Hyperion looks for it in every maps folder.


What is new in each version: Patch Notes in the menu, and docs/release-notes.md.
Settings shows the version and build at the bottom left.

If Hyperion doesn't start
Each start writes Data/hyperion_startup.log, a line for each step, and the five starts before
it are kept in Data/logs/. When a start fails, Hyperion says so in a window, and on the standard
error when it was started from a terminal, with where that log is. When Data/ cannot be written
to, the log goes to $TMPDIR/Hyperion/, or /tmp/Hyperion/.
- libSDL3.so.0 must stay beside {program}.
- "Permission denied": the archive's program lost its mark as one; chmod +x {program}.
- A message that names GLIBC_2.xx: the distribution is older than Hyperion needs (above).
- If Hyperion seems to start and nothing comes, the log's last line says the step it stopped
  at.

If Hyperion stops on an error
It saves a report in a folder of its own in Data/crashes/, with report.txt, report.json and
the session's logs. Data/hyperion_crash.log is a copy of the last report.

When you ask for help
The window that says a start failed or Hyperion stopped has the details in a box: press Copy
to clipboard and paste it in a GitHub issue or on Discord, with what you were doing, before
you close the window, since what is copied goes with it unless a clipboard manager keeps it.
The same text is on the standard error. It names no folder, network address or name of yours.
The logs and the report hold more if they are asked for; they name the folders Hyperion uses,
with your home folder as ~, and not your computer's or your own name.
"""


README_MAC = """Hyperion {version} for Mac

Hyperion plays StarCraft: Brood War with an engine of its own: the game's maps, and its
classic replays and saved games, play as the original plays them, and Hyperion's own maps
go past the original's limits (up to 512 tiles a side and 32 players for now).

This is a test build.

Maps
Data/maps/Hyperion/ holds Hyperion's own maps, the ones its skirmish tests and the
benchmark play: the Big Game Hunters sizes up to 32 players and the remade Elderlands and
Garden of Aiur. Those past 32 players or 512 tiles a side, beyond
what Hyperion aims at for now, are in Data/maps/Hyperion/Experimental/. The game's own maps
are listed beside them from the StarCraft install Hyperion reads; the canvas (--control, then
the live guide) needs none, and its sample scenes play on a map made on the spot when theirs
is missing.

What it needs
- A Mac with Apple silicon (M1 or later) and macOS 13 Ventura or later.
- StarCraft: Brood War's data files, either:
  - Remastered's: StarCraft from Battle.net (free), installed where Battle.net puts it,
    /Applications/StarCraft. Settings, Data and mods can name another place.
  - Classic's: a StarCraft folder copied from a Windows install, with the game's MPQ files
    (StarDat.mpq, BrooDat.mpq, patch_rt.mpq, and StarCraft.mpq and BroodWar.mpq, the CDs'
    files) and Local.dll. StarCraft.exe is not needed.
  Hyperion reads the game's art, sounds and maps from those files; it holds none of them.

Installing
Unzipped, this is a folder holding Hyperion.app and the folders it uses. Keep it where you
like, in your home folder for example (not Downloads or Desktop, which macOS guards), and not
inside a StarCraft folder: Hyperion leaves the game's folders as they are. At its first start
Hyperion asks for the folder of each StarCraft you have, filled in with StarCraft from
Battle.net when it is in /Applications/StarCraft, and which of them the game reads; Settings,
Data and mods, Game files switches to the other later.

Beside Hyperion.app are this README, docs/ with the documents (docs/licenses/ has the
licenses of the typefaces inside Hyperion), and Data/ with everything else
Hyperion brings: its mods, maps, tools and web pages. Hyperion keeps everything of its own in
Data/ too: profiles, saved games, replays, settings, logs and its cache. To remove Hyperion,
delete its folder.

From an earlier Hyperion: in the same folder as before, Hyperion moves what its folder
Hyperion/ held into Data/ at its first start, profiles, saves and replays included, and the
Hyperion folder can go once that is done; so can the maps folder an earlier package put
beside Hyperion.app, unless you keep maps of your own in it.

The first start
Hyperion is not signed by Apple, so macOS stops it the first time with a message that it
cannot check it. To allow it once: open System Settings, Privacy & Security, scroll to
Security, and click Open Anyway beside Hyperion's name, then start it again and confirm.
Or, in Terminal, clear the download's mark on the folder, then start Hyperion.app as usual:
    xattr -dr com.apple.quarantine <the folder>
macOS may also ask whether Hyperion may find devices on your local network: allow it to
play LAN games.

Starting a game from the command line
    Hyperion.app/Contents/MacOS/Hyperion --map <map file> [--race zerg|terran|protoss|random] [--computers <n>]
    Hyperion.app/Contents/MacOS/Hyperion --map <map file> --ums      (Use Map Settings: the map's triggers run)
    Hyperion.app/Contents/MacOS/Hyperion --map <map file> --rules classic|hyperion   (the classic rules, or Hyperion's, the default)
    Hyperion.app/Contents/MacOS/Hyperion --replay <replay file>
    Hyperion.app/Contents/MacOS/Hyperion --load <saved game>
Give full paths. A map can be .scm, .scx or Hyperion's own .hxm.

For map editors
    Hyperion.app/Contents/MacOS/Hyperion --hxm-check <map.hxm> [<report.txt>]
checks a map an editor wrote, without starting the game: errors and warnings a line each,
exit code 0 when it plays, 1 when it is refused, 2 when it cannot be read. It needs no
game files. docs/hxm-for-editors.md says what a Hyperion map is and how to support it;
docs/hxm-format.md is the format field by field.

Testing live
    Hyperion.app/Contents/MacOS/Hyperion --control
opens Hyperion's live channel, a WebSocket on 127.0.0.1 port 27117, through which an editor,
a script or an AI changes the running game: it pushes a map's tiles at any of the classic
sizes and switches to it with a fade, loads a map file the same way, and plays scripted
cutscenes (camera moves and zooms, fades, cinema bars, captions, transmissions, units made
and ordered). Started with --control, Hyperion goes straight to its menus, with no opening
cinematic. A program connects with the key Hyperion writes as it opens the channel, to
control-27117.key in Hyperion's folder of the user's application data; the clients here
read it themselves.

While it runs, http://127.0.0.1:27117/guide in a browser is the live guide: every capability
explained and tried on the game from there, the sample scenes played on their maps, a scene
builder, and the code for each in C++, Python and JavaScript. docs/live-guide.html is
the same to read offline, and docs/live-channel-for-editors.md has the messages and
the cutscene steps in full. http://127.0.0.1:27117/ is Debug Tools' panel: among its tools, a
list of every map Hyperion finds (with a minimap, its size, players and description, and
buttons to play it) and the sample scenes, each played on the map it was made for.

For an AI assistant: Copy for AI, in Debug Tools' panel and in its overlay in a game (F6),
copies a briefing on the live channel with this install's paths and the game's state, to paste
into the assistant's chat; it is also served at http://127.0.0.1:27117/ai-context.

Data/tools/hxlive.py is a client in Python's standard library alone, for the command line:
    python3 Data/tools/hxlive.py status
    python3 Data/tools/hxlive.py map push --tileset jungle --size 128x128 --scene Data/tools/cutscenes/temple-flyover.json --reveal
    python3 Data/tools/hxlive.py map load <map file> --ums --wait
    python3 Data/tools/hxlive.py cutscene play Data/tools/cutscenes/canvas-sample.json
    python3 Data/tools/hxlive.py map list temple
A map can be given by its file name alone; Hyperion looks for it in every maps folder.


What is new in each version: Patch Notes in the menu, and docs/release-notes.md.
Settings shows the version and build at the bottom left.

If Hyperion doesn't start
Each start writes Data/hyperion_startup.log, a line for each step, and the five starts before
it are kept in Data/logs/. When a start fails, Hyperion says so in a window, with where that
log is. When Data/ cannot be written to, the log goes to macOS's folder for temporary files,
in Hyperion/ there ($TMPDIR in Terminal).
- When macOS stops Hyperion before it starts, see The first start above: Hyperion writes no
  log until macOS lets it run.
- If Hyperion seems to start and nothing comes, the log's last line says the step it stopped
  at.

If Hyperion stops on an error
It saves a report in a folder of its own in Data/crashes/, with report.txt, report.json and
the session's logs. Data/hyperion_crash.log is a copy of the last report; macOS's own report
is in Console, Crash Reports.

When you ask for help
The window that says a start failed or Hyperion stopped has the details in a box: press Copy
to clipboard and paste it in a GitHub issue or on Discord, with what you were doing. It names
no folder, network address or name of yours. The logs and the report hold more if they are
asked for; they name the folders Hyperion uses, with your home folder as ~, and not your
computer's or your own name.
"""

# The app's Info.plist. LSMinimumSystemVersion is the build's (build_engine_linux.py's
# MACOS_MIN); the local network's line is what macOS shows when it asks for LAN games.
INFO_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>Hyperion</string>
  <key>CFBundleIconFile</key><string>Hyperion</string>
  <key>CFBundleIdentifier</key><string>io.github.mad-man-dan.hyperion</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Hyperion</string>
  <key>CFBundleDisplayName</key><string>Hyperion</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{short}</string>
  <key>CFBundleVersion</key><string>{build}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.strategy-games</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSLocalNetworkUsageDescription</key><string>Hyperion finds and joins LAN games on your network.</string>
</dict>
</plist>
"""


def shipped_maps() -> list:
    """The remade maps a package carries: all of MAPS's but UNSHIPPED_MAPS."""
    return [m for m in sorted(MAPS.glob("*.hxm")) if m.name not in UNSHIPPED_MAPS] if MAPS.exists() else []


def shipped_mod_files() -> list:
    """The mods' files a package carries: not a mod's art/, which is built from the player's own
    game files (tools/hxunit.py), nor the mods of UNSHIPPED_MODS."""
    return [f for f in sorted(MODS.rglob("*")) if f.is_file() and "art" not in f.relative_to(MODS).parts
            and f.relative_to(MODS).parts[0] not in UNSHIPPED_MODS]


def version() -> str:
    m = re.search(r'#define HYPERION_VERSION_TEXT "Hyperion-([^"]+)"', VERSION_H.read_text())
    if not m:
        sys.exit(f"no HYPERION_VERSION_TEXT in {VERSION_H}")
    return m.group(1)


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def experimental(path: Path) -> bool:
    """A map past the targets for now, 32 players and 512 tiles a side (PRD 2026-10-12):
    the skirmish list leaves it to an Experimental folder (the user, 2026-10-13)."""
    with zipfile.ZipFile(path) as z:
        doc = json.loads(z.read("map.json").decode("utf-8"))
    return len(doc.get("players", [])) > 32 or max(doc.get("size", [0, 0])) > 512


def crlf(text: str) -> bytes:
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def lf(text: str) -> bytes:
    return text.replace("\r\n", "\n").encode("utf-8")


def package_linux(args, ver: str) -> int:
    """The Linux package: the same files as the zip's, as a .tar.gz for unpacking in place."""
    built = ENGINE / "src117" / "build" / f"linux-{args.linux}"
    name = "Hyperion.x86_64" if args.linux == "x64" else "Hyperion.aarch64"
    program, sdl = built / name, built / "libSDL3.so.0"
    for p in [program, sdl, *DOCS]:
        if not p.exists():
            sys.exit(f"missing {p} (python tools/build_engine_linux.py --arch {args.linux})")
    out = args.out / f"hyperion_{ver}_linux-{args.linux}_{time.strftime('%Y%m%d_%H%M%S')}"
    out.mkdir(parents=True)
    listing = []
    archive = out / f"Hyperion-{ver}-linux-{args.linux}.tar.gz"
    now = int(time.time())
    with tarfile.open(archive, "w:gz") as t:
        def put(name: str, data: bytes, mode: int = 0o644):
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, now
            t.addfile(info, io.BytesIO(data))
            listing.append(f"{name}  {len(data)} bytes  sha1 {hashlib.sha1(data).hexdigest()}")
        put(name, program.read_bytes(), 0o755)
        put("libSDL3.so.0", sdl.read_bytes(), 0o755)
        for f in shipped_mod_files():
            put("Data/mods/" + f.relative_to(MODS).as_posix(), f.read_bytes())
        for f in sorted((NOTES / "media").rglob("*")) if (NOTES / "media").is_dir() else []:
            if f.is_file():
                put("Data/notes/" + f.relative_to(NOTES).as_posix(), f.read_bytes())
        for f in sorted(WEB.rglob("*")) if WEB.is_dir() else []:
            if f.is_file():
                put("Data/web/" + f.relative_to(WEB).as_posix(), f.read_bytes())
        for f in sorted(HUDS.rglob("*")) if HUDS.is_dir() else []:
            if f.is_file():
                put("Data/huds/" + f.relative_to(HUDS).as_posix(), f.read_bytes())
        for f in WEB_PROGRAM:
            if f.is_file():
                put("Data/web/play/" + f.name, f.read_bytes())
        for d in DOCS:
            put("docs/" + d.name, lf(d.read_text(encoding="utf-8")))
        for f in TOOLS:
            tool = "Data/tools/" + ("cutscenes/" if f.suffix == ".json" else "") + f.name
            put(tool, lf(f.read_text(encoding="utf-8")), 0o755 if f.suffix == ".py" else 0o644)
        put("docs/live-guide.html", lf(GUIDE.read_text(encoding="utf-8")))
        cpu = "x86-64 (Intel or AMD)" if args.linux == "x64" else "ARM64 (aarch64)"
        put("docs/licenses/fonts.txt", lf(font_licenses()))
        put("README.txt", lf(README_LINUX.format(version=ver, arch=args.linux, cpu=cpu, program=name)))
        maps = shipped_maps()
        for m in maps:
            put("Data/maps/Hyperion/" + ("Experimental/" if experimental(m) else "") + m.name, m.read_bytes())
    (out / "contents.txt").write_bytes(lf("\n".join(listing) + "\n"))
    keep_symbols(ver, release=True, program=program)
    print(f"{archive}  ({archive.stat().st_size:,} bytes)")
    print(f"{len(maps)} test maps; contents.txt lists everything")
    return 0


def package_mac(args, ver: str) -> int:
    """The Mac package: Hyperion.app, signed ad hoc, and the Linux package's folders beside it,
    zipped by ditto. Made on the Mac."""
    if sys.platform != "darwin":
        sys.exit("--mac packages on a Mac: tools/mac_sync.py sends the tree there")
    import shutil
    built = ENGINE / "src117" / "build" / "mac-arm64"
    program, sdl = built / "Hyperion.arm64", built / "libSDL3.0.dylib"
    for p in [program, sdl, *DOCS]:
        if not p.exists():
            sys.exit(f"missing {p} (python3 tools/build_engine_linux.py)")
    out = args.out / f"hyperion_{ver}_mac-arm64_{time.strftime('%Y%m%d_%H%M%S')}"
    stage = out / f"Hyperion-{ver}-mac"
    app = stage / "Hyperion.app" / "Contents"
    (app / "MacOS").mkdir(parents=True)
    (app / "Resources").mkdir()
    listing = []

    def put(name: str, data: bytes, mode: int = 0o644):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(mode)
        listing.append(f"{name}  {len(data)} bytes  sha1 {hashlib.sha1(data).hexdigest()}")

    put("Hyperion.app/Contents/MacOS/Hyperion", program.read_bytes(), 0o755)
    put("Hyperion.app/Contents/MacOS/libSDL3.0.dylib", sdl.read_bytes(), 0o755)
    m = re.search(r"v?(\d+)\.(\d+)\.(\d+)", ver)
    short = ".".join(m.groups()) if m else "0.0.0"
    number = re.search(r"HYPERION_BUILD_NUMBER (\d+)", (built / "build_id.h").read_text()) if (built / "build_id.h").exists() else None
    put("Hyperion.app/Contents/Info.plist", lf(INFO_PLIST.format(short=short, build=number.group(1) if number else short)))
    put("Hyperion.app/Contents/PkgInfo", b"APPL????")
    # the icon: hyperion.ico's largest picture at each of the icon set's sizes
    work = out / "icon"
    iconset = work / "Hyperion.iconset"
    iconset.mkdir(parents=True)
    png = work / "hyperion.png"
    subprocess.run(["sips", "-s", "format", "png", str(ENGINE / "hyperion" / "hyperion.ico"), "--out", str(png)],
                   check=True, capture_output=True)
    for side in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{side}x{side}{'@2x' if scale == 2 else ''}.png"
            subprocess.run(["sips", "-z", str(side * scale), str(side * scale), str(png), "--out", str(iconset / name)],
                           check=True, capture_output=True)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(app / "Resources" / "Hyperion.icns")], check=True)
    listing.append("Hyperion.app/Contents/Resources/Hyperion.icns")
    shutil.rmtree(work)

    for f in shipped_mod_files():
        put("Data/mods/" + f.relative_to(MODS).as_posix(), f.read_bytes())
    for f in sorted((NOTES / "media").rglob("*")) if (NOTES / "media").is_dir() else []:
        if f.is_file():
            put("Data/notes/" + f.relative_to(NOTES).as_posix(), f.read_bytes())
    for f in sorted(WEB.rglob("*")) if WEB.is_dir() else []:
        if f.is_file():
            put("Data/web/" + f.relative_to(WEB).as_posix(), f.read_bytes())
    for f in sorted(HUDS.rglob("*")) if HUDS.is_dir() else []:
        if f.is_file():
            put("Data/huds/" + f.relative_to(HUDS).as_posix(), f.read_bytes())
    for f in WEB_PROGRAM:
        if f.is_file():
            put("Data/web/play/" + f.name, f.read_bytes())
    for d in DOCS:
        put("docs/" + d.name, lf(d.read_text(encoding="utf-8")))
    for f in TOOLS:
        tool = "Data/tools/" + ("cutscenes/" if f.suffix == ".json" else "") + f.name
        put(tool, lf(f.read_text(encoding="utf-8")), 0o755 if f.suffix == ".py" else 0o644)
    put("docs/live-guide.html", lf(GUIDE.read_text(encoding="utf-8")))
    put("docs/licenses/fonts.txt", lf(font_licenses()))
    put("README.txt", lf(README_MAC.format(version=ver)))
    maps = shipped_maps()
    for m in maps:
        put("Data/maps/Hyperion/" + ("Experimental/" if experimental(m) else "") + m.name, m.read_bytes())

    # signed ad hoc, the library first, then the app around it; checked as macOS will
    subprocess.run(["codesign", "--force", "-s", "-", str(app / "MacOS" / "libSDL3.0.dylib")], check=True)
    subprocess.run(["codesign", "--force", "-s", "-", "--identifier", "io.github.mad-man-dan.hyperion",
                    str(stage / "Hyperion.app")], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(stage / "Hyperion.app")], check=True)
    archive = out / f"Hyperion-{ver}-mac-arm64.zip"
    subprocess.run(["ditto", "-c", "-k", "--keepParent", str(stage), str(archive)], check=True)
    (out / "contents.txt").write_bytes(lf("\n".join(listing) + "\n"))
    keep_symbols(ver, release=True, program=program)
    print(f"{archive}  ({archive.stat().st_size:,} bytes)")
    print(f"{len(maps)} test maps; contents.txt lists everything; {stage} is the unzipped folder")
    return 0


def windows_files(ver: str) -> list:
    """The Windows zip's files: each its name in the zip and its source, a file or the bytes."""
    files = [("Hyperion.exe", BUILT), ("SDL3.dll", SDL3)]
    for f in shipped_mod_files():
        files.append(("Data/mods/" + f.relative_to(MODS).as_posix(), f))
    for f in sorted((NOTES / "media").rglob("*")) if (NOTES / "media").is_dir() else []:
        if f.is_file():
            files.append(("Data/notes/" + f.relative_to(NOTES).as_posix(), f))
    for f in sorted(WEB.rglob("*")) if WEB.is_dir() else []:
        if f.is_file():
            files.append(("Data/web/" + f.relative_to(WEB).as_posix(), f))
    for f in sorted(HUDS.rglob("*")) if HUDS.is_dir() else []:
        if f.is_file():
            files.append(("Data/huds/" + f.relative_to(HUDS).as_posix(), f))
    for f in WEB_PROGRAM:
        if f.is_file():
            files.append(("Data/web/play/" + f.name, f))
    for d in DOCS:
        files.append(("docs/" + d.name, crlf(d.read_text(encoding="utf-8"))))
    for t in TOOLS:
        files.append(("Data/tools/" + ("cutscenes/" if t.suffix == ".json" else "") + t.name,
                      crlf(t.read_text(encoding="utf-8"))))
    for t in CPP_CLIENT:
        files.append(("Data/tools/hxlive_cpp/" + t.name, crlf(t.read_text(encoding="utf-8"))))
    files.append(("docs/live-guide.html", crlf(GUIDE.read_text(encoding="utf-8"))))
    files.append(("docs/licenses/fonts.txt", crlf(font_licenses())))
    files.append(("README.txt", crlf(README.format(version=ver))))
    for m in shipped_maps():
        files.append(("Data/maps/Hyperion/" + ("Experimental/" if experimental(m) else "") + m.name, m))
    return files


def keep_symbols(ver: str, release: bool, program: Path = BUILT) -> None:
    """The built program and its symbols copied to SYMBOLS/<version>_b<build>_<commit>_<id>/, so
    a crash report from that build can be read (tools/crash_symbols.py). On Windows they are
    Hyperion.exe and its PDB, and the id is the PDB's: its GUID's start and its age, which the
    linker raises at each link, as builds not committed share the rest. On Linux the program holds
    its debug info, on a Mac the .dSYM beside it does; the id is the platform and the start of the
    GNU build id or the UUID. A packaged build's are kept, with release.txt; of the others, the
    last SYMBOLS_KEEP."""
    from crash_symbols import elf_id, macho_id, pe_id
    build_id = program.parent / "build_id.h"
    if program.suffix == ".exe":
        found = pe_id(program) if program.is_file() else None
        keep = [program, program.with_suffix(".pdb")]
        tag = found and f"{found[0].split()[-1][:8]}-{found[0].split()[-1][32:]}"  # the GUID's start, the age
    else:
        found = (elf_id(program) or macho_id(program)) if program.is_file() else None
        dsym = program.with_name(program.name + ".dSYM")
        keep = [program] + ([dsym] if dsym.is_dir() else [])
        tag = found and f"{program.parent.name}_{found[0][:8].lower()}"
        if found and macho_id(program) and not dsym.is_dir():
            print(f"[symbols] no {dsym.name}: this Mac build's frames can be named only beside its objects")
    if not all(p.exists() for p in keep) or not build_id.is_file() or found is None:
        print(f"[symbols] no {' or '.join(p.name for p in keep)}, build_id.h or build id: reports from this build"
              " will show offsets only")
        return
    text = build_id.read_text()
    number = re.search(r"HYPERION_BUILD_NUMBER (\d+)", text)
    commit = re.search(r'HYPERION_COMMIT "([^"]+)"', text)
    folder = SYMBOLS / f"{ver}_b{number.group(1) if number else 0}_{commit.group(1) if commit else 'unknown'}_{tag}"
    folder.mkdir(parents=True, exist_ok=True)
    for src in keep:
        if src.is_dir() and not (folder / src.name).exists():
            shutil.copytree(src, folder / src.name)
        elif src.is_file() and not (folder / src.name).is_file():
            shutil.copy2(src, folder / src.name)
    if release:
        (folder / "release.txt").write_text(time.strftime("packaged %Y-%m-%d %H:%M:%S\n"))
    others = sorted((d for d in SYMBOLS.iterdir() if d.is_dir() and not (d / "release.txt").exists()),
                    key=lambda d: d.stat().st_mtime)
    for old in others[:-SYMBOLS_KEEP]:
        shutil.rmtree(old, ignore_errors=True)
    print(f"[symbols] {folder}")


def install_write(path: Path, data: bytes) -> str:
    """data written at path unless it holds it already: "new", "updated" or "" when the same."""
    if path.is_file() and path.read_bytes() == data:
        return ""
    state = "updated" if path.exists() else "new"
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.write_bytes(data)
    except PermissionError:
        # a running Hyperion.exe, or the SDL3.dll it loaded, cannot be written but can be
        # renamed: it goes on as <name>.old, and the next start is the new one
        old = path.with_name(path.name + ".old")
        old.unlink(missing_ok=True)
        path.rename(old)
        path.write_bytes(data)
        state += ", the running one left as " + old.name
    return state


def install(folders: list, ver: str) -> int:
    """The Windows zip's files put in each folder as unzipping it there would: only what differs
    is written, and nothing else in the folder is touched."""
    for p in [BUILT, SDL3, *DOCS]:
        if not p.exists():
            sys.exit(f"missing {p}")
    files = windows_files(ver)
    failed = 0
    for folder in folders:
        if not folder.is_dir():
            print(f"[install] {folder}: no such folder")
            failed += 1
            continue
        # an earlier install's renamed program, once nothing runs it
        for name in ("Hyperion.exe.old", "SDL3.dll.old"):
            try:
                (folder / name).unlink(missing_ok=True)
            except PermissionError:
                pass
        changed, same = [], 0
        for name, src in files:
            try:
                state = install_write(folder / name, src.read_bytes() if isinstance(src, Path) else src)
            except OSError as e:
                print(f"[install] {folder / name}: {e}")
                failed += 1
                continue
            if state:
                changed.append(f"  {name}  ({state})")
            else:
                same += 1
        print(f"[install] {folder}: Hyperion {ver}, {len(changed)} files written, {same} already as built")
        for line in changed:
            print(line)
    keep_symbols(ver, release=False)
    return 1 if failed else 0


def install_folders() -> list:
    """The folders engine/install_to.txt names."""
    if not INSTALL_TO.is_file():
        return []
    lines = (l.split("#", 1)[0].strip() for l in INSTALL_TO.read_text(encoding="utf-8").splitlines())
    return [Path(l) for l in lines if l]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", action="store_true", help="build Hyperion.exe first")
    ap.add_argument("--out", type=Path, default=ENGINE / "testrun" / "share", help="where the zips go")
    ap.add_argument("--linux", choices=["x64", "arm64"], help="package the Linux build for this machine")
    ap.add_argument("--mac", action="store_true", help="on a Mac: package the Mac build as Hyperion.app")
    ap.add_argument("--install", nargs="?", const="", metavar="FOLDER",
                    help="put the Windows zip's files in FOLDER, or in each folder engine/install_to.txt names")
    args = ap.parse_args()

    if args.build and args.linux:
        if subprocess.run([sys.executable, str(ENGINE / "tools" / "build_engine_linux.py"), "--arch",
                           args.linux]).returncode != 0:
            sys.exit("build_engine_linux.py failed")
    elif args.build and subprocess.run([sys.executable, str(ENGINE / "tools" / "build_engine_x64.py"),
                                        "--no-stormlib"]).returncode != 0:
        sys.exit("build_engine_x64.py failed")
    # the release notes name this version, and docs\release-notes.md is written from them
    notes = [sys.executable, str(ENGINE / "tools" / "release_notes.py")]
    if subprocess.run(notes + ["check"]).returncode != 0 or subprocess.run(notes + ["md"]).returncode != 0:
        sys.exit("release_notes.py: the release notes do not match the version (docs/versioning.md)")
    if args.linux:
        return package_linux(args, version())
    if args.mac:
        return package_mac(args, version())
    if args.install is not None:
        folders = [Path(args.install)] if args.install else install_folders()
        if not folders:
            sys.exit(f"--install: no folder given and none in {INSTALL_TO}")
        return install(folders, version())
    for p in [BUILT, SDL3, *DOCS]:
        if not p.exists():
            sys.exit(f"missing {p}")

    ver = version()
    out = args.out / f"hyperion_{ver}_{time.strftime('%Y%m%d_%H%M%S')}"
    out.mkdir(parents=True)
    listing = []

    build_zip = out / f"Hyperion-{ver}.zip"
    files = windows_files(ver)
    with zipfile.ZipFile(build_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for name, src in files:
            if isinstance(src, Path):
                z.write(src, name)
                listing.append(f"{name}  {src.stat().st_size} bytes  sha1 {sha1(src)}")
            else:
                z.writestr(name, src)
                listing.append(f"{name}  {len(src)} bytes")

    maps = [name for name, _ in files if name.startswith("Data/maps/")]
    (out / "contents.txt").write_bytes(crlf("\n".join(listing) + "\n"))
    keep_symbols(ver, release=True)
    print(f"{build_zip}  ({build_zip.stat().st_size:,} bytes)")
    print(f"{len(maps)} test maps; contents.txt lists everything")
    return 0


if __name__ == "__main__":
    sys.exit(main())
