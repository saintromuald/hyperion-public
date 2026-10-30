# platform: the layer between the game and the system

## Overview

1.17 is a Windows program: it opens a window, pumps Windows' messages into `WndProc`, calls
Win32 for files, time, threads and events, keeps its settings in the registry, and reads its
own exe for resources. Hyperion keeps that code as it is and puts a layer under it, in four
parts. The window is SDL's on every platform, and `port_winmsg.cpp` makes Windows' messages out
of SDL's events, so `WndProc` (`src117/lang/event_Win.cpp`) and the game's pumps are unchanged.
Off Windows, `src117/compat/` gives the SDK's headers and `port_posix.cpp` gives the functions,
over POSIX and SDL. Paths, the player's data folder and Hyperion's own command line options are
`port_paths.cpp`; the settings file that stands in for the registry is `port_settings.cpp`,
with `port_json.cpp` as the reader and writer for Hyperion's other JSON. The helpers every
module shares are here too: the log files (`port_log`), and a steady clock, ASCII lower case,
whole-file reads and 1.17's game speeds (`port_util.h`). `port_discord.cpp` tells the Discord
app on the same computer what the player is doing. And `port_standalone.cpp` makes
1.17's data the engine's own: the globals' starting values are Hyperion's source
(`src117/gen/data117.cpp`), no exe of 1.17's is read, and no code of 1.17's runs.

Everything here is presentation and plumbing. Input reaches the simulation only as the commands
the game makes of it, which replays record, so a change here must keep every replay identical
(`../replay-test.md`).

## Status

| platform | state |
|---|---|
| Windows x64 | the main build. `tools/build_engine_x64.py` into `src117/build/x64/Hyperion.exe` |
| Linux x64 | built and on master since 2026-10-16, the full replay suite identical. `tools/build_engine_linux.py` into `src117/build/linux-x64/Hyperion.x86_64` |
| Linux arm64 | the same, `Hyperion.aarch64`, the suite identical under emulation and played on a Jetson Orin NX |
| browser (Emscripten) | 64-bit WebAssembly, `tools/build_engine_web.py` into `src117/build/web/`. Threads are fibers, the picture is SDL's renderer on WebGL, and Hyperion's own menus need the GPU presenter, so 1.17's menus show |
| Mac | arm64 for macOS 13 on, on `hx-mac`: `tools/build_engine_linux.py` run on the Mac, into `src117/build/mac-arm64/Hyperion.arm64`; the quick suite identical. Packaged as `Hyperion.app`, signed ad hoc (below) |

## Key files

