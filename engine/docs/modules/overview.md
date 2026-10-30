# Hyperion's engine as a whole

Start here before working anywhere in `engine/`. This page explains how the program is
put together and which module doc covers which code. Each module doc in this folder
describes the current state of its area. The longer docs one folder up (`../hyperion-exe.md`,
`../replay-test.md`, `../hud-format.md`, `../maps.md`) are references.

## What Hyperion is

Hyperion is a 64-bit program that plays StarCraft: Brood War 1.17. Its game code is
a reconstruction of 1.17 in C++: 4,970 functions that once compiled to 1.17's
exact bytes, copied into `engine/src117/` to be changed. No code of
1.17's runs, and no exe of 1.17's is read: the game's initialised globals are Hyperion's
source (`gen/data117.cpp`). The game files come from the player's MPQs and
`Local.dll`. Storm, DirectDraw, DirectSound and Battle.net are replaced by the port's own
code on SDL3.

The rule that shapes everything is that the simulation stays 1.17's unless a change says
otherwise. A replay recorded by 1.17 plays the same game in Hyperion on every frame. The
replay harness proves this against the original exe on every commit (`build.md`, "The
gates"). A change that alters what a game frame does is a new revision of Hyperion's rules.
It stays separate from 1.17's rules and is recorded in Hyperion's own replays
(`replays.md`).

## The source tree

| Path | What it is |
|---|---|
| `src117/lang/` | the game: one C++ file per 1.17 source file (276; `src117/tus.txt` lists the 263 compiled). Names follow the game's own symbols. The port's changes inside these files are marked `// Hyperion` (about 1,700 of them). |
| `src117/h/` | the game's headers: structs, class layouts, globals; plus some port headers (`port_synchash.h`, `poollimits.h`, `hxrules.h`) |
| `src117/port/` | Hyperion's own code, about 130 modules named `port_<name>`, each opening with a comment that says what it holds. A folder an area, as the module docs below divide them: `loop/`, `render/` (with the GPU presenter's `shaders/`), `data/`, `front/`, `gameplay/`, `replays/`, `lan/`, `tools/`, `platform/`, `lab/` and `hud/`. The builds and the tools find a file by its name (`tools/port_files.py`), so a module can change area without them changing |
| `src117/gen/` | generated: the globals, their 64-bit layouts, saved layouts, the code map (`build.md`) |
| `src117/compat/` | Win32 declarations for the Linux and browser builds |
| `hyperion/` | the program's start (`hyperion.cpp`), version, icon, resources |
| `tools/` | builds, the replay harness (`tools/replaytest/`), the benchmark (`hxbench.py`), the live channel's clients, generators |
| `web/` | the pages a LAN room serves (`web/lan/`) and the browser build's page (`web/play/`) |
| `notes/release-notes.json` | the player-facing release notes |
| `re/` | the address book: every 1.17 function's and global's address, size and name |
| `testrun/`, `oracle/` | local, gitignored: game folders, runs, benchmarks, the reference exe |

## How the program runs

1. `wWinMain` in `hyperion/hyperion.cpp` finds the game's folder (its own, or
   `HYPERION_GAME`), installs the crash reporter, sees that the MPQs are there, and fills
   the engine's block with 1.17's starting values (`port_standalone.cpp`, `gen/data117.cpp`). A global whose layout changes at 64
   bits is translated (`gen/wide117.h`). Each code address in the data is mapped to the
   engine's function (`gen/code_map.cpp`).
