# Building Hyperion

Hyperion builds on Windows first. The Linux, Mac and browser builds use two things the Windows
build makes: the GPU shaders, compiled with Microsoft's shader compiler, and SDL3's patched
source. So build for Windows once before the others.

## Windows

You need:

- Windows 10 or 11, 64-bit
- [Visual Studio 2026](https://visualstudio.microsoft.com/) with the "Desktop development with
  C++" workload
- Python 3 (3.13 is what the builds are made with) and Git
- an internet connection for the first build, which downloads the libraries

Keep the checkout at a short path such as `C:\hyperion`. Some libraries' CMake builds fail
under a long one.

    git clone https://github.com/Mad-Man-Dan/hyperion-public C:\hyperion
    cd C:\hyperion\engine
    python tools/get_libraries.py
    python tools/build_engine_x64.py --no-install

`get_libraries.py` downloads each library at the version the build expects (`THIRD-PARTY.md`)
into `engine/third_party/` and builds SDL3. Run it again when a library's version changes. The
build writes `engine/src117/build/x64/Hyperion.exe` with `SDL3.dll` beside it.
`--no-install` leaves the build where it is; without it, the build also copies itself into the
folders a file `engine/install_to.txt` names, one a line, if you make one.

To make the zip a player unpacks: `python tools/package.py`. It goes into
`engine/testrun/share/`.

## Linux

The Linux build runs on Windows in Docker, or on a Linux machine with clang, lld, CMake and
Ninja and the packages `engine/tools/linux/Dockerfile` lists. Build for Windows first (above),
then from `engine/`:

    python tools/build_engine_linux.py                 # x64
    python tools/build_engine_linux.py --arch arm64    # 64-bit ARM; under emulation on Windows, slowly

It writes `engine/src117/build/linux-x64/Hyperion.x86_64` (or `linux-arm64/Hyperion.aarch64`)
with `libSDL3.so.0` beside it. `python tools/package.py --linux x64` makes the `.tar.gz`.

## Mac (Apple silicon)

You need Xcode's command line tools (`xcode-select --install`) and CMake and Ninja
(`brew install cmake ninja`). The Mac builds from a copy of the tree with the Windows build's
shaders and the libraries' sources. From the Windows machine:

    python tools/mac_sync.py --host you@your-mac --third

Then on the Mac:

    cd ~/hyperion/src/engine
    python3 tools/build_engine_linux.py
    python3 tools/package.py --mac

The program is `src117/build/mac-arm64/Hyperion.arm64`. The package is `Hyperion.app` in a zip.
The release's own Mac package is built on GitHub's Mac runner
(`.github/workflows/mac-release.yml`).

## Browser

`python tools/build_engine_web.py`, in Docker with Emscripten, after `get_libraries.py` on
Windows (it builds SDL3 from the same patched source). A LAN room serves the result to players
who join from a browser.

## Running your build

Hyperion reads the game's files from your own StarCraft: StarCraft: Remastered from Battle.net,
or a classic Brood War install with its MPQ files. Start `Hyperion.exe`. It finds a Battle.net
install by itself, and otherwise asks where your files are. For one run you can say which:

    Hyperion.exe --install remastered
    Hyperion.exe --install-folder "D:\Games\StarCraft"

Hyperion keeps profiles, replays, saves, settings and logs in a `Data` folder beside the
program, or in the folder `--data <folder>` names. It never writes into the game's folder.
Every option and variable is in `engine/docs/hyperion-exe.md`.

## Tests

From `engine/`, after a build:

    python tools/run_tests.py

These tests need none of the game's files:

| Test | What it checks |
|---|---|
| `notes` | the release notes are well formed and name the build's version |
| `catalogs` | the translation catalogs match the sources and each other |
| `huds` | every built-in HUD design passes Hyperion's own check |
| `maps` | a map written from nothing passes the map check, and compiles to the same classic map twice |
| `mods` | every built-in mod's files read |

`--game <StarCraft folder>` adds the full check of each built-in mod against your game's files.
`--only huds,maps` runs some of them.

The replay comparisons that keep the game playing exactly as the original need the game's
files and a collection of replays, so they run before each release rather than here
(`engine/docs/replay-test.md`).

## When something fails

- **CMake "is not able to compile a simple test program"**: the checkout's path is too long.
  Move it to a short one such as `C:\hyperion`.
- **"no ... run python tools/fetch_...py"**: a library is missing. Run
  `python tools/get_libraries.py` again.
- **The Linux build says there are no shaders**: build for Windows first.
- **Windows Defender quarantines `Hyperion.exe`**: unsigned new programs are sometimes flagged.
  Exclude your build folder.
