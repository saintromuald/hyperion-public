# Hyperion.exe

`Hyperion.exe` plays StarCraft 1.17 with Hyperion's engine, from the player's own game
files, the MPQs and `Local.dll`. It is one 64-bit program: the engine is linked into it, and
no code of 1.17's runs. It reads no `StarCraft.exe` either: 1.17's starting values are
the engine's own source (`src117/gen/data117.cpp`).

Sources: `hyperion/hyperion.cpp` (the start), `hyperion/hyperion.def` (the exports),
`hyperion/hyperion.rc`, `hyperion/hyperion.ico` and `hyperion/make_icon.py` (the icon),
`hyperion/version.h` (Hyperion's version, shared with the engine), and the engine
(`src117/`). `tools/build_engine_x64.py` builds it into `src117/build/x64/Hyperion.exe`.

## Start-up

The C runtime starts at `wWinMainCRTStartup` (the link's `/ENTRY`), so it calls
`hyperion.cpp`'s `wWinMain`, not the game's `WinMain`, which runs last. Its first call starts
the start-up log, `hyperion_startup.log` in the data folder, a line for each step below and
the game's own up to the first frame shown; a start that fails says so in the error window,
which names the log and has a text to copy for a GitHub issue or Discord
(`modules/platform.md`, "The start-up log"; `modules/tools.md`, "The error window").
`SDL3.dll` is loaded late, so a missing one is said there too, before any call of SDL's.

1. Find the game's folder: `HYPERION_GAME`, else the classic StarCraft folder Settings keeps
   (`"classic"`, unless StarCraft from Battle.net is the install chosen), else the folder
   `Hyperion.exe` is in. Its files are
   1.17's MPQs there, or a StarCraft: Remastered install (`HYPERION_DATA_SOURCE`,
   `modules/data.md`, "Where the data comes from"). A file
   named in `HYPERION_GAME` (the 1.17 exe, as scripts once named it) stands for
   its folder. `HYPERION_GAME` is set to the folder, with its separator, for the LAN room's
   fingerprint and files, and Storm's `SFileGetInstallPath` answers it, so the MPQs, maps
   and saves are read there when `Hyperion.exe` is elsewhere.
2. Install the crash reporter (`src117/port/tools/port_crash.cpp`), which writes a folder
   for each crash under `crashes\`: `report.txt` with what happened, the build, the system,
   the session and the game being played, the faulting thread's call stack unwound from the
   fault, the last lines every module logged, the registers, a scan of the stack and the
   modules; `report.json` with the same; a minidump Visual Studio opens; and copies of this
   run's logs. Each address is an offset into its module, and where the build's PDB can be
   found (`src117/build/x64/`, as the exe names it) also a function, source file and line;
   `tools/crash_symbols.py` names them for a player's build, which has none.
   `hyperion_crash.log` in the data folder is a copy of the last report. The crashed process
   then starts `Hyperion.exe --crash-report <folder>`, which shows the error window for it
   (`modules/tools.md`, "Crashes and traces").
3. See that the game's files are there: `StarDat.mpq` in the game's folder, or StarCraft from
   Battle.net. A start a player made with no install kept, or whose kept one's files are gone,
   asks for the folder of each install they have and which the game reads, keeps the answer
   and goes on (`modules/data.md`, "Which install, as the player chose it"); Quit ends
   it. Any other start without them ends with a message naming the files Hyperion needs. The
   game's language is decided with the install: Settings' Language or `HYPERION_LANGUAGE`,
   the install's text language by default (`modules/data.md`, "The install's language", which
   also has the test variables `HYPERION_TEST_TEXT_LOCALE` and `HYPERION_TEST_LOCALE_FILES`).
   Then fill the engine's block (`src117/h/oc_data.h`,
   `src117/port/platform/port_standalone.cpp`) with 1.17's starting values
   (`src117/gen/data117.cpp`, `PortGameDataInit`), translating the globals whose layout
   changes at 64 bits (`src117/gen/wide117.h`). Each address of 1.17's code the data holds goes to the
   engine's function through `src117/gen/code_map.cpp`; one with no row there goes to
   `PortOriginalCodeReached`, which stops the game with a crash report if it is ever
   called. `hyperion_init.log` records the counts.
4. Load the DLLs `HYPERION_DLLS` names (`path[@export];...`, the export defaulting to
   `oc_init`), calling each export with the main thread's id, the contract `rt_launcher`
   uses. The replay harness loads its observer this way; a player's run loads none.
5. Call the game's `WinMain` (`PortRunGame`), and end with what it returns.

The process's module is this exe, so its header and resources are Hyperion's. The engine
reads 1.17's resources from `Local.dll`, loaded as data (`PortLoadResources`), or from its
own stand-in when there is none (`port_local.cpp`, `modules/data.md`), and the two the game
took from 1.17's exe, VCOD #202 and the accelerator table 0x71, from its own copy
(`PortGameVcod`, `PortGameAccelerators`).

`Hyperion.exe` exports `oc_observe` (the replay observer's interface,
`src117/port/replays/port_observe.h`) and `oc_screen`, `oc_screen_size` and `oc_view_toggle` (the
screen, for development tools).

## The exe on disk

A few things the game reads from its exe file now come from `Hyperion.exe`:

- The version. `get_program_version_info` (init.cpp) reads the product version of the
  exe file into `gszVersionNumber`, "Version 1.17.0", which goes to SNet in the version
  record. `hyperion.rc` gives `Hyperion.exe` product version 1.17.0.1, 1.17's own, so
  that record is unchanged, and Hyperion's own file version from `hyperion/version.h`.
  The main menu shows Hyperion's version from the same header instead,
  `Hyperion-v0.1-beta` (`gluMain_dlg_activate`, gluMain.cpp).
- The path. `GetModuleFileName` names `Hyperion.exe`; the game only takes its folder.
- The signature. `Hyperion.exe` is not signed. The system survey (`SystemSurvey.cpp`)
  records whether the exe carries Blizzard's signature, for Battle.net. SNet's code-sign
  check covers network provider DLLs, not the exe.
- The icon. Hyperion's icon is the Battlecruiser, the Hyperion's class of ship, as 1.17
  draws it: `hyperion/make_icon.py` builds `hyperion/hyperion.ico` from the player's MPQs
  (the user's decision, PRD 2026-09-27). `Hyperion.exe` carries it as resource 1, the icon
  Explorer shows, and `InitWindow` gives the window class the same one
  (`src117/port/render/port_window.cpp`).

## Running it

In a 1.17 folder: `Hyperion.exe` and the x64 `SDL3.dll` (from `tools/fetch_sdl3.py` and
`tools/build_sdl3.py`, `third_party/SDL3-3.4.16/lib/x64/`) beside `Local.dll` and the MPQs, then
run `Hyperion.exe`. It imports `SDL3.dll`, and Windows looks for it beside the exe; it
needs nothing else of the install's, `Storm.dll`, `Smackw32.dll` and the network
providers included. The window is titled "Hyperion" and has square corners on Windows 11
(`src117/lang/main.cpp`, `InitWindow`; `src117/port/render/port_window.cpp`), and the game draws
through SDL3 (`modules/render.md`): as Settings saved it ("Display", "Monitor and
resolution"; borderless full screen until the player chooses), or as `HYPERION_WINDOW` asks
(`fullscreen`, `windowed`, `background`, `hidden`); a `background` window opens centred on
the screen `HYPERION_DISPLAY` names, `left`, `right` or a number (1 the primary), so test
runs keep off the main screen. `HYPERION_RENDERER` picks the presenter: the GPU presenter
on `vulkan` (the default since 2026-10-18, falling back to the driver SDL picks, Direct3D
12 on Windows) or `direct3d12` (the default from 2026-10-02), or SDL's renderer (`sdl`, and
the fallback when no GPU driver starts) (`modules/render.md`). Without it, Settings'
Renderer on Windows (`"renderer"`, `vulkan` or `direct3d12`, 2026-10-20) names the GPU
presenter's driver, with the same fallbacks; `hyperion_render.log` names the one in use.
`--install classic|remastered` and `--install-folder <folder>` name the game's files for one
run, before the environment and Settings, and are not kept (`modules/data.md`, "Which
install, as the player chose it"); a run given either never shows the first start.
`--after <process id>` is Settings' Restart now's: the copy it starts waits at the
single-instance check for that one to end, up to 15 seconds, instead of quitting. With the GPU presenter the GPU alone draws the map (`HYPERION_GPU_SCENE`
`gpu`, the default there; `modules/render.md`); `screen` shows the map as the GPU draws it
beside the software's, the other stages only draw and check it, and `off` leaves the
software drawing it. `HYPERION_PERF=1` writes frames and times each second to `hyperion_perf.log`,
with the game frames a second and each part of a game frame, and in a Hyperion replay each
check's wait and each of its domains' time and words; `HYPERION_PROFILE=1` samples the
game's thread and writes `hyperion_profile.log` every 10 seconds, named from `Hyperion.pdb`
beside the exe, or a moment of its own when the
live channel's `profile` asks. `HYPERION_FRAME_LOG=<folder>` writes a row for every game frame
and every frame shown, `tools/hxrender.py`'s input (`modules/tools.md`).
`HYPERION_HD_NO_BATCH=1` draws every HD sprite by itself, and
`HYPERION_FOG_CPU=1` makes the fog's light map on the processor where the GPU alone draws the
map, to compare, `HYPERION_SCENE_STEP=<n>` fixes the map layer's step zoomed out, and
`HYPERION_PASS_KEEP_CHECK=1` makes what a pass keeps again and logs any difference to
`hyperion_pass_keep.log` (`modules/render.md`). `HYPERION_FINDER_CHECK=1` compares the unit
finder's sides and grid, the contour rows, the local search's points and strips and the
captains' unit slots with the walks they replace (`hyperion_finder_check.log`), and
`HYPERION_FIND_CHECK=1` runs a medic's searches the shortcut leaves out
(`hyperion_find_check.log`; both `modules/loop.md`). The check runs on a pool of
worker threads (`src117/port/loop/port_workers.cpp`), half the processor's threads up to 16, or
the number Settings' "Processor threads" gives (Performance, `workerThreads`);
`HYPERION_WORKERS=<n>` goes before the setting, 1 for none, and `HYPERION_SYNC_SERIAL=1` hashes its
domains one after another on the game's thread, so that the profile sees them. Between frames
the loop rests until what is due next (`PortClockRest`, `port_clock.cpp`) unless Settings'
"Rest between frames" (`loopRest`) is off or `HYPERION_LOOP_REST=0`; it never rests with a DLL
injected, as under the replay harness, or in the browser. The rest ends before what is due by
the latest its timer has woken in its last 256 waits and 0.2 ms more, 0.5 to 2 ms;
`HYPERION_LOOP_MARGIN_US=<us>` fixes it when measuring. Where the processor's
cores do not all share one L3 (a Ryzen X3D), Settings' "Game's cores" (Performance,
`gameCores`: Auto, Large cache, Any) holds the game's thread on the cores of the largest, Auto
once a game has 2,000 units (`PortCoresFit`, `port_workers.cpp`); `HYPERION_CORES=cache`,
`other` or `any` goes before the setting. In a game of 4,096 units or more
the pool also makes the circle walk's fog reads beside it, from the last unit while the walk
goes from the first (`playervision.h`): `HYPERION_VISION_AHEAD=0` reads in place,
`HYPERION_VISION_AHEAD_FEWEST=<n>` sets the fewest units for the pool to read, and
`HYPERION_VISION_AHEAD_CHECK=1` reads both ways in every game and writes any difference to
`hyperion_vision_ahead.log`. `HYPERION_VIEW_SEEN_CHECK=1` asks the sets behind the byte a
sprite the draw reads for a replay's viewers past slot 7 every time as well, and writes a
difference to `hyperion_view_seen.log` (`ViewHxSpriteSeen`, `port_vision.cpp`).
A drawn frame is one command buffer for the graphics card, and the processor prepares at most
two frames before the card has drawn them, or the number Settings' "Frames ahead" gives
(Graphics, `framesAhead`, 1 to 3); `HYPERION_FRAMES_AHEAD=<n>` goes before the setting. With
"Vertical sync" on, no more frames are drawn than the display shows, whatever "Frame rate" asks.
`engine/tools/hxbench.py` measures builds and these settings on replays.
With the GPU presenter the menus are Hyperion's own front end (`front-end.md`) unless the
setting "Hyperion's menus" is off; `HYPERION_FRONT=1` or `0` overrides the setting, and
`HYPERION_FONT`, `HYPERION_FONT_WIDE`, `HYPERION_FONT_BOLD` and `HYPERION_FONT_MONO` name font
files to use in place of the ones built in. The front end's checks without a person (`HYPERION_FRONT_SIZE`,
`_SNAP`, `_SNAP_AT`, `_SNAP_SCREEN`, `_EXIT`, `_WORLD`, `_SETTINGS`, `_OPEN`) are listed at the top of
`src117/port/front/port_front.cpp`.

The player's data is Hyperion's own (PRD 2026-09-27): profiles (`characters\`), replays
(`maps\replays\`), saved games (`save\`), saved and downloaded maps, error logs,
screenshots, the map cache, the mods and Hyperion's own logs (`hyperion_*.log`) go in a
folder `Data\` that it makes in the folder `Hyperion.exe` runs from, a folder of its own, with
the game's files read where they are installed, so the game's folders are left alone (PRD
2026-10-13 and 2026-10-21); or in another given on the command line as `--data <folder>` or
`--data=<folder>`, which is created if it is missing (`src117/port/platform/port_paths.cpp`,
reached from `InitPaths`; logs through `PortLogFile`). The folder was `Hyperion\` before
0.6.2's last build, and what one holds moves into `Data\` at the start (`platform.md`). The replay harness and `tools/play.py` give their game folder as
`--data`, so their data stays where it was. 1.17 keeps them in
`Documents\Starcraft\`, which StarCraft: Remastered also uses; Hyperion leaves that
folder alone, so its first run asks for a new ID. The option is taken out of the command
line before 1.17's `parse_args` reads it. A setting will take its place. The settings, the
game's own and Hyperion's, are `settings.json` in the data folder (`port_settings.cpp`):
1.17's calls to Storm's registry functions read and write it, so vanilla's registry key is
neither read nor changed (`front-end.md`, "F3 so far"). Without the file, a first run plays
the intro movies, as 1.17 does on a new install, unless the game was started from the
command line (`--map`, `--replay`, `--load`, `--canvas`), which goes straight in and leaves
the intro for the first start from the menus (`RunGame`, `lang/main.cpp`).

`tools/package.py` makes the zip a player unpacks into a folder of its own: `Hyperion.exe`,
`SDL3.dll`, a `README.txt`, `docs\` with the documents for editors, and `Data\` with the mods,
tools, web pages, the release notes' pictures and the test maps, in a new folder under
`testrun/share/`.

Maps can come from other folders too. 1.17's map and replay lists show the `maps` folder of
the data folder and of the install folder together, and with StarCraft from Battle.net's files
its `Maps` folder too (`platform.md`); `--maps <folder>`, given once for each folder (up to
eight, the Battle.net install's then left out at eight), adds folders to them, such as
`Documents\Starcraft\Maps`. Each folder stands for a `maps` folder, so its `BroodWar`
and `replays` subfolders merge with the game's own. Hyperion only reads from these folders;
what it saves still goes in its data folder. A saved game remembers a map from one of them
by its name under `maps`, and finds it there again when started with the same `--maps`.
Like `--data`, the option waits for a setting to take its place.

A game can be played wider than 640x480 (`modules/render.md`): `--view <width>`, or a
shape such as `--view 16:9`, sets the width in game pixels, rounded to a multiple of 16
between 640 and 1712 (16:9 is 848, 21:9 is 1120, 32:9 is 1712; a display's own size works
too, `--view 5120:2160` is 1136). The height stays 480. The view takes that width when a
game starts and goes back to 640x480 for the menus when it ends
(`src117/port/render/port_view.cpp`, `PortViewSet`). Wider, the map fills the screen, all 480 rows
of it beside 1.17's console, which sits in the middle with the game's dialogs. A window
(`HYPERION_WINDOW=windowed`) takes the game's shape, and the menus show in its middle. A
setting will take the option's place.

F5 in a game switches the art between classic and HD, as Remastered's F5 does, blending
the two over half a second (`hd.fade`; `modules/render.md`); it needs the GPU
presenter, and does nothing without it. While Remastered's HD is held (PRD 2026-10-21) it
does nothing at all, and a stored `art` is left as it is but not used. Settings' HD lighting lights the HD art from its
own normal, specular and occlusion maps, as Remastered does (`hd.lighting`; `modules/render.md`). Ctrl+F5 (F5 before 2026-10-02) switches the view
between 640 and the wide width, between two frames: the wide width is `--view`'s, or
without one the wide View shape last chosen, 16:9's 848 or the display's shape (a
5120x2160 display gives 1136; 16:9 when the display is no wider than 4:3). Settings' Hotkeys tab can give either another function key
that 1.17 leaves free, and each keeps what it switched to as Settings' Art and View
shape; a game starts at View shape's width unless `--view` gives one (`front-end.md`, F3
so far), the display's shape by default since 2026-10-19.

A replay, a game or a saved game can be started from the command line, without the menus
(`src117/port/gameplay/port_launch.cpp`, reached from `RunGlues`):

    Hyperion.exe --replay <file>
    Hyperion.exe --map <file> [--race zerg|terran|protoss|random|<a mod's race>] [--computers <1-63>]
                 [--computer-race zerg|terran|protoss|random|<a mod's race>]
    Hyperion.exe --map <file> --ums
    Hyperion.exe --map <file> --rules classic|hyperion
    Hyperion.exe --map <file> --teams <t,t,...> [--starts <s,s,...>]
    Hyperion.exe --map <file> --ai-levels <l,l,...>
    Hyperion.exe --map <file> --handicaps <p,p,...>
    Hyperion.exe --map <file> --mutators <preset|name=value,...>
    Hyperion.exe --mod <id> [any of the above]
    Hyperion.exe --load <file>
    Hyperion.exe --canvas[=<tileset>]

`--canvas` starts Debug Tools' canvas (`src117/port/tools/port_canvas.cpp`): Hyperion
makes a map on the spot, flat ground of the tileset named (badlands, platform,
install, ashworld, jungle, desert, ice, twilight; jungle's grass by default) with the two
players' start locations and no units (since 2026-10-15; with `HYPERION_CANVAS_BATTLE=1`, or
the live channel's `canvas` with `battle`, two armies and a trigger that keeps sending waves
at each other), writes it to
`mods\debug-tools\canvas.scx` in the data folder, and plays it as a Use Map Settings
game. `hyperion_canvas.log` says what was made. 1.17's tips dialog is skipped for it.

Hyperion's own maps, `.hxm` files (`../../docs/hxm-format.md`), play like any
other: the map list shows them, and `--map` takes one. The first time one is opened it is
compiled into a `scenario.chk` and an MPQ holding that and the map's other files, kept in
the data folder's `cache\hxm\` and made again when the map is newer
(`src117/port/data/port_hxm.cpp`); Storm's archive calls open that in the map's place, so the
load passes, the briefing and a replay's copy of the map are the game's own path.
`hyperion_hxm.log` says what was compiled and why a map was refused. Maps up to 512
tiles a side play; `maps.md` has the limits and how a larger map differs.

    Hyperion.exe --hxm-compile <in.hxm> <out.chk>

writes the `scenario.chk` alone and stops, for `tools/hxmap/check.py engine`, which checks
that it is byte for byte what the map tools write.

    Hyperion.exe --hxm-check <in.hxm> [<report.txt>]

is the check a map editor runs on a map it wrote (`../../docs/hxm-for-editors.md`): the
compile, then what it lets through but a game would not play as the map means (units
outside the map or owned by no slot, links to no unit, open slots with no start location,
more units than a game holds, locations defined twice or missing, a short tiles layer, a
size or player count past the targets). Errors and warnings go a line each to the report,
or to standard output without one, then a `plays:` or `refused:` line; the exit code is 0
when the map plays, 1 when it is refused, 2 when it cannot be read. A map of a newer format
version than the build reads is refused, here and when played.

    Hyperion.exe --crash-report <folder>

shows the error window for the crash report in that folder, from its `report.json` (or the
head of `report.txt`), and ends. A crashed copy starts it; it reads no game files.
`HYPERION_ERROR_WINDOW=drawn` draws the window with SDL on Windows, as it is off Windows.

    Hyperion.exe --mod-check <id> [<report.txt>] [--install <...>] [--data <folder>]

is the check a modder runs on a mod (`../../docs/mod-data.md`, Checking a mod;
`src117/port/data/port_modcheck.cpp`): once the game's files are open, its data loads as
start-up loads it, with no message box, then every file its records name and each cinematic of
its own is decoded, and the process ends before the front end. `error:`, `warning:` and
`note:` lines go to the report, or to standard output without one, then `plays:`, `plays with
errors:` or `refused:`; the exit code is 0 with no error, 1 with one, 2 when no mod has the id.
It skips the check for another running Hyperion, and its logs go to the data folder's
`mod-check\`.

    Hyperion.exe --hud-check <layout.json | folder | package.hud> [--style <style.json>]
                 [--pick <option>=<value>]... [--light #rrggbb] [--layout <W>x<H>] [--out <report.json>]

checks a HUD design without a game (`modules/hud.md`, `hud-format.md`): a layout and
its style, a design's folder (its `hud.json`, its `style.json`, its parts files) or a `.hud`
package, unpacked as a game unpacks it. It prints JSON with `plays`, `errors`, `warnings`,
`panels` and `timelines`, and with `--layout` the panels' boxes at that screen size, for the
comparison with the design center's player (`tools/hud/hudgolden.mjs`). The exit code is 0 when
the design plays, 1 when it has errors, 2 when a file does not read.
`HYPERION_HUD=<layout.json>[;<style.json>]` plays a design straight from its files in place of
Settings' choice, for its maker; its files are read again within a second of being saved.

Replays of Hyperion games: a game past 1.17's limits, on a map past 256 tiles a side or
with players past 7, writes its `LastReplay` as Hyperion's (`hxRS`): 1.17's
blocks, then sections with the rule version, a fingerprint of the game data, the
simulation's revision (`src117/h/hxsim.h`), the slots past 12, the colours (and, when a
slot has one, its colour in full RGB, `RGBS`), and the state's hash every 24 frames (`src117/port/replays/port_hxreplay.cpp`, `port_synchash.cpp`).
`--replay` plays one, refuses it when its rules, game data or revision are not this
build's, and checks each hash as it plays; `hyperion_replay.log` says how the checks went,
and the live channel's `status` has them as `sync`. Any other game's replay is 1.17's, as
before.

Saves of Hyperion games: a game with players past 7 saves 1.17's sections with the slots
past 7 after 1.17's eight, and two of its own, `HXGM` and `HXST`, which hold the rule
record, the slots, and what a game needs to go on exactly as it was saved: the random
state, the lists' order, the counters between passes
(`src117/port/replays/port_hxsave.cpp`); and `HXRC`, when a slot has a colour in full RGB, which
the loaded game takes again, and `HXAI` in a game with computer levels (below). Loading one compares the state with the hash saved and
says so in `hyperion_save.log`. A replay of a game that saved notes the frames, and makes
the same saves when played, to `hyperion-replayed-save.tmp` in the temporary folder. A map
past 256 tiles a side saves its pathing tables whole, under a version of the `PATH`
section that 1.17 refuses, with the contour counts and records at their full width.

`--seed=<n>` seeds a new game with n in place of the clock. `--record=<frames>[,<path>]`
plays a game by itself, unwatched, to that frame, or until the local player has lost, then
leaves it as Quit Mission does, copies its replay to the path and ends (with no tips at
the start, whose pause would hold it at frame 0); under the replay harness it goes back to
the menus instead (`src117/port/replays/port_record.cpp`). With `--map` and a seed, two recordings
are the same file. `--record-orders=<n>` has the local player give orders while it
records: every n frames it selects up to 12 of its units, those last in the unit table
first, and sends them to right-click or move to one of the last units of another player,
through 1.17's own commands, so that the replay holds commands that name units.
`--record-orders=<n>,queue` queues those commands (shift), so the units' queues fill to 8
(hxsuite's `h16_xl1800_queue`, `HX_OPT_ORDER_QUEUE`).

`--colour=<slot>:<#rrggbb>[,<slot>:<#rrggbb>...]` gives slots a colour in full RGB at each
game's start, over what the map gives them (stage G, `src117/port/render/port_teamcolor.cpp`).
The full colour draws the colour itself (`modules/render.md`); the 8-bit drawing
takes the nearest palette entries, among those tunit.pcx's ramps use, for the slot's ramp
and minimap dot. A colour also comes from a map: CRGB's RGB mode for slots 0 to 7, and
HXRG, a player's `"rgb"` in an `.hxm`, for those past 7 (`maps.md`). Presentation only.

A LAN room (`src117/port/lan/port_lanroom.cpp`): `--room` runs one, with no window and
no game, until it is stopped or its console has been gone a minute; `--room-name`,
`--room-password` and `--room-key` (the console's key, accepted from this machine only) set
it up, and its settings are `lanroom.json` in the data folder. The console gives its room the
key and the password in `HYPERION_ROOM_KEY` and `HYPERION_ROOM_PASSWORD` instead, for its start
alone: a command line is any program's to read, an environment only this user's. Each is read
and cleared as Hyperion starts (`PathsSecrets`, `port_paths.cpp`). It answers UDP 6111, takes
rooms' clients and seats on TCP 6112 (`HYPERION_LAN_PORT`) and serves its page on 7117
(`HYPERION_LAN_WEB_PORT`); `HYPERION_LAN_BIND` binds them to one address. `--join
<address>[:<port>]` joins the room there as the front end comes up, with `--room-password`;
`--lan-name` is the name the room shows, `--race` this player's race in its lobbies and
`--lan-ready` says Ready in each one it is seated in, for a test's seat. A match the room
launches is played through 1.17's own lobby (`port_lanlobby.cpp`), and its replay is 1.17's.
Presentation and transport only; `hyperion_lan.log` has the room's evening and each match's
relay.

A room's match is fed to the room as it is played (`port_lanfeed.cpp`): each seat sends the
record's blocks as they complete, the room writes the match's replay into
`maps\replays\<room name>\` and passes the feed to who watches. Watching plays the match as a
replay, the room's delay behind, in `lan\watching.rep` of the data folder; `--lan-watch`
watches each match being played, for a test. As a match goes live the room starts its copy:
this exe with `--lan-copy <match>:<token>` (the token the room made, accepted from this
machine only), `--data <data>lan\copy<match>`, `HYPERION_WINDOW=hidden` and
`SDL_AUDIO_DRIVER=dummy`. The copy follows the feed with no delay, holds its state hash
against the players', sends the console a picture of each player's army in turn and the
watchers where the fighting is, and exits when its replay ends; the room ends it when the
room stops. `"copies": "Off"` in `lanroom.json` starts none. The room writes the token as
`env` (`--lan-copy <match>:env`) and gives it in `HYPERION_CHILD_TOKEN`, read and cleared at
the start; `--lan-stream` and `--lab-worker` take `env` the same way.

The AI lab's farm (`modules/lab.md`) plays its games in workers: this exe with
`--data <folder> --lab-worker <port>:<token>:<worker>`, which plays the games the lab gives it
one after another, each in the folder the lab names. Beside a benchmark run's options (`--bench`,
its map, players, levels, seed and length, `--bench-speed max --bench-shown none`; a stress
run's `--bench-army` and `--bench-pools`, `modules/tools.md`) with
`--lab-game <id>` and `--lab-out <folder>`, a worker plays that one game and ends. The worker
skips the intro films, plays no music, and sets its own `HYPERION_WINDOW=hidden`, `SDL_AUDIO_DRIVER=dummy`, `HYPERION_RENDERER=sdl` with
SDL's software renderer, `HYPERION_WORKERS=1` and `HYPERION_QUIET=1`, connects to the lab on
127.0.0.1 at the port, and ends when the lab is not there or goes. `HYPERION_QUIET`, set to
anything, does what `hyperion_quiet.flag` does: a crash writes its report and ends with no
window.

The room's page (`engine/web/lan`, put in `Data\web\lan` by the packages and beside the
program in `web/lan` by the builds, `PortPackagedFile`; `HYPERION_LAN_WEB_ROOT` points elsewhere) is what a browser on the network opens at the
room's address and port 7117: the room's password and a name, on a PC the choice of the browser or
installing, the lobby of the match the player is seated in with race, Ready and chat, and the
matches being played. It speaks the room's JSON over the WebSocket at `/ws`; a page is told only
the room's name, version and who is in it until its hello is let in, and nothing it says counts
before. Its typefaces are Hyperion's own, served by the room from the exe's resources at
`/fonts/`. Installing and the tournament say "Not available yet" until their stages.

A player in a browser plays from a seat on the host (`port_lanstream.cpp`): as they sit in a
lobby the room starts this exe again with no window, its sound to SDL's dummy device, in a data
folder of its own (`lan/seat<n>`) and under their name, `--lan-stream <device>:<token>`; it is let
in by the token, listed nowhere, and plays their seat as an installed copy plays its own (Start
Match waits for it, up to 30 s). It offers the page a WebRTC peer (libdatachannel with Mbed TLS,
`tools/fetch_webrtc.py` and `tools/build_webrtc.py`; the Linux build makes its own), the room
passing the two descriptions between them (`"rtc"`): the frame as shown, 8 bits a pixel made
I420, as H.264, by NVENC through CUDA where NVIDIA's driver has it (`HYPERION_STREAM_ENCODER=
openh264` asks for the processor's) or by OpenH264; the mixer's sound, tapped after its clamp
(`PortSoundTap`), as Opus at 48 kHz; and a data channel the browser's mouse and keys come back
on, in game pixels, which become the window's messages (`WinMsgRemoteMouse`, `WinMsgRemoteKey`;
the window is held active, as 1.17 scrolls only in the active window). Every two seconds the seat
tells the room its encoder, size, frames and bits a second (the device's `"stream"`). The page stays
on the stream through 1.17's score screen after a match, which the player continues; when they
leave the stream for the page, their browser goes or two minutes pass, the seat presses Enter on
it itself, so it is back in the room for the next match (`"score"` while it is up). A page that
goes while its game is under way leaves its device in the room, gone, and the same player's page
opened again takes the seat back; the seat ends when the page goes otherwise, when the room
stops, or when it loses the room.

Watching in a browser is the same seat watching instead of playing. The page's Watch on a live
match (`{"t":"watch","match":n}`) or Watch the Big Screen (`"big":true`) has the room start it,
and the room tells it which match to show (`"follow"`); it asks to watch as Watch does on the
console, the room's delay behind, with Replay TV's director at the camera, and the page shows
the Live bug, the delay, the match and its players over the stream, with no input. The room ends
a page's watching once its seat has shown the match to Match Over and left it, five seconds on.
The Big Screen goes to a live match, waits while nothing is played, goes on to the next when its
own has been shown, and goes to another match with a fight when its own has had none for 20 s
(the copies' fight reports, no sooner than 30 s after it last went); the page's device names the
match it is on.

Since 2026-10-17 the streamed seat is the fallback for browsers: the main route is to be Hyperion
compiled to WebAssembly and run in the browser, as a lockstep seat of the room over its
WebSocket, its 1.17 files served by the room (the host's setting) or taken from the player's own
folder (`modules/lan.md`).

The front end's Multiplayer is the rooms (`port_front_lan.cpp`): the rooms this network
answers with, Join a Room with the room's password, or an address; Host a Room starts the
room as a process of this exe (`--room` with a key of its own, no window) and opens its
console (`port_front_lanhost.cpp`, `port_front_landevices.cpp`): the room as tables, rows
or pictures, the join card with its address, a code for a phone's camera (`port_qr.cpp`)
and the password, this machine's load, the devices in one list, and the room's settings.
A match's lobby is Skirmish's set-up, hosted (`port_front_skirmish.cpp`); the host sits in
a seat or gives it up to run the room. The Original's Screens opens 1.17's own. For a check
without a person, `HYPERION_LAN_KEY` is the key the console gives its room, so a script can
read the room as a second console.

`--control[=<port>]` opens the live channel (`src117/port/tools/port_control.cpp`): a
WebSocket on 127.0.0.1, port 27117 unless given, through which a tool reads and changes
what Hyperion shows while it runs. It also opens on the canvas and while a mod whose
`mod.json` says `"channel": true` is active and the player has allowed it (the Mods screen
asks; `--mod` naming it allows it for that start), as Debug Tools' does; `hyperion_control.log`
says where it listens, or why it could not. Each start makes a key and writes it to
`%LOCALAPPDATA%\Hyperion\control-<port>.key` (elsewhere `$XDG_DATA_HOME/Hyperion/`, or
`~/.local/share/Hyperion/`, made 0600), which only this user reads: a WebSocket opens with it
as `?t=<key>` and is refused with 401 without it, and so is `/snapshots/`. The clients in
`tools/` read the file. `HYPERION_CONTROL_KEY` (16 hex digits or more) sets the key, for a
tunnel from another machine. Each message is a JSON object,
`{"id": 1, "cmd": "status"}`, answered with the same id and `"ok"`, run on the game's thread
between frames:

| command | does |
|---|---|
| `status` | the front end, the menus or a game; its frame, paused, canvas, changed, map size; in a game `view`, where the camera looks and its zoom, and `scroll`, the game's own view (its top left), the target of a centre-view trigger's scroll while one runs (`to`, null otherwise; the trigger waits until it arrives) and whether the window counts as active; `music`, the track 1.17's music plays (its index in the songs' table, 22 the menus', 18 a victory's score) or -1 |
| `players` | each slot in use: its type, race, name, minerals, gas, supply (its race's; in halves: used; given up to the cap; given; the cap), start location and units, the tiles it sees and has explored, the other players' units it sees, its triggers and its victory state |
| `hash` | the state's hash a domain at a time (units, objects, players, map, sprites, ai) and the random state; with a path, each domain's bytes as hashed, to find where two runs differ (`port_synchash.cpp`) |
| `list`, `get {name}`, `set {name, value}` | the live values: `gamma`, `colour_cycling`, and the effects' (`effects`, `bloom.*`, `grade.*`, `vignette.*`, `lights`, `lights.*`; `modules/render.md`), `hd`, `hd.*` (`modules/render.md`), and `colour.full` (`modules/render.md`) |
| `snapshot {name?}` | the next frame as a PNG in the data folder's `snapshots\`: the game at its size, or the front end at the window's (GPU presenter; the SDL renderer saves 1.17's screen) |
| `pause`, `resume`, `step {frames?, fast?}` | 1.17's pause, in a single-player game, and a few frames at a time; with `fast`, the step's frames run back to back instead of at the game's speed, the clock pacing frames and not being part of the game (pathbench). A step ends early where the game stops itself, as a trainer session does at the end of the replay it started from. A paused game takes no input, and this pause has no dialog to end it from, so the player's first click or key in the window ends it (`PortControlPlayerInput`) |
| `camera {x, y}` | the view's centre, in map pixels |
| `canvas {tileset?}` | from the front end's home screen, starts the canvas |
| `dialogs` | the open dialogs: each one's name when it is one of the console's or the edges' (`minimap`, `statdata`, `statport`, `statcmd`, `textbox`, `statres`, `statlb`), its text, and its rectangle on the screen (`port_view.cpp`, `PortViewDialogs`) |
| `lab_status`, `lab_start {at_once?, games?, project?}`, `lab_pace {multiple?, paused?}`, `lab_stop`, `lab_games {since?, max?}`, `lab_watch {game?, live?, to_end?, leave?}` | the AI lab's farm (`modules/lab.md`): how it stands and each worker's game, its games started, paced and stopped, the lines of its project's `games.jsonl`, and a game watched from its start or live, run to its end with `to_end`, left with `leave`, and its state checked against its worker's |
| `subscribe {topics}` | from then, every message published on those topics, as `{"topic": ..., "data": ...}`, until the client closes or subscribes again (`[]` for none); the AI lab's `lab.workers`, `lab.games` and `lab.samples` (`modules/lab.md`, the stream; `tools/lab_stream.py` is a client) |
| `lights`, `light {image, color?, radius?, intensity?, flicker?, remove?}`, `lights_save` | the 2D lights' table (`modules/render.md`): listed, an entry changed, added or removed, and saved as `lights.json` in the data folder |
| `looks`, `look {name, rule?, material?, ramp?, images?, units?, clarity?, shine?, glow?, sparkle?, plume?, light?}`, `look_unit {unit, look}`, `looks_save` | the looks (`modules/render.md`, "Looks"): listed, one changed or added, one given to a unit by hand (`"none"` for none, `""` back to the game's), and saved as `looks.json` in the active mod's folder, or the data folder with no mod |
| `richbases` | in a game with rich bases: the map's read (bases, mains, naturals, distances from each start, shares, sets, chances), the outcome and the units that turned rich (`modules/gameplay.md`, "Rich bases") |
| `spawn {unit, x, y, player?, count?, attack?: [x, y], move?: [x, y]}` | in a single-player game: units made as the trigger action Create Unit makes them, for any slot of the game, attack-moved or moved when asked, and answered with their indexes in the unit table; when fewer are made than asked, `said` holds what the game put on screen meanwhile, such as "Unit unplaceable!" for a building whose footprint is blocked; the game is marked as changed |
| `clear {player}` | in a single-player game: every unit of a slot removed, as the trigger action Remove Unit does (pathbench, between its groups); the game is marked as changed |
| `units {ids}` | units by their index: each one's type, player, position, order, move target, hit points and its maximum (`hpMax`, at its owner's handicap), shields, energy and paralysis timer, or that it is gone (pathbench); with `{player?, type?, max?}` in place of ids, the first units of a player or type, each with what a mineral field, geyser or refinery holds (`resources`) |
| `pathstats` | the pathing's shape and its counters: regions, areas, contours, the time the regions took to build, and the region and local searches, those that filled their budget and those that settled for the closest place (`h/port_pathstats.h`) |
| `region {x, y}` | a point's region, the region's group (0x1FFD is not walkable) and its area: a ground unit reaches the points of its own area (pathbench) |
| `colour {slot, rgb?}` | in a game: a slot's colour in full RGB, `"#rrggbb"` to set it (and its ramp for the 8-bit drawing), `""` to clear it (the ramp staying), none to read it |
| `reveal` | in a single-player game: the fog off for every player, computers too, as the cheat black sheep wall; for a test's view |
| `select {x, y, radius?, player?}` | in a single-player game: the slot's units in the square that far round the point (96 unless given) selected by a drag box's rules, its own units without its buildings when there are units, else one unit, to see what a selection draws without the mouse |
| `hud` | the HUD's data layer (`modules/hud.md`): its values under the HUD format's names, the card's buttons, the console's buttons, the players' board (`players`), and the hits of the design shown |
| `hud_events {since?}` | the HUD's events after that serial: `game.start`, `select.change`, the alerts, `selection.subgroup`, `selection.page`, `group.recall` and the rest |
| `hud_press {place, button, group or eye}` | in a game: a press the HUD would make, through 1.17's handler: the card's place 0 to 8, a button by name (`menu`, `terrain`, `diplomacy`, `messaging`, `idle`, `army`, and in a replay `play`, `slower`, `faster`, `tv`, `follow`), a control group 0 to 9, or in a replay a player's eye by slot 0 to 7 |
| `hud_stats {side?, popup?, page?}` | in a replay or an observer game: the players' stats' side view and popup on or off, and the popup's page 0 to 3, as Shift+F1, F1 and Tab change them (a posted key carries no Shift); answers whether they may show, each view's state, the page, and whether the design places each view |
| `hud_tip {place}` | in a game: the tooltip of the card's button at place 0 to 8 as a pointer resting on it shows it, the design's or 1.17's bubble; -1 hides it. The game's `snapshot` has no HUD, so capture the window to see it |
| `press {key or vk, char?}`, `click {x, y, right?, move?}` | a key or a click posted to the game's window as Windows posts one (`port_winmsg.cpp`), so a check drives the front end on Linux as a posted message does on Windows; `key` is a letter or digit, `enter`, `escape`, `tab` or an arrow, and `char` types it too. `move` only moves the pointer there, with no press. A click is marked as a test's (`PORT_TEST_MOUSE`), so a background window started with `HYPERION_TEST_MOUSE=1` takes it; another background window takes none |

While the channel is open, a single-player game keeps running when its window is not
the active one, so the panel or a tool can be used beside it; 1.17 stops it otherwise, as
Hyperion does unless Settings' Run in the background (`runInBackground`, the Game tab) is on
(`PortControlKeepsRunning`, `PortClockBackgroundOn`).
Scrolling at the screen's edge still stops. The look commands change only what is shown. `gamma` is 1.17's own and is kept on exit as
the Settings slider's is. The sandbox's spawn and reveal change the game, so its replay
no longer plays back the same. Only this machine reaches the channel: it listens on
127.0.0.1, refuses a browser page served from anywhere but itself (its Origin) and a
request naming any host but 127.0.0.1 or localhost.

Three clients come with it, in `tools/control/`: `hyperion_ctl.py`, the command line
(`python tools/control/hyperion_ctl.py set gamma 120`); `hyperion_mcp.py`, an MCP server
the repository's `.mcp.json` registers as `hyperion`, whose `snapshot` returns the frame
as an image so a Claude session can look at what it changed; and Debug Tools' panel,
`mods/debug-tools/panel.html`, which the channel serves at `http://127.0.0.1:27117/`:
sliders for the values, the scene and sandbox commands, and the last snapshot.

Each option also takes the form `--name=value`, and a file is found from the folder
Hyperion is started in. A replay plays in the mode it was recorded in, Brood War's or the
original's. `--map` is a melee game with Brood War when it is installed, against
computers of random race, by default one in every other start slot as on Play Custom,
at the speed the last game was made with. Play Custom seats up to 7; on a Hyperion map
with players past 7, `--computers` takes up to 63 (`PLAYERS_MAX` - 1), and by default, as from the menus, each
slot past 7 the map opens gets a computer of the race the map gives it. `--race` defaults to random; it
also takes a race of the loaded mod's (`--mod`, `docs/mod-data.md`), by its name, which
the local player plays as its base with the race's own start units and units.
`--computer-race` gives every computer a race in place of random, a mod's too for the 7
Play Custom seats; the start positions move the players, so the game gives a mod's race to
a computer of its base. With
`--ums` the game is Use Map Settings instead, as Play Custom starts it: the map seats the
players and its triggers run (`LaunchScenario`, which the canvas uses too); the replay
suite's trigger game uses it. `--teams` gives each player a team, the local
player's first and then the computers' in turn, 0 for none (`src117/port/gameplay/port_teams.h`): the
game plays by Hyperion's rules with its teams allied, allied victory and shared vision within a
team, enemies across; a benchmark run (`--bench`) takes it too. `--starts` gives each player, in the
same order, the map's start location it plays from (the start of that slot, -1 for a random
one as 1.17 gives); the person's must be one of the first eight. It is not a rule: the
replay's slot records already say where each player started. `--ai-levels` gives each player,
in the same order, a computer level (`src117/port/gameplay/port_ai.h`): `c` Classic AI,
1.17's own; `1` to `7` StarCraft II's Very Easy, Easy, Medium, Hard, Harder, Very Hard and
Elite; `v`, `r` and `i` the Cheater levels, vision, resources and insane, which play as
Elite and cheat as StarCraft II's do. A level other than Classic plays by Hyperion's rules
with the option `HX_OPT_AI_LEVELS`, and the replay (`AILV`) and the save (`HXAI`) hold each
slot's level; the skirmish and benchmark set-ups choose them a computer at a time.
`hyperion_ai.log` names the levels each game played with. `--handicaps` gives each player, in
the same order, a handicap in percent, StarCraft II's (`modules/gameplay.md`,
"Handicaps"): 100 to 50 in steps of 10, any other number taken to the nearest. A player's
units and buildings have that share of their maximum life and shields. Any below 100 plays by
Hyperion's rules with the option `HX_OPT_HANDICAP`, and the replay (`HCAP`) and the save
(`HXHC`) hold each slot's; the skirmish set-up chooses them a row at a time. With `--join`, the
first is the seat's own in a room's match. `hyperion_handicap.log` names each slot's.
`--mutators` gives the game mutators (`modules/gameplay.md`, "Mutators"): a preset's
name (`standard`, `plenty`, `gold-rush`, `infinite`, `quick-start`; `rich` is Plenty, its name
before rich bases), or values by name, separated
by commas: `minerals` and `gas` as a share (`x2`, `1.5`, `50%`) or `endless`, `start` as
minerals/gas (`1000/500`), `supply` as a cap or `unlimited`, `workers` 1 to 12, `boost` 0 to
100 by 25, `fog` as `normal`, `explored` or `none`, `build` as a share of 1.17's times (`x0.5`)
and `tech` as `normal`, `all`, `noupgrades` or `noresearch`; rich bases (`modules/gameplay.md`,
"Rich bases") as `rich` (`off`, `naturals`, `radius`, `far` or `every`; `middle`, its name
before v0.6.21, is `radius`), `radius` (tiles, 16 to 256, or `quarter`, `third` or `half` of the
map, or a share such as `40%`), `chance` (25, 50, 75 or 100), `fair=off`, `atleast=off`,
`announce=off` (the game's start says nothing of them) and `richseed=map`; and the unit limit,
Game Options' (`modules/loop.md`, "Caches for large games"), as `units` (`auto` or a count,
1,700 to 262,144), e.g. `--mutators minerals=x2,gas=endless,workers=8,boost=50`, `--mutators supply=600,units=54400`, `--mutators
fog=none,build=x0.5,tech=all` or `--mutators rich=radius,radius=64,chance=75`. Any
other than 1.17's
plays by Hyperion's rules with the option `HX_OPT_MUTATORS`, and the replay (`MUTS`) and the
save (`HXMU`) hold the set; `hyperion_mutators.log` names it.
`--rules` sets the rules a new game plays by
(`src117/h/hxrules.h`): Hyperion's, the default since 2026-10-22, unless it or the
`"hyperion"` section of `settings.json` (`"rules"`) says `classic`, 1.17's to the frame; a game
past 1.17's limits plays by Hyperion's whatever it says, a game over the network by the classic
rules, and a replay or save by the rules it records. `--mod`
loads a mod's data, its units and the rest past 1.17's rows (`docs/mod-data.md` at
the repository's root); a new game with them plays by Hyperion's rules, and a replay or save
of one plays only in a run with the same mod's data. `--load` loads a
saved game as the Load screen's Ok does, 1.17's saves and Hyperion's alike, a multiplayer
one as a single-player game. The local player
gets the name 1.17's mission skip gives a player with no profile chosen. After the game
the menus come up as usual, and they do at once when the file cannot be played, or when
`--race`, `--computer-race` or `--computers` is not one of the values above. These options are taken out of
the command line before `parse_args` too. A map whose terrain uses the tiles Remastered
1.22 added, as recent ladder maps do, plays when the game's files come from a Remastered
install, whose tilesets have them, and is refused with 1.17's MPQs alone.

`tools/play.py` does this for a developer: on its first run it makes `testrun/play/` from
hardlinks to testrun's MPQs and `Local.dll`, `Patch_rt.mpq`, the x64 `SDL3.dll`, the
untouched 1.17 exe and copies of the player's 1.17 profiles, and on every run it copies in
the built `Hyperion.exe` when it differs, then starts the game. `--window` sets
`HYPERION_WINDOW`, `--build` builds first, and other options (`--replay`, `--map`,
`--maps`, `--data`) go to `Hyperion.exe`. The folder has no maps of its own, so without a
`--maps` the script passes the map folders it finds: Remastered's `Maps` (from Battle.net's
uninstall entry, as `tools/assets/scasc.py` finds it) and `Documents\Starcraft\Maps`.

