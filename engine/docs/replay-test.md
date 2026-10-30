# Replay test

The replay test plays a Brood War replay on the untouched 1.17 oracle and on
Hyperion, at full speed and without a person at the keyboard. It logs the game state
every simulated frame and reports the first frame, if any, where Hyperion's game
differs from the oracle's.

Where the game's C++ was first proved function by function against the oracle, this test
checks the assembled result end to end, running whole games. The build under test is the
x64 `Hyperion.exe`, the build `hx64`.

## Pieces

| File | Role |
|---|---|
| `tools/replaytest/rt_observer.cpp` | the observer DLL, in every run: injected into the oracle, loaded by `Hyperion.exe` (`HYPERION_DLLS`); it holds no game code |
| `tools/replaytest/rt_launcher.cpp` | starts the oracle suspended, injects the observer, waits for exit |
| `tools/replaytest/replaytest.py` | sets up the game folder, runs builds and replays, writes the report |
| `tools/replaytest/compare.py` | plays any replays on the oracle and on Hyperion and shows, for a person to read, that the two games are the same: snapshots with their hashes and each player's numbers, the score screen's numbers at the end and the game's result ("Comparing any replay") |
| `tools/replaytest/repfile.py` | reads a replay's command stream (with its own PKWARE explode) and lists the frames the original loses ("The restart in M1G2-Leta"); its header's map and players and its players' quits |
| `tools/replaytest/resave.py` | copies a replay with its map saved as StarCraft: Remastered saves it (version 64 or 206, STRx): Hyperion playing the copy has to match the original playing the replay unchanged |
| `tools/replaytest/corpus.json` | the classic replays the suite plays: each file's sha1, where it came from, its sets and any end the oracle is known to come to |
| `tools/replaytest/loadmatrix.py`, `tools/porttest/savecompare.py` | save a replay's game on each build, load every save on every build and play on ("Saved games"); compare the saves |
| `tools/build_replaytest.bat` | builds the oracle's 32-bit `src117\build\rt_observer.dll` and `rt_launcher.exe`; `tools/build_engine_x64.py` builds the x64 observer beside `Hyperion.exe` |

## Running it

```powershell
cmd /c "tools\build_replaytest.bat"
python tools/build_engine_x64.py
python tools/replaytest/replaytest.py setup
python tools/replaytest/replaytest.py gate             # every commit: the quick suite
python tools/replaytest/replaytest.py gate --hx        # and hxsuite
python tools/replaytest/replaytest.py gate --merge     # a merge: full, archive, hxsuite, Linux
python tools/replaytest/replaytest.py suite --quick    # the quick set alone
python tools/replaytest/replaytest.py corpus           # every replay corpus.json names, here
```

