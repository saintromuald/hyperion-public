# tools: driving, inspecting and measuring a running Hyperion

## Overview

Hyperion has one door into a running game and several ways of reading what it does through
it. The door is the live channel: a WebSocket on `127.0.0.1` that a script, an editor, a
browser page or an MCP server sends one JSON command at a time to, and that answers on the
game's own thread. Everything else in this area either feeds that channel or writes files
beside the game: Debug Tools' overlay and canvas (a map made on the spot, so a change can be
seen in a real game), the benchmark (a game of computers played as a replay, logged to a
folder), `hxbench.py` (two builds or two settings measured against each other on the same
replays), the perf and profile logs, the camera trace, and the crash report.

Nothing here changes the simulation except the sandbox commands, which say so and mark the
game as changed. The overlays, the traces, the benchmark and the measurements are presentation
only, so a replay still plays the same game under them.

## Key files and scripts

| File | What it does |
|---|---|
| `src117/port/tools/port_control.cpp`, `.h` | the live channel: the listening socket and its thread, the WebSocket handshake and framing, the small HTTP server, and the commands: `kCommands` names each one, what it needs (a game, a single-player game) and its `Cmd*` handler, and `Handle` runs them |
| `src117/port/tools/port_debugview.cpp` | Debug Tools' overlay: the game in a window with readings round it, full screen with the panels over it, or a counter; a layer over the game (`render.md`), whose area is where the GPU presenter puts the game |
| `src117/port/tools/port_trace.cpp` | the camera trace, the minimap press and drag, and a held scroll key, all driven by the channel |
| `src117/port/tools/port_crash.cpp`, `.h` | the crash report: a folder for each under `crashes\` with `report.txt`, `report.json`, a minidump and the run's logs, `hyperion_crash.log` a copy of the last; the handlers for a fault, `abort`, `std::terminate`, an invalid parameter and a pure call, installed by `PortInstallCrashReporter`; the notes it keeps (`PortCrashNote`, `PortCrashSession`) |
| `src117/port/tools/port_crashnotes.cpp`, `.h` | what the report says of the session: the game being played and the last five, the screen or the frame now |
| `src117/port/tools/port_startup.cpp`, `.h` | the start-up log, `hyperion_startup.log`: a line for each step, the error window when a start fails, the watchdog, and how the last start and session ended (`platform.md`, "The start-up log") |
| `src117/port/tools/port_errorwin.cpp`, `.h`, `port_errorwin_dlg.h` | the error window: what went wrong, where its log or report is, and a text to copy for a GitHub issue or Discord; Windows' dialog `IDD_PORT_ERROR`, or the drawn window; and `--crash-report <folder>`, the window a crashed copy starts for its report |
| `src117/port/tools/port_share.cpp`, `.h` | that text: a start-up log or a `report.json` boiled down to what helps, within Discord's 2,000 characters in a code block, with no folder, network address or name of the player's |
| `src117/port/tools/port_sysinfo.cpp`, `.h` | the build, the system, the displays, the presenter, the install and the run, gathered once for the start-up log and the crash report, the user's folder masked |
| `tools/crash_symbols.py` | a player's crash report named: the report's build id (the PDB's, the GNU build id or the Mach-O UUID) found among the builds `tools/package.py` kept in `testrun/share/symbols/` and the current builds, each frame named through dbghelp on Windows, `addr2line` (WSL's on Windows) or `llvm-symbolizer` for Linux, `llvm-symbolizer` for a Mac |
| `src117/port/tools/port_bench.cpp` | a benchmark run: its folder under `benchmarks\`, `settings.json`, `seconds.csv`, `summary.json`, `frames.csv` and `run.rep` |
| `src117/port/tools/port_benchinfo.cpp`, `.h` | what a run's overlay reads of the game: the selected units in detail, and each player's bank, supply, units and whether it is still in |
| `src117/port/tools/port_benchoverlay.cpp`, `.h` | what a run's overlay shows and where: layout, density, see-through, the seven Screen styles, and the changes made during a run |
| `src117/port/tools/port_benchview.cpp` | the run's screen itself: the top strip, the minimap, the numbers once a second, the parts, the frame-time graph and the end summary |
| `src117/port/tools/port_canvas.cpp`, `.h` | the canvas: a Use Map Settings scenario written into `mods\debug-tools\canvas.scx` and played, empty or with two armies; also the target of `map_push` |
| `src117/port/tools/port_framelog.cpp`, `.h` | the frame log: a row for every game frame and every frame shown, `HYPERION_FRAME_LOG` or the channel's `framelog` |
| `tools/hxbench.py` | before-and-after measurement: builds, configurations and settings over replays, as JSON lines |
| `tools/hxrender.py` | large games measured frame by frame: a bench of computer players, its replay watched at zoom 1 to 16 with the frame log on, and the report; snapshots of one frame two ways and their comparison; the game alone sampled over a stretch of a replay |
| `tools/hxlive.py` | the channel from the command line, standard library only; `ws_call` is the client `hxbench.py` reuses |
| `tools/hxlive_cpp/` | the same client for C++ in one header over WinHTTP (`hxlive.h`), with `example.cpp` and `build.bat`, for a map editor's author |
| `tools/control/hyperion_ctl.py` | a friendlier command line over the channel (units by name), and the `Client` class |
| `tools/control/directions.py` | every direction of a unit's image at once: unit types that start facing each of the 32 directions, a ring of them on the canvas, and a numbered 32-direction test sheet |
| `tools/control/hyperion_mcp.py` | the `hyperion` MCP server from `.mcp.json`: `status`, `snapshot`, `pause`, `step`, `camera`, `canvas`, `spawn`, `select`, `lights`, `dialogs` and the rest as tools |
| `tools/play.py` | play the latest build from `testrun/play/`, a game folder of its own it refreshes each run |
| `tools/snapdiff.py`, `tools/snapprobe.py` | name the ranges a rewind self-test or a write probe reported, by the 1.17 global or the linker symbol they fall in |

## The live channel

`PortControlStart` binds `127.0.0.1` only and starts one thread that moves bytes. It is tried
once: a port another program holds stays held, and the attempt is logged to
`hyperion_control.log` in the data folder. `PortControlPoll` runs every message on the game's
thread, called by the presenter each frame it shows and by the front end each of its frames.

The port is `PORT_CONTROL_PORT`, 27117, unless `--control=<port>` gives another.
`PortControlPoll` starts the channel when it is not running and one of three things is true,
checked every 60 polls: `--control` was given, the active mod asks for the channel (Debug
Tools does) and the player has allowed it (`PortModChannelAllowed`: `--mod` named it, or the
setting `modChannel.<id>` holds the mod's folder, which the Mods screen's question writes), or
the running game is the canvas `--canvas` asked for.

Once it is bound, `MakeKey` makes 128 random bits (or takes `HYPERION_CONTROL_KEY`) and
`WriteKey` writes them to `PortControlKeyFile(port)` through `PortWritePrivateFile`. The
WebSocket upgrade needs `?t=<key>` (`KeyGiven`, a constant-time compare) and is answered 401
without it; `/snapshots/` needs it too. The guide, the panel and the AI context are served
without it and carry it on from their own address. The key is written only after the bind, so
a second Hyperion that could not bind leaves the first one's file alone. While the channel is running,
`PortControlKeepsRunning` also keeps the game going when its window is not focused, so an
unwatched run does not stop.

A client sends one JSON object a message, `{"id": <any>, "cmd": "<name>", ...}`; the answer
carries the same `id` and `"ok"`, with `"error"` when that is false. A browser may connect
only from a page Hyperion served itself (any other `Origin` is refused), and a request naming
any host but `127.0.0.1` or `localhost` is refused. A plain GET serves Debug Tools' panel
(`mods\debug-tools\panel.html`), `/guide` the editors' guide, `/cutscenes/` the sample scenes,
`/snapshots/<name>.png` the frames the panel shows, and `/ai-context` the briefing the
overlay's Copy for AI copies (`PortControlAiContext`).

The commands, by what they do:

- where it is and what it holds: `status` (`hello` is the same), `players`, `units`,
  `dialogs`, `maps`, `map_info`, `mods`, `mod_submissions`, `settings`, `region`, `pathstats`,
  `hash` (the replay check's hash a domain at a time, with `dump` for the bytes).
- what is shown: `list`, `get`, `set` (the registered live values, each with its range,
  default, step and help), `snapshot`, `minimap`, `camera`, `colour`, `overlay`, `lights`,
  `light`, `lights_save`, `looks`, `look`, `look_unit`, `looks_save` (`render.md`, "Looks"),
  `setting`.
- the game's pace: `pause`, `resume`, `step` (with `frames` and `fast`), `clock`, `perf`,
  `trace`, `trace_rows`, `scroll`.
- measuring: `framelog {on?, folder?, mark?}` starts or stops the frame log and marks
  a moment in it; `profile {reset?, write?}`, with `HYPERION_PROFILE=1`, starts the sampler's
  counts again and writes them to a path.
- the sandbox, which changes the game: `spawn`, `clear`, `reveal` and `select` (single player;
  `select`'s `sound` plays the selection's voice, as a click does, which a test of a unit's
  voices needs), `allies` (the local player's alliance row, as 1.17's diplomacy dialog sends it), `canvas`,
  `map_push`, `map_load`, `mod_push`.
- cutscenes: `cutscene_play`, `cutscene_stop`, `cutscene_status`.
- input posted to the window as Windows posts it: `press`, `click` (`double` for a double
  click, its second press as a window with CS_DBLCLKS gets it; `phase` `down` or `up` for half a
  press, so a drag is a down, moves with `move`, then an up), and `trainer` for a key with
  modifiers a posted key cannot carry.
- added by another part with `PortControlCommand`, which answers them: the AI lab's
  `lab_status`, `lab_start`, `lab_pace`, `lab_stop`, `lab_games` and `lab_watch` (`lab.md`).
- `subscribe {topics}`: from then, every message another part publishes on those topics
  (`PortControlPublish`) is sent to the client as `{"topic": ..., "data": ...}`, until it closes
  or subscribes again; a client more than 8 MB behind misses them until it catches up. The AI
  lab's are `lab.workers`, `lab.games` and `lab.samples` (`lab.md`, the stream).

A value is registered with `PortControlFloat` (a float the caller owns) or
`PortControlValue` (a getter and a setter), once, at start or when the effect that owns it is
made; registering a name again replaces it. `fStep` of 1 marks a switch, 0 a value that slides.

Who uses it: Debug Tools' panel page and the live guide, both served by the channel itself;
`tools/hxlive.py` and `tools/control/hyperion_ctl.py` from a shell; `tools/hxlive_cpp/` from
an editor; the `hyperion` MCP server, which connects to 27117 or `HYPERION_CONTROL_PORT` and
reconnects after a restart; and `hxbench.py`, which starts its own Hyperion with
`--control=27217` and uses the channel for `clock`, `settings`, `setting` and `camera`.

## Debug Tools and the canvas

`PortDebugViewAvailable` is true in a run with `--control` or `HYPERION_DEBUG_VIEW`, and while
a tool mod's channel or the canvas's runs, except over a campaign's mission
(`RulesCampaignMission`), which no mod reaches (PRD 2026-10-27): there F6 does nothing and an
overlay turned on in a skirmish stays hidden (`PortDebugViewShown`). While the overlay is off,
a button at the top of the game opens it in the canvas, or in a run with `--control` or
`HYPERION_DEBUG_VIEW`; a skirmish played while a tool mod's channel is open shows no button,
and F6 opens the overlay there.
The overlay has four modes, `off`, `window`, `full` and `counter`; F6 goes from one to the
next and back (Settings' Hotkeys tab changes the key), the channel's `overlay` sets it by
name, and the panel's buttons do the same. In `window` the game is letterboxed into the
area its layer gives (`render.md`, the layers over the game) and nothing is drawn over it; in `full` the same panels are see-through
over the game. Round the game: the top strip (frame, speed, time, and buttons that change
Settings' frame rate, vertical sync and game speed at once), the game clock on the left
(`port_clock.cpp`), the numbers a second on the right (`port_perf.cpp`), and the last frames
shown at the foot. Setting a mode starts the clock if it was not running. The mouse over the
panels reaches the overlay, not the game. `PortBenchViewShown` takes precedence: a benchmark
run's overlay replaces this one.

The canvas is a map Hyperion writes and plays: flat ground of one tileset (Jungle's grass
unless another is named, `PORT_CANVAS_TILESET`; a tileset's grass where it has one, never
creep, since 2026-10-27), explored but
fogged, player 1 on the west and a scriptless computer on the east, no units, written as a
Brood War Use Map Settings scenario into `mods\debug-tools\canvas.scx` with the player's own
exe's VCOD. It starts from `--canvas[=<tileset>]`, from the front end's home screen while
Debug Tools is active, or from the channel's `canvas`; with `battle` true (or
`HYPERION_CANVAS_BATTLE=1`) each side gets an army and one trigger keeps sending waves, so
the middle of the map always has a fight in it. `map_push` is the same map at the size and
with the tiles the push gives (`PortCanvasPushNext`, checked by `PortCanvasCheck`).

The panel's Demo reel (its Demo tab) shows what the engine can do on the game itself, in seven
chapters of about three minutes in all, from the menus or a game: Volcanis coming in under
cinema bars from the whole map; the zoom, closer than 1.17 draws and out to the whole map; the
same scroll key held with `camera.smooth` off, 1.17's 8-pixel steps uneven against the display's
frames, and on, glided evenly (at the slowest "Keyboard scroll speed", the game's zoom); light
and colour, from 1.17's pixels through `colour.full`, the bloom and a night of 2D lights over a
fight; the Theta-9 sample scene on Steal the Beacon's deck (plain installation ground when that
map is missing); 1,024 units in one battle on Volcanis, 32 at each of 16 places a side (64
round one place do not all fit); and the Volcanis sample battle, whose last step hands the game
to the player. A scene's camera may go out to the whole map
(`PortViewZoomWhole`), past the player's zoomMax. It is the page's own script (`REEL` in
`panel.html`) over the channel's commands, a chapter's map switched to with `map_push` and its
scene, the look and the camera changed between and during them with `set`, and the key held
with `scroll`. Every live value and Settings row it changes is noted the first time and put back
at the end or on Stop (a page closed mid-reel leaves them); "from" starts at a chapter, those on
Volcanis making its outpost first.

## The benchmark

A benchmark game is every slot a computer with the local player an observer who sees the whole
map and cannot lose, Free For All by default, at a fixed seed so two builds play the same
game. It starts from the `benchmark` mod's set-up screen, or from the command line:
`--bench` with `--map`, plus `--bench-frames`, `--bench-speed`, `--bench-races`,
`--bench-type ffa|melee`, `--bench-shown window|full|none`,
`--bench-frames-shown free|display`, `--bench-picture gpu|256`, `--bench-against <folder>`,
`--bench-frames-csv` and `--bench-replay <folder>`. A command-line run ends the program;
a run from the menus holds at its last frame under the summary. The set-up's five scenarios
play Big Game Hunters remade at five sizes, `maps\Hyperion\<title>.hxm` (or in its
`Experimental\` folder, where a package files a map past 32 players or 512 tiles, as the 4x4
and the 8x8),
which it looks for in the data folder, then in the game's folder, where a package puts them,
then in the `--maps` folders (`MapPath` in `port_front_bench.cpp`; before 2026-10-19 the data folder only, so a
package's set-up found none). The fifth, Extreme, is the largest game Hyperion holds: 64
computers on the 8x8, 1,024 tiles a side, as fast as it goes for 30 minutes, in place of the
design's greyed card for a mod's own scenario (2026-10-26); the set-up seats 8, 16, 32 or 64,
as many as the map has starts.

The set-up's Supply cap (200, the original's, to 1,000, or Unlimited) and Unit limit (Automatic,
or 3,400 to 262,144) are the mutators of the same names (`gameplay.md`, "Mutators";
`loop.md`, "Caches for large games"), for large battles: past 200 or with a limit the game
plays by Hyperion's rules, and Automatic makes room for every player's whole cap. They reach
the game as `--mutators` does (`PortBenchSettings::szMutators`, `PortLaunchBench`), the
command line shows them so, and a command-line run takes `--mutators` too. `settings.json` has
`mutators`, the set as a room says it, when there are any, and `unit_table`, the units the
game held; a run is held only against runs with the same mutators, and Run Again takes them
back.

The set-up's right columns are two tabs, as the skirmish set-up's (the user, 2026-10-26: "add a
mutator option so that we can enable mutators for the benchmark also"): Benchmark Options, the
options the whole height beside the map and the log, and Mutators, the skirmish set-up's tab
(`MutatorsTab`, `port_front_mutators.h`) across both columns, keys O and U. The set-up keeps
one `MutatorSet`, which the tab, the Supply cap and the Unit limit all change, and
`MutatorsList` (`port_mutators.h`) writes it as `--mutators` lists it. The Held Against box
is set aside for now (the user: "Can we remove the against held. for now"); Runs still shows
the run a run is held against.

A stress run adds `--bench-army <n>`, n units given each computer at the first
frame of the run, and `--bench-pools <k>`, the pools 1.17's times k, up to 13, when that is
more than the players take (`PortBenchStressGame`). The game plays by Hyperion's rules with
the option `HX_OPT_STRESS`, and its replay's `STRS` holds the army and the scale, so the run,
a sweep and a profile of it play the same game; the armies are made by the build that plays
the replay, so a run is compared with runs of the same build. The army is a mix of the three
races' fighters, so it uses only part of a computer's own supply: in fifteenths, 3 marines, 4
zerglings, 2 zealots, 2 hydralisks, a dragoon, a goliath, a mutalisk and a wraith, made as
Create Unit makes them, one to each point of a grid a tile and a half apart round a point
between the computer's start and the next computer's, on the points in its start's area
(`StressArmies`). Each unit is taken from its computer's AI (`SAI_RemoveUnit`) and attacks
toward the next computer's start; a second in and every ten seconds after, the units are
taken from the AI again, which takes a unit back in once it counts it as made, and the idle
ones sent on to the start after (`StressWave`). Made at the start, in one block, or left to
the AI, the armies stood still: Create Unit filled the mineral line, a unit inside a block
found no path out, and the AI kept its units home and refused them a far target, wanting a
transport. `hyperion_bench.log` says how many units were made, and a minute and five minutes
in how many of the armies' units are alive and how many have not moved. `settings.json` has
`army` and `pools`. On Big Game Hunters 8x8 with 64 players, `--army 150 --pools 13` makes
9,151 units, 19,390 in all. Until revision 15 most of an army stood even so: 1.17's 1,024
paths (`sai_PathPoolBuf`) were taken by the first units to move, and a unit that found none
gave its order up; a game past 1.17's limits has 25,600 now. The finder holds more than
32,767 entries then, which `sai_Contours.cpp` once read as a 16-bit count.

`PortBenchStart` makes a folder under `benchmarks\` in the data folder, named for the time,
the map and the players, and writes `settings.json` (what was asked for, the time, the
version, commit and branch from `build_id.h`, and the renderer) and `seconds.csv`'s header.
`PortBenchEnd` writes `summary.json`: how long it ran, frames shown and their mean and worst
1 %, game frames a second, the most units and when, each part's ms a game frame, why it
ended, each player as it ended, the overlay's changes, and `end_state`, the replay check's
domains folded into one hash, which says whether two runs played the same game. `frames.csv`
holds every shown frame's time with `--bench-frames-csv`, and `run.rep` plays the run again.

The overlay (`port_benchview.cpp`) is the run's own screen: the top strip with Start or Stop
(S), Pause (P), Slower (D), Faster (U), Restart (R), Hide Overlay (H) and Set-up (Escape); the
camera's three ways (C) of following the action, the whole map, or free; the run's settings,
log folder and the last lines of `seconds.csv` on the left with the minimap; the numbers once a
second and a game frame by part on the right, each against the earlier run it is held against;
and the frame-time graph at the foot: the last minute of shown frames a quarter second at a
time (the worst as a band, the mean as a line, the earlier run dashed), a game frame's parts
stacked, or the units over the whole run; H leaves a small counter only.
`port_benchoverlay.cpp` decides what shows and where, names the seven Screen styles, reads the
flags and `settings.json`, and keeps the key a run compares by, so a run is only held against
runs under the same overlay. `port_benchinfo.cpp` is what the overlay reads: the selected units
in full detail (hit points, shields, energy, upgrades, build queue, cargo, larvae) and each
player's bank, supply, units and whether it is still in. Pause holds the clock, and paused time
is left out of every number.

The replay HUD a run shows is the run's too. Its pause is the overlay's Pause
(`order_replay_play` calls `PortBenchViewPause`), and its Slower and Faster, like the
overlay's and D and U, step the run's speed (`order_replay_slowdown` and `order_replay_speedup`
call `PortBenchSpeedStep`): Slowest to Fastest, then x2 to x16, then as fast as it goes. The
strip, the readouts' rate and how often frames are shown follow the speed now
(`PortBenchSpeedNow`), `summary.json`'s `speed_changes` lists each step with its game frame,
and a restart plays at the set-up's speed again. The game's Menu in a run, the HUD's Menu
button or F10 (`do_gamemenu_dlg`), shows the overlay if H hid it and asks Stop the
benchmark?, or shows a finished run's summary again (`PortBenchViewMenu`), since 1.17's
console and its menu are hidden under the overlay.

## Measuring with hxbench

`tools/hxbench.py` has two modes. `fast` runs the replay under the harness
(`tools/replaytest/replaytest.py run --build hx64 --window background`) as fast as playback
goes: the game frames a second, the perf parts, the processor cores the process used over the
busy part of the run, and its share of the GPU's 3D engine. `pace` starts Hyperion itself
with `--replay` and `--control=27217`, at the replay's own speed, kept running unfocused by
the channel; after a warm-up it measures a window (`--seconds`, 20 by default) and reads the
game clock back through the channel for frames drawn and game frames a second and how evenly
they came.

```
python tools/hxbench.py fast --replays h64_bgh8x8 --config auto --config off:HYPERION_WORKERS=1
python tools/hxbench.py pace --replays m1_IronManLadder_1v1_LostTemple_2003 --config rest --build base=testrun/bench/builds/base-v0.3.5-obs
```

- `--config NAME[:VAR=VALUE,...]` sets environment variables for the run. Every run gets
  `HYPERION_DISPLAY=right` and `HYPERION_PERF=1`, and `HYPERION_DLLS` is cleared.
- `--build NAME=FOLDER` puts that folder's `Hyperion.exe`, `Hyperion.pdb` and
  `rt_observer.dll` into `testrun/hx64` and `src117/build/x64` for its runs, keeping what was
  there as `<name>.hxbench-kept` and putting it back after. Without `--build` the exe already
  in `testrun/hx64` runs as `current`. Reference builds are kept locally under
  `testrun/bench/builds/`.
- `--setting "Label=value"` (pace) changes one of Settings' rows through the channel for the
  run and puts it back after; `--camera X,Y,ZOOM` (pace) centres and zooms the replay's camera
  after the warm-up; `--arg` passes anything else to Hyperion, such as `--view=5120:2160`.
- `--presentmon` (pace) traces the measured window with PresentMon, which is local only at
  `tools/local/presentmon/`, and reports what reached the display: how far apart frames were
  shown, how long after the game handed them over, and the present mode. Each capture's CSV
  lands in `testrun/bench/presentmon/`.
- `--renderer` (default `vulkan`, Hyperion's own), `--repeat`, `--tag`.

Every run appends one JSON object to `--out`, `testrun/bench/results.jsonl` by default, with
the mode, build, configuration, environment, replay, repeat, time, tag and the measurements.

## Profiling

`HYPERION_PERF=1` writes `hyperion_perf.log` in the folder the game runs in, once a second:
frames shown, passes drawn, the present's and the scene's ms, and each part of a game frame in
ms. The parts are `events`, `creep`, `ai`, `fog`, `units`, `movers`, `triggers`, `check`,
`minimap`, `ui` and `draw`, marked in `gameloop.cpp`; the marks only read the performance
counter. A pass that runs no game frame is held and dropped (`PortPerfHold`, `PortPerfKeep`),
so the wait between frames does not land in `events`. A benchmark run turns the same numbers
on by itself, and the channel's `perf` serves them live.

`HYPERION_PROFILE=1` samples the game's thread from a thread of its own about once a
millisecond, walks its stack by the x64 unwind tables, and writes `hyperion_profile.log` every
10 seconds: each function alone and with what it calls, named from the PDB beside the exe
through dbghelp. The build writes `Hyperion.pdb`; copy it beside the exe that runs. Windows
only: off Windows `HYPERION_PROFILE` does nothing. The channel's `profile` starts the counts
again (`reset`) and writes them where it is told (`write`), so one moment is sampled by
itself: `hxrender.py sweep --profile` writes one for each hold, `hxrender.py profile` one for
the game alone over a stretch of a replay.

### The frame log and hxrender

`HYPERION_FRAME_LOG=<folder>` (or `1` for `framelogs\<stamp>` in the data folder), or the
channel's `framelog`, writes a row for each game frame and each frame shown, where
`HYPERION_PERF` gives a second's means. The folder holds `game.csv` (each game frame's time,
number, units and each part in ms), `drawn.csv` (each frame shown: the time since the last,
the events', UI's and draw's ms, the pass's description, the scene's upload, the present and
the card's wait in it, the distortions, the map layer's pixels, the camera and zoom, the
primitives and the images among them, the HD draws, and the units in the camera's sight) and
`marks.csv`. While it is on, the perf parts are timed.

`tools/hxrender.py` drives it. Its game folder is `testrun/scale/`, made as `play.py` makes its
own, with the maps of `testrun/play/maps/Hyperion`; a run's outputs go under
`testrun/scale/runs/<stamp> <name>/`.

```
python tools/hxrender.py bench --players 32 --map "Big Game Hunters 4x4" --frames 60000
python tools/hxrender.py bench --players 64 --map "Big Game Hunters 8x8" --army 150 --pools 13 --name stress
python tools/hxrender.py sweep <run>/bench --art sd|hd [--quick] [--zooms 2,4,8,16] [--label x] [--env V=1] [--profile] [--display]
python tools/hxrender.py report <sweep folder> [--against <sweep folder>]
python tools/hxrender.py shots <run>/bench --art hd --zooms 1,4,16 --label a [--env V=1]
python tools/hxrender.py compare <shots folder> <shots folder>
python tools/hxrender.py profile <run>/bench [--start F] [--end F]
```

`sweep` plays the bench's replay from before its most units, every setting that paces frames
off, puts the camera over the busiest spot and holds, ramps and pans from zoom 1 to 16 (holds at
1, 2, 4, 8 and 16 with `--quick`, or 1 and the zooms `--zooms` names, up to a replay's whole
map), each step marked; with `--display` the frame rate and vertical sync are left as a player
has them, to see what the game keeps of its speed. `report` breaks it down by step and zoom,
gives the game frames' spikes and what led each, the worst frames, how cost grows with what
is drawn, and a comparison. `shots` holds the replay at one frame and snapshots each zoom, the
HD water's ripple held still; two `shots` folders `compare` pixel by pixel (1.17's colour cycling
and the cursor still move, so two runs of one build are the measure of what differs by
chance). `profile` steps the replay as fast as it goes and samples the game alone.

## Crashes and traces

A fault the game does not handle writes a folder of its own, `crashes\<stamp>_<pid>\`. So does
an end that is not a fault: `abort`, `std::terminate` (which a throw no one catches reaches, on
any thread, its type and `what()` named), an invalid parameter to the C library, a pure virtual
call, and 1.17's own fatal errors once the start is done. `report.txt` holds, in turn:

- what happened, where (module and offset; for an end that is not a fault, the first frame
  past the reporter and the C library), the access, and the thread with its name;
- the build, the system, the displays, the presenter and the install, as `port_sysinfo.cpp`
  gathered them at the start;
- the session (`port_crashnotes.cpp`): how long it ran, whether the start finished, the screen
  or the game's frame, the game being played (its line as Discord shows it, the map's size,
  the local player, the mutators, the mod) and the last five played;
- the call stack, unwound with the module's unwind data, with function, file and line where
  the PDB can be found (with none, Hyperion's frames are offsets only, since its few exports
  would name them wrong);
- the last 64 lines every module logged (`PortLogger::Line` feeds `PortCrashNote`), with
  their times;
- the registers, a scan of the stack for return addresses into code in case the unwind stops
  early, and the modules loaded with their versions.

Each part is on disk once it is made, so a fault inside the reporter keeps the parts before
it. `report.json` has the same, for an online report later. Beside them are `minidump.dmp`
and copies of the start-up log and the logs this run wrote (`hyperion_*.log`, the last 256 KB
of one past 1 MB). `hyperion_crash.log` in the data folder is a copy of the last `report.txt`,
and the last 10 folders are kept. Then the process starts a copy of the program,
`Hyperion --crash-report <folder>`, and ends; the copy shows the error window and nothing
else. A crash in the C library's checks (an invalid parameter) waits for the window, since
its process goes on. When the copy cannot be started, a box says where the report is.
`hyperion_quiet.flag` in the working folder keeps both away, which is what the replay harness
relies on, as does `HYPERION_QUIET` set to anything, which an AI lab worker sets for itself
. Only a fault no handler took is reported: a fault a `__try`
expects, such as Storm's block-header probe, is not a crash. The main thread keeps 64 KB of
its stack back, so a stack overflow still has room to write its report. Off Windows the fault
is a signal, handled on a stack of its own of a megabyte, Hyperion's frames are offsets, and
there is no minidump. Heap corruption and
`__fastfail` end the process past every handler; the next start says only that the last
session ended with no word (`platform.md`, "The start-up log").

A player's build comes without its PDB. `tools/package.py` keeps each Windows build's exe and
PDB in `testrun/share/symbols/<version>_b<build>_<commit>_<id>/`, every packaged build's and the
last 10 installed, and `python tools/crash_symbols.py <report folder>` finds the one whose PDB
id the report names and gives each frame its function, file and line. It reads the text a
player pasted from the error window too, saved to a file or given with `-`.

Off Windows the report's `symbols` fact is the program's file name and its build id: the GNU
build id the linker writes (`-Wl,--build-id`, read through `dl_iterate_phdr`) on Linux, the
Mach-O `LC_UUID` on a Mac. Hyperion's frames are offsets from the program's first byte in
memory, so a frame's address in the file is the offset plus the first `PT_LOAD`'s address (0
for Hyperion's) or the `__TEXT` segment's. Packaging a Linux build keeps the program, which
holds its debug info, in `<version>_b<build>_<commit>_linux-x64_<id>/` (or `linux-arm64`);
packaging on the Mac keeps `Hyperion.arm64` and the `.dSYM` that `dsymutil` writes beside it
at the link, in the Mac's copy of the tree. `crash_symbols.py` finds the program by its id there
or in `src117/build/linux-*` and `mac-arm64`, and names Linux frames with `llvm-symbolizer` or
`llvm-addr2line` when one is on PATH, else `addr2line` (on Windows through `wsl.exe`, the default
distribution's); a Mac's only with `llvm-symbolizer`.
`HYPERION_TEST_CRASH=<kind>[:menus|game]` makes a crash on purpose to try it: `av`, `abort`,
`terminate`, `throw`, `invalid` or `purecall`, at the front end's first frame or a game's
start.

### The error window

A start that fails, a start or session that ended with no word, and a crash each show the
same window (`port_errorwin.cpp`): a title, what happened, where its log or report is, and a
box of text with Copy to clipboard, Open the folder and Close. The text is what a player
pastes into a GitHub issue or on Discord, so `port_share.cpp` makes it to fit there: wrapped
in a code block, at most 1,900 characters, and cleaned of what is the player's own.

- Every path loses its folders: Hyperion's program, data and game folders become `<program>`,
  `<data>` and `<game>`, others `...\`, and the file's name stays. Network addresses (IPv4
  with a port, IPv6, a MAC) become `<address>`. The run's folders, the environment's values,
  the computer's and the user's names are left out.
- For a start: the failure, the build, the system, the newest steps, then the rest of the
  facts and older steps as they fit.
- For a crash, from `report.json`: what, where, the access and the thread, the build and the
  system, the session and the game, the call stack's top, the last lines from the logs that
  name nothing of the player's (`kShareableLogs`; the LAN, Discord, menus, replays, saves,
  keys and others are left out), then the rest as it fits.

On Windows the window is a dialog; `HYPERION_ERROR_WINDOW=drawn` draws it with SDL instead,
as it always is off Windows (`port_drawn.cpp`, `platform.md`). The text also goes to stderr
off Windows. On Linux what is copied lasts only while the window is open, unless a clipboard
manager keeps it, so the window says to paste it first. The browser build has the console
only.

The camera trace is `trace {"what":"camera","on":true}`: a row each time the camera is placed,
with the game view's middle, what the camera made of it, the zoom, the easing's step, the
cursor, and which function moved the game's view, named from the PDB. Turned off, it writes
`<stamp>_camera.csv` under `traces\` in the data folder and answers with the file and an
account of the motion. `trace_rows` reads the last 500 rows while it runs. `minimap` presses
or drags the minimap through its own handler, and `scroll` holds an arrow key for a time and
traces every pass, so a run can repeat the same moves and compare them. The account then says
how often the game's view stepped and by how much, how many passes showed no motion, and how
much the camera's move from one pass to the next varied.

## Running test instances safely

Test runs go on the right-hand display, in a window that takes neither the focus nor the
mouse: `HYPERION_DISPLAY=right` picks the screen whose left edge is furthest that way (`left`,
or a number, 1 being the primary; the error window goes there too), and `HYPERION_WINDOW=background` makes the window the
frame's size, unfocused, with a taskbar entry to close it by. `HYPERION_WINDOW=hidden` never
shows or presents at all, which is for runs with nothing on screen; a hidden run has no window
to close, so note its PID and close it when the work is done. `HYPERION_GAME_SIZE=WxH` sizes
a background window in a game, for capturing an overlay at a size.

Ports: the channel is 27117 by default, and a second instance needs `--control=<port>` of its
own. `hxbench.py` uses 27217 for the instance it starts, so its runs do not collide with a
game started by hand. The MCP server reads `HYPERION_CONTROL_PORT` when it should not use
27117.

`tools/play.py` is the user's own game folder, `testrun/play/`: it is made on the first run,
and every run copies in the built `Hyperion.exe`, the x64 `SDL3.dll`, their PDBs, Hyperion's
mods, the LAN page and the release notes' pictures when they differ. A test instance never
runs or writes there: the profiles, settings, saves and logs in that folder are the user's.
Use `testrun/hx64` (the harness's and `hxbench`'s folder) or `--data=<folder>` instead.

## Settings and environment variables

| Name | What it does |
|---|---|
| `--control[=<port>]` | open the live channel; 27117 when bare |
| `--canvas[=<tileset>]` | start Debug Tools' canvas |
| `--data=<folder>` | the data folder: profiles, logs, `snapshots\`, `traces\`, `benchmarks\`, `crashes\` |
| `--bench`, `--bench-*` | a benchmark run from the command line (see above) |
| `HYPERION_WINDOW` | `fullscreen`, `windowed`, `background` or `hidden` |
| `HYPERION_DISPLAY` | `left`, `right` or a screen's number, for a background window |
| `HYPERION_GAME_SIZE=WxH` | a background window's size in a game, for a capture |
| `HYPERION_RENDERER` | `sdl`, `direct3d12` or `vulkan`; Vulkan is the default, Direct3D 12 the fallback |
| `HYPERION_GPU_SCENE` | the GPU scene: `terrain`, `sprites`, `map`, `screen`, `gpu`, `off` |
| `HYPERION_PERF=1` | `hyperion_perf.log` once a second |
| `HYPERION_PROFILE=1` | `hyperion_profile.log` every 10 seconds, sampled; the channel's `profile` for a moment |
| `HYPERION_FRAME_LOG=<folder>` | the frame log, `1` for one in the data folder |
| `HYPERION_DEBUG_VIEW` | Debug Tools' overlay available without the channel |
| `HYPERION_CANVAS_BATTLE=1` | the canvas with two armies and waves |
| `HYPERION_CONTROL_PORT` | the port the MCP server connects to |
| `HYPERION_TEST_MOUSE=1` | a background window takes a posted mouse message |
| `HYPERION_GPU_CHECK=1` | the GPU's frame compared with 1.17's (full colour off under it) |
| `HYPERION_SNAPTEST=<period>,<back>[,<frame>]` | the rewind self-test, read with `tools/snapdiff.py` |
| `HYPERION_TEST_CRASH=<kind>[:menus\|game]` | a crash on purpose: `av`, `abort`, `terminate`, `throw`, `invalid` or `purecall`, in the menus (the default) or at a game's start |
| `HYPERION_TEST_STARTUP=<stage>:fail\|stall` | the start-up step of that name fails, or stops, for the error window and the watchdog |
| `HYPERION_ERROR_WINDOW=drawn` | the error window drawn with SDL on Windows, as off Windows |
| `--crash-report <folder>` | the error window for a crash report's folder; what a crashed copy starts |
| `HYPERION_SNAPPROBE=<a>,<b>` | `snapprobe.txt` over those game frames, read with `tools/snapprobe.py` |
| `HYPERION_SYNC_DUMP=<frame>,<folder>` | each hash domain's bytes at that frame, to compare two runs |
| `HYPERION_WORKERS`, `HYPERION_CORES`, `HYPERION_LOOP_REST`, `HYPERION_LOOP_MARGIN_US`, `HYPERION_FRAMES_AHEAD`, `HYPERION_VISION_AHEAD*` | the performance settings `hxbench.py` measures; `../hyperion-exe.md` has them all |

## Gotchas

- `PortControlStart` tries once. A port held by something else (a Hyperion that did not close)
  stays held for the whole session, and `status` then just fails to connect. Check
  `hyperion_control.log` in the data folder, and close stray `Hyperion.exe` processes.
- Every command runs on the game's thread in `PortControlPoll`, which only runs while frames
  are being shown. A game that is not presenting answers nothing.
- `snapshot` takes one frame at a time and the answer comes after that frame; a second
  `snapshot` while one is in flight is refused.
- `pause`, `step`, `spawn`, `select`, `reveal`, `scroll` and `minimap` need a single-player
  game; `players`, `units`, `hash`, `camera`, `colour`, `bench_info`, `region` and `pathstats`
  need any game. The error says which.
- Player input clears a pause the channel set (`PortControlPlayerInput`), so a step sequence
  that someone clicks into stops stepping.
- A step stops on its frame: the game loop calls `PortControlFrameRan` after each game frame,
  which pauses the game there, and the next poll answers. The poll alone would let a fast
  step run on until a frame is drawn, thousands of frames at a set frame rate.
- A canvas starts differently each run (its seed, the frame the pause lands on), so a picture
  test pauses first, spawns and selects while paused and takes its pictures without stepping;
  `reveal` lifts the fog that units spawned while paused have not yet seen through.
- Snapshots and traces hold the player's game art and data. They stay in the data folder on
  this machine: nothing from them goes into git or onto a page.
- A benchmark is only held against a run with the same game, teams and overlay; runs made
  before a field existed read as whatever the old default was, so old folders are not
  comparable with new ones.
- `hxbench.py` restores the builds it placed in a `finally`. A run killed outright leaves
  `*.hxbench-kept` files behind and the wrong exe in place; check for them before trusting
  the next build. Its `fast` mode measures the harness's run, which copies
  `src117/build/x64/Hyperion.exe` over the game folder's before each run, which is why
  `--build` places the exe in both folders.
- PresentMon is local only, and unelevated it traces by process id, not by name.

## Further reading

- `../hyperion-exe.md`: the program, its command line, the data folder and the performance
  settings
- `../replay-test.md`: the replay suite, the gate every engine change passes, and `compare.py`,
  which plays any replay on 1.17 and on Hyperion and shows that the two agree ("Comparing any
  replay")
- `render.md`: the presenters and the GPU scene that `--renderer` and `--scene` pick
- `front-end.md`: Hyperion's menus, where the benchmark's set-up and the Mods screen live
- `../../../docs/live-channel-for-editors.md`: the channel written for an editor's author,
  with the cutscene step format (do not edit; it ships in a package)
