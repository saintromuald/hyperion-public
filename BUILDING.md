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
`engine/testrun/share/`. For the public Hyperion Shield and private Hyperion Zero variants, see
`docs/release-variants.md`.

## Linux

The Linux build runs on Windows in Docker, or on a Linux machine with clang, lld, CMake and
Ninja and the packages `engine/tools/linux/Dockerfile` lists. Build for Windows first (above),
then from `engine/`:

    python tools/build_engine_linux.py                 # x64
    python tools/build_engine_linux.py --arch arm64    # 64-bit ARM; under emulation on Windows, slowly

It writes `engine/src117/build/linux-x64/Hyperion.x86_64` (or `linux-arm64/Hyperion.aarch64`)
with `libSDL3.so.0` beside it. `python tools/package.py --linux x64` makes the `.tar.gz`.

### On Linux without a Windows build

This fork can build for Linux with no Windows machine:

1. `python tools/get_libraries.py` (skip `fetch_spirv_cross.py` if it fails: its MSVC
   build is only for the Mac's MSL shaders).
2. The SDL3 source with the port's patches, which the Windows build would make:

       python -c "import sys; sys.path.insert(0, 'tools'); import build_sdl3; \
                  build_sdl3.fetch_source(); build_sdl3.apply_patches()"

3. The shaders, with dxc under Wine (needs wine installed):

       python tools/build_shaders_linux.py

4. Then `python tools/build_engine_linux.py` as above.

### This fork's change: Remastered's HD

Upstream holds Remastered's paid content off (`PortRemasteredOwned` returns false in
`src117/port/data/port_casc.cpp`), so its builds draw the classic art. This fork returns
true: running on the player's own StarCraft: Remastered install, the HD art, HD console,
portraits and Remastered's recordings are read, and Settings' Art (or F5) switches HD.

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

## The online match's relay netcode

The SB provider (the "Online (rally point)" network) links a Rust static library built from
the vendored `engine/tools/rallypoint2/` (`engine/tools/rallypoint2/PINNED.md` — upstream
ShieldBattery/rally-point2, MIT/Apache-2.0). The build needs a Rust toolchain:

- Rust 1.95 or newer (`rustup`), on Windows (MSVC) or Linux;
- `cargo` on the PATH. On Windows, the engine build runs it directly; on Linux,
  `build_engine_linux.py` does. `--no-sb` skips the crate and stubs the provider.

The engine build also produces the two server binaries from the same workspace, into
`engine/src117/build/<target>/rallypoint/release/`:

- `rally-point-relay` — the game relay each session dials (QUIC);
- `rally-point-coordinator` — the control plane that places sessions on relays and mints
  the per-seat launch JSONs;
- `rp_dev_setup` — a helper for certificates, tenants and the dev coordinator's sessions.

### Running the stack yourself

For development and a LAN party's server, the dev-tenant shape (see
`engine/docs/sb-netcode-port.md` §"Server bring-up" and the scripts under
`engine/tools/sbtest/`):

1. Certificates and a tenant: `rp_dev_setup gen-cert`, `rp_dev_setup gen-tenant`.
2. The coordinator, with the tenant's keys (a dev tenant accepts any relay's enrollment):

       rally-point-coordinator --listen 0.0.0.0:14910 \
           --dev-tenant --tenant my-tenant --kid dev-key-1 \
           --allow-insecure-control --tenant-key <pkcs8> --dev-tenant-client-key <seed>

3. One relay per region, enrolled on it:

       rally-point-relay --listen 0.0.0.0:15000 --relay-id 1 \
           --coordinator-url http://<host>:14910 --cert cert.pem --key key.pem

4. The room (the lobby's host) mints each match's sessions itself, with these settings in
   its `lanroom.json`: `sbCoordinator` (the URL), `sbTenant`, `sbKid`, `sbClientSeed` (the
   app-server seed), and `sbServerName` (the relay cert's name). A match with
   `"transport":"sb"` in its JSON plays online.

The room's legacy development minter is disabled by default. Only local development harnesses
should set `HYPERION_SB_DEV_MINT=1`; production matches use authenticated ShieldBattery
formations and canonical manifests, never `rp_dev_mint`.

Headless two-seat gates, all `--coordinated` (see `docs/sb-netcode-port.md` §10):
`engine/tools/sbtest/{run_game,desync,reconnect,drop,sb_room}.sh`, plus the bridge's C ABI
test `engine/tools/rallypoint2/bridge/ctest/run.sh` and the command-table conformance check
`cmdtable.sh`. The canonical Zero gate is
`python3 tools/sbtest/zero_acceptance.py --steps 600 --timeout 90`; add `--result` for raw v2
reports and clean departures, or `--drop` for hard-kill eviction and survivor evidence. Use
`--rule 2` for the anti-meta start and `--team-size 2` or `--team-size 3` for 2v2/3v3. These
use only loopback development services and dummy result credentials.

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
