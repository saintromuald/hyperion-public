# engine

Hyperion's engine: the game's C++, Hyperion's own code around it, and the tools that
build and test them.

## What it is

`Hyperion.exe` is one 64-bit program (`docs/hyperion-exe.md`). None of the original's code
runs: the game is the C++ here, the C runtime is the compiler's, and Storm, the PKWARE code
and the drawing routines are the port's own (`src117/port/`). It reads no classic exe. The
game's globals start from Hyperion's own source (`src117/gen/data117.cpp`), copied as the
program starts into a block of the engine's own (`src117/h/oc_data.h`), with a global whose
layout changes at 64 bits translated into a definition of its own (`src117/gen/wide117.h`).
The code addresses classic's data holds go to the engine's functions through
`src117/gen/code_map.cpp`.

The game's own files are the player's: Hyperion reads a classic install's MPQs or a
StarCraft: Remastered install (`docs/modules/data.md`).

## Rules for this tree

- The replay suite is the gate: a change that is not meant to change the simulation must
  keep every replay identical to the original on every frame (`docs/replay-test.md`). A
  change that is meant to change it says so, and goes behind a rule set so classic replays
  still play.
- No Blizzard files in git. The original exe and the game data stay outside it
  (`engine/oracle/` and `engine/testrun/` are gitignored).
- The game's function and global names are the original's own; keeping them keeps the code
  readable against every other source about the game.

## Layout

| Path | Role |
|---|---|
| `src117/h/` | headers: structs, class layouts, declarations |
| `src117/lang/` | the C++ bodies, one file per original source file (`src117/tus.txt` lists the ones the build compiles) |
| `src117/port/` | Hyperion's own code: Storm, the renderer and platform layer on SDL3, the formats at 64 bits, saves, the crash reporter, the names the game reaches the port's bodies by (`port_names.cpp`); a folder an area (`loop`, `render`, `data`, `front`, `gameplay`, `replays`, `lan`, `tools`, `platform`), as `docs/modules/overview.md` maps them |
| `src117/gen/` | generated source: `globals117.h`, the game's globals; `wide117.h` and `wide117.json`, the globals laid out anew at 64 bits; `save117.h`, the saved records' layouts; `code_map.cpp`, classic's code addresses to the engine's functions; `data117.cpp`, the globals' starting values; `layouts117.cpp`, the dialog layouts |
| `src117/tus.txt` | the TUs the build compiles, which the tools that probe it read too |
| `hyperion/` | `Hyperion.exe`'s start (`hyperion.cpp`), exports, icon and version (`docs/hyperion-exe.md`) |
| `re/globals117.json` | every named global's original address, type and translation unit, which the build binds the globals with |
| `locale/` | the translation catalogs (`../docs/translating.md`) |
| `huds/`, `mods/` | the built-in HUD designs (`docs/hud-format.md`) and the mods shipped with the program |
| `docs/modules/` | the engine's areas, one doc each: start with `overview.md`, which maps every file in `src117/port/` to its doc; `build.md` for building and the gates |
| `docs/` | `hyperion-exe.md`, the program and every variable and option; `replay-test.md`, the gate; `hud-format.md`, the HUD design format; `maps.md`, the maps Hyperion plays |
| `tools/` | the build, the harnesses and the generators (below) |

### Tools

| Path | Role |
|---|---|
| `tools/build_engine_x64.py` | build `Hyperion.exe` and the x64 replay observer (VS 2026, x64) |
| `tools/build_engine_linux.py` | build the Linux x64 and arm64 programs, in Docker or on a Linux machine |
| `tools/build_engine_web.py` | build the browser program (Emscripten) |
| `tools/package.py` | package a build, the Mac bundle included |
| `tools/wave117/abs_globals.py`, `tools/coff.py` | bind the game's globals at link time to their places in the engine's block of classic's data |
| `tools/save_layouts.py`, `tools/decl_widths.py` | ask the compiler for each saved type's layout on both widths, and check declarations against their definitions |
| `tools/layouts117.py` | write `src117/gen/layouts117.cpp` from the game's dialog files, and check it back against them |
| `tools/replaytest/` | play replays on the original and on Hyperion and compare every frame, with saves and loads (`docs/replay-test.md`); `compare.py` shows a replay's comparison to read, in the terminal and as a page |
| `tools/porttest/`, `tools/build_porttest.bat` | run our bodies against the original's code, read from the exe (or, for Storm's own functions, the install's `storm.dll`) at run time, over every input that matters; 32-bit, as the original is |
| `tools/play.py` | play Hyperion from `testrun/play/`, a game folder of its own that it makes on the first run and refreshes with the latest build on every run (`docs/hyperion-exe.md`, "Running it") |
| `tools/fetch_sdl3.py`, `tools/build_sdl3.py` | fetch SDL3 into the gitignored `third_party/`, then build its x64 DLL from the same version's source with the port's patches (`tools/sdl3-patches/`) |
| `tools/fetch_stormlib.py`, `tools/fetch_casclib.py` | fetch StormLib and CascLib into `third_party/`: StormLib's pklib is PKWARE's implode and explode, and the rest reads the MPQs |
| `tools/fetch_dxc.py`, `tools/fetch_spirv_cross.py` | fetch Microsoft's DirectX Shader Compiler, which builds the GPU presenter's shaders to DXIL and SPIR-V, and Khronos' SPIRV-Cross, which `tools/shader_msl.py` runs to turn each shader's SPIR-V into Metal Shading Language for the Mac |
| `tools/build_stormlib.bat` | build 32-bit StormLib, all but its pklib, its names given an `Sl` prefix (`src117/port/data/port_stormlib_names.h`), for `build_porttest.bat`; `build_engine_x64.py` builds the x64 one |
| `tools/comment_check.py`, `tools/comment_lint.py` | prove a change to `src117`'s C++ touched comments only, and check the comments' style |

## Building

```
python engine/tools/build_engine_x64.py
```

It needs Visual Studio 2026 with the x64 tools at the default path, and SDL3, StormLib,
CascLib, dxc and SPIRV-Cross in `third_party/` (`python tools/fetch_sdl3.py` then
`python tools/build_sdl3.py`, `python tools/fetch_stormlib.py`,
`python tools/fetch_casclib.py`, `python tools/fetch_dxc.py`,
`python tools/fetch_spirv_cross.py`; the full list is in `docs/modules/build.md`);
`--no-stormlib` keeps the StormLib already built. Output lands in `src117/build/x64/`
(gitignored). To play, put `Hyperion.exe` and the x64 `SDL3.dll` in a classic game folder
and run `Hyperion.exe`, or run `python tools/play.py`, which does that in `testrun/play/`.

After a change to a global's type or a saved type, run `tools/save_layouts.py`, which
rewrites `src117/gen/save117.h`, and build again.

## Replay test

`tools/replaytest/replaytest.py` resolves its paths from `engine/`. It needs a copy of the
original exe at `engine/oracle/StarCraft.exe` and a game folder at `engine/testrun/` (data
files and `storm.dll`, see `docs/replay-test.md`); both stay out of git. It runs the
original beside any other copy of the game, and the `hx64` build from `testrun/hx64/`, laid
out as a player's install.
