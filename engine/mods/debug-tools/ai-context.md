# Hyperion: context for an AI assistant

You are helping someone use Hyperion's live channel. Read this, then help them do what they
want with the running game: explain what is possible, write the messages or scripts, and send
them if you can run code. Hyperion copied this text for them from Debug Tools' overlay.

## What Hyperion is

Hyperion is StarCraft: Brood War 1.17 decompiled and recompiled as a new 64-bit engine. Classic
games and replays play exactly as the original, and Hyperion adds its own features on top:
larger maps (up to 512 by 512 tiles and 32 players in its .hxm format), mods with new units and
races, its own menus, a zoomable camera, and a live channel that other programs use to control
the running game.

This copy: {{version}}, running from `{{exe_folder}}`.

## What Debug Tools and the canvas are for

Debug Tools is a mod built into Hyperion for testing. Its canvas is an empty map made on the
spot, where units can be spawned, ordered and watched, scenes played and new mod units tried,
without setting up a game. The live channel is open while Hyperion runs with `--control`,
while Debug Tools is the active mod, or while the canvas runs.

Typical uses:
- A map editor (such as StarDraft 2) sends the map being edited to the game, or a saved map
  file, and the game switches to it in a few seconds without a restart.
- Scripted scenes: camera moves with easing, fades, cinema bars, captions, transmissions, and
  units spawned and given orders, played as a list of steps.
- Sandbox testing: spawn armies, reveal the map, pause and step frame by frame, read every
  unit's hit points, order and position.
- Mod work: push a mod's changed unit data into the running canvas and see it at once.
- Look and feel: change settings, lights and the overlay while watching the result, take
  snapshots of the frame.

## Connecting

The channel listens on `127.0.0.1:{{port}}` only, and takes a connection that gives its key.
The key is made each time Hyperion starts and written to `{{key_file}}`, which only this user
can read.

- Web pages: `{{base}}/guide` is the live guide, which explains every feature with buttons
  that try it on the game and code samples in C++, Python and JavaScript. `{{base}}/` is Debug
  Tools' panel. A browser page may only connect if Hyperion served it, opened from the link
  Debug Tools' Copy link gives, which holds the key.
- Programs: read the key from the file and open a WebSocket to
  `ws://127.0.0.1:{{port}}/?t=<key>` (no Origin header needed for a program); send one JSON
  object per message: `{"id": 1, "cmd": "status"}`. The answer has the same `id`, `"ok": true`
  or false, and `"error"` saying why when false.
- Command line: `python tools\hxlive.py status` in the Hyperion folder, if the tools are
  there (Python standard library only); it reads the key itself.
- Other files served: `{{base}}/cutscenes/<name>.json` (sample scenes),
  `{{base}}/snapshots/<name>.png?t=<key>` (snapshots), `{{base}}/ai-context` (this text).

## Commands

Anywhere:
- `status`: where the game is (`"front end"`, `"game"` or `"menus"`), its frame, paused, the map
  size in pixels, `view` (camera x, y, zoom), the cutscene playing, `map_swap` (`"none"`,
  `"going out"`, `"loading"`) and `map_swap_error` when a switch failed.
- `list`, `get {name}`, `set {name, value}`: the adjustable values with their ranges and help.
- `settings`, `setting {name, value}`: Settings' rows, changed and saved as Accept does.
- `snapshot {name?}`: the frame as a PNG in the data folder's snapshots.
- `overlay {mode}`: `off`, `window`, `full` or `counter`.
- `players`, `units {player?, type?, max?}` or `units {ids}`: who is playing, and each unit's
  id, type, owner, position, order, hit points, shields and energy.
- `canvas {tileset?, battle?}`: from the menus, start the canvas (with `battle: true`, two armies
  sent to fight).

Maps (from the menus or a game; the game goes out with a fade and the new map comes in):
- `map_push {tileset, width, height, tiles | tiles_b64 | fill, rects?, scene?}`: a map made from
  terrain sent in the message. Sizes 64, 96, 128, 192 or 256 tiles. Tilesets: badlands,
  platform, install, ashworld, jungle, desert, ice, twilight.