| file | what it holds | key functions |
|---|---|---|
| `src117/port/platform/port_winmsg.cpp`, `.h` | the window, SDL's events turned into `WM_` messages, timers, key state, capture, accelerators. The header holds `WinMsg` (Windows' `MSG`) and the procedure and timer types | `WinMsgCreateWindow`, `WinMsgPeek`, `WinMsgGet`, `WinMsgDispatch`, `WinMsgTranslate`, `WinMsgPost`, `WinMsgSend`, `WinMsgDefProc`, `WinMsgSetTimer`, `WinMsgKillTimer`, `WinMsgKeyState`, `WinMsgAsyncKeyState`, `WinMsgSetCapture`, `WinMsgReleaseCapture`, `WinMsgShowCursor`, `WinMsgLoadAccelerators`, `WinMsgTranslateAccelerator`, `WinMsgRemoteKey`, `WinMsgRemoteMouse`, `WinMsgTicks`, `WinMsgSdlWindow`, `WinMsgWindowHandle` |
| `src117/port/platform/port_paths.cpp` | the player data folder, the split of Hyperion's command line options out of 1.17's, the extra map folders | `PortPathsSplitCommandLine`, `PortDataFolder`, `PortLogFile`, `PortCommandOptions`, `PortGameCommandLine`, `PortMapFolderCount`, `PortMapFolderPath`, `PortMapFindInFolders`, `PortMapRelativePath`, `PortCdOptional` |
| `src117/port/platform/port_settings.cpp`, `.h` | `settings.json`: its own small reader and writer, sections and entries under one lock; the header's `PortSettingKind` names the three kinds | `PortSettingKindOf`, `PortSettingNumber`, `PortSettingString`, `PortSettingData`, `PortSettingSetNumber`, `PortSettingSetString`, `PortSettingSetData`, `PortSettingRemove`, `PortSettingsHold`, `PortSettingsRelease`, `PortSettingsPath` |
| `src117/port/platform/port_json.cpp`, `.h` | the reader for Hyperion's other JSON (mods, lights, the live channel, the LAN room, hotkeys), into `PortJson`, a tree whose lookups never fail; and the one way the port writes a string into JSON, and a tree back as text | `PortJsonParse`, `operator[]`, `At`, `Str`, `Num`, `size`, `PortJsonQuote`, `PortJsonWrite` |
| `src117/port/platform/port_log.cpp`, `.h` | every module's log file, `hyperion_<name>.log` in the data folder: FRESH (emptied by the run's first line, kept open) or SHARED (appended to line by line, for files several processes write, as the LAN's), one lock for every line | `PortLogger`, its `operator()` (printf's formats, a `std::string` taken for `%s`) and `Line` |
| `src117/port/platform/port_drawn.cpp`, `.h` | the small drawn windows of SDL's for when Windows' dialogs are not there: the first start's window and the error window off Windows. A window and renderer, Saira and IBM Plex Mono from the exe's resources through stb_truetype (a character neither has from the game's fallback faces, `MapTextFace`, so a translated window draws any script), the light and dark themes, text wrapped at its newlines and in long words, buttons, events in the window's own units | `PortDrawnOpen`, `PortDrawnShow`, `PortDrawnClose`, `PortDrawnText`, `PortDrawnWrapped`, `PortDrawnButton`, `PortDrawnEvent` |
| `src117/port/platform/port_discord.cpp`, `.h` | the line under the player's name in Discord: what they are doing, read from the game and the mod loaded, sent over Discord's local pipe from a thread of its own | `PortDiscordPump`, `PortDiscordStop` |
| `src117/port/platform/port_util.h` | header-only helpers free of the game's headers: a steady millisecond clock, A-Z lower case, a whole file read with a cap, 1.17's game speeds | `PortNowMs`, `PortLower`, `PortReadFile`, `PortReadText`, `PortSpeedFrameMs`, `kPortSpeeds` |
| `src117/port/platform/port_standalone.cpp` | 1.17's data as the engine's block, the wide globals' translation, what 1.17's start-up did, 1.17's resources | `PortGameDataLoad`, `PortStartUp`, `PortRunGame`, `PortDataAddress`, `PortOriginalAddress`, `PortOriginalSpans`, `PortDataStore`, `PortDataRow`, `PortDataGrown`, `PortGameResources`, `PortLoadResources`, `PortModuleResource`, `PortOriginalCodeReached`, `OcRand`, `OcSrand`, `PortCrtRandState` |
| `src117/port/platform/port_posix.cpp` | the Win32 the engine calls, off Windows: errors, paths, handles, files, the process, time, threads and events, memory, modules and PE resources, strings and code pages, rectangles, the window's few queries, registry and security stubs, the Microsoft C runtime names; and browser fibers | `PortNativePath`, `PortPosixSetCommandLine`, `PortWebTurn`, `oc_posix_hook`, and about 240 Win32 functions under their own names |
| `src117/compat/*.h` | `windows.h` and the SDK headers the engine includes, off Windows only: Windows' values and layouts, with `basetsd.h`, `bcrypt.h`, `ddraw.h`, `imm.h`, `intrin.h`, `emmintrin.h` (SSE2 in NEON on ARM), `mmsystem.h`, `shellapi.h`, `shlobj.h`, `winsock2.h`, `ws2tcpip.h` | types and declarations |
| `hyperion/hyperion.cpp` | the program's entry, `wWinMain` on Windows and `main` elsewhere, both into `HyperionMain` | `HyperionMain`, `CheckGame`, `LoadDevelopmentDlls` |
| `src117/h/platform.h` | the type floor: `DWORD`, `ULONG` and `LONG` 32 bits wide everywhere, `OC_UNALIGNED`, `OC_X86_LAYOUT`, `OcRand` | macros and typedefs |

## How it works

### Start-up

The link's entry is the C runtime's, so `hyperion/hyperion.cpp` runs first, not the game's
`WinMain`. `HyperionMain` does the same work on every platform:

1. `--crash-report <folder>` shows the error window for a crash report and ends
   (`tools.md`, "The error window"). `--hxm-check` is answered next, since a map check needs no
   game.
2. The game's folder is found (`GameFolder`): `HYPERION_GAME`, else the classic StarCraft folder
   Settings keeps (`"classic"`, `data.md`), else the program's folder
   (`PortProgramFolder`: on a Mac, the folder `Hyperion.app` is in, below); a
   file named there stands for its folder.
   `HYPERION_GAME` is set to the folder, with its separator. `--hxm-compile` stops here.
3. `PortInstallCrashReporter` goes in, and `--room` takes over for a LAN room, which needs no
   game data.
4. A start a player made with no install kept, or whose kept one's files are gone
   (`PortInstallMayAsk`, `PortInstallChosen`, `PortInstallFound`), asks for the folder of each
   install they have and which is the base (`PortInstallFirstStart`, `data.md`), and finds the
   game's folder again. `HaveGameFiles` looks for `StarDat.mpq` in the game's folder, or the
   source `PortGameSource` decides, and without them a message names the files Hyperion
   needs and the process ends. `PortGameDataInit` fills the engine's block,
   and `LoadDevelopmentDlls` loads what `HYPERION_DLLS` names, each `path[@export]`, calling
   the export (`oc_init` by default) with the main thread's id.
5. `WriteInitLog` writes `hyperion_init.log`, and `PortRunGame` calls the game's `WinMain`
   with the command tail, as 1.17's start-up did.

On Windows `wWinMain` first asks to keep full speed and fine timers (`KeepFullSpeed`, against
power throttling), and builds its arguments from `CommandLineToArgvW`. Off Windows `main`
ignores `SIGPIPE` and hands its arguments to `PortPosixSetCommandLine`, which rebuilds the
single string `GetCommandLineA` gives.

1.17's `InitACL`, which kept other processes away from the game's, is not called, so Task
Manager can write a dump of a copy that hangs. It has no part in the game.

### The start-up log

Every start writes `hyperion_startup.log` in the data folder (`src117/port/tools/port_startup.cpp`),
a line for each step with the seconds since the start, written through as it comes, so after a
hang the last line is the step it stopped at. `PortStartupBegin` is the entry's first call, and
its lines wait in memory until the data folder is known. The head gives the date, the process
and whether a player made the start or a script, a test or a tool did, then what
`port_sysinfo.cpp` gathered: the build (version, commit, the PDB's id), the system (the OS,
CPU, memory, locale, SDL) and the run (the command line, every `HYPERION_*` variable, a few
named settings). The displays, the presenter and the install are written as their steps come.
Paths name the user's folder `%USERPROFILE%` (`~` off Windows); the computer's and the user's
names stay out.

`PortStartupStep(stage, ...)` writes a step: the data folder, the settings read
(`PortSettingsReadNote`), the game's folder, the crash reporter, the install and Battle.net's
storage (with CascLib's error), the first start's window and its answer, the game's files, one
copy on the data folder, `Local.dll`, the memory check, the archives, the window, video and
sound, every presenter tried with its reason (`PortGpuWhy`), the cinematics, the title, the
graphics, and "complete" at the first frame shown (`PortStartupFrame`), the front end's or a
game's. SDL's own messages go in while the start runs. The log before moves to
`logs\startup_<stamp>.log`, which keeps five. A copy that finds another on its data folder
raises that one's window and ends (`port_instance.cpp`), its log under `logs\` so the running
copy's stays. A tool's run that ends on purpose (`--mod-check`, a lab worker, a benchmark)
says so with `PortStartupEnded`.

A start that fails says so. `PortStartupFail` writes the line, then the error window says what
failed and where the log is, with a text to copy that `port_share.cpp` makes from the log
(`tools.md`, "The error window"), and the process ends with 1. The log's text is kept in
memory for it as it is written (`g_written`, the head and the tail of one past 256 KB). The ends that come
there: `Fail` in `hyperion.cpp`, `PresentFatal`, 1.17's `appcritf` and fatal errors while the
start is pending (`PortCrashCritical`, `PortCrashFatal`), a dialog of 1.17's that `Local.dll`
does not have (`gds_local.cpp`), and a first start's window that cannot
open. When the data folder cannot be written the log goes to the temporary folder's
`Hyperion\`, and its first line says why. On Windows `SDL3.dll` is a delay-loaded import
(`build_engine_x64.py`), so a missing one is said in the window, Windows' dialog, naming the
folder before any call of SDL's.

The next start reads the last log. One with neither "complete" nor a failure says once that
the last start did not finish. One that completed with no line for its end and no crash report
says the last session ended with no word, and its logs are copied to
`crashes\<stamp>_unfinished\`. A watchdog thread writes the step and the main thread's stack
when no step has come for 30 seconds, once a step; waits on the player (the first start's
window, 1.17's dialogs, a cinematic) do not count. Each of these shows the error window
too. `HYPERION_QUIET` and `hyperion_quiet.flag` keep the windows away, as they do the crash
report's. `HYPERION_TEST_STARTUP=<stage>:fail|stall`
makes a step fail or stop, to try each of these.

### 1.17's data

`PortGameDataInit` (`port_standalone.cpp`) fills the engine's block in three steps. It once read
them from the player's `StarCraft.exe` (`PortGameDataLoad`); now the values are Hyperion's source,
`src117/gen/data117.cpp`, which `tools/data117_source.py` writes once from the oracle exe
(the user's decision, PRD 2026-10-19):

1. The game's own part of 1.17's `.rdata` and `.data` is copied to its 1.17 addresses inside
   the engine's block (`src117/h/oc_data.h`), with every global the engine names past it and
   what the game's part points at: 61,865 bytes in 433 pieces. The rest stays zero: the
   original's C runtime, its imports, its crash reporter and PKWARE's tables, which nothing
   of Hyperion's reads. `HYPERION_DATA_POISON=1` fills those places with 0xA5 instead, for a
   check of that (the full suite and `hxsuite` stay identical with it).
2. The dwords the exe's base relocations named, which the file keeps, are every pointer in
   that data. A pointer into the data becomes
   the engine's address for that global (`PortDataAddress`); a pointer into 1.17's code becomes
   the engine's function for it through `src117/gen/code_map.cpp`, or, with no row, the trap
   `PortOriginalCodeReached`, which raises `0xE0520002` with the caller's address. The counts of
   both go in `hyperion_init.log`.
3. Globals whose layout changes at 64 bits have storage of their own in `g_wide117`, and each
   is translated field by field by the table in `src117/gen/wide117.h`. The block's copy is
   then cleared.

`PortDataAddress` and `PortOriginalAddress` map between 1.17's addresses and the engine's in
both directions, `PortOriginalSpans` gives the two spans for the state hash's quick test, and
`PortDataStore` and `PortDataRow` put a file's rows (DAT columns, a mod's rows past 1.17's)
through the same translation. `PortStartUp`, which the game's `WinMain` calls first, does what
1.17's static start-up did: the 31 list heads' second words, three critical sections, the main
thread's id, the league event and the C runtime's `__isa_available`.

1.17's resources come from `Local.dll`, loaded as a data file (`PortLoadResources`,
`PortModuleResource`), so none of its code runs and a 64-bit process can read the 32-bit
file. The two the game took from the exe are in `data117.cpp`: VCOD #202, which every map's
VCOD section is checked against (`PortGameVcod`), and the accelerator table 0x71, the
multiplayer chat's keys, which `Local.dll` lacks (`PortGameAccelerators`). The exe's string 3,
the locale, which `GetLocale` asked after `Local.dll`'s, was never there, so the locale falls
back to 0x409 as before. With no `Local.dll` at all, in an install of a language that has none
or on a Mac, `port_local.cpp` stands in for it (`data.md`, The install's language). Off Windows the same calls go to
`port_posix.cpp`'s own reader of a PE resource directory, and Hyperion's own resources are a
table the Linux build links in (`resources_posix.cpp`, written by `tools/build_engine_linux.py`).

### Where the player's files live

`PortDataFolder` gives `Data\` in the folder the program runs from, a folder of Hyperion's own
anywhere (PRD 2026-10-21), with the game's files read where they are installed, or the folder
given as `--data <folder>`; it is created if it is missing, and the path always ends in a
separator. Before 0.6.2's last build the folder was `Hyperion\`: at the start, what a
`Hyperion\` beside the program holds moves into `Data\` (`PathsMoveInto`), a file or folder
`Data\` lacks whole, a folder both have merged, of a file both have the old copy deleted when
it holds the same bytes and else left where it was (the package's newer copy wins), and
`Hyperion\` goes once empty. Its `docs\` goes the same way into the `docs\` beside the program,
where a package puts the documents now. In StarCraft from Battle.net's own
folder (a `.build.info` beside the program), whose `Data\` is Blizzard's storage, the folder
stays `Hyperion\` (`PathsHomeName`). `SFileSetAppDataDir(PortDataFolder())` from `InitPaths`
(`lang/main.cpp`) points Storm at it, so profiles, replays, saved games, saved and downloaded
maps, the map cache, mods and screenshots all land there. 1.17's own `Documents\Starcraft\` is
left untouched. Hyperion's logs go there through `PortLogFile`, which returns a path in a
buffer of the calling thread's. On Linux the program is `Hyperion.x86_64` or
`Hyperion.aarch64`, named so when a file could not share its name with the `Hyperion/` folder
it made beside itself. In a browser the program's folder is `/hyperion`, the IndexedDB mount
the page fills with the player's 1.17 files, with `Data/` beside them for its data.

The program's folder is `PortProgramFolder` (`port_paths.h`): the exe's on Windows and Linux.
On a Mac the program is `Hyperion.app/Contents/MacOS/Hyperion`, and its folder is the one
`Hyperion.app` is in (`MacAppFolder`, `port_posix.cpp`), so `Data/` sits beside the app as it
sits beside the program elsewhere.

What a package brings besides the program is `docs\` beside it and the rest in that `Data\`
(`tools/package.py`, the user's wishes of 2026-10-21 for a tidy folder): the mods, tools, maps,
`web\` and `notes\`. `PortPackagedFile` finds a packaged file by its path there, and failing
that beside the program, where the builds and `tools/play.py` put `web\` and `notes\`, packages
before 0.6.2 did, and a package puts `docs\`; the LAN room's pages, the release notes' pictures
and Copy for AI's list of files look through it. The maps in `Data\maps\` are found as the data
folder's maps. A downloaded
app still in quarantine is run by macOS from a read-only copy in a folder of the system's (App
Translocation); `MacAppFolder` asks the Security framework for the original's place
(`SecTranslocateCreateOriginalPathForURL`, through `dlsym`), so a tester's first start reads
the folder the app was unzipped in. Starting another copy of the program (a LAN room, a host
copy, a streamed seat, a lab worker) still takes `GetModuleFileNameA`, the program itself.

`port_paths.cpp` also parses Hyperion's command line. `PortPathsSplitCommandLine` walks the
line word by word, takes out every option in `PortOptions` (`port_paths.h`), and hands the rest to
1.17's own `parse_args`, which would otherwise match a folder's name as a prefix of one of its
switches. Each option also takes the `--name=value` form, and file options are made absolute
against the folder the program started in. Up to `PORT_MAP_FOLDERS` (8) extra map folders come
from `--maps`; each stands for a `maps` folder, and `PortMapFolderPath`,
`PortMapFindInFolders` and `PortMapRelativePath` translate names under `maps\` to and from
them. When the game's files are StarCraft from Battle.net's, its `Maps\` folder is one more
after them (`PathsMapFoldersOnce`, on the first call, once the start has chosen the files), as
1.17's own two places, the data folder and the game's folder, which is then Hyperion's own
folder, do not reach it; a classic StarCraft folder elsewhere is the game's folder already.
Hyperion only reads those folders.

### The window and the messages

`InitWindow` (`lang/main.cpp`) no longer registers a class or creates a window: it calls
`WinMsgCreateWindow(WndProc, "Hyperion")`, which starts SDL's video subsystem and creates a
hidden 640x480 SDL window. On Windows `SDL_RegisterApp` gives the class 1.17's name,
`SWarClass`, and `CS_DBLCLKS`, so the instance check and the test tools still find it, and the
handle handed back is the real `HWND`; elsewhere it is the `SDL_Window *`. The renderer sizes
and shows the window later (`PortPresentInit`), and `port_window.cpp` sets square corners and
Hyperion's icon.

On a Mac the window is made at the screen's own pixels (`SDL_WINDOW_HIGH_PIXEL_DENSITY`), so a
Retina screen draws two pixels a point and the picture is as sharp as on Windows. SDL measures a
window and the mouse in points there, and the game in pixels: `GetClientRect`,
`GetWindowRect`, `SetWindowPos`, `ClientToScreen`, `ScreenToClient`, `GetCursorPos`,
`SetCursorPos`, `ClipCursor` and `GetSystemMetrics` (`port_posix.cpp`) turn one into the other
by the window's pixel density, and so does the presenter between a window position and the
game's (`PresentDensity`, `port_present.cpp`). A hidden or background window, as the harness's
and the lab's, stays at a pixel a point.

The pump is `WinMsgPeek` and `WinMsgGet`, which the game reaches from `GetNextMessage`
(`lang/event_Win.cpp`) and the front end from its own loops. `WinMsgPeek` hands out the queue
first, then pumps SDL once through `Translate`, then a due timer; `WinMsgGet` waits with
`SDL_WaitEventTimeout` until an event or the next timer. `Translate` makes the messages Windows
would have sent, with the bits the game reads:

- keys by scancode to a virtual-key code and Windows' set 1 scan code, with the extended, Alt,
  previous-state and transition bits of `lParam`. Letters take their code from the layout,
  keypad keys follow Num Lock, and Alt with a key, Alt itself and F10 are `WM_SYSKEYDOWN` and
  `WM_SYSKEYUP`.
- text as `WM_CHAR` in Windows-1252, the code page 1.17's text is in; a character the page does
  not have is dropped. `WinMsgTranslate` adds what `TranslateMessage` made and SDL's text does
  not carry: Backspace, Tab, Enter, Escape, Ctrl with a letter, and `WM_SYSCHAR` for Alt.
- buttons as down, up and, for the second click of a pair, the double click; the wheel in
  120ths with the pointer's screen position; motion and buttons in the window's client pixels,
  scaled by `SDL_GetWindowPixelDensity`.
- focus as `WM_ACTIVATEAPP`, which also tells the sound layer and drops every held key and the
  capture; exposure as `WM_PAINT`; the close box or Alt+F4 as `WM_SYSCOMMAND` with `SC_CLOSE`,
  which `WinMsgDefProc` turns into `WM_CLOSE` and then `WM_DESTROY`.

`WinMsgDispatch` calls the window procedure, or a timer's callback where one was given.
`WinMsgSetTimer` and `WinMsgKillTimer` keep a small table with a due time per timer, and hand
out ids from `0x7F00` up for the form that names no window. On Windows a message hook (`WindowsHook`) lets through
what a tool posts straight to the window and SDL would otherwise swallow or strip:
`WM_COMMAND`, the `WM_APP` range, a test's mouse message (marked `PORT_TEST_MOUSE` in `wParam`)
and keys or characters arriving while the window does not hold the keyboard. `WinMsgRemoteKey`
and `WinMsgRemoteMouse` are the same door for a streamed seat's browser input
(`port_lanstream.cpp`).

Key state is kept in a 256-byte table in Windows' form, bit 7 down and bit 0 toggled, read by
`WinMsgKeyState`; `WinMsgAsyncKeyState` answers the mouse buttons from SDL's live state
instead. Capture is `SDL_CaptureMouse`, with the `WM_CAPTURECHANGED` Windows sends. The window
holds the cursor count only; turning a window position into the game's 640x480 pixels, and
dropping input for a background or hidden window, belong to the present layer
(`PortMouseIgnored`, `PortMouseToGame`, `port_present.cpp`).

### What port_posix.cpp stands in for

The whole file is inside `#if !defined(_WIN32)`; on Windows it is empty. Its sections:

- **errors**: a last error per thread, in Windows' codes, made from `errno`, and a few
  `FormatMessageA` strings.
- **paths**: `OcPath` turns the engine's Windows-form path into the file system's. A drive letter
  is the root, backslashes become slashes, and where a name is not there in the case given, the
  folder is searched for it in any case, as Windows would find it. `WinPath` is the reverse,
  with the root as drive `Z:` as Wine gives it, since 1.17's code looks for a backslash to find
  a file's folder. The port's own calls into SDL and the archive libraries take
  `PortNativePath`. The C library's `fopen` and `remove` are wrapped at the link
  (`-Wl,--wrap`), because `<cstdio>` takes `h/compiler.h`'s macro away again.
- **handles**: a `HANDLE` is an object of this file (file, find, event, thread, process,
  mapping, module), tracked in a live set so a handle that is not one fails as on Windows. Every
  waitable object sits under one mutex and one condition, which is never destroyed, since
  destroying it at exit hung the game.
- **files**: `CreateFileA`, `ReadFile`, `WriteFile`, the file pointer and times,
  `FindFirstFileA` over `readdir` with `fnmatch`, attributes, copy, move, delete, directories.
- **the process and its environment**: the command line from `main`, the environment,
  `ExitProcess`, process and thread ids, `CreateProcessA` through `posix_spawn`, the processor
  count and memory, the computer and user names, `SHGetFolderPathA` by the XDG base directories,
  `ShellExecuteA` through `xdg-open` (`open` on a Mac, `window.open` in a browser). There is no
  Windows folder and there are no drive letters, so font lookups and CD searches find nothing.
  On a Mac the run is one of `NSProcessInfo`'s activities from `main` on
  (`PortPosixKeepRunning`, user initiated and latency critical, through the Objective-C
  runtime's calls declared with plain pointers), so App Nap does not slow a game whose window
  is not seen: a LAN game, a lab worker, the harness's runs. The system can still sleep.
- **time**: `GetTickCount`, `timeGetTime`, `QueryPerformanceCounter`, the `FILETIME` and
  `SYSTEMTIME` conversions, `Sleep`, and the multimedia timers as a thread each. The replay
  observer takes the clock, `Sleep` and `CreateEventA` through `oc_posix_hook`, since it cannot
  patch an import table off Windows.
- **threads, events, waits and memory**: `CreateThread` on pthreads, events,
  `WaitForSingleObject` and `WaitForMultipleObjects` on the one condition, critical sections as
  recursive mutexes inside 1.17's own 40 bytes, suspend and resume for the profiler;
  `VirtualAlloc` and friends on `mmap`, with each reservation's size kept so `MEM_RELEASE` can
  free it, file mappings, and a named memory mapping as a file in the temporary folder
  (`port_instance.cpp`'s instance lock).
- **modules, resources and strings**: `LoadLibraryA` on `dlopen`,
  `LOAD_LIBRARY_AS_DATAFILE` as the PE resource reader, `FindResourceA`, `LoadStringA`, the
  version block, Hyperion's own resources from the linked table, and Windows-1252 case,
  comparison and `wsprintfA`.
- **the window's queries**: the calls that only ever name the game's own window, which go to
  `port_winmsg.cpp`; other programs' windows are never found. `MessageBoxA` is an SDL message
  box and a line on standard error, and `DialogBoxParamA` runs 1.17's few dialogs (a missing
  disc, a fatal error) with their text in a message box whose button reaches the procedure as
  `WM_COMMAND`.
- **the registry, security and the Microsoft C runtime names**: no registry keys, so reads fail
  with `ERROR_FILE_NOT_FOUND` and writes with `ERROR_ACCESS_DENIED`; tokens, SIDs and ACLs fail
  as Windows does when it refuses, so the callers' failure paths run; and `_time64`, `_strupr`,
  `_beginthreadex`, the `_s` functions and the `_mbs` ones. 1.17's own settings never come here:
  they go to the settings file through Storm (below).
- **threads in a browser**: with no shared memory on a plain http page, Hyperion's threads are
  Emscripten fibers on Asyncify. They take turns wherever one waits, sleeps or meets a held
  critical section, and the browser gets its turn from the program's own fiber, at least once a
  frame through `PortWebTurn`, which `PortPresentFrame` calls.

### Discord

With the Discord app running on the same computer, the player's profile shows Playing Hyperion
and a line of what they are doing. The application is Hyperion's on Discord's
developer site, id `1564423796356685854`, and its picture is the art asset `hyperion`. There is
no Discord library: `port_discord.cpp` speaks Discord's local RPC itself, on Windows its pipe
`\\.\pipe\discord-ipc-0` to `-9`, elsewhere the Unix socket `discord-ipc-N` in
`XDG_RUNTIME_DIR`, `TMPDIR`, `TMP`, `TEMP` or `/tmp`, and in the Flatpak's and the Snap's
folders under them. A frame is an opcode and a length, both 32-bit little-endian, then JSON: a
handshake (`{"v":1,"client_id":...}`, answered by READY), then `SET_ACTIVITY` with the process
id; a ping is answered with a pong.

`PortDiscordPump` runs once a frame, from `PortPresentFrame` in a game and from the front end's
own loop in the menus. Once a second it describes what the player does, and a thread of its own
talks to the pipe, so a Discord that is slow or gone never holds a frame. The thread starts on
the first call that wants a line, tries the pipe every 15 seconds while Discord is away, and
sends at most once every 4 seconds (Discord takes five in 20). Discord clears the line itself
when the connection closes, so an exit needs nothing.

The line comes from what is loaded, never from a list of mods (the user, 2026-10-26: "Have it
read the mod based on whats loaded"):

| doing | first line | second line |
|---|---|---|
| a game a mod's own mode started (a mod whose `mode` is not the campaign's) | the mod's name from its `mod.json`, and the map | the players |
| a skirmish | Skirmish, and the map | `1 vs 3 computers`, `2 vs 2`, or `N players, M computers` |
| a LAN game | LAN game, and the map | the players |
| watching a LAN game, a skirmish or a replay | Watching ..., and the map | the players |
| the campaign | Campaign, and the mission | |
| a LAN room | In a LAN room | the match's map, with its seats as the party (`2 of 4`) |
| the menus | the screen (Setting up a skirmish, Choosing a replay ...), or on a mod's own screen the mod's name | |

The picture's tip is `Hyperion · ` and the mod or game loaded. The clock under the line starts
again only when what the player does changes. Settings, Hyperion, Show in Discord (`discord` in
the settings file, on by default) turns it off. A copy no player sees says nothing: the LAN
room's own process, a browser player's seat on the host, an AI lab worker, and a test's window
(`HYPERION_WINDOW`, `hyperion_quiet.flag`) unless `HYPERION_DISCORD=1`. Its log is
`hyperion_discord.log`, each line it sends. The browser build has no Discord.

## Settings file

One file, `settings.json` in the player data folder (`PortSettingsPath`). It holds a section per
key and a named value per entry:

```json
{
  "hyperion": {"frontEnd": 1},
  "Starcraft": {"speed": 4, "Gamma": 100, "Recent Maps": {"hex": "0a00ff..."}}
}
```

A `REG_DWORD` is a JSON number, a `REG_SZ` a string, and `REG_BINARY` (or `REG_MULTI_SZ`, with
`"multi": true`) an object holding the bytes as hex. Section and value names are matched without
regard to case, as the registry matches them. 1.17's own settings arrive through Storm's
registry calls, which `port_stormbase.cpp` answers from this file: `RegSection` makes the
section out of the key name, with the prefix `Battle.net\` for the flag Storm uses for that
hive, and the sixteen preferences, the gamma and the recent maps keep 1.17's own names.
Vanilla's registry key is never read or written. Hyperion's own settings are the `hyperion`
section.

Code reads a setting with `PortSettingNumber("hyperion", "loopRest", &d)` and writes it with
`PortSettingSetNumber`, with `PortSettingString` and `PortSettingData` for the other two kinds;
a reader leaves its variable alone and returns false when the entry is not there, so a caller's
default survives. `PortSettingKindOf` says which kind an entry is, `PortSettingRemove` deletes
one, and a number read as a string, or the reverse, converts as Storm's calls do. Every setter
writes the whole file through a temporary file and `MoveFileExA`, unless `PortSettingsHold` and
`PortSettingsRelease` bracket a group of changes, as the settings screen's Accept and 1.17's
exit do. One `SRWLOCK` guards it all.

`port_json.cpp` is a separate, fuller reader (RFC 8259, `\u` escapes and surrogate pairs into
UTF-8, 64 levels deep) for Hyperion's other files: `mod.json`, `lights.json`, `lanroom.json`,
the hotkeys, the live channel's messages. A lookup that finds nothing gives an empty value, so a
chain of lookups needs no checks. Every string the port writes into JSON goes through
`PortJsonQuote` (`"` and `\` escaped, a control character as `\u00XX`, UTF-8 as it is), and
`PortJsonWrite` writes a parsed tree back as text.

## Environment variables

Read by the files of this area. The renderer's and the front end's own are in
`../hyperion-exe.md`.

| variable | effect |
|---|---|
| `HYPERION_GAME` | the game's folder, instead of the program's; a file named there stands for its folder. `hyperion.cpp` sets it to the folder, with its separator |
| `HYPERION_DATA_POISON` | `1`: the places of 1.17's data that `data117.cpp` leaves out filled with 0xA5, for a check that nothing reads them |
| `HYPERION_DLLS` | development DLLs to load at start-up, `path[@export];...`, the export defaulting to `oc_init` and called with the main thread's id. The replay observer comes in this way |
| `HYPERION_DISCORD` | `0`: Discord is told nothing; `1`: a test's window tells it all the same, to check the line |
| `HYPERION_DUMP_DATA` | a path to write the 1.17 data block to as the game starts, for comparing two loads |
| `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` | off Windows, where `SHGetFolderPathA` answers for Documents, Application Data and Local Application Data |
| `TMPDIR` | off Windows, what `GetTempPathA` gives, `/tmp` without it |
| `USER` | off Windows, what `GetUserNameA` gives |

## Integration points

- `src117/lang/main.cpp`: `InitWindow` asks for the window, `InitPaths` points Storm at the data
  folder, `WinMain` calls `PortStartUp` first. `src117/lang/event_Win.cpp`: `WndProc` and
  `GetNextMessage`, the game's side of the pump, and the IME calls `port_posix.cpp` stubs.
- `src117/port/render/port_present.cpp` and `port_window.cpp`: the window's size, the mouse in game
  pixels, the icon and the corners (`render.md`). `port_stormbase.cpp`: Storm's registry
  calls onto the settings file. `port_crash.cpp`: installed from `hyperion.cpp`, and off
  Windows it takes faults as signals through `PortCrashRaise`.
- The readers of `PortCommandOptions`: `port_launch.cpp`, `port_record.cpp`, `port_bench.cpp`,
  `port_lanroom.cpp`, `port_front*.cpp`.
- `PortDiscordPump`: called from `PortPresentFrame` (`port_present.cpp`) and the front end's
  `RunScreen` loop (`port_front.cpp`); it reads the front end's screen through `PortFrontScreen`.
- `src117/gen/code_map.cpp`, `src117/gen/wide117.h` and `src117/h/oc_data.h`: the tables
  `port_standalone.cpp` works from. `tools/build_engine_linux.py` and `build_engine_web.py`
  build `src117/compat/` and `port_posix.cpp`, and generate `resources_posix.cpp`.

## Gotchas

- **1.17's 32-bit fields.** `DWORD`, `ULONG` and `LONG` are 32 bits on every platform
  (`src117/h/platform.h`), because 1.17's fields are. Never write `long` or `unsigned long` in
  engine code: Windows keeps `long` at 32 bits and Linux and Mac do not.
  `%lu` is 64 bits on every 64-bit build but Windows, so a 32-bit value takes `%u`.
- **Pointer-wide fields in 1.17's data.** A global that holds a pointer is not read in the block;
  it lives in `g_wide117` and is reached through `PortDataAddress`. Writing a file's bytes
  straight over such a global corrupts it: use `PortDataStore` and `PortDataRow`.
- **Alignment off Windows.** The x86-64 ABI lets a compiler assume an array of 16 bytes or more
  is 16-aligned, and 1.17's globals sit at 1.17's offsets, so 614 declarations carry
  `OC_UNALIGNED`. `build_engine_linux.py --check-align` lists any that need it.
- **`rand` is `OcRand`.** The game's random numbers are MSVC's LCG in `port_standalone.cpp`,
  named apart so no library's call to `rand` moves the game's state. The state is one global,
  which a Hyperion save keeps.
- **Case and separators.** Off Windows a path is matched name by name without regard to case,
  which costs a directory read per missing name; names inside an MPQ keep their backslashes,
  which Storm hashes, so only a path that reaches the system goes through `OcPath`. The engine
  sees Wine's form, `Z:\home\...`.
- **A test's mouse, and timers.** SDL's events would lose the `PORT_TEST_MOUSE` bit, so posted
  mouse messages come through the Windows hook instead; off Windows the live channel's `press`
  and `click` do that work (`port_control.cpp`). Timers fire from the pump only, so a game that
  does not pump gets none, and a timer's callback runs on whichever thread pumps.
- **Services that answer nothing off Windows.** The registry, code signing, ACLs,
  `DuplicateHandle`, `GetWindowsDirectoryA`, drive strings and other programs' windows all fail
  or come back empty by design; a new caller must cope with that answer.
- **In a browser.** The threads are fibers, so a loop that never waits holds the page still;
  `PortWebTurn` covers the frame loop. Sockets are TCP over a WebSocket, there is no network
  search, the GPU presenter has no backend, and the install CD's archives may be missing
  (`PortCdOptional`).

## Open items

- Mac: `Hyperion.app`, signed ad hoc, not by Apple (`tools/package.py --mac`). The
  user tried the front end, a skirmish, sound and a Retina window on 2026-10-19 ("Looks
  sharper, mouse lines up fine"), and a downloaded package's first start on 2026-10-20. Its calls of its own: the memory from `sysctl` and the Mach host's
  statistics (`GlobalMemoryStatus`; 1.17 stops on dialog 109 when it reads none), the program's
  path from `_NSGetExecutablePath`, the crash report's registers and code range from macOS's
  thread state and Mach-O header, and a lab worker that watches the lab through kqueue, as
  macOS has no `PR_SET_PDEATHSIG`.
- A critical section is Windows x64's size, 40 bytes, as each of 1.17's has only that room in
  the wide storage (`gen/wide117.json`, sized by MSVC's probes); its pthread mutex is kept
  apart. A compat type that holds a system type must keep Windows x64's size the same way.
- `--data` and `--maps` are to be replaced by settings. The Korean input method is Windows only;
  off Windows the IME calls are stubs and SDL's own composition stands in.
- The registry stub means Remastered is not found on Linux unless Settings or
  `HYPERION_REMASTERED` names it; a Mac looks in `/Applications/StarCraft`, Battle.net's place
  for it (not yet tried with an install there).

## Further reading

- `../hyperion-exe.md`: the program, start-up, the exe on disk, running it, On Linux, In a
  browser, and every other environment variable.
- `../replay-test.md`: the gate every change here must pass. `render.md`: the window's
  size, the presenters and the mouse in game pixels.
