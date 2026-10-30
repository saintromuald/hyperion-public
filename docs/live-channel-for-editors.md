# Hyperion's live channel for map editors

This is for the author of a map editor, or of a script or an AI, who wants to test a map in
a running Hyperion without restarting it: push a map's tiles and see them in the game, load
a map file, and script the camera and the units as a story scene would. It says how to reach
the channel, what the messages are, and the cutscene format. `hxm-for-editors.md` beside
this file covers the map format itself.

Written 2026-10-15, for Hyperion v0.3.0-beta. The messages here are stable for testing; scenes
stored in a map and started by its triggers are planned, and will use the same step format.

The live guide shows the same with each part tried on the game, the message it sends and the
code for it in C++, Python and JavaScript: `http://127.0.0.1:27117/guide` while Hyperion runs
with `--control`.

| | in a Hyperion package | in Hyperion's source |
|---|---|---|
| this file | `docs\live-channel-for-editors.md` | `docs/` |
| the live guide | served at `/guide`; `docs\live-guide.html` to read offline | `engine/mods/debug-tools/guide.html` |
| the C++ client | `Data\tools\hxlive_cpp\` (`hxlive.h`, `example.cpp`, `build.bat`) | `engine/tools/hxlive_cpp/` |
| the Python client | `Data\tools\hxlive.py` | `engine/tools/hxlive.py` |
| the sample scenes | `Data\tools\cutscenes\` | `engine/mods/debug-tools/cutscenes/` |

## Reaching the channel

The channel is a WebSocket on `127.0.0.1`, port 27117 unless Hyperion is started with
`--control=<port>`. Start Hyperion with `--control` to open it; it is also open while Debug
Tools is the active mod (the Mods menu) and while its canvas runs. It listens on the loopback
address only, so only programs on the player's own machine reach it, and only with its key:
each start writes a new one to `%LOCALAPPDATA%\Hyperion\control-<port>.key`
(`~/.local/share/Hyperion/` on Linux and the Mac), which a client sends as `?t=<key>` on the
WebSocket's address, `ws://127.0.0.1:27117/?t=<key>`. Without it the channel answers 401.
The clients Hyperion ships read the file themselves, and Debug Tools' Copy guide link gives a
page's address with the key in it. While it is open,
`http://127.0.0.1:27117/guide` is the live guide and `http://127.0.0.1:27117/` Debug Tools'
panel, which has the same commands among its tools.

For an AI assistant, `http://127.0.0.1:27117/ai-context` is a briefing on the channel: what it
is for, every command, the scene steps, where the docs and clients are in this install, and how
to connect (from `mods\debug-tools\ai-context.md`, its paths and port filled in). Debug Tools'
overlay has Copy for AI, which copies it with the game's status at that moment, and Copy guide
link; the panel has Copy for AI too. Paste it into the assistant's chat to start.

Started with `--control`, Hyperion goes straight to its menus: 1.17's opening cinematic, which
otherwise plays on a player's first start, waits for a start without it. A game the channel
starts with `map_load` does not open 1.17's tips either, which would pause it.

A test from an editor goes like this: the editor starts `Hyperion.exe --control` once, waits
until `status` answers, and from then on sends `map_push` or `map_load` each time the map is
to be tried, with a `scene` when the test is a cutscene. Hyperion stays running between tests.
Map switches go through Hyperion's own menus, which are on unless the player turned them off
in Settings.

A message is one JSON object, `{"id": <any>, "cmd": "<name>", ...}`. The answer carries the
same `id` and `"ok"`; when `ok` is false, `"error"` says why in a sentence. Send one message
and wait for its answer before the next.

A browser page may connect only if Hyperion served it; a client that is not a browser (no
`Origin` header) may always connect.