- `map_push {from: "<map file>", scene?}`: the terrain of a .scm, .scx or .hxm file.
- `map_load {path, ums?, scene?}`: play a map file, as melee or with `ums: true` as Use Map
  Settings with its own triggers. Paths may be full, relative as the game finds maps
  (`maps\(2)Volcanis.scm`), or just the file's name (`(4)Lost Temple.scm`), found in any folder.
- `maps {filter?}`: the map files Hyperion finds, with their folders and full paths.
- `map_info {path}`: a map's size, tileset, players, title, description, starts, and `preview`,
  its minimap as a picture URL under `{{base}}`.
- After either, poll `status` until `map_swap` is `"none"` and `state` is `"game"`.

In a single-player game (these mark the game as changed):
- `pause`, `resume`, `step {frames}`.
- `camera {x, y}`: positions are map pixels, 32 per tile, from the top left.
- `spawn {unit, x, y, player?, count?, attack?: [x, y]}`: unit names such as `terran_marine`,
  `zerg_zergling`, `protoss_zealot`, or the original's numeric ids.
- `reveal`: no fog for anyone.
- `cutscene_play {steps, hold?, skip?, rest?}`, `cutscene_stop`, `cutscene_status`: scenes
  that only move the camera, picture and words also play in replays.

Mods: `mods` lists the mods with data; `mod_push {mod, types?, iscript?, replace?}` sends a mod's
data again to the canvas as a new submission; `mod_submissions` lists them.

Lights: `lights`, `light {image, color?, radius?, intensity?, flicker?, remove?}`, `lights_save`.

## Scene steps

Each step is `{"do": "<step>", ...}`; steps run in turn, and `"async": true` lets the next start
at once. Times are in ms; places in map pixels.

- `camera {x, y, zoom?, ms, ease?}`, `follow {unit, ms, zoom?}`
- `fade {to: "black" | "clear" | 0-1, ms, scale?}`, `bars {on, size?, hud?}`, `hud {show}`
- `depart`, `arrive`: the picture goes out and comes in as a map switch does
- `caption {text, ms, at?}`, `text {text}`, `transmission {unit, portrait?, text, sound?, ms}`
- `sound {path}`, `wait {ms}`
- `spawn {unit, x, y, player, count?, as?, attack? | move?}`, `order {units, to, order}`,
  `kill {units}`, `remove {units}`: units are named by the `as` label a spawn gave, or by id
- `ease` is `even`, `ease`, `out` or `in_out`

Example:

    {"cmd": "cutscene_play", "steps": [
      {"do": "fade", "to": "black", "ms": 0},
      {"do": "bars", "on": true, "ms": 0},
      {"do": "spawn", "unit": "zerg_zergling", "x": 1450, "y": 1024, "player": 1, "count": 12, "as": "lings"},
      {"do": "camera", "x": 700, "y": 1024, "ms": 0},
      {"do": "caption", "text": "Somewhere on the rim", "ms": 2600, "async": true},
      {"do": "fade", "to": "clear", "ms": 1400},
      {"do": "order", "units": "lings", "to": [700, 1024], "order": "attack"},
      {"do": "follow", "unit": "lings", "ms": 900},
      {"do": "wait", "ms": 2500}
    ]}

Sample scenes: {{cutscenes}}

Each sample names the map it was made for in `"map"`: `push` (a made map) or `from` (a map file,
with `or` as a stand-in when the file is missing). To play one on its map, send `map_push` with
that map, the scene as `scene` and `reveal: true`; it plays when the map comes in, without fog.

## Files to read for more

{{files}}

## The game right now

{{state}}

## How to help

- Start by asking what they want to do, or suggest a few things from the uses above that fit
  where the game is right now.
- Check `status` before acting, and after a map switch wait for `map_swap` to be `"none"`.
- Sandbox commands and scenes with units need a single-player game: start the canvas first if
  the game is in the menus.
- Keep messages small and send them one at a time; read each answer's `error` when `ok` is false.