2. It loads any DLLs named in `HYPERION_DLLS` (only the replay harness's observer) and calls
   the game's `WinMain` (`PortRunGame`).
3. The game initialises as 1.17 does, through the port's Storm (`data.md`). Unless turned
   off, Hyperion's own front end replaces 1.17's menus (`front-end.md`).
4. A game runs in `RunGameLoop` on the main thread: window messages, one simulation step
   when due, the UI, drawing, then a rest (`loop.md`). Drawing ends in `PortPresentFrame`,
   which hands the game's 8-bit screen, or the GPU's own scene, to the display (`render.md`).
5. The game's state is hashed every 24 frames in Hyperion's own games and replays
   (`loop.md`, `replays.md`).

The same program runs in other roles, chosen on the command line (`port_paths.cpp` parses
it):

| Role | How | Doc |
|---|---|---|
| a player's game | no arguments, or `--map`/`--race`/`--players` and the like to skip the menus | `gameplay.md` |
| replay playback | `--replay <file>` | `replays.md` |
| a recorded test game | `--record=<frames>` | `replays.md` |
| a LAN room, no window | `--room`, `--room-name`, `--room-password` | `lan.md` |
| a seat joining a room | `--join=<address>` | `lan.md` |
| a benchmark | `--bench` and its options | `tools.md` |
| the trainer | `--trainer` | `gameplay.md` |
| a Debug Tools canvas | `--canvas` | `tools.md` |
| under the live channel | `--control[=<port>]` | `tools.md` |
| any of them on its own data | `--data=<folder>`: profiles, saves, replays, logs and settings live there instead of `Data\` beside the exe | `platform.md` |

## Threads

| Thread | Does | Doc |
|---|---|---|
| main | the window, the game, the UI, drawing and presenting | `loop.md`, `render.md` |
| worker pool | the state check's domains, the fog reads ahead in large games; only reads, only between frames | `loop.md` |
| audio | SDL's mixing callback (`MixCallback`, `port_dsound.cpp`) and Storm's streamed-audio thread (`DdaThread`, `port_stormfile.cpp`) | `data.md` |
| HD loader | loads Remastered's HD art in the background (`port_hd.cpp`) | `render.md` |
| mod art loader | makes a mod's art sheets HD art in the background (`port_modart.cpp`) | `render.md` |
| the profiler | samples the main thread when `HYPERION_PROFILE=1` | `loop.md` |
| stream encoder | a streamed seat's video encoding (`port_lanstream.cpp`); the room and the LAN client poll their sockets on the main thread | `lan.md` |
| the live channel | accepts the control connection (`port_control.cpp`); commands are acted on from the main thread (`PortControlPoll`) | `tools.md` |

Code on any thread other than main must not write game state, and must read it only while
the game stands still between frames.

## Module docs

| Doc | Covers | Main files |
|---|---|---|
| `loop.md` | the game loop, the clock and rest, frame pacing, the worker pool, the game's cores, the state hash, the per-frame measuring hooks, the large-game caches | `lang/gameloop.cpp`, `port_clock`, `port_workers`, `port_synchash`, `port_vision`, `port_perf`, `port_pools` |
| `render.md` | from the game's screen to the display: SDL renderer and the GPU presenter (Vulkan by default), scenes, HD art, lights, effects, the console, the camera and view | `port_present`, `port_hwcursor`, `port_gpu*`, `port_draw`, `port_hd`, `port_lights`, `port_looks`, `port_mask`, `port_maptext`, `port_overlay`, `port_scene`, `port_teamcolor`, `port_view`, `port_camera`, `port_console`, `port_window`, `port_motion`, `port_storm` (blits) |
| `data.md` | Storm's replacement, MPQ and CASC, formats, video, sound, maps (`.hxm`), worlds, mods and mod data, intros and cutscenes | `port_storm*` (not net, lan or the blits), `port_casc`, `port_remastered`, `port_install`, `port_install_window`, `port_smacker`, `port_video`, `port_webm`, `port_mp4`, `port_sysvideo*`, `port_subtitles`, `port_chatfilter`, `port_pklib`, `port_formats`, `port_dsound`, `port_hxm`, `port_map`, `port_mods`, `port_moddata`, `port_modcheck`, `port_typenames`, `port_iscript` |
| `front-end.md` | Hyperion's menus: home, Settings, skirmish, campaign, replays, profiles, notes, the kit and themes | `port_front*` (not LAN), `port_frontkit`, `port_frontlobby`, `port_ui`, `port_clay`, `port_world`, `port_intro` |
| `gameplay.md` | launching games, players and teams past 8, the AI boundary, handicaps, mutators, triggers, cutscenes, the trainer, hotkeys, raised limits; the game bodies the port provides | `port_launch`, `port_session`, `port_players`, `port_teams`, `port_score`, `port_ai`, `port_handicap`, `port_mutators`, `port_richbases`, `port_trigrun`, `port_eud`, `port_cutscene`, `port_trainer*`, `port_hotkeys`, `port_keys`, `port_veclen`, `port_pathstats`, `port_names` |
| `replays.md` | 1.17 and Hyperion replays, saves, snapshots, Replay TV, the rules revision, the replay harness in brief | `port_hxreplay`, `port_hxsave`, `port_record`, `port_replay`, `port_observe`, `port_snap*`, `port_tv`, `port_rules`, the save half of `port_formats` |
| `lan.md` | the LAN room, relays and seats, the feed and watching, streamed seats, the browser build in the room, Storm's network layer | `port_lan*`, `port_stormlan`, `port_stormnet`, `port_front_lan*`, `port_qr`, `port_instance`, `web/` |
| `tools.md` | the live channel, Debug Tools and the canvas, the benchmark, hxbench, profiling, crash reports, traces | `port_control`, `port_debugview`, `port_trace`, `port_crash`, `port_bench*`, `port_canvas`, `tools/hxbench.py`, `tools/hxlive*` |
| `platform.md` | start-up and 1.17's data, the window and messages, input, paths, settings, JSON, logs and the shared helpers, Discord, the POSIX and browser stand-ins for Win32 | `port_standalone`, `port_winmsg`, `port_posix`, `port_paths`, `port_settings`, `port_json`, `port_log`, `port_discord`, `port_util.h`, `compat/` |
| `build.md` | every build target, third-party code, generated files, test folders, the gates, versions and packages | `tools/build_*.py`, `tools/replaytest/`, `tools/package.py`, `hyperion/version.h` |
| `lab.md` | the AI lab: its screen, the farm of worker processes, the wall, watching a farm's game | `port_front_lab`, `port_lab*`, `mods/lab/` |
| `hud.md` | the HUD designs: their files and format, drawing them, input over them, their tooltips, Settings' HUD tab; Hyperion's selection of 255 units with its subgroups | `port_hud*`, `port_gpu_hud`, `port_front_hud`, `port_zip`, `port_selwide`, `h/selectlimits.h`, `engine/huds/` |
| `addons.md` | the add-ons a player calls up over a game: none at present, and what the engine keeps for them | |

## Conventions

- Game functions and globals keep the ledger's names (`CUnit::EntityUpdate`,
  `gameloop_update`). An unnamed `sub_XXXXXX` gets a real name once its purpose is known.
- Port code uses PascalCase functions with a module prefix (`PortGpuFrame`,
  `VisionHxSpriteSet`), camelCase locals with Hungarian prefixes (`nUnits`, `dMargin`,
  `pszName`), `g_` globals and UPPER_SNAKE constants.
- Every port file opens with a comment that says what it holds and why. Comments are plain
  sentences that explain the reason and the measured effect, not the mechanics.
- A change to a game file is marked `// Hyperion` with the reason.
- Each port module has its header, `port_<name>.h`, beside its .cpp, and a file includes the
  headers of the modules it uses: one of its own area by name (`"port_view.h"`), one of another
  area by its path (`"../data/port_formats.h"`), so a file's includes show which areas it
  leans on. The `port_` prefix keeps every object name apart from the game's in the flat build
  folders. A game file's include names what it takes
  (`#include "../port/render/port_view.h"  // Hyperion: g_portView`). A function the game's
  C++ defines for the port is declared in the port header of its caller, naming the file
  that defines it.
- Port headers a game file includes keep to plain types. `lang/main.cpp` and `gds_file.cpp`
  declare their own `fopen`, so the headers they include bring in no standard library; a
  module's C++-only calls go in a header of their own (`port_moddata_push.h`). Headers with
  SDL's types (`port_gpu.h`, `port_gpu_internal.h`) are for the presenter's files.
- No function is declared by hand in a .cpp. Storm's functions come from `h/Storm.h` in the
  game's files, which also declare them locally as 1.17's did, and from `port_stormapi.h` in
  the port's; StormLib only through `port_stormlib.h`. 1.17's globals come from
  `gen/globals117.h`, or from `port_globals117.h` for those it lacks that several port files
  read; a global one port file reads is declared there, and the pools of units, sprites,
  thingies, bullets and the AI stay with the save, hash and snapshot files that walk them.
- Environment variables are `HYPERION_*`. Each one is listed in `../hyperion-exe.md`, and
  each module doc lists its own. A player-visible option is a Settings row backed by
  `settings.json` (`front-end.md`, `platform.md`); the variable, where there is one, takes
  precedence for testing.
- Text a player reads names no takes or internal modules. An option not built yet shows
  greyed with "Not available yet".

## Where a module or a mod plugs in

Some of what Hyperion shows is a list that code adds to without the code that walks it
knowing, and some of it a mod gives as data. A module adds itself at file scope with the
list's macro, so nothing else changes to take it in; a mod's data is read from its folder,
and what it gets wrong is named in `hyperion_mods.log` and left as 1.17 has it.

| What | A module adds | A mod gives | Read |
|---|---|---|---|
| a layer over the game: what it draws, the area it leaves the game, its keys and clicks | a `PortOverlayLayer` with `PORT_OVERLAY_LAYER` (`port_overlay.h`) | | `render.md`, the layers over the game |
| the console's image for a race or for replays, whether its regions move to a wide view's edges, the map's height at 640 | | `hud.json`'s `"console"` | `port_console.h`, `docs/mod-data.md` |
| the minimap's dot colours | a `PortMinimapLook` with `PORT_MINIMAP_LOOK` (`port_minimap.h`) | `hud.json`'s `"minimap"` | `render.md`, `docs/mod-data.md` |
| the selection's circles and health bars | a `PortSelectionLook` with `PORT_SELECTION_LOOK` (`port_selection.h`) | `hud.json`'s `"selection"` | `render.md`, `docs/mod-data.md` |
| units, weapons, flingy, sprites, images, orders, techs, upgrades and their art | | `types.json`, `iscript.txt`, GRPs | `data.md`, `docs/mod-data.md` |
| a world: its maps, menus and campaign | | `mod.json` | `data.md` |
| how the computer players play | the AI boundary (`port_ai.h`) | | `gameplay.md` |
| a program of its own in Hyperion's process | an x64 DLL named by `HYPERION_DLLS` | | `../hyperion-exe.md` |

The looks and the layers are presentation: they change no game frame, and a replay plays the
same with them or without. A change to what the game does is a rules revision (`replays.md`):
the selection's size, 12 units, is one, since it travels in the selection's commands and in
replays and saves.

## Where to start for common tasks

| Task | Read |
|---|---|
| make something faster | `loop.md`, then `tools.md` (measuring) |
| change how something looks | `render.md`, and the seams above |
| add a menu screen or a setting | `front-end.md` |
| a change that alters the simulation | `replays.md` (rules revision), `gameplay.md`, and the gate in `build.md` |
| read a new file format or asset | `data.md` |
| network play | `lan.md` |
| a crash or a hang | `tools.md` (crash reports), `platform.md` |
| ship a build | `build.md` |