For C++, `tools\hxlive_cpp\hxlive.h` is a client in one header over Windows' own WinHTTP, with
nothing else to link: a connection, a call that sends a message and waits for its answer, the
messages built as strings, MTXM to base64, and a wait for a map switch. `example.cpp` uses it
as a whole program and `build.bat` builds that with MSVC. The editor's own JSON library reads
the answers; the header has only what is easy to get wrong.

`hxlive.py` is the same in Python's standard library alone, and works from the command line:

    python Data\tools\hxlive.py status
    python Data\tools\hxlive.py map push --tileset jungle --size 128x128 --scene Data\tools\cutscenes\temple-flyover.json --reveal
    python Data\tools\hxlive.py map push --from "maps\(2)Volcanis.scm" --scene Data\tools\cutscenes\volcanis-last-stand.json --reveal
    python Data\tools\hxlive.py map load "D:\maps\test.scx" --ums --wait
    python Data\tools\hxlive.py cutscene play Data\tools\cutscenes\canvas-sample.json
    python Data\tools\hxlive.py send "{\"cmd\": \"reveal\"}"

`--wait` waits until the new map runs and prints its status; `--reveal` waits too, then lifts
the fog. `map push --from` sends the map file's path for Hyperion to read, or a
`scenario.chk`'s tiles read here.

`status` answers, among the rest, `"state"` (`"front end"`, `"game"`, `"menus"`), `"frame"`,
`"map"` (in a game, its size in map pixels), `"view"` (in a game, where the camera looks:
`{"x", "y", "zoom"}`, as a camera step names it), `"cutscene"` (as `cutscene_status` gives it)
and `"map_swap"` (`"none"`, `"going out"`, `"loading"`), with `"map_swap_error"` when the last
switch could not start its map.

## Testing a map: map_push and map_load

Both work from a single-player game, a replay or Hyperion's front end. From a game, the
picture goes out over `ms` milliseconds (1000 unless given): the console drops and fades and
the map shrinks to 86 % as it goes to black. The game is then left as Quit Mission leaves it,
the map is started from the menus, and it comes in as a skirmish's start does: out of black,
settling from 86 % to full size, the console rising last. Nothing is saved from the game left.

    {"cmd": "map_push", "tileset": "jungle", "width": 128, "height": 128,
     "tiles_b64": "<MTXM as base64>", "ms": 1000, "scene": {...}}

`map_push` makes a map from what it is given and plays it. It is the canvas map of Debug
Tools: player 1 (the local player) starts on the west and player 2, a computer with no AI
script, on the east, every unit and tech available, the map explored but fogged (send
`reveal` to lift the fog). What it takes:

| field | |
|---|---|
| `tileset` | `badlands`, `platform`, `install`, `ashworld`, `jungle`, `desert`, `ice` or `twilight`, or `"0"` to `"7"`; badlands when left out |
| `width`, `height` | in tiles, each 64, 96, 128, 192 or 256; 64 when left out |
| `tiles` | the map's MTXM values (group × 16 + variation), a row at a time, width × height of them |
| `tiles_b64` | the same as base64 of little-endian 16-bit values, which is the MTXM section's own bytes; better for big maps |
| `fill` | one MTXM value for every cell, in place of `tiles` |
| `rects` | `[{"x", "y", "w", "h", "tile"}]` in tiles, painted over the tiles in turn |
| `from` | a map file (`.scm`, `.scx`, `.hxm`) whose tileset, size and tiles are taken in place of the fields above; its units, locations and triggers stay behind |
| `ms` | how long the way out takes |
| `scene` | a cutscene (below), played when the map starts |

With none of `tiles`, `tiles_b64` and `fill`, the ground is the tileset's first plain
buildable group, varied as a brush varies it. Each tile's group must be one the tileset has;
the answer names the first that is not.

    {"cmd": "map_load", "path": "D:\\maps\\test.scx", "ums": true, "scene": {...}}