The replay harness runs it from `testrun/hx64/`, laid out the same way, as its `hx64`
build (`replay-test.md`); `replaytest.py setup` builds the oracle's folder and that one.

## On Linux

`tools/build_engine_linux.py` builds the same program for Linux into
`src117/build/linux-x64/`, or `linux-arm64` on an ARM machine: `Hyperion.x86_64` (or
`Hyperion.aarch64`), `libSDL3.so.0`, which it finds beside itself (its rpath is `$ORIGIN`),
and `rt_observer.so` for the replay harness. The program is not called `Hyperion` alone:
on Linux a file cannot share its name with a folder beside it, and the data folder it made
there was `Hyperion/` until 0.6.2's last build (`Data/` since). On Windows the build runs in Docker, in the image `tools/linux/Dockerfile`
describes; on Linux it runs where it is, with the packages that file lists.

In a 1.17 folder: `Hyperion.x86_64` and `libSDL3.so.0` beside `Local.dll` and the MPQs, then `./Hyperion.x86_64`, with the same arguments as on Windows.
`tools/package.py --linux x64` (or `arm64`) packs it as a `.tar.gz` to unpack there. The game sees paths in
Wine's form (`Z:\home\...`), and `src117/port/platform/port_posix.cpp` turns them back into the
system's, matching each name without regard to case as Windows does. With no game's files
found, the first start's window is one Hyperion draws (`port_install_window.cpp`), the same
as Windows' (checked in WSL, 2026-10-21). The GPU presenter
draws through Vulkan. Sound needs the system's PulseAudio, PipeWire or ALSA library, which
SDL loads when it starts; without one the game plays silent. The Korean input method is
left out for now.

