# Building, testing and shipping Hyperion

## Overview

One source tree builds Hyperion for three targets: Windows x64 (the main one), Linux x64
and arm64, and the browser as WebAssembly. Every target compiles the same translation units,
`src117/tus.txt`: the game's C++ in `src117/lang/` and the port's in `src117/port/`, whose
files sit in a folder an area; `tools/port_files.py` finds a port file by its name for the
builds and the tools that read the sources. The Windows build talks to Win32 directly. The
others get Win32's calls from `src117/port/platform/port_posix.cpp`, declared by
`src117/compat/`, which only those builds put on the include path. A build holds 1.17's starting values as source (`src117/gen/data117.cpp`,
the user's decision, PRD 2026-10-19), and no other Blizzard data: the program reads the
player's MPQs and `Local.dll` at start-up, and no 1.17 exe (`overview.md`, `platform.md`).

## Status

All three targets build on master. Windows is played and gated every commit. Linux x64 and
arm64 are built before every merge and run in WSL or Docker. The browser build runs in the
LAN room's join page. The Mac's (arm64, macOS 13 on) builds on the Mac itself, from the tree
`tools/mac_sync.py` sends it, plays the quick suite identically, and is packaged
there as `Hyperion.app` (`tools/package.py --mac`).

## Targets

| Target | Command (from `engine/`) | Toolchain | Output |
|---|---|---|---|
| Windows x64 | `python tools/build_engine_x64.py` | VS 2026 x64 (`vcvarsall.bat` under `C:\Program Files\Microsoft Visual Studio\18\Community`) | `src117/build/x64/Hyperion.exe`, `Hyperion.pdb`, `rt_observer.dll` |
| Linux x64 | `python tools/build_engine_linux.py` | clang, lld, CMake, Ninja; on Windows in the `hyperion-linux` Docker image (`tools/linux/Dockerfile`) | `src117/build/linux-x64/Hyperion.x86_64`, `libSDL3.so.0`, `rt_observer.so` |
| Linux arm64 | `python tools/build_engine_linux.py --arch arm64` | as above, in `hyperion-linux-arm64` under emulation (slow) | `src117/build/linux-arm64/Hyperion.aarch64` |
| Browser | `python tools/build_engine_web.py` | Emscripten in its own image (`emscripten/emsdk`) | `src117/build/web/hyperion.js`, `hyperion.wasm`, loaded by `engine/web/play/` |

All outputs are gitignored. Builds take a few minutes each. The Linux and web builds compile
SDL3, StormLib and CascLib from source once (`--sdl`, `--libs` rebuild them).

The browser build has no threads, because a page served over the LAN's plain http gets
none. `port_posix.cpp` runs Hyperion's threads as fibers, and the game loop yields to the
browser where it would sleep, both through Asyncify.

## Third-party code

Fetched into the gitignored `third_party/` by scripts in `tools/`:

| Library | Fetch | Use |
|---|---|---|
| SDL3 3.4.16 | `fetch_sdl3.py`, then `build_sdl3.py` | window, input, audio, the GPU API. The DLL is built from source with the patches in `tools/sdl3-patches/` (one today: D3D12 descriptor tables sized before writing, the crash of 2026-10-15). |
| StormLib 9.40 | `fetch_stormlib.py` | MPQ reading, PKWARE implode/explode |
| CascLib 3.0 | `fetch_casclib.py` | StarCraft: Remastered's CASC storage, for HD art |
| DXC | `fetch_dxc.py` | compiles the GPU presenter's shaders to DXIL and SPIR-V |
| SPIRV-Cross (vulkan-sdk-1.4.363.0) | `fetch_spirv_cross.py`, which also builds its command line with cl | turns each shader's SPIR-V into Metal Shading Language for the Mac (`tools/shader_msl.py`) |
| Clay 0.14 | `fetch_clay.py` | the front end's layout |
| stb | `fetch_stb.py` | `stb_truetype` for the front end's text, `stb_vorbis` for the videos' sound and a mod's Ogg Vorbis, `stb_image` for a mod's PNG and TGA art. `tools/stb-patches/` holds the build's fixes to them, applied after the download by `third_party_patches.py`; a build stops on a copy without them |
| dr_libs (commit `dfe8377`: dr_mp3 0.7.4, dr_flac 0.13.4, dr_wav 0.14.6) | `fetch_drlibs.py`, each file checked against its SHA-256 | a mod's MP3, FLAC and WAV sounds and music (`port_audiodecode.cpp`); public domain or MIT-0 |
| libvpx 1.17.0, with libwebm | `fetch_libvpx.py` | Remastered's WebM videos (`data.md`, Video): the VP8 and VP9 decoders in plain C, configured once into the committed `tools/libvpx/` (its `sources.txt` lists the files), and the WebM parser libvpx carries; compiled by the build scripts into `webm.lib` / `libwebm.a`. Not in the browser build |
| dav1d 1.5.4 | `fetch_dav1d.py`, checked against its SHA-256 | a mod's AV1 videos (`data.md`, Video): plain C, no assembly, configured by hand in the committed `tools/dav1d/` (`config.h`), its bit-depth files compiled once for 8 bits and once for 16; into `dav1d.lib` / `libdav1d.a`. BSD. Not in the browser build |
| libwebp 1.5.0 | `fetch_libwebp.py` | a mod's WebP art (`data.md`, mod data): the decoder alone, into `image.lib` / `libimage.a` with `stb_image` |
| none: the system's | | H.264, H.265 and AAC (`data.md`, Video), with no code of theirs in Hyperion: Media Foundation on Windows (`mfplat.dll` loaded at run time, `mfuuid.lib` linked for its names), VideoToolbox, CoreMedia, CoreVideo and AudioToolbox on a Mac (linked), the player's FFmpeg on Linux (`libavcodec`, loaded at run time) |
| Fonts | `fetch_fonts.py` | Saira, Michroma, IBM Plex Mono and Chakra Petch's four cuts, and Noto Sans and Noto Sans JP, KR, SC and TC for the letters those lack (all OFL), linked into the exe as resources; Chakra Petch is the HUD tooltips' (`hud.md`). The Noto faces are cut to their regular weight and to each language's common characters with every character the catalogs use (`fetch_fonts.cut()`, which each build runs and which cuts again when the catalogs need a character the cuts lack; about 6 MB in all). Every package carries the faces' licenses in `docs/licenses/fonts.txt` (`package.py`) |
| WebRTC stack | `fetch_webrtc.py`, `build_webrtc.py` | libdatachannel, OpenH264, Opus, for the LAN room's streamed seats (`lan.md`) |

## Generated files

Some sources are generated and committed. Rerun the generator when its input changes:

| File | Generator | When |
|---|---|---|
| `src117/gen/globals117.h` | `tools/regen_globals.py` | the game's globals change |
| `src117/gen/wide117.h`, `wide117.json` | `tools/wide_globals.py` | a global's type changes |
| `src117/gen/save117.h` | `tools/save_layouts.py` | a saved type's layout changes |
| `src117/gen/code_map.cpp` | `tools/code_map.py` lists the missing rows | a function 1.17's data points at gets a body |
| `src117/gen/data117.cpp` | `tools/data117_source.py`, from the oracle exe | the engine names another of 1.17's globals |
| `src117/gen/layouts117.cpp` | `tools/layouts117.py`, from 1.17's MPQs | never for 1.17's layouts; a layout of Hyperion's own is edited in the table (36) |
| `src117/gen/eud_code116.inc` | `tools/eud_code116.py --exe116 <1.16.1 StarCraft.exe>`, from the 1.16.1 exe and the oracle | a function the cards or unit panels point at gets a body |
| `src117/gen/cp949.inc` | `tools/gen_cp949.py`, from Python's cp949 codec | never; Korean CP949 as Unicode (map text) |
| `release_notes.h` (in the build folder) | `tools/release_notes.py header`, run by the build | every build |
| `locale_catalogs.h` (in the build folder) | `tools/locale_catalog.py header`, run by the build, from `engine/locale/*.po` | every build; `locale_catalog.py extract` after Hyperion's text changes (`front-end.md`, "Text in the player's language") |