The replays are named in `tools/replaytest/corpus.json`, each with its file's sha1, where
it came from and the sets it is in; the files themselves are kept in
`testrun\rt_replays\` (gitignored). `corpus` checks that each is there and unchanged, and
names the source of one that is missing. The sets:

- `quick`, on every commit (`gate`, `suite --quick`): `m1` (a version-59 map played as a
  scenario), `m4` (8 players), `s2` (three computers, whose AI plays again in playback) and
  `s3` (the original loses commands);
- `full`, at a merge (`suite`): the 10 replays of `modes\` and `sscait_pick\`;
- `archive`, at a merge (`gate --merge`, `suite --set archive`): the other 30 replays of the
  SSCAIT 2016 archive (`sscait2016\`), the six the full set already has left out.

A row's `known` records an end the oracle is known to come to: `Ro8\M1G1-Leta` does not
load on 1.17, and `Ro8\M1G2-Leta` crashes it ("The restart in M1G2-Leta"). A reference
that ends that way does not fail the run, and a replay the oracle cannot load is not
compared. `--replays` takes other replays or folders.

Each replay is played on the oracle once and on Hyperion once for each kind of game files
`--data` names: by default StarCraft: Remastered's install alone and then 1.17's MPQs
(`remastered,mpq`), since new players are taken to have Remastered's files (PRD,
2026-10-21). A run on the install is tagged `hxr` and runs from `testrun\hxr\`, which holds
`Hyperion.exe` and SDL3 and none of 1.17's files; a run on the MPQs is `hx64`, from
`testrun\hx64\`. The oracle reads only MPQs, so both are held to the same oracle run, and a
difference between them is a difference in the files. `both` (Remastered's searched before
the MPQs) is there for development. The harness names the install it reads, `--remastered`,
else `HYPERION_REMASTERED`, else the folder Battle.net's uninstall entry gives, and hands it
to the engine as `HYPERION_REMASTERED`, so the settings cannot point a run elsewhere. Each
engine run must show it read what it was given: its `hyperion_init.log` names the files,
and on the install `HYPERION_FILE_LOG` writes `hyperion_files.log` (every name and where it
came from) and `hyperion_casc.log` (every name asked of the install), which the run keeps as
`hxr_files.log` and `hxr_casc.log`; a file from an MPQ, or a name the install refused with
no SD file read in its place, fails the run. The lookup tries the install's top folders after
`SD\`, so a unit's portrait is refused as Remastered's HD `.smk` on the way to the SD
`.webm`, which passes. A name whose SD form was looked up and is not in the install is listed
with the names found nowhere, marked "HD only", and passes: the Ultralisk's fourth fidget
(`portrait\ZUltra\ZUlFid03`) is in the HD files alone, and 1.17's fidget chooser then plays
the third (`sub_4A6B40`), as it does for any variant that does not open. A run's `summary.json` holds each verdict and what the run ran on: the commit and
whether the engine's files differed from it, the install and its build (`.build.info`),
and the sha1 of each MPQ and DLL the oracle read. When Battle.net has updated the install
since the last run, the harness says so.

A suite exits 1 when any run fails. A run passes when it read its files, played the replay
to its end, and matched the reference on every frame the reference has (through the first
frame where 1.17 loses commands, where it does); a reference that ended early fails it too,
unless `corpus.json` names that end. A run that wrote no frames at all, as when the engine
did not start, fails with its launcher's exit code. Each of these was made to happen on
2026-10-22 and failed as it should: a run cut at frame 1,000, an install folder that is no
install, a `patch_rt.mpq` beside a Remastered run (before `open_archives` stopped opening it)
and an hxsuite entry with no replay.

The Linux build is the build `lx64`: `python tools/build_engine_linux.py`, then
`suite --builds oracle,lx64` (with `--quick` or not). On Windows it runs in WSL, in the
distribution `HX_WSL_DISTRO` names (`Ubuntu` unless set); on a Linux machine it runs
there, with the oracle's kept runs (`testrun\rt_cache\oracle\`) copied from a Windows
machine, as the oracle itself only runs on Windows. Its folder is `testrun\lx64\`, laid
out as `hx64`'s, with the Linux `Hyperion.x86_64`, `libSDL3.so.0` and the observer as
`rt_observer.so`, copied in when the build changes. There is no launcher: the harness runs
the program under `timeout`, with the observer's settings in its environment and every
path as the program sees it (`/mnt/d/...` in WSL). On Remastered's files its run is `lxr`,
from `testrun\lxr\` with no MPQ, the install handed over by its `/mnt/...` path; the quick
suite on both kinds of files was identical on 2026-10-22 (`rt_runs/lx_both_20261022`).

The ARM64 build is `la64` (`build_engine_linux.py --arch arm64`, `testrun\la64\`,
`Hyperion.aarch64`). On Windows it runs in the `hyperion-linux-arm64` Docker image, which
Docker runs under emulation, with the repository mounted at `/src`, SDL's offscreen video
and software renderer and no sound: about 20 times slower than x64, so give it
`--timeout 3000 --menu-timeout 900 --stall-secs 300` (its main menu alone takes half a
minute).

The Mac's build is `ma64` (`build_engine_linux.py` run on the Mac, `testrun/ma64/`,
`Hyperion.arm64`, `libSDL3.0.dylib`, `rt_observer.dylib`). It runs only on an Apple-silicon
Mac, where it is, with the repository's `engine/` copied there and the oracle's kept runs
with it; macOS has no `timeout`, so Python ends a run at its limit. Over SSH the window is
SDL's Cocoa one, in the background, as the logged-in user's session has the window server
(SDL's offscreen video wants EGL, which macOS does not have).

The oracle plays a replay the same way every time: three runs of each of the 10 gave the
same frame and player lines. So its runs are kept in `testrun\rt_cache\oracle\`, one per
replay, exe, observer, game arguments, run settings and the MPQs and DLLs it reads (their
sha1s, kept by size and time in `rt_cache\file_sha1.json`), and a suite copies a kept run
instead of playing it again, its snapshots included. Only a run that reached the replay's
end is kept. The observer counts by its bytes less the time stamps and checksum a relink
changes, so rebuilding it from the same source keeps the cache; a change to its code
starts a new one.
`--no-cache` runs it anyway; deleting the folder empties the cache.

Hyperion's own replays, of games 1.17 cannot play (a map past 256 tiles a side, players
past 7; `hxRS`), have no oracle run to be held to. They hold themselves instead:
a Hyperion game hashes its state every 24 frames as it is played and its replay carries
those hashes (`SYNC`), which the replay checks as it plays back
(`src117/port/loop/port_synchash.cpp`, `port_hxreplay.cpp`). So the reference is the game the
replay recorded, not another run of the replay.

The set is made, not collected. `tools/replaytest/hxsuite.json` lists its games, each a
map of `testrun\play\maps\Hyperion\`, a race for the local player, a seed and a length
in frames. `hxrecord` plays each with Hyperion's `--record` and `--seed` (the local
player idle, or giving orders every `orders` frames with `--record-orders`, the other
slots computers), puts its replay in `testrun\rt_replays\hyperion\`
and keeps the live game's log in `testrun\rt_cache\hx_ref\` as its reference
(`hxref.json` names the commit, "live" before it). A seed gives the same game each time,
the replay byte for byte, so the set can be made again by any build of the same
simulation revision (`src117/h/hxsim.h`); a build of another refuses the old replays,
and the set is recorded again. The set is in `testrun\`, which every branch shares: after
going to a branch of another revision, `hxrecord` comes before `hxsuite`, or every replay
is refused ("another revision of Hyperion's simulation").

`hxsuite` plays each game of `hxsuite.json` on `hx64`, on each kind of files `--data`
names as the suite does (`hxr` and `hx64`, both held to the one reference), and fails it
when:

- the manifest names a game with no replay (`hxrecord --only <name>` records it); a replay
  in the folder the manifest does not name is reported and not run;
- the replay has no kept reference (`--keep-ref` keeps the first run's);
- the run read other files than it was given;
- the replay is refused (other rules, data or revision);
- one of its checks differs from its game, or a check is left unmade;
- its log differs from the live game's (the frame lines, the player and score lines, and
  the `H` lines of the state's hash, logged every 24 frames).

`hxrecord` plays each game once, on the first kind of files `--data` names (Remastered's
install by default), and its reference names the commit with `+dirty` when the engine's
files differed from it.

```powershell
python tools/replaytest/replaytest.py hxrecord                    # the set from hxsuite.json, with this build
python tools/replaytest/replaytest.py hxrecord --only h16_bgh2x   # one game of it
python tools/replaytest/replaytest.py hxsuite                     # the set against its games
```

An entry with a `checkpoint` is saved at that frame as it is recorded (the observer's
`save_frame`), and the save is kept beside its replay as `<name>.sav`, the frame in
`hxref.json`. `hxsuite` then also loads the save on `hx64` to the replay's last frame, as
`load` in the replay's run folder, and fails it unless every `H` line after the frame
saved, and every frame, player and score line, is the live game's. The live game's hash at
the frame saved itself is taken before the save changes the game, so it is left out. A
replay of a game that saved makes the same saves as it plays, so its
checks hold too. Sixteen entries have one now; the first nine were `h16_bgh2x_save`, `h16_goa2x_save` and
`h16_bghxl_save`, the first three games saved at frame 3,000, on maps of 256, 384 and 512
tiles a side, `h16_xl1800_full`, saved at 4,500 past 2,047 units,
`h16_xl1800_orders`, saved at 4,000 with its local player's selection, and the 32-player
`h32_bgh3x3_save` and `h32_bgh3x3_orders`, saved at 3,000 and 4,000, and the 64-player
`h64_bgh4x4_save` and `h64_bgh4x4_orders`, saved at the same frames. An entry with
`orders` is loaded with `--record-orders` too, so that the game loaded gives the orders
the game saved went on to give.

The set (`hxsuite.json`, the list itself) is 30 games. Since this paragraph it gained
`h32_triggers`, `h4_lt_rules`, `h3_hybrid`, `h3_wide`, `h5_lt_hybrid_race`, `h6_lt_hybrid_computer`,
`h16_bgh2x_levels`, `h16_xl1800_queue`, `h2_wide_select`, `h4_lt_handicap`, `h4_lt_mutators`,
`h4_lt_mutators_later` and `h4_lt_rich`, and 23 entries now have a checkpoint.
`h3_wide` (2026-10-27) is `h3_hybrid`'s game with the wide-test mod, the
hybrid-test mod's rows behind rows of padding whose names sort first, which
`tools/replaytest/wide_test_mod.py` makes into `testrun/test_mods/wide-test/`, kept local
like the maps (a mod in `engine/mods/` ships to players); the harness takes an entry's mod
from there when `engine/mods/` has none: its techs and upgrade are 256 and on, its orders and weapons 260 and on, its
flingy 268, its sprite 1,100 and its images 2,100, so its research, upgrades and casts go
out as Research 0x97, Upgrade 0x98 and Action 0x96, where `h3_hybrid` sends 0x30, 0x32 and
0x15 at the same frames; its save at 1510 holds `HXWI` and `HXMT`. Its
scripts have 200 KiB of scripts no image runs before the hybrid's, so the Hybrid Reaver's
run past 64 KiB on the wide jumps.
`h4_lt_rich` (2026-10-25) is Lost Temple with rich bases past the naturals at a 75% chance
(`rich=naturals,chance=75`, `modules/gameplay.md`, "Rich bases"), on the seed and Protoss
computers of `h4_lt_mutators`: its replay's `RICH` holds the 15 resources that turned rich, its
save at 2510 their bytes and the loads carried, and it plays to 3900.
`h4_lt_mutators` and `h4_lt_mutators_later` (2026-10-24) are the ones recorded with
`HX_OPT_MUTATORS` (`modules/gameplay.md`, "Mutators"). `h4_lt_mutators_later` is Lost
Temple with no fog of war, build time ×0.5 and every tech and upgrade researched
(`fog=none,build=x0.5,tech=all`), on the same seed and computers as `h4_lt_mutators`; its save
at 2510 holds the halved tables and the open map. `h4_lt_mutators` is Lost Temple with the
resource and supply mutators at once, mineral fields at
half, geysers that never run dry, 1,000 / 500 and 8 workers to start, a cap of 300 and a supply
boost of 50%, from its entry's `"mutators"`, which becomes `--mutators`; saved at frame 2510 and
played to 3900. Its three computers are Protoss (`"computer_race"`), on seed 405: with Zerg the
replay drew other creep tiles at frame 2592, the tracker's bug on creep tiles (the C runtime's
rand in `cover_creep_tile_cb`), which a game with no creep cannot meet.
`h4_lt_handicap` (2026-10-23) is the one recorded with `HX_OPT_HANDICAP`
(`modules/gameplay.md`, "Handicaps"): Lost Temple with the local Terran at 50% and three computers
at 60, 80 and 100%, from its entry's `"handicaps": "50,60,80,100"`, which becomes `--handicaps`;
saved at frame 2510 and played to 3900.
`h2_wide_select` (2026-10-23) is the one recorded with `HX_OPT_SELECTION`, Hyperion's selection
(`modules/hud.md`), on the wide selection test map (`tools/hxmap/widemap.py`): the local player's
200 zerglings and its buildings selected together, up to 255 units sent in chunks of 0x8C (about
9,500 unit ids over 275 chunks), its three Gateways training together, and control group 1 made
of the selection and called back. Those rounds are the recorder's in such a game only
(`port_record.cpp`, `WideRound`), so the games recorded before and their saves give the orders
they gave.
`h16_xl1800_queue` (2026-10-22) is the one recorded with `HX_OPT_ORDER_QUEUE`, StarCraft:
Remastered's order fixes (`modules/replays.md`): its entry's `"queue": true` adds `,queue` to
`--record-orders`, so the local player's moves and right-clicks are queued (shift). The others
were recorded before the option and play without it, which keeps them as they were. Ten are of 16 players: one per remade map, one played to its
end, the first three again saved at a checkpoint (the `_save` entries, above), two
past 2,047 units, and `h16_bgh2x_levels` (computers of set levels): `h16_bgh2x` (Big Game Hunters 2x, 256 tiles), `h16_goa2x` (Garden of Aiur 2x, 384)
and `h16_bghxl` (Big Game Hunters XL, 512), 6,000 frames and 250 checks each, and
`h16_goa2x_end`, Garden of Aiur 2x again until the idle local player has lost (5,427
frames), through melee defeat and victory, whose triggers walk the foes and all players
groups. `h16_xl1800_full` is played on Big Game Hunters XL 1800, the XL map with 1,816
units placed (`tools/hxmap/remake.py`): a 16-player game's pools are twice 1.17's, with
3,400 units and wide unit ids, so the game starts past 1.17's 1,700, passes 2,047 and
reaches 2,222, and is saved at 4,500. `h16_xl1800_orders` is the same map with the
local player giving orders every 24 frames: its replay holds Select, its changes,
RightClick and Action in their wide forms (0x89, 0x8A, 0x8B, 0x94 and 0x95), with ids of
units past index 2,047. Each needs 4 to 8 seconds to record. Recorded twice, they were the same files byte for byte, and every check
of each matched on playback (`rt_runs\hx_20261011_161011`). Since hash version 3
(2026-10-11), the hashes of one game recorded by two builds that differ elsewhere are the
same at every check, so a rebuild does not need the set recorded again; a change to what a
Hyperion game does does, with the revision raised. Version 2 held that only by chance:
see the fifth finding below.
A change to how the hash is made does too: version 9 (2026-10-18) mixes the same
words over four chains, and the set was recorded again with it, every frame, player and score
line the same as version 8's; the version 8 set is kept in `testrun\hash_v8_refs\`.

Three are of 32 players (stage D), on Big Game Hunters tiled 3 by 3 (384 tiles a
side, 2,102 units placed): `h32_bgh3x3` for 6,000 frames, `h32_bgh3x3_save` saved at
3,000, and `h32_bgh3x3_orders` with the local player giving orders, saved at 4,000.
Their slots run to 32, past 1.17's DWORDs of players, and their unit table is 6,800.
Three are of 64 players, on Big Game Hunters tiled 4 by 4 (512 tiles a side, every
resource kept, 3,744 units placed): `h64_bgh4x4`, `h64_bgh4x4_save` and
`h64_bgh4x4_orders`, as the 32-player three. Their slots run to 64, which takes sets of
players of two words, and their unit table is 13,600; each records at about 600 frames
a second. Two more are of 64 players on Big Game Hunters tiled 8 by 8 (1,024 tiles a side,
32,768 pixels, 9,920 units placed): `h64_bgh8x8` for 6,000 frames and
`h64_bgh8x8_orders` with the local player giving orders, saved at 4,000. Their positions
pass 16 bits, and they record at about 210 frames a second.

A seventh finding came with them: 1.17's reader of a replay's commands (`net_record_read_turn`,
`net_record.cpp`) reads the last frame's block to the end of the buffer and then returns
as if there were none, so a replay never plays its last recorded frame of commands. A
game with orders records one every 24 frames to its end, and its playback went on
otherwise from the frame after the last. A Hyperion game's playback hands that block over;
1.17's replays keep 1.17's reader. (It had been read at first as the local player's loss,
which ended `h32_bgh3x3_orders` the frame after its last orders.)

An eighth came from a game played by hand: its replay failed its checks from the first
right-click on the ground, in the objects and sprites alone. Such a click moves the black
X (`show_target_x`, `AllocBlackX`), a thingy of the first pool that only the local
player's view uses, and a playback leaves it where the game began it. The hash leaves it
out from version 5, and the orders the harness gives include a move to the ground every
fourth round, shown as a player's click shows it (`port_record.cpp`), so the set keeps
such clicks. A replay recorded with an earlier version of the hash plays unchecked.

When a check differs, `HYPERION_SYNC_DUMP=<frame>,<folder>` writes each domain's bytes as
hashed at that frame, `live.U` to `live.S` from the game and `replay.U` to `replay.S` from
its replay; set it for `hxrecord --only <game>` and then `hxsuite --replays <its replay>`,
and compare the two. The first runs of this set found four things that way: an image's map
position, which only the draw sets; a heap pointer in one of CUnit's pointer-wide unions;
the move-target unit at CUnit +0x14, a pointer that the save's layout copies as 4 bytes,
whose low half changed with the build; and the under-attack notice and minimap blink,
which a live game sets on the local player's units and a replay never does. All four are
now hashed as `port_synchash.cpp` says. A fifth came with the larger pools: a 4-byte field
padded to 8, such as an order's target position, was hashed as a pointer when its 8 bytes
pointed into the program, and the padding holds the high half of an old pointer from the
stack a struct was copied from. A position beside it could then make an address in the
14 MB the pools take, depending on where Windows loaded that build, and on what the stack
held, which a replay's other code paths change. Since hash version 3 only the four slots
whose other arm is a unit are tested, and only for a unit of the pool. A sixth came with
the first game that gives orders: 1.17 links a selected unit's selection circle and health
bar into its sprite's images, from stores of their own outside the image pool, and the
hash's walk of a sprite's images stopped at an image outside the pool, so a sprite the
local player had selected was read without its images. A game loaded from a save selects
again what the local player had selected (`RestoreSelection`), where the live game, whose
orders go out as commands alone, shows no selection; since hash version 4 the walk skips
those images and an image's links are read past them.

The observer covers every slot of a Hyperion game: its `P` and `Q` lines run to the last
slot (past neutral's), `h_stat` folds in the slots past 11, and the engine hands it the
unit table's size (`port_observe.h`, version 9). A 1.17 game's lines are as before.

The observer reads a frame at the first clock call after the frame advanced, which is how
the oracle's runs read 1.17's games. A Hyperion game's frames are read instead at a hook
the engine calls after each frame's update and check, before the saves a replay makes
again (version 10, `pfnSetFrameHook`), and its log ends with `V,frame_hook,<frames>`. A
clock call can come from inside a frame: in a replay the local nation is another player
than in the game it recorded (Hyperion's playback, through 1.17's code, makes slot 0
local), so the "cannot
create more units" line, whose timer reads the clock, comes at other frames, and the
frame was read half run.

`suite` writes `testrun\rt_runs\<stamp>\`: one folder per replay with a CSV log, a
JSON summary and the observer config per build, and `report.html` for the whole
run. `run --build oracle|hx64 --replay X.rep` runs one; `report <dir>` rebuilds the
page. `hx64` runs `Hyperion.exe` from `testrun\hx64\`, a folder laid out as a player's
install (`hyperion-exe.md`), copying the build in when it is newer. For a replay on
which the original loses recorded commands, the report compares `hx64` with the oracle
only through the first such frame, since from there it plays the recorded commands and
the oracle does not. Each run keeps Hyperion's `hyperion_init.log`, and
`hyperion_crash.log` when there is one.

The oracle reaches the replay through the menus (the menu driver, below). Hyperion
starts it from the command line instead, `--replay` on the staged file
(`src117\port\gameplay\port_launch.cpp`, since 2026-09-27), so its config has no menu script and
the game starts about half a second after the process does, not nine. The menus stop
while the window is in the background (`GetNextMessage` waits for a message while the
window is inactive), and a run that waited on them could time out when another window
had the focus. `--menus` puts an engine build back on the menu path.

An engine build draws through SDL3 (`modules/render.md`), and `--window` picks how its window
shows during a run (`HYPERION_WINDOW`). `--renderer` picks its presenter
(`HYPERION_RENDERER`: `sdl`, `direct3d12`, `vulkan`; `sdl` when not given, though the
engine's own default is `vulkan`), and `--gpu-check` has the GPU
presenter read every frame back and compare it with the screen through the palette; the
report puts the count beside the run's verdict (`modules/render.md`). `--scene` has the GPU
draw the frame from its description too, to the stage named (`terrain`, `sprites`, `map`
or `screen`; `off` when not given, though the GPU presenter's own default is `gpu`), and
with `--gpu-check` compares it with the game's (`modules/render.md`); the
counts are in the run's `hyperion_render.log`. `--scene gpu` has the GPU alone draw the
map; the draw buffer's snapshots then show nothing of it, and `--snap-screen`'s are the
frame as shown, read back from the GPU, to compare with the oracle's. The default window, `background`, is a
640x480 window that takes neither the focus nor the mouse, so a run can be watched while
you work; `hidden` draws every frame and shows none; `windowed` and `fullscreen` are a
player's modes. The oracle ignores it and shows cnc-ddraw's window, placed off the
desktop.

A replay selects nothing, so a run shows no hit-point bars, drag box or help bubble.
`tools/replaytest/drag.py [seconds]` finds the background window and posts drags to it,
resting the pointer on the command card now and then; the run takes them with
`HYPERION_TEST_MOUSE=1` in its environment, which lets a background window take mouse
messages marked with `PORT_TEST_MOUSE` in wParam and nothing else, so the user's own
mouse still reaches nothing. Only the drawing depends on it: a replay's commands are the
replay's.

`--stay` lets the game go on past the replay's end until `--timeout` ends the run, so a
run can reach the score screen: the replay's end box waits for Exit Replay, which a key
posted to the window gives. No replay run reached the score screen before, and at 64 bits
it crashed there (`gluScore.cpp`).

`--view <width>` or `--view 16:9` plays an engine build's games at a wider view
(`modules/render.md`); the oracle stays 640x480 and its cached runs are reused. The logs
compare as ever, since the view is not in the simulation. The snapshots are the view's
size, and the report counts a pair of different sizes apart instead of comparing it.
`--view-every N` switches the view while the replay plays: the observer calls the
engine's `oc_view_toggle` (F5's action) at every frame a quarter into each N, so the
view alternates between `--view`'s width and 640. With `--snap-every` at the same N,
every other snapshot is at 640 and is compared with the oracle's.

`--snap-every N --snap-screen` saves the whole screen every N frames, as the player sees
it, from every build, and the report counts the snapshots that match the reference byte
for byte (`modules/render.md`). The mouse can be used meanwhile. cnc-ddraw
hands the oracle the real mouse whenever the pointer crosses its window, which moved
the oracle's cursor and changed its snapshots from there on, so the window sits off
the desktop. An engine build's `background` and `hidden` windows drop mouse messages,
but for a test's marked ones (above).

A replay's view stays where the replay starts, so those snapshots show one corner of
the map and nothing scrolls. `--snap-move` adds a camera move halfway to each snapshot:
the view is centred on a player's unit (neutral ones mostly sit in unexplored black),
picked by the frame, the same unit in every build since their units are the same,
through the game's own
`gamemap_center_on_mtx`, between two game updates where the centre-view trigger runs.

One run of ours goes at a time, since every run stages its replay in the same
folder. Runs do not collide with other copies of the game:
- the game folder's exe is named `Hyperion.exe`, so a harness that waits on any
  `Starcraft.exe` does not see our runs, and ours waits only on
  `Hyperion.exe` (by name: the game's `InitACL` denies other processes the
  rights needed to read its exe path);
- the observer answers the game's single-instance check with an unnamed event
  (below), so the oracle starts next to another copy and never blocks one;
- Hyperion replaces that check with one scoped to its own folder
  (`src117\port\lan\port_instance.cpp`).

## Comparing any replay

`suite` answers whether a build passes; `compare.py` shows a person that a replay plays the same
on both, with the numbers to see it by. It takes any 1.16 or 1.17 replays, or folders of them
(searched with their subfolders), plays each on the oracle and on Hyperion, and prints the
comparison in the terminal and writes it as a page. Made 2026-10-19, at the user's asking for a
check that 1.17 and Hyperion agree on a replay's end, its outcome and its resources, snapshot by
snapshot, that a person can read.

```powershell
cd engine
python tools/replaytest/compare.py "C:\StarCraft\maps\replays\my game.rep"
python tools/replaytest/compare.py my_replays --every 10s     # a snapshot every 10 seconds
python tools/replaytest/compare.py my_replays --brief --open  # the hash rows only; open the page
```

| Option | What it does |
|---|---|
| `--every` | the snapshots' period: `10s`, `1m` (the default), `2.5m`, or frames as `360f` |
| `--brief` | the hash rows alone, without each player's numbers under them |
| `--open` | opens `compare.html` in the browser when the run ends |
| `--build` | the Hyperion build held to 1.17: `hx64` on Windows (the default), or `lx64`, `la64`, `ma64` where they run |
| `--out` | the run's folder, in place of `testrun\rt_runs\compare_<stamp>\` |

It takes the suite's run settings too (`--window`, `--timeout`, `--no-cache` and the rest), with
`--updates 32` as `suite` has it: the oracle's run of a replay is kept under its settings
(below), so with the suite's settings a replay the suite has played is not played again on the
oracle. A replay new to the oracle takes about 10 seconds in the menus and then plays at about
2,500 frames a second; Hyperion's plays at 3,000 to 5,000.

What it prints for a replay (m1, its snapshots every 2 minutes, most rows left out):

```
m1_IronManLadder_1v1_LostTemple_2003.rep  in ...\testrun\rt_replays\modes  1.17 against Hyperion (hx64)
  KBK21-The Lost Temple, 128x128, 26,600 frames (18:37 at Fastest)
    CryStaL[InCa]  Zerg, left at 18:34
    HwaSin[S.sIR]  Terran
  a number is shown once where both builds have it, as 1.17 | Hyperion where they do not

  time      frame   state hash          running hash
                    1.17      Hyperion  1.17      Hyperion
     2:00     2,856   fe3b29ea  fe3b29ea  a0ea3d01  a0ea3d01  ok
      CryStaL[InCa]  minerals 358  gas 0  supply 12/17  units 14  buildings 1  ok
      HwaSin[S.sIR]  minerals 134  gas 0  supply 13/18  units 12  buildings 2  ok
     ...
    18:37    26,600   e05ad189  e05ad189  c7fefe6a  c7fefe6a  ok

  the end of the game
                         CryStaL[InCa]   HwaSin[S.sIR]
  minerals                       2,788           2,816  ok
  minerals gathered              5,138           8,666  ok
  units produced                    22              72  ok
     ...

  result: HwaSin[S.sIR] won: CryStaL[InCa] left at 18:34 (the same in both)
  IDENTICAL: identical on all 26,600 frames, and at the end; 10 of 10 snapshots the same