For ARM64 (a Jetson, a Raspberry Pi 5, an ARM laptop): `build_engine_linux.py --arch arm64`
builds `Hyperion.aarch64` on Windows in the emulated image (an hour or so the first time,
with SDL), or natively on the ARM machine, and `package.py --linux arm64` packs it. On the
machine: unpack the package into a 1.17 folder copied from Windows and run
`./Hyperion.aarch64`. The replay suite holds the ARM build to the original from Windows,
under emulation (`replaytest.py suite --builds oracle,la64`, `replay-test.md`); the
harness's oracle side needs Windows, so on the ARM machine itself the build is played.

## On a Mac

`tools/build_engine_linux.py` on a Mac (Apple silicon, Xcode's command-line tools, Homebrew's
CMake and Ninja) builds `Hyperion.arm64` and `libSDL3.0.dylib` into `src117/build/mac-arm64/`,
drawn through Metal; `tools/mac_sync.py` sends the tree from Windows. `tools/package.py --mac`,
also on the Mac, makes `Hyperion.app`: the program as `Contents/MacOS/Hyperion` with SDL beside
it, an `Info.plist` (macOS 13 on, the local network's line for LAN games) and an icon from
`hyperion.ico` (`sips`, `iconutil`), signed ad hoc with `codesign -s -`, and beside the app the
Linux package's folders; `ditto` zips it. The program's folder is the one `Hyperion.app` is in
(`modules/platform.md`), so the app goes in a 1.17 folder copied from Windows, or anywhere with
StarCraft from Battle.net in `/Applications/StarCraft`. As Apple has not signed it, a tester
allows it once (System Settings, Privacy & Security, Open Anyway) or clears the quarantine
with `xattr -dr com.apple.quarantine`; the package's README says so.

## In a browser

`tools/build_engine_web.py` builds the same program as WebAssembly into
`src117/build/web/`: `hyperion.js` and `hyperion.wasm`, with the page that runs them,
`engine/web/play/`, copied beside them in `play/`. It runs in Docker in the image
`emscripten/emsdk:6.0.10`; SDL3, StormLib and CascLib are built with it the first time
(`--sdl`, `--libs` again). `--debug` links with Emscripten's checks, the map
`hyperion.map` and `hyperion.cpp`'s debug information.

The build is 64-bit WebAssembly (Memory64), so its layouts are the Linux x64 build's. It
is served on a LAN only, never published: a page on the LAN's plain http is given no
threads, as shared memory needs a secure page. So Hyperion's threads are Emscripten
fibers on Asyncify (`port_posix.cpp`): they take turns where one waits, sleeps or meets
a critical section another holds, and the browser has its turn from the program's own
thread, at least once a frame (`PortWebTurn`, from `PortPresentFrame`). The picture is
SDL's renderer on WebGL; the GPU presenter has no browser backend. The window is the page's
canvas, always at the game's size whatever Display says (`PresentShow`), and the page scales it
to the tab: SDL's full screen there waited for a click and left the canvas at its size while
SDL drew at the screen's, so the frame came out shifted and cropped (2026-10-20). The mouse is
the canvas's: SDL's canvas handlers hand their window back to the program as a pointer, which
in 64-bit WebAssembly is a BigInt, so the page's `SDL3.JSVarToCPtr` makes one (a number threw,
and no movement or click reached the game), and `GetCursorPos` reads SDL's position in the
window, not the page's (`port_posix.cpp`). F11 is the browser's full screen: the page lets it
past the game, whose own F11 (follow the selected unit) a browser's player does not have. In full
screen (F11, or the page's own) the page holds the browser's pointer lock on the canvas while the
tab has the focus, taken when the screen goes full and again at a click in the game: SDL moves
the cursor by the mouse's movement then, and `WinMsgWebHeld` (`port_winmsg.cpp`) keeps it inside
the window, a button held or not, so the view scrolls at the screen's edges as on a PC. Esc or
leaving the tab lets the mouse go, as the browser has it. When
the program ends (Exit Program), `PortWebExit` tells the page (`hyExit`): a room's tab goes back
to the room's page, and the room starts the seat's Hyperion again for its next match; the page
alone loads again.

The page keeps the player's 1.17 files in the browser's storage (IndexedDB, mounted at
`/hyperion`, the program's folder there, with `Data/` its data): from the host when it
offers them (`?offer=`), from the folder the
player chooses (its `maps` too) or files picked one by one, or from an address given as `?files=`, from which it
fetches Local.dll and the three data archives, and what the address's
`files.json` lists. Nothing is uploaded. The setup lists each file with what it is and
whether it is kept yet. Where the browser has a folder picker (`showDirectoryPicker`, on
localhost or https) the page uses it, and the browser asks to let the page view the folder.
Elsewhere, as on a LAN's plain http, a folder input stands in, and Chrome words it as an
upload; the setup says beforehand that nothing leaves the device. The install CD's archives, the music and the
movies, may be missing there (`PortCdOptional`): the start goes on without them, and the
cinematics' screen is left out, as a spawned copy leaves it. `?args=` gives the command
line (a JSON list for arguments with spaces) and `?env=` the environment, for tests.

A browser's sockets are Emscripten's: TCP carried over a WebSocket to the address and port
the program names. The page sends them to the room's web port (`?room=<address:port>`),
where `/tcp/<port>` joins them to the room's game port, the only one it takes
(`port_lanweb.cpp`), and the room counts the connection as the browser's address. The
socket waits there poll and sleep so the page has its turns (`port_lannet.cpp`), and the
network's search, which a browser cannot make, is left out. Hyperion's own menus need the
GPU presenter, so in a browser 1.17's menus show; a copy given `--join` waits for the
room's match with no screen of its own (`port_front.cpp`, `SeatWait`), the room's page
being its lobby.

A LAN room serves the browser build at `/play/` from `web/play` in the package's `Data\`
or beside the program (or `HYPERION_LAN_PLAY_ROOT`); the builds, `package.py` and `play.py` put `hyperion.js` and
`hyperion.wasm` there once `build_engine_web.py` has made them. While the room's Settings
offer them ("StarCraft's files", Players in a Browser), it serves its own 1.17 files at
`/files/`: Local.dll and the three data archives, and an empty
`files.json`, the CD's archives being left to a player's own folder. A page that plays in
its tab (route `tab`) runs `/play/?embed=1` in a frame. The room gives the page a token
for its seat (`{"t":"tab"}`), and the frame starts Hyperion with
`--join 127.0.0.1:<port> --lan-tab <device>:<token>`: its hello carries the token, and the
room counts it as that page's seat, as it counts a streamed seat on the host. The frame
says how it stands to the page (`{hy: 'state'}`, and `{hy: 'game'}` from `SeatWait`), and
comes over the page while its player plays or watches. Watching in the tab is the room
telling that seat to follow a match (`follow`, as for a streamed seat), and its `end` to
leave it. The play page keeps where its files came from in `localStorage`
(`hyperion.play`). Its setup (`?setup=1`, linked from the join page) saves that and the
game's `settings.json` to a file and loads them back.

Three things the other builds were let off are held to here:

- A call through a pointer of another type than its function's traps in WebAssembly,
  where x86 and x64 let it through. The link refuses a direct one (the build looks for
  wasm-ld's `_bitcast_invalid` stubs), and `EMULATE_FUNCTION_POINTER_CASTS` lets the
  indirect ones through. A function 1.17's code declares `void` but whose result its
  caller reads from EAX returns nothing here, so it is declared with its result
  (`AllocCB`).
- `%lu` is 64 bits on every 64-bit build but Windows; a 32-bit value goes with `%u`.
- The state hash tells a pointer from an integer in a union by where it points
  (`port_synchash.cpp`, `IsInteger`), which on x64 is above 4 GB. The browser's memory
  starts its data, stack and heap at 2 GB (`GLOBAL_BASE`), above every integer 1.17 keeps
  there, and the hash takes a value that points into a pool, 1.17's data or the heap for
  a pointer; an image's draw and update functions are hashed as whether it has them.
- A union or struct passed by value goes in registers on x86 and x64 when it is small, and
  by its address in WebAssembly. An image's draw call passes its colouring value as the
  union's pointer-wide arm (`CSprite.cpp`, `IMAGERENDERPROC`), which the draw procedures
  take.

Tested in headless Chrome: the main menu, a skirmish on Lost Temple, and
`h16_bgh2x.rep` of the Hyperion set played through, all 250 of its recorded hashes
agreeing with the game the Windows build recorded.

## The 32-bit loader

`Hyperion.exe` was once a 32-bit loader, and the engine a DLL of its own. The loader
mapped the player's exe into its own image at 0x400000, over a placeholder section,
resolved its imports, loaded the engine DLL, which hooked the original's functions one by
one, and called the original's entry point. One finding of that time: `Storm.dll` had to
be loaded by Windows as the process started, since loaded later with `LoadLibrary` it
opened archives but found no file in them, so the loader imported one Storm ordinal.