## Folders a build is run from

| Folder | Holds | Made by |
|---|---|---|
| `engine/testrun/hx64/` | the replay harness's game folder: the data and the x64 build, no 1.17 exe | `replaytest.py setup` (copies the build on each run) |
| `engine/testrun/hxr/`, `lxr/` | the same with none of 1.17's files, for the runs on StarCraft: Remastered's install alone; earlier runs' file logs in `old_logs/` | `replaytest.py` on each run |
| `engine/testrun/lx64/`, `la64/`, `ma64/` | the same for the Linux builds and, on a Mac, the Mac's | `replaytest.py setup` |
| `engine/testrun/play/` | the user's play folder | `tools/play.py`, which copies the latest build in on every run. Test runs never touch it. |
| `engine/testrun/bench/` | `results.jsonl`, `builds/<name>/` (builds kept for comparing), `profiles/`, `presentmon/` | `hxbench.py` and by hand |
| `engine/testrun/rt_runs/<stamp>/` | each harness run's logs, `summary.json` (each verdict, the commit, the files and the install's build) and `report.html`; a gate's runs in `gate_<stamp>/` | `replaytest.py` |
| `engine/testrun/rt_runs/compare_<stamp>/` | a comparison's logs, `snapshots.csv` per replay, `compare.html` and `report.html` | `replaytest/compare.py` |
| `engine/oracle/StarCraft.exe` | the 1.17 exe the harness runs as the reference, and `tools/data117_source.py` and `tools/code_map.py` read | by hand, never committed |

Nothing in `testrun/` is deleted. Each run makes a new folder.

## The gates

`python tools/replaytest/replaytest.py gate` runs them, each on both kinds of game files,
and prints one line for the commit message; it exits 1 when any run fails.