```

In a console the marks are a green check and a red cross; written to a file or a pipe whose
code page has neither, they are `ok` and `DIFFERS`. A `-` marks a snapshot that is shown and not
counted, after the frame where 1.17 loses commands.

- The header: the map and its size from the replay's header, the length at Fastest (42 ms a
  frame), and each player's name, race, team (when the header has more than one team number), a
  computer slot as such, and the time the player left, from the replay's quit commands (0x57).
  A random slot's race is the one whose supply the player has.
- A snapshot falls on a frame of the `P` and `Q` lines (every 24 frames), the one nearest its
  time, so a minute is 1,440 frames and they do not drift; times are rounded to the second. The
  last frame is always one.
- The state hash is 8 hex digits of SHA-1 over the snapshot frame's whole record in the log: the
  `F` line but `h_raw` (the random number state and count, the unit count, `h_sem`, `h_stat`)
  and every `P` and `Q` line of the frame. The running hash folds in every frame's record from
  the first, so two equal running hashes say that every frame up to the snapshot was equal, not
  the snapshot's alone. Both are worked out by `compare.py` from the two logs; they are not the
  engine's own state hash (the `H` lines of a Hyperion replay's checks).
- A player's line: minerals and gas on hand (`P`), supply used and given for the race played, as
  the game shows it (`Q`'s supply rows halved, a half rounded up, the given capped at the
  limit), units owned and buildings owned (`Q`'s rows 1 and 8; units leave out buildings).
- The end of the game: each build's last numbers, minerals and gas on hand, supply, the two
  gathered, units produced, owned, lost and killed, buildings constructed, owned, lost and razed,
  and the four scores.
- The result, from each build's own end: a side is out when every player of it left or owns no
  buildings at the end, and when one side is left standing it won. A side is a team when the
  header has more than one team number, else a player. So it is an account, not 1.17's own
  verdict: a game the replay ends before anyone is out (bot games often do) reads "no result".
  Where the two builds' games part after lost commands, their results can differ, and both are
  given (`s1`: 1.17's game ends with two players in, Hyperion's with one).
- The verdict follows the gate's rule. `IDENTICAL`: every frame matches, and the two games end
  on the same frame. `IDENTICAL TO m:ss`: 1.17 loses recorded commands there, and every frame
  matches through it; the verdict says how many frames after it differ and whether the games
  come together again. `DIFFERS`: the first frame that differs, which gets a row of its own
  among the snapshots marked "the first difference", and what differs there, with both builds'
  values (up to ten lines). `NOT COMPARED`: 1.17 could not play the replay. A game that does not
  reach the replay's end is named in the verdict with how it ended (`crashed`, `load_failed`).
- The last line counts the replays by verdict and gives the page's address.

It writes `testrun\rt_runs\compare_<stamp>\`:

| File | What it holds |
|---|---|
| `compare.html` | the whole run as a page: a table of the replays and their verdicts, then per replay its players, verdict and result, the end of the game side by side (1.17 and Hyperion columns per player), minerals and units curves (1.17 solid, Hyperion dashed over it), the snapshots' hashes, and per player each snapshot's numbers from both builds; light and dark by the system's setting |
| `<replay>\snapshots.csv` | every 24 frames, for every player, each number from both builds side by side, the frame's state hashes and whether all of it is the same |
| `<replay>\oracle.csv`, `hx64.csv` and their `.json` and `.cfg` | the two runs' logs, as `suite` keeps them |
| `report.html` | the suite's report of the same runs, with the first frame by field |

Two replays of one name in different folders get folders `<name>` and `<name>_2`. The exit
code is 0 when every replay matched (identical, or identical to where 1.17 loses commands) and
none was skipped, else 1.

Its limits:
- The oracle runs on Windows alone. Elsewhere only a replay whose oracle run is kept, copied from
  a Windows machine, can be compared.
- No copy of `Hyperion.exe` may be running: the harness's runs stage the replay in one place and
  refuse to start beside one (`game_running`). Close a game being played first.
- A replay of StarCraft: Remastered (1.21 on) is another format and is skipped as unreadable; a
  Hyperion replay (`hxRS`, a map or players past 1.17's) is one 1.17 cannot play and is skipped,
  `hxsuite` being its check. Both are named in the output.
- The numbers shown are the observer's: what each player has and the score screen's counters.
  The state hash covers far more (every live unit's fields, the counts per unit type, research
  and upgrades), so a difference that shows in no number still fails the snapshot.

The first runs, 2026-10-19 (`testrun\rt_runs\compare_20261019_205426`, the suite's 10 replays):
seven identical to the end; `s1`, `s2` and `s3` identical to 26:08, 18:49 and 27:23, where 1.17
loses commands. `s3` then parts for 298 frames, to frame 39,538 (27:41), and is the same from
there to its end, the end of the game and the result included. From the SSCAIT 2016 archive,
`Ro8\M1G1-Leta` was not compared (1.17 refuses it, `load_failed`), `Ro4+BronzeMatch\M1G1-Leta`
(another game under the same name) is identical, and `Ro8\M1G2-Leta` is identical to 9:30,
1.17's game crashing at frame 19,062 (13:21) as "The restart in M1G2-Leta" tells. A copy of
`m1`'s Hyperion log with one player's minerals raised by 8 at frame 20,016 read `DIFFERS` from
that frame, "HwaSin[S.sIR], minerals: 1.17 2,000, Hyperion 2,008", and every running hash after
it differed. The run shown to the user is `compare_20261019_205927` (`m1`, `m4`, `s3`).

## The game folder

`setup` builds `testrun\rt\`, the oracle's:
- hardlinks to testrun's data files and DLLs;
- `Patch_rt.mpq`, because loading a replay needs `game\nconsole.pcx` and only that
  archive has it;
- a copy of the oracle exe named `Hyperion.exe`, with DYNAMICBASE cleared, so the
  exe is at 0x400000, where the observer's addresses are;
- a copy of testrun's `ddraw.ini` with a `[Hyperion]` section, since cnc-ddraw
  reads per-game settings from a section named after the exe; it lets the game
  handle closing and puts the window off the desktop (`posX`, `posY`). A change to
  the section needs `setup` again;
- its own `maps\replays`, so staging a replay never touches another folder;
- copies of the player's 1.17 profiles in `characters\`, from `Documents\Starcraft\`,
  when it has none. Hyperion keeps profiles in the exe's folder
  (`src117/port/platform/port_paths.cpp`) while the oracle keeps them in Documents, and the menu
  path takes the ID the ID screen shows. Without a profile Hyperion stops at the ID
  screen (screen 5).

It also builds `testrun\hx64\`, Hyperion's: hardlinks to the MPQs and `Local.dll`, `Patch_rt.mpq`,
the x64 `SDL3.dll`, the built `Hyperion.exe`, `maps\replays` and the profiles, the same way,
and no `StarCraft.exe`. `--data` picks where an engine build reads the game's files
(`HYPERION_DATA_SOURCE`): `mpq`, the default; `both`, a Remastered install's files over the
MPQs; `remastered`, the install alone, from `testrun\hxr\`, which holds `Hyperion.exe`,
`SDL3.dll` and the profiles and none of 1.17's files. Both read the same data files. Before each
run the harness writes the folder's `settings.json`, where Hyperion keeps 1.17's settings
in place of the registry (`front-end.md`, "F3 so far"): 1.17's defaults with the intro
movies marked as seen, so every run starts from the same settings and none waits on a
movie.

## What the observer does

It changes no code byte of the game. It replaces the exe's GetTickCount and Sleep
imports (on Linux, where there is no import table, Hyperion's own `oc_posix_hook` takes
the observer's functions in their place, `port_posix.cpp`), and does its work inside
the GetTickCount hook on the game thread. It also replaces the exe's CreateEventA
import: when the game asks for the named event of its single-instance check
(`CheckForOtherInstances`, 0x466F20), the hook creates an unnamed one, which
never already exists, so the check always passes.

- **Virtual clock.** Every GetTickCount call returns 50 ms more than the last,
  which always exceeds the 42 ms a Fastest frame takes. The frame pump never
  waits, and the game runs as fast as the CPU allows. A replay's simulation reads
  the replay's own milliseconds-per-frame table, never the clock (triggers get
  `sgTicksPerFrame_g`), so the clock sets only the pace.
- **Batched repaints (`--updates N`).** This sets `sgdwUpdatesPerFrame_g`, the
  count the game itself raises to 10 at replay speed x8 and above. The game then
  simulates N frames per repaint.
- **Window held active.** A single-player game advances only while its window is
  active, so the observer holds `sgbWindowActive_g` at 1 and a run can sit behind
  other windows.
- **Menu driver.** It presses keys from a script, one step per menu screen.
  S (Single Player), E (Expansion) and Enter reach screen 22, the expansion
  campaign screen. Its result 11 opens the replay browser (screen 12,
  `do_create_dlg(1)`), but no control on that screen has index 11. The observer
  ends the dialog with result 11 exactly as a push button would: it sets
  `gnDlgReturnVal` and sends the dialog event 2. The runner stages the replay as
  `~oc_run.rep`, which sorts last, so pressing Down selects it, and O starts it.
- **Stop.** The run ends when the replay's frame count is reached, at
  `--max-frames`, when the game drops back to the menus before it starts
  (`load_failed`), when the menus have not started a game after `--menu-timeout`
  seconds (`menu_timeout`, which is also how an engine build's `--replay` it could not
  play ends), or when nothing moves for `--stall-secs`. A game thread that
  makes no GetTickCount call for 10 seconds is suspended once and its place logged:
  a `W,...,stuck,` record with its EIP and the first code addresses on its stack,
  each as module+offset.
- **Snapshots.** `--snap-every` saves the draw buffer, or with `--snap-screen` the
  screen: Storm's surface on the oracle, locked as its blitters lock it, and the
  engine's `oc_screen` export on Hyperion. With snapshots on, the cursor's
  animation clock (`sgdwCurFrame_g`, `sgdwCurNextTick_g`) restarts when the game
  starts. Its frame counts clock calls since the process began, menus included,
  and the menus follow wall time, so without the restart two runs show the same
  cursor frame only by chance. `--snap-move` (`snap_move=1`) moves the camera to a
  unit halfway to each snapshot.

## The log

One line per record, comma-separated, first field the kind:

| Kind | Fields |
|---|---|
| `F` | frame, random state, random call count, live units, h_sem, h_raw, h_stat |
| `P` | frame, player, minerals, gas, units, hit points (every `--stats-every` frames) |
| `Q` | frame, player, minerals gathered, gas gathered, then the 28 score counters (`glGameCounts_g`, named in `replaytest.py`'s `SCORE_NAMES`), for players 0 to 7, every `--stats-every` frames and on the last frame |
| `T` | frame, wall-clock ms since the game started (every 240 frames) |
| `G` | game start: playback flag, the replay's frame count, seed, speed |
| `E` `K` `U` | menu screen changes, keys sent, dialog control dumps |
| `S` | end: reason, frame, frames logged, game ms, frames per second |
| `X` | a fault: frame, exception code, faulting address, accessed address, ECX, and up to 8 code addresses found on the stack above ESP (mostly return addresses; a sketch of the call stack). Logged and flushed before the game's own handler runs; a run that dies on it ends as `crashed` |
| `Y` | a unit's raw 336 bytes at `--dump-frame`, on x86 |
| `Z` | a unit's `h_sem` fields at `--dump-frame`, on every build |
| `V` | a note on the run: where the observer reads from, the `--check-observe` count, a save, a load, the pinned seed, and at the end an engine build's presenter with the GPU check's frames and differences (`V,presenter`) |
| `H` | two kinds. `H,<frame>,...`: a Hyperion game's state hash every `sync_every` frames, the random state and count and then each domain's hash. `H,<module>,GetTickCount=,Sleep=,CreateEventA=`: at the start, which of those imports the observer hooked in the module named |
| `B`, `C` | at the start: where the exe and its data were placed (`exe_base`, `data_base`, the delta from 1.17's addresses), and the run's clock and its settings |
| `R` | the first frames' call site the frame was read from |
| `N` | at each snapshot, the frame and the main thread's calls into the clock so far |
| `D` | every 5 seconds from the watchdog: the clock calls, from the main thread and others, the game's mode and its menu screen |
| `W` | the watchdog's end of a run that stopped: the main thread stuck (with its context), `stall_in_game` at a frame, or `menu_timeout` on a screen |

`h_sem` hashes each live unit's index, type, owner, hit points, shields, energy,
position, order and order state, order target and status flags. `h_raw` hashes
every dword of each live unit except `pPath` (+0x100). That field points into a
heap block whose place differs between builds, so the same path record has a
different address. A pointer field that points into an engine's block of
1.17's data is taken as the 1.17 address it stands for. The records are sampled at the first GetTickCount
after each simulated frame, which is the call at the end of the frame loop in
`gameloop_update`.

`Q` lines name their 28 counters by `replaytest.py`'s `SCORE_NAMES`, from the score screen's
and the triggers' reads (`gluScore.cpp`, `TrigEnum.cpp`). Before 2026-10-19 the names had units
owned and units produced swapped, and buildings owned and buildings constructed, so a report
from before then names a difference in those rows by the other's name.

`h_stat` hashes the stats: each player's minerals and gas on hand and gathered, the 28
score counters the score screen and the triggers read (units and buildings produced,
lost, killed and razed, the scores, supply), the counts per unit type (`glCounts_g`),
and the research and upgrade tables, 1.17's and Brood War's. They are plain data that
every build keeps in 1.17's layout, so the observer reads them at 1.17's addresses on
every build, the x64 one included.

A frame matches when the random state, the random call count, the unit count, `h_sem`
and `h_stat` all agree, and on the frames with `P` and `Q` lines, when every player's
lines agree. `h_raw` is reported separately. The report also compares the last `Q`
lines, the numbers the score screen shows, and names any that differ.

The observer reads the oracle's values at 1.17's addresses. An engine build hands them
over itself through its `oc_observe` export (`src117/port/replays/port_observe.h`): the game
state for the `G`, `F` and `P` lines, the used units as records of the fields `h_sem`
takes, in 1.17's widths, the palette, and the controls the harness sets (the window
kept active, the updates per frame, the cursor held, the camera moved). Both fill the
same records and one path hashes and logs them, so the log does not depend on where
the engine keeps its state, which the 64-bit build needs.
`h_raw` and the `Y` dump still read 1.17's memory. With
`--check-observe` an engine build's observer also reads 1.17's memory and compares it
with what `oc_observe` handed over, every frame; the first differences are `X,
observe_diff` lines and the count ends the log as `V,observe_check,<frames>,<differences>`.
The engine keeps 1.17's data in a block of its own, so with an engine the
observer reads "1.17's memory" in that block, whose address `oc_observe` gives; the log's
`B,data_base` line records it. The menu driver ends a dialog through `oc_observe` too.

## Saved games

`--save-frame N` saves the replay's game at frame
N, between two game updates, where the save menu's command runs, with a fixed time stamp
in place of the clock's so that every build writes the same header; an engine build saves
through `oc_observe`'s `pfnSave`, the oracle by calling `saveload_write_save_data` at its
address. A single-player save wants exactly one human, and a replay has one per player,
so the save is written as a multiplayer game's (`.mlx` for Brood War). The harness copies
it into the run folder as `<tag>.mlx` and logs `V,save,<frame>,<name>,<ok>`. The oracle's
Storm keeps its user data in `Documents\StarCraft`, so its save is moved out of there,
and a folder the save made there is removed again.

`--load <file>` starts a build from a saved game in place of the replay, and plays it
with no input to `--max-frames`. An engine build is given `--load` on its command line
(`src117/port/gameplay/port_launch.cpp`: the Load screen's Ok, `sub_498CD0`, then the game start
reads the file). The oracle reaches it through the menus: past the profile to the
Brood War campaign screen, where the `{LOAD}` key opens the file as the Load screen's Ok
does and `{RETURN0}` ends the screen (`V,load,menu,<file>,<ok>`). Loading in place,
into a game already running, is not possible even in 1.17: the oracle crashes in
`find_units_in_rect`, from `CUnit::DecodeFromSave` through `TrackCollision`.

A single-player game seeds its random numbers from the clock (`new_game`), and the same
seed picks the race of a slot left random, which a loaded game's local slot is, so two
runs of a loaded game agree only with the seed pinned. With a load the observer pins it
to 0x7E5A2031 (`V,pin_seed`): an engine build takes it through `pfnPinSeed`, which
`new_game` reads through `PortObserveSeed`, and on the oracle the observer points
`new_game`'s call to `_time64` (0x443FE9) at a function of its own.

`tools/replaytest/loadmatrix.py` runs the whole check for one replay:

```powershell
python tools/replaytest/loadmatrix.py --replay <file> --frame N --until M --out <dir>
```

Each build saves the game at frame N (`<dir>\save\`). Then each engine build loads every
save, the oracle's and each engine build's, and the oracle loads its own (a Hyperion save
need not load in 1.17), each tagged `<build>_from_<save>` (`<dir>\load\`), and every run
is compared with `oracle_from_oracle` on every frame from the load to M: the random state
and count, the unit count, `h_sem` and `h_stat` (and `h_raw`, while there were 32-bit
engine builds to compare it between).

`tools/porttest/savecompare.py A B` compares two saves item by item. It runs
`tools/porttest/savedump.cpp` (built by `tools\build_porttest_x64.bat`), which splits a
save into its raw runs and its compressed chunks, decompressed with the CRC checked, and
reads a Hyperion save's sections. `--expect` leaves out what two builds' saves of one
game differ in by design: the map's path (`MapFileName`), and pointers 1.17 writes that
a load does not read, the path glob's contour table and each AI banker's demand heap,
which the load clears.

## Checks already made (2026-09-26)

Oracle only, replay 01 (5,341 frames):
- two runs are identical on every frame, `h_raw` included;
- the real clock (first 1,200 frames), the virtual clock and `--updates 32` are
  identical, so the speed-up does not change the game.

Speed on replay 01:

| Mode | Frames per second | Against real time |
|---|---|---|
| real clock | 18 | 0.7x |
| virtual clock | about 3,000 | about 130x |
| virtual clock, `--updates 32` | about 17,600 | about 740x |

Each run also spends about 9 s in the menus.

First suite (`testrun\rt_runs\suite1`, `--updates 32`), five 2014 iCCup-era replays:
- 01, 02, 03 and 05 (81,188 frames, 3.7 to 37.3 minutes of game time each) are identical to
  the oracle on every frame, `h_raw` included, for both the byte-exact build
  (4,930 trampolines) and the held build (4,955);
- 04 (Fighting Spirit) does not load on the oracle itself ("error loading
  scenario file"), so it tests nothing;
- 05, a 37-minute game, simulates in about 6 s.

Two more suites (`testrun\rt_runs\modes1`, `sscait1`), all identical to the oracle
on every frame for both builds:
- game modes, from the test replays of github.com/msikma/bwscrep (2003 to 2009):
  Iron Man Ladder, Ladder, a 4-player Slaughter, an 8-player Team Capture The
  Flag (134,438 frames);
- the SSCAIT 2016 bot tournament archive (sscaitournament.com): a 3-bot Free For
  All and a 5-bot Top vs Bottom on Big Game Hunters, and four one-on-one games
  covering all three races (222,806 frames).

The other game modes in the bwscrep set (Capture The Flag, Free For All, Greed,
Sudden Death, Team Free For All, Team Melee, Top vs Bottom, Use Map Settings) are
recorded by 1.21 or later, whose replay format 1.17 cannot read.

Replay 03 is against a computer player. A replay does not record the computer's
actions; the AI runs again during playback, so that replay also exercises the AI
code.

## Checks made in Hyperion (2026-09-26)

With `sub_491300`, `veclen` and `veclen_precise` in the payload, and a helper process
holding the vanilla instance event:

- the modes and sscait_pick sets (10 replays) for the held and cpp builds, and the whole
  SSCAIT 2016 archive (27 replays) for the held build: 47 comparisons, 1,342,532 frames,
  identical to the oracle on every field, `h_raw` included
  (`testrun\rt_runs\20260926_171102`, `20260926_172018`);
- two archive replays fail on the oracle itself. `Ro8\M1G1-Leta.REP` does not load.
  `Ro8\M1G2-Leta.REP` plays 19,062 of its 21,593 frames and then crashes in `free_game`,
  which calls `dlg_grp_free` with a null console group (oracle EIP 0x4C2C7F), at
  `--updates 1` as at 32. Every build logs the same 19,062 frames and then the same fault
  (`testrun\rt_runs\20260926_173701`).

After the last sync of the game's C++, for the held and cpp builds: the modes
and sscait_pick sets, the Finals, Ro16 and Ro8 folders of the SSCAIT 2016 archive, and its
Ro4 and bronze-match folder, run for the first time. 44 replays play to the end identical
to the oracle on every field, 88 comparisons, 2,440,042 frames
(`testrun\rt_runs\sync151_modes`, `sync151_archive`, `sync151_ro4`; one held run whose
menu script stalled on the first screen was rerun in `sync151_rerun`). `Ro8\M1G1-Leta`
and `Ro8\M1G2-Leta` fail as before, the same way in every build. The Ro4 folder reuses
the Ro8 file names, so it needs its own `--out` folder.

## The restart in M1G2-Leta

`Ro8\M1G2-Leta.REP` ends at frame 19,062 because 1.17 plays a restart command the replay
does not contain, and then crashes. Found on 2026-09-26 with a logging build and proved by
emulation.

Replay turns are read by `net_mgr_replay_turns` through a turn buffer, a local `CNetUnkn` built on
every call over static arrays of 0x200 bytes (`byte_672DE8`, sizes in `dword_672FE8`, owners
in `byte_672BE8`). When the next recorded frame is ahead of the game, the frame's commands
are drained one call early (`sub_478890`, `net_record_read_turn`) and dispatched on the next call. If
they do not fit in 0x200 bytes, `net_record_expand` grows the buffer onto the heap, and the early
return frees it. The next call dispatches the whole count from the static arrays, which
hold only the commands written before the growth, followed by stale bytes from earlier
frames. So a frame over 512 command bytes that follows a frame with no commands plays
partly as other frames' commands. An emulation of the static arrays over the whole replay
predicts all 100 commands the logging build saw at frame 19,062, byte for byte. One of
them starts with a stale 0x08, the restart command. In playback `recv_net_cmd_RestartGame`
ends the game with mode 5, `free_game` frees the console theme and clears
`sgpCurrConsoleDlgGrp_g`, the restart's `new_game` fails because a replay has no map to
restart, and the second `free_game` calls `dlg_grp_free` with the null theme.

Hyperion's `free_game` skips a null theme. The held and cpp builds then play the same
19,062 frames as the oracle, identical on every field, and return to the main menu
(`testrun\rt_runs\fix_m1g2`). With the fix, the modes and sscait_pick sets stay identical
for both builds, 20 comparisons, 714,488 frames (`testrun\rt_runs\fix_modes`).

The lost commands are the larger bug. It is playback only: live games take their turns
another way. Frames it hits in the replays used here: `Ro8\M1G2-Leta` 38 (from frame
13,575), `Ro8\M1G1-Leta` 22, `Ro8\M1G3-Leta` 11, `team_game` 6, `Ro16\6-TOMA_ANDR-PVP` 4,
`FFA` 2 and `Ro8\M4G2-kras` 1 (`python tools/replaytest/repfile.py <replay>` lists them;
it reads the command sizes from the original's own table, `cmd_size_bytes_g`).

Hyperion plays the recorded commands.
`src117/port/replays/port_replay.cpp` keeps the turn buffer's growth across calls, so a frame
read one call early is dispatched whole; `net_mgr_replay_turns` calls it around its read. A replay
that never grows the buffer plays exactly as before. On `M1G2-Leta` the held and cpp builds
are identical to the oracle on every field through frame 13,575, differ from frame
13,576, when the first lost frame's commands take effect, and play to the replay's end at
frame 21,593 with no restart (`testrun\rt_runs\turns_m1g2`).

The suite with the fix, held and cpp against the oracle, 2026-09-26: the modes and
sscait_pick sets (10 replays) and 26 of the 27 replays in `Finals+FFA+TeamGame`, `Ro16`
and `Ro8`, 72 comparisons over 1,722,656 frames, all passing. Replays the original plays
whole are identical on every frame; the affected ones are identical through their first
lost frame and play to `replay_end`. `M1G1-Leta` fails to load on the oracle too. The run
was paused before `Ro8\M4G3-kras` and the `Ro4+BronzeMatch` set (`testrun\rt_runs\turns_*`).