`map_load` plays a map file that is on the player's machine: `.scm`, `.scx` or `.hxm`. With
`ums` true it plays as Use Map Settings (the map's own players and triggers); otherwise as
melee, as `--map` plays one, with the command line's race and computers. When a map cannot
start, `status` gives `map_swap_error` and Hyperion stays on its menus; for an `.hxm`,
`hyperion_hxm.log` in the `Hyperion` folder says why.

`path` and `from` take a full path, or a name Hyperion looks for as the game looks for a map:
in its data folder, then the StarCraft folder (so `maps\(2)Volcanis.scm` finds the install's),
then each folder given with `--maps`, and failing that by the file's name alone in any folder
under those, so `(4)Lost Temple.scm` finds the one in `maps\ladder\`. A pushed map is 64, 96,
128, 192 or 256 tiles a side; a larger one is played with `map_load`.

    {"cmd": "maps", "filter": "temple"}
    {"cmd": "map_info", "path": "(4)Lost Temple.scm"}

`maps` lists the map files Hyperion finds in those places and the folders in them, each with
its `name`, `folder`, `root` and full `path`; `filter` keeps the ones whose folder and name hold
it, and `fresh: true` reads the folders again at once (otherwise after ten seconds).
`map_info` reads one map without playing it: its size, tileset, player slots, the title and
description the game shows, its start locations (pixels and player), whether `map_push` can take
its terrain (`pushable`), and `preview`, its minimap with the starts marked, served as a picture
under `/snapshots/`. Debug Tools' panel uses both for its map list.

An editor that keeps its map in memory can send its MTXM with `map_push` after every change,
and `map_load` a saved file when the test needs its units, locations and triggers.

`reveal: true` on either lifts the fog for every player as the map comes in, before its scene
starts, as `reveal` would once the game runs.

A scene file may name the map it was made for, which the panel's and the guide's Play switch
to first: `"map": {"push": {"tileset": "badlands", "width": 64, "height": 64}}` for a made map,
or `"map": {"from": "(2)Volcanis.scm", "or": {...}}` for a map file, with `or` a pushed map to
use when that file is not there. `cutscene_play` itself ignores it, so a scene played with
`cutscene_play` plays where the game is.

A pushed map starts explored but fogged, as the canvas does. Send `{"cmd": "reveal"}` once the
new game runs (`status` says `"state": "game"` and `"map_swap": "none"`) to lift the fog for
every player, which a camera tour of the map wants.

## Cutscenes: cutscene_play

    {"cmd": "cutscene_play", "steps": [...], "hold": true, "skip": true, "rest": true}

The steps run in turn. Each step waits for the one before to finish, unless that one was
sent with `"async": true`, in which case the next starts at once, under it: a caption can
stay up while the camera moves. While a cutscene plays, the player's keys and clicks and the
game's cursor are held (`hold`); Escape ends it (`skip`). At its end the picture eases back to
the game's in half a second (`rest`; with `rest` false the picture stays as the last step left
it, which suits a scene that ends in black before a `map_load`). A new `cutscene_play` ends
the one playing; `cutscene_stop` ends it as Escape does; `cutscene_status` says which step
plays and lists the units its spawns made, by label.

A cutscene that only moves the camera, the picture and words plays in any game and in a
replay. One with `spawn`, `order`, `kill` or `remove` plays only in a single-player game and
marks the game as changed. The steps are checked before any runs; the answer names the first
wrong step.

Places are map pixels (32 a tile), from the map's top left. Times are milliseconds (`ms`).
A unit is named by a label a `spawn` gave (`"as"`), by its id (as `units` and `spawn` answer
it), or by a list of either. Unit types are 1.17's ids or the names `.hxm` maps use
(`terran_marine`, `zerg_zergling`, `protoss_zealot`).

| step | fields | what it does |
|---|---|---|
| `camera` | `x`, `y`, `zoom`, `ms`, `ease` | the view's middle glides to x, y and the zoom to `zoom` (1 the game's own, 2 twice the map across, down to 0.5 closer than a game allows and up to the whole map, as far as a replay goes, past the player's own limit); either may be left out |
| `follow` | `unit`, `ms`, `zoom` | the camera glides onto the unit and stays on it until the next camera step |
| `fade` | `to`, `ms`, `scale`, `ease` | black over the picture: `to` is `"black"`, `"clear"` or an amount 0-1; `scale` zooms the picture itself as it fades (0.86 a skirmish's start, 1 as drawn, above 1 closer) |
| `bars` | `on`, `size`, `hud`, `ms` | cinema bars top and bottom, `size` a share of the height each (0.12); the console goes with them unless `hud` is true |
| `hud` | `show`, `ms` | the console dropped and faded out, or back |
| `depart` | `ms` | the picture goes out as `map_push` takes it out |
| `arrive` | | the picture comes in as a skirmish's start brings it |
| `caption` | `text`, `ms`, `at` | words over the picture, in the bottom bar when the bars are in; `at` is `bottom`, `middle` or `top`; `\n` breaks a line |
| `text` | `text`, `ms` | a line among the game's messages, as Display Text Message puts it |
| `transmission` | `unit`, `portrait`, `text`, `sound`, `ms` | as the trigger Transmission: the portrait (a unit type, else the unit's own) talks, the unit flashes and pings the minimap, the text shows and the sound plays; with the console hidden the text comes as a caption |
| `sound` | `path` | a sound from the game's files, as Play WAV plays it |
| `spawn` | `unit`, `x`, `y`, `player`, `count`, `as`, `attack` or `move` | units made as Create Unit makes them, then sent to `[x, y]` |
| `order` | `units`, `to`, `order` | `move`, `attack` or `patrol` to `[x, y]`, as the Order action gives it |
| `kill`, `remove` | `units` | the units killed (their deaths shown) or removed |
| `wait` | `ms` | nothing, for a while |

`ease` is `even`, `ease`, `out` or `in_out`; a camera glides `in_out` and a fade eases `ease`
unless told otherwise. When `ms` is left out a step takes a time that suits it: a second for
the camera, 0.8 s for a fade, and for words a second and a half and a reading pace.

The sample scenes show the steps at work: `canvas-sample.json` (the canvas: a fade in, a
zoom out and in, a transmission, a follow), `theta-9-haunting.json` (a dark deck and something
unseen: a slow drift, hard cuts, a whip pan, a push-in), `temple-flyover.json` (a camera tour
of Lost Temple's terrain), `volcanis-last-stand.json` (a battle: a crane down, a montage of
cuts, a tracking shot) and `tarsonis-starfall.json` (fleets in space: an orbit, dips to black,
a chase). Their places are for the map each is named after. A short scene, which
`canvas-sample.json` has in full:

    {"steps": [
      {"do": "fade", "to": "black", "ms": 0, "scale": 0.86},
      {"do": "bars", "on": true, "ms": 0},
      {"do": "spawn", "unit": "zerg_zergling", "x": 1450, "y": 1024, "player": 1, "count": 12, "as": "lings"},
      {"do": "camera", "x": 700, "y": 1024, "ms": 0},
      {"do": "caption", "text": "Somewhere on the rim of the Koprulu sector", "ms": 2600, "async": true},
      {"do": "fade", "to": "clear", "ms": 1400, "scale": 1.0},
      {"do": "order", "units": "lings", "to": [700, 1024], "order": "attack"},
      {"do": "follow", "unit": "lings", "ms": 900},
      {"do": "wait", "ms": 2500},
      {"do": "fade", "to": "black", "ms": 1000}
    ]}

## What is planned

- Scenes kept in an `.hxm` map and started by its triggers, in this same step format.
- Cutscene styles of their own beyond 1.17's look: other captions, bars and transitions.
- `map_push` with the map's units, locations and triggers, and past 256 tiles through `.hxm`.