| When | Run |
|---|---|
| Every commit | `replaytest.py gate`: the quick suite, four replays (`corpus.json`'s quick set), oracle against Hyperion on every frame, Hyperion on StarCraft: Remastered's install alone (`hxr`) and on 1.17's MPQs (`hx64`) |
| A commit touching the simulation or the state hash | `gate --hx`: hxsuite too, Hyperion's own replays against their recorded hashes and kept references, on both kinds of files |
| A commit touching drawing | also the suite with `--renderer vulkan --scene screen --gpu-check`: every GPU frame compared with the game's own, a frame that differs failing the run. The build itself refuses a shader uniform block past 4 KB or laid out differently for SPIR-V than for Direct3D (`tools/shader_layout.py`); HD art is not in the check, so a change to it is also looked at in a game under Vulkan |
| A commit touching Storm, the file layer or the formats | also the port tests, `tools/build_porttest.bat` and `build_porttest_x64.bat`, then `storm_test.exe testrun\storm.dll` and the others the header of the `.bat` lists: each runs the port's code against the original's. On 2026-10-22 all twelve, ten x86 and two x64, gave no difference (`rt_runs/porttest_20261022`); storm_test and smk_test link again with the stubs in `tools/porttest/` (SDL's header, the Remastered archive, the presenter's palette, the WebM player) |
| A commit that changes comments only | first `python tools/comment_check.py`: the code with the comments stripped is unchanged, and every `/*0xNN*/` offset and `Hyperion` marker is kept; `tools/comment_lint.py` checks the comments against the rules (`overview.md`, Conventions) |
| A merge to master, or a major update | `gate --merge`: the full set, the archive set (the rest of the SSCAIT 2016 archive), hxsuite, all on both kinds of files, and the Linux x64 (both) and arm64 (the MPQs) builds' quick suites; the user's word to merge |

A run passes when it read the files it was given (its `hyperion_init.log`; on Remastered's
install alone, no file from an MPQ and no name refused, from its file logs), played the
replay to its end, and matched the reference on every frame the reference has: through the
frame where 1.17 itself loses recorded commands where it does (s1 to s3), and not at all
where `corpus.json` records that the oracle cannot play the replay. A reference that stops
early fails the run unless `corpus.json` names that end. The oracle always reads the MPQs,
so both kinds of files are held to the same oracle run. The harness is documented in
`../replay-test.md`, and summarised in `replays.md`.

The harness keeps its windows on the right-hand screen (`HYPERION_DISPLAY=right` unless set
otherwise). Do not build while a suite or a benchmark is running: the build replaces the exe
they copy.

## Versions, release notes and packages

- The version is in `engine/hyperion/version.h` (`HYPERION_VERSION_*`, the text, and the
  file version spelled out).
- `engine/notes/release-notes.json` is the log of public releases, newest first, each entry
  agreed with the user when the release goes out; nothing is added at a merge
  (`../../../docs/versioning.md`). `tools/release_notes.py check` confirms the newest release
  is not past `version.h`. `release_notes.py md` writes
  `docs/release-notes.md`. Insert entries as text: rewriting the whole JSON reformats every
  older entry. Player-facing wording only, with no take numbers or internal names.
- `tools/package.py` zips a build for players (`--linux x64|arm64` for a tarball, `--mac` on
  a Mac for `Hyperion.app` in a zip, signed ad hoc) into a new folder under `testrun/share/`.
  A package carries no Blizzard files and goes in a folder of its own. Beside the program it
  puts only the SDL library, README.txt and `docs`; the mods, tools, maps, `web` and `notes` go
  in `Data\`, the default data folder, where the program finds them (`PortPackagedFile`,
  platform.md). The mods for Hyperion's own testing, Hybrid Test and the AI Lab, are in no
  package or install (`UNSHIPPED_MODS`, the user's decision of 2026-10-27), nor are its test
  maps (`UNSHIPPED_MAPS`).
- `tools/package.py --install <folder>` puts the Windows zip's files in a folder as unzipping
  would, writing only what differs and touching nothing else there, the player's own `Data\`
  included; a running `Hyperion.exe` is renamed `Hyperion.exe.old` and goes on, and the next
  install removes it. Each Windows build that succeeds installs so into the folders
  `engine/install_to.txt` names (one a line, not committed; on the user's machine
  `E:\Hyperion`, the user's request of 2026-10-21), unless built with `--no-install`.
- `tools/mac_sync.py --host user@mac` sends the engine's tree to a Mac to build and package:
  what git tracks, the shaders and the browser program the Windows builds wrote, the package's
  documents and maps, and `build_origin.json` with the commit, branch and build number, which
  `build_engine_linux.py` reads where there is no git. `--third <folder>` adds a third-party
  source a new library needs.
- The full process is in `../../../docs/versioning.md`.

## Gotchas

- Repo files are CRLF (`.gitattributes`). Scripts that write files must write CRLF, and
  Git Bash's `sed -i` silently turns a file LF.
- Windows Defender once quarantined a test copy of `Hyperion.exe`. The test folders are
  excluded. A build that "vanishes" is probably the same thing.
- From Git Bash, run `.bat` builds through PowerShell: `cmd //c` mangles paths with spaces.
- The x64 build and the Linux build must agree on layouts. Both use the x64 binding of the
  globals, and the replay suite is what proves they match.
- `rt_observer.dll` is built with Hyperion and loaded only through `HYPERION_DLLS`. A
  player's run loads no DLLs.
- The x64 build compiles the port's files with `/EHsc`, so the standard library's exceptions
  unwind, and the game's `lang/` files with the original's flags, which leave unwinding out. The
  Linux build has exceptions on by default. The browser build leaves them at Emscripten's
  default, where a throw ends the program.

## Further reading

- `../../README.md`: the engine tree's layout and rules.
