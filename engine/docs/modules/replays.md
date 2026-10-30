# Replays, saved games, snapshots and the rules a game plays by

## Overview

Four things that answer the same question, "what game was this, and will it play that way again?"
**Replays**: 1.17's `.rep` for a game 1.17 could have played, and Hyperion's own `hxRS` for a game
by Hyperion's rules, which carries a rule record and a check of the state. **Saved games**: 1.17's
save in a container of Hyperion's, plus sections that let a loaded Hyperion game go on frame for
frame as the game saved did. **Snapshots**: the game's state copied in memory at a frame's end and
put back in the same game, which the trainer's timelines and a replay's slider are built on.
**The rules**: classic
(1.17 to the frame) or Hyperion's, decided once as a game starts and recorded in every replay and
save, so a file plays back with the code it was played with.

The state hash itself is `port_synchash.cpp`, described in `loop.md`; here it appears only as what
replays and saves check against.

## Status

Shipped in v0.3.6-beta (2026-10-18): replays and saves of Hyperion games, the rule record,
snapshots and Replay TV. The simulation revision is `HX_SIM_REVISION` 16
(`src117/h/hxsim.h`); the state hash is version 9 (`h/port_synchash.h`). A classic game's replay
stays 1.17's byte for byte, which the replay suite holds to.

## Formats

### 1.17's `.rep`

Written and read unchanged for any game by classic rules: the magic `reRS` (0x53526572), a
0x279-byte header, the command record and the map's CHK, each through `compress`/`decompress`
(`lang/replay.cpp`, `LoadReplayData`, `write_replay`). Hyperion changes one thing about playing
one: 1.17 loses a frame's commands when a turn buffer grows past 0x200 bytes, and Hyperion plays
the recorded commands instead (`port_replay.cpp`, below).

### Hyperion's replay, `hxRS`

A game by Hyperion's rules (`HxRules()`: every game past 1.17's limits, and any game started so)
writes `hxRS` (0x53527868) in place of `reRS`, so 1.17 and the tools that read its replays never
take one for theirs. The same three blocks follow, then the marker `HYPERION`, a format version
(2), and sections, each a 20-byte header (id, version, flags, record count, record size, byte
count) and then its records, uncompressed. In write order (`ReplayHxWrite`):

| Section | What it holds |
|---|---|
| `RULE` | the rule record (below) |
| `RACE` | a mod's race each slot played, when a slot has one |
| `AILV` | each slot's computer level and style, in a game with `HX_OPT_AI_LEVELS` |
| `HCAP` | each slot's handicap in percent, a byte a slot, in a game with `HX_OPT_HANDICAP` (`gameplay.md`) |
| `MUTS` | the game's mutators, one record of sixteen 32-bit values, 64 bytes (fields' and geysers' shares in percent, 0 endless; start minerals and gas; supply cap, 0 none; start workers; supply boost in percent; fog, 0 normal, 1 explored, 2 none; build time in percent; tech, 0 normal, 1 researched, 2 no upgrades, 3 no research; the rules' revision; rich bases, 0 off, 1 past the naturals, 2 outside a radius (the middle ground before v0.6.21), 3 farthest only, 4 every base; its chance in percent; its rules, 1 fair, 2 at least one, 4 the same every game, 8 not announced at the start; its radius, tiles, or below 0 a share of the map in percent; the unit limit, 0 automatic), in a game with `HX_OPT_MUTATORS` (`gameplay.md`); the shorter records of earlier builds read with the values they lack at 1.17's: 60 bytes (v0.6.21 to v0.6.24, the unit limit automatic), 56 bytes (v0.6.16 to v0.6.20, the radius 48 tiles), 44 bytes (v0.6.10 to v0.6.15, no rich bases), 40 (v0.6.9, no revision) and 28 (v0.6.9's first builds, the first seven) |
| `RICH` | the resources that turned rich, their unit indices, 4 bytes each, in a game with rich bases (`gameplay.md`, "Rich bases"); playback marks them in place of the read |
| `STRS` | a benchmark's stress run, in a game with `HX_OPT_STRESS`: the units given each computer at the first frame and the pools' scale (`modules/tools.md`) |
| `UNTS` | the unit table, one 32-bit count, when it is not the slots' 1,700 for each 8 (`loop.md`, "Caches for large games"); playback sizes the pools by it |
| `SLOT` | the `PlayerData` records of the slots past 1.17's 12, 36 bytes each |
| `COLR` | every slot's colour as drawn: its eight-entry ramp and minimap entry |
| `RGBS` | every slot's colour in full RGB where it has one (stage G) |
| `SYNC` | the state's hash every 24 frames of the game as it was played |
| `SAVE` | the frames the game was saved at |

The rule record is `RuleRecord`: the rule version (`"1.17"` or `"hyperion"`), a SHA-1 of the
eleven data files the simulation reads (the eight `arr\*.dat` tables, `iscript.bin`,
`aiscript.bin`, `bwscript.bin`, as the archives give them now), the build that wrote it
(`HYPERION_VERSION_TEXT`), the simulation's revision, and from revision 12 the game's options
(`HX_OPT_*`) and the mod whose data the game played with, by name and fingerprint. The
fingerprint covers the mods it depends on too, and `szDepends` names them with their
versions (`ModDataDepends`), so a refusal can say which to install. A save of a Hyperion game
carries the same record in `HXGM`.

### Hyperion's save

Every save Hyperion writes begins as 1.17's does (banner, tag, compressed header, player data),
so the Load screen lists it as before, and then goes into the port's container: the marker
`HYPERION`, a format version, and each of 1.17's parts in a section of its own (`PLYR GAME IMGS
SPRT THNG FLNG UNIT BULT ORDR CREP DECY MAPS TRIG STRS SELS PATH AI DATS VIEW`, and `RCMD`;
`port_formats.cpp`, `PortSaveBegin` to `PortLoadEnd`). `PortLoadBegin` tells the two formats
apart by the marker, so Hyperion loads 1.17's saves as well; 1.17 does not read Hyperion's.

A game by Hyperion's rules (`HxSaveNeeded()`, the same test) adds sections of its own:

| Section | Where | What it holds |
|---|---|---|
| `HXGM` | after `GAME` | the rule record, whether the game has slots past 1.17's, the set-up (`PlayersSaveSetUp`), and the sprites' and images' free lists as they stand with the power fields down |
| `HXRC` | after `HXGM` | the slots' colours in full RGB, when a slot has one |
| `HXRA` | after `HXRC` | each slot's mod race, when a slot has one |
| `HXAI` | after `HXRA` | each slot's computer level and style, with `HX_OPT_AI_LEVELS` |
| `HXHC` | after `HXAI` | each slot's handicap, with `HX_OPT_HANDICAP` |
| `HXMU` | after `HXHC` | the game's mutators, `MUTS`'s record (either length, `PortLoadSectionBytes`), with `HX_OPT_MUTATORS` |
| `HXUN` | after `HXMU` | the unit table, `UNTS`'s count, when it is not the slots' |
| `HXST` | after `VIEW` | the state 1.17's save leaves out (below) and the state's hash as saved |
| `HXWI` | after `HXST` | the high bytes of the ids x64 keeps in 16 bits where 1.17's records have a byte, and the high halves of the images' offsets into their scripts, record by record (`PortSave117HighGet`) for the units, bullets, orders and images, when a mod's id passes 255 or its scripts 64 KiB |
| `HXMT` | after `HXWI` | each slot's state of a mod's techs and upgrades past 1.17's rows (`PlayerModTechSave`), when the game has them |

A map past 256 tiles a side writes its pathing tables whole under its own version of the `PATH`
section, `OC_PATH_VERSION_WIDE` 0x10E in place of 0x0E, which 1.17 refuses and a Hyperion load
takes only for a map of that size (`lang/sai_LoadSave.cpp`).

### Versioning, and what happens to older files

- The **replay format version** is 2, and a reader skips sections it does not know, so a build
  that gains a section still plays older replays and an older build skips a new one. The **rule
  record** is read at its own size: this build's, the one before `szDepends`, or revision 11's
  shorter one (`ReplayHxRuleSizeKnown`), which is then refused by its revision.
- The **simulation revision** is the hard gate. `ReplayHxRuleRefusal` refuses a replay or save
  whose rule version is unknown, whose data fingerprint differs from the data loaded now, whose
  revision is not this build's, which carries option bits this build does not have, or which
  played with a mod this run did not load (or another version of that mod's data, unless
  `checkModFingerprint` is 0; where the mods under it differ, the refusal names both lists).
- The **hash version** is not a gate. `SYNC` is read only when the section's version equals
  `SYNC_HASH_VERSION`, so a Hyperion replay recorded with an earlier hash plays normally but
  unchecked, and a save loaded is logged "not compared": since version 9 (2026-10-18)
  that is every file written before that date. The harness's Hyperion set was recorded again with
  version 9; the version 8 references are kept in `testrun\hash_v8_refs\`.

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `src117/port/replays/port_hxreplay.cpp`, `h/port_hxreplay.h` | Hyperion's replay sections, the rule record, the state check, the AutoSave copy | `ReplayHxWrite`, `ReplayHxRead`, `ReplayHxPeek`, `ReplayHxRuleMake`/`RuleRefusal`/`RuleApply`, `ReplayHxGameStart`, `ReplayHxFrame`, `ReplayHxSync`, `PortReplayKeep` |
| `src117/port/replays/port_hxsave.cpp`, `h/port_hxsave.h` | a Hyperion game's save sections and everything 1.17's save leaves out | `HxSaveSetUp`/`HxLoadSetUp`, `HxSaveState`/`HxLoadState`, `HxSaveBeforePsiLinks`, `HxSaveLoaded`, `HxSaveGoesOn`, `HxSaveReplay` |
| `src117/port/replays/port_rules.cpp`, `h/hxrules.h` | which rules a game plays by, and its options | `HxLimits`, `HxRules`, `RulesForGame`, `RulesChooseNext`, `RulesRecorded` |
| `src117/h/hxsim.h` | the revision of what a Hyperion game does, with a line per change | `HX_SIM_REVISION` |
| `src117/port/replays/port_replay.cpp` | the replay turn buffers that outlive the call that grew them | `PortReplayTurnsUse`, `PortReplayTurnsKeep`, `PortReplayTurnsSave`/`Restore` |
| `src117/port/replays/port_record.cpp` | `--record`: a game played by itself to a frame, its replay kept; `--record-orders` | `PortRecordFrame`, `PortRecordAfterGame` |
| `src117/port/replays/port_observe.cpp`, `.h` | `oc_observe`, what the replay harness's observer reads from an engine build | `oc_observe`, `ObserveState`, `ObserveUnits`, `ObserveSlot`, `ObserveSync`, `ObserveHash`, `ObserveSave`, `ObservePinSeed`, `PortObserveSeed` |
| `src117/port/replays/port_snap.cpp`, `.h` | the trainer's snapshots: the state as shared 4 KB pages, by line | `PortSnapTake`, `PortSnapRestore`, `PortSnapLatest`, `PortSnapShare`, `PortSnapThin`, `PortSnapDropAfter`, `PortSnapFrame`, `PortSnapStatic`, `PortSnapAlloc` |
| `src117/gen/snapranges.h`, `tools/snapranges.py` | the parts of 1.17's two global blocks a snapshot copies; the build writes it again when the blocks' layout moved | `kSnapData`, `kSnapWide` |
| `src117/port/replays/port_replayseek.cpp`, `.h` | a replay moved to any frame: its snapshots while it plays, and the move | `PortReplaySeekTo`, `PortReplaySeekFrame`, `PortReplaySeeking`, `PortReplaySeekable`, `PortReplaySeekAfterGame` |
| `src117/port/replays/port_snapprobe.cpp` | a debugging probe: what a game keeps and changes in memory while it plays | `PortSnapProbeFrame`, `PortSnapProbeAlloc` |
| `src117/port/replays/port_tv.cpp` | Replay TV and following one unit: where the camera looks on its own | `PortTvToggle`, `PortTvTarget`, `PortTvHottest`, `PortFollowToggle`, `PortFollowAt` |

## How it works

### The rules a game plays by

`RulesForGame` runs first in `game_init`. For a replay being played back or a save being loaded
it takes what the file's rule record said (`RulesRecorded`, called from `ReplayHxRuleApply`); for
a new game it takes the launch's choice (`RulesChooseNext`, kept in the session), else
`--rules`, else the `"rules"` setting. Since 2026-10-22 a new game plays by Hyperion's rules
unless one of them says `classic` (or `1.17`, `RulesChosenClassic`); the skirmish set-up's Rules
starts on Hyperion the same way. A game over the network (`gbMultiPlayerMode`) plays the rules
its room's launch names, the host's choice, which every seat is told (`port_lanlobby.cpp`, since
v0.6.8-beta); one made on 1.17's own screens names none and plays the classic rules. Hyperion's
rules come on anyway when a mod's data loaded, teams were chosen, a computer plays at a level
other than Classic, a player has a handicap or the game has a mutator. `HxLimits()` is the separate question of whether the game is past 1.17's
limits (slots past 7, a map past 256 tiles a side); every such game plays by Hyperion's rules,
and so does a new game on an EUD map (`gameplay.md`).

The options recorded so far are `HX_OPT_PATH_BUDGET_117`, `HX_OPT_TEAMS`, `HX_OPT_AI_LEVELS`,
`HX_OPT_STRESS`, `HX_OPT_EUD`, `HX_OPT_ORDER_QUEUE`, `HX_OPT_SELECTION`, `HX_OPT_HANDICAP`
(each slot's units at its share of their maximum life and shields, `gameplay.md`,
"Handicaps"; the slots' percents in `HCAP` and `HXHC`) and `HX_OPT_MUTATORS` (the
game's mutators, `gameplay.md`, "Mutators"; the set in `MUTS` and `HXMU`); a replay or save of
an EUD game plays it as one from its rule record, whatever the map's triggers are.
`HX_OPT_SELECTION` is Hyperion's selection (`hud.md`): selections and groups of up to
255 units, own buildings together and SC2's subgroups. Every new game by Hyperion's rules
records it but one on an EUD map, whose triggers read 1.17's arrays. Its selections travel as
0x8C, chunks of up to 60 unit ids (`net_cmds.h`), which the recorder, the player and every
parser of commands take (`NetCmdWideSize`), and its save keeps every human's selection and
groups in `HXST`. A replay or save without it selects 12. `HX_OPT_ORDER_QUEUE` is StarCraft:
Remastered's order fixes, which every new game records; it is an option rather than a
revision so that the replays and saves of revision 16 made before it still play, without them.
In `CUnit::SetOrder_Target_type_interrupt` an order the game gives a unit is dropped when the
unit's queue holds 8 or the order pool is at 90%, as a player's queued command is, unless the
unit's order is UndoTransparentWorker (0x97); and an SCV putting up a building (0x21) with a full
queue still gets UndoFlyer (0x96), which brings it down from the air. As in Remastered, an
UndoFlyer meeting only the pool's limit is still dropped. hxsuite's `h16_xl1800_queue` records
the option, with the local player's commands queued (`--record-orders 24,queue`).

### Recording

`ReplayHxNoteGame` runs as a game ends, before its map closes, and remembers whether the game
played by Hyperion's rules; `write_replay` then writes `hxRS` and calls `ReplayHxWrite` after the
map block, or writes 1.17's replay unchanged. While the game is played, `ReplayHxFrame` (called
from `gameloop.cpp` after each frame's update and triggers) computes the state's hash every 24
frames and appends it, and `ReplayHxSaved` notes the frame whenever the game is saved.
`PortReplayKeep` then copies `LastReplay.rep` into `maps\replays\AutoSave\`, named by the time and
the map, unless the `keepReplays` setting is off.

`--record=<frames>[,<path>]` plays a game with nobody at the keyboard: at the frame, or sooner if
the local player has lost, `PortRecordFrame` leaves the game through `gameloop_init(4)` as Quit
Mission does, so the replay is written as after any game, and `PortRecordAfterGame` copies it to
the path and ends the process (under the harness, with `HYPERION_DLLS` set, it goes back to the
menus instead). `--record-orders=<n>` has the local player give orders meanwhile, so the replay
holds commands that name units: up to 12 of its units selected, then a right-click or a move on
another player's unit, a move to the ground every fourth round, a mod's spell or research every
fifth. With `--seed`, two recordings of one game are the same file byte for byte.

A command whose order, tech or upgrade is past 255, a mod's, is recorded in a wide
form with the id in a WORD: Action 0x96, Placebox 0x8D, Research 0x97, Upgrade 0x98
(`net_cmds.h`, `NetCmdWideSize`). Only such an id is sent in one, so a game of 1.17's ids
records 1.17's commands.

### Playback

`LoadReplayData` reads the three blocks, then `ReplayHxRead` for an `hxRS` file (and
`ReplayHxForget` for a 1.17 one, which sets classic rules). `ReplayHxRead` applies the rule record
or refuses the replay, and keeps `SLOT`, `COLR`, `RGBS`, `RACE`, `AILV`, `HCAP`, `MUTS`, `STRS`, `SYNC` and `SAVE`
for the game that follows. As the game sets up, `ReplayHxAssignSlots` fills the slots past 12 and
`ReplayHxColors` puts back every slot's colour, the first eight included, so a colour chosen in
the set-up survives playback. `ReplayHxGameStart` then clears what is recorded or checked.

1.17's reader loses commands: when the next recorded frame is ahead of the game the frame is read
one call early, and if its commands do not fit in the 0x200-byte turn buffer the growth is freed
with the call's `CNetUnkn`, so the next call dispatches stale bytes (`replay-test.md`, "The restart
in M1G2-Leta"). `port_replay.cpp` keeps the grown buffers across calls, so such a frame is
dispatched whole, and a replay that never grows the buffer plays exactly as before.

### The check

While a Hyperion game is played its hash goes into `SYNC`; played back, `Check` in
`port_hxreplay.cpp` recomputes the hash at each frame the replay holds one for and compares every
domain plus the random state and count. The first difference sets `REPLAY_SYNC_MISMATCH` with the
frame and a bit per domain, logged to `hyperion_replay.log`. A frame the playback never reached
leaves its check unmade, which the harness counts as a failure. `ReplayHxSync` publishes the
status, which the live channel and the harness's observer both read. To find what a difference is,
`HYPERION_SYNC_DUMP=<frame>,<folder>` writes each domain's bytes as hashed at that frame, `live.*`
from a game and `replay.*` from its replay. The hash itself is `loop.md`.

### Saving and loading

`saveload_write_save_data` writes 1.17's parts in the port's sections, and for a Hyperion game
`HxSaveSetUp` and `HxSaveState` add theirs. `HXST` holds what 1.17's save leaves out, each found by
comparing a loaded game with the live one: the random state and count and the C runtime's `rand`
state; the order of the free and used lists of units, sprites, images, thingies and bullets, which
a load would otherwise rebuild in slot order; the counters between the game's passes (the trigger
pass and clock, the leaderboard, the victory check, the fog and power update, the order slots, the
creep, the AI's); the AI's demands, whose pointers are saved as the pool or captain table they
point into and an offset; the player tables' extra entries and the vision of the slots past 7; the
cargo lists of a game with wide unit ids; and in a game with rich bases, the resources that turned
rich and each unit's rich byte (`RichBasesSave`, `RichBasesLoad`), after the rest, which `HXMU`,
read first, says is there.

The AI's six pools (`h/aipools.h`) are saved where 1.17 saves them. A game of 1.17's
sizes writes each whole in 1.17's shape, its records and then its free head, through a copy
(`TAiPool117`, `AiPoolSave`, `AiPoolLoad`), since the storage on 64 bits holds 262,144 records
and a layout of the storage would put the head past them; `save_layouts.py` makes the pool
layouts from that copy, and `port_synchash.cpp` asserts their sizes. A game of grown pools
writes its records in their element layout (`SAVE117_Soldier` and the others) and then the head,
an id in 4 bytes (`AiPoolCompress`); the save's `HXUN` gives the table before the pools are read.

A mod's order, weapon, tech and upgrade ids pass 255, and x64 keeps them in 16 bits
where 1.17's records have a byte: a unit's orders and research, a bullet's weapon, an order's
target. The save keeps 1.17's layout, that byte (`SAVE117_UNSIGNED8`), and `HXWI` the high
bytes. CUnit's +0xC8 is a union of a pointer and the bytes of a building's tech and upgrade, a
carrier's or reaver's counts: `SAVE117_PTRBYTES` saves the pointer, or the bytes by a layout of
their own, by the test `CUnit::SaveEncode` makes (`PortSaveUsesPointer`). An image's three
offsets into the scripts (`iscript_off_t`) are 32 bits on x64: the save keeps their
low halves where 1.17 has its WORDs (`SAVE117_OFFSET16`, `tools/save_layouts.py`'s
`SCRIPT_OFFSETS`) and `HXWI` the high halves.

Loading goes the other way: `HxLoadSetUp` reads `HXGM` before the game starts (a save without it
is 1.17's, which turns the extra player entries off and sets classic rules),
`HxSaveBeforePsiLinks` puts the sprite and image free lists back before `RestorePsiLinks` rebuilds
the power fields, so the fields take the slots they took in the game saved, and `HxLoadState` reads
`HXST`. Once the game is set up, `HxSaveLoaded` restores the random state, compares the state with
the hash saved and logs the verdict; `HxSaveGoesOn` makes `gameloop.cpp` skip the two game updates
a start would run, and `HxSaveRunGame` restores the fog countdown a start would set to 1.

Saving changes the game it is taken from: the power fields come down and go up again and the
collision boxes are sorted again. A replay of a game that saved therefore notes those frames in
`SAVE` and calls `HxSaveReplay` at each of them on playback, which writes a save to
`hyperion-replayed-save.tmp` in the temporary folder (`HxSaveTarget` redirects the path and
suppresses the wait dialog) purely for the effect on the game.

### Snapshots for the trainer

A snapshot is the simulation's state copied in memory at a game frame's end and put back in place
in the same game, so every pointer stays good: the parts of 1.17's two global blocks that are the
game's (`gen/snapranges.h`, from `tools/snapranges.py`), the heap blocks a game allocates as it
starts (noted by the allocator through `PortSnapAlloc` for the sites in `kBlocks`), the port's own
statics that say so with `PORT_SNAP_KEEP`, the replay recorder or reader by its place in its
buffer, the replay feed's grown turn buffers and the single-player loopback's queues. Everything
that is not the game, from windows to the camera, is left alone.

A snapshot is a list of 4 KB pages and shares each page equal to the previous snapshot's, so only
what changed is stored. Snapshots are kept by line, a line being one of the trainer's timelines;
`PortSnapShare` shares a parent's up to a branch, and `PortSnapRestore` puts back the latest at or
before a frame and tells `ReplayHxRewound`, so the replay's checks and recorded hashes follow the
game back. The trainer takes its own snapshots every `TRAINER_SNAP_EVERY` (120) frames and at each
retry point and branch (`port_trainer.cpp`); `PortSnapFrame`, called from the loop, only does
something when the self-test is on.

The self-test is `HYPERION_SNAPTEST=<period>,<back>[,<diff frame>]` on any replay run: a snapshot
every 120 frames, a rewind of `back` frames at each period, and the demand that every frame played
again hash as it did the first time and every snapshot frame find the whole state byte for byte as
it was, with the drawing's own bits (`ViewBits`) left to the hash. It writes `snaptest.txt`, plus
`snapstate.txt` and `snapdiff.txt` for the first difference. `port_snapprobe.cpp` is a separate
read-only probe, `HYPERION_SNAPPROBE=<a>,<b>`: the heap blocks a game holds and the bytes that
changed in 1.17's `.data` and the wide block between two frames (`snapprobe.txt`).

`gen/snapranges.h` gives the global blocks' parts as offsets, so it holds only while the blocks'
layout does. It was written once on 2026-10-18 and the wide block grew after (the pools sized for
the largest game), so from then until 2026-10-25 a restore copied the wrong bytes and left most of
the unit table as it was: the first rewind of the trainer, or of the self-test, crashed in
`CUnit::PathInit` on a unit with no sprite. `build_engine_x64.py` now runs
`tools/snapranges.py --if-stale` before it compiles, which writes the header again when the layout
moved and says so; the new header is then to be committed with the change that moved it.

### Moving through a replay

A HUD layer whose action is `replay.seek` (`modules/hud.md`, Replays) moves the replay to any
frame, back or on; `port_replayseek.cpp` does the moving, on the trainer's snapshots.

- **The snapshots.** While a replay plays under a design that can move it (`PortHudCanSeek`), the
  loop's frame end (`PortReplaySeekFrame`) takes a snapshot on a line of its own (`0x5EE0`) at
  the first frame it can and then every 240 frames. They share their unchanged pages, so a
  16-player game keeps about 130 MB over 3,600 frames; past 768 MB the step doubles and every
  other one goes (`PortSnapThin`), the first kept. They are dropped when the game ends
  (`PortReplaySeekAfterGame`).
- **A move.** `PortReplaySeekTo` puts back the latest snapshot at or before the frame when the
  frame is behind the replay, or when a snapshot lies between the replay and the frame, and then
  lets the loop run the replay's own commands at once to the frame (`PortReplaySeeking`): no rest,
  a picture every 50 ms so the move is seen, the sound off and put back at the end. The replay
  plays on from a snapshot as it plays from the start, so a frame reached by a move is the frame
  the replay plays, and its recorded checks follow it back (`ReplayHxRewound`). What is the
  viewer's is kept across the put back: the speed and its multiplier, the pause, the players'
  vision shown and Show All, the selection's console copy refreshed and the unit under the cursor
  forgotten. A move plays past the replay's pause and leaves it paused.
- **Where it cannot.** A game watched as it is played (a LAN room's match, the lab's, a watched
  skirmish), a trainer session, and a replay with no length. A frame before the first snapshot is
  taken as the first snapshot's.

### Replay TV

`port_tv.cpp` moves the camera on its own while a replay plays, and never touches the game's own
view. Every four game frames it scores the map in squares of 384 pixels: a unit that lost hit
points or shields, or that is gone since the last look, heats its square, every unit warms it a
little, and the heat cools each look. It looks at the hottest square and the eight round it, holds
a shot at least 4.5 seconds unless a fight elsewhere is three times as hot, fits the zoom to the
box of what happened there, and pauses for six seconds after anything the viewer does. The camera
reads the aim through `PortTvTarget` (`port_view.cpp`); the TV hotkey and the benchmark's action
camera toggle it, and a LAN match watched a delay behind gets the room's read-ahead hint through
`PortTvHottest`. `PortFollowToggle` and `PortFollowAt` keep the selected unit in the middle until
it dies.

## The replay harness in brief

`tools/replaytest/replaytest.py` is the gate for anything in this area; `replay-test.md` has the
detail.

- `suite` plays the ten 1.17 replays in `testrun\rt_replays\modes\` and `sscait_pick\` on the
  untouched 1.17 oracle and on the build under test, logs the game state every frame and reports
  the first frame that differs. `suite --quick` takes three of them. **The gate rule**: every
  replay must stay identical to the original on every frame, or, for a replay where 1.17 loses
  recorded commands, through the first such frame, since from there Hyperion plays the recorded
  commands and the oracle does not.
- `hxrecord` records the Hyperion set from `hxsuite.json` (23 games) with `--record` and `--seed`,
  keeping each game's live log as the replay's reference and saving at the frame an entry names as
  its `checkpoint`. `hxsuite` plays each of those replays on `hx64` and fails it when the replay
  is refused, when a check differs or is left unmade, or when its log differs from the live game's;
  for an entry with a checkpoint it also loads the save and holds the loaded game to the same log.
- `loadmatrix.py` and `savecompare.py` are the save checks: every build saves at one frame, every
  build loads every save, and the saves themselves are compared item by item.
- `compare.py` plays any replays on 1.17 and on Hyperion and shows the comparison for a person to
  read: snapshots with both builds' hashes and each player's numbers, the end of the game from both,
  and the game's result, in the terminal and as a page (`replay-test.md`, "Comparing any replay").

## Settings and environment variables

| Name | Where | What it does |
|---|---|---|
| `--replay <file>` | command line | plays a replay without the menus; refuses one whose rules, data or revision are not this build's |
| `--load <file>` | command line | loads a saved game, 1.17's or Hyperion's, as the Load screen's Ok does |
| `--record=<frames>[,<path>]`, `--record-orders=<n>` | command line | plays a game by itself to that frame and keeps its replay, the local player giving orders every n frames (`port_record.cpp`) |
| `--seed <n>` | command line | seeds a new game with n in place of the clock, so a recording repeats |
| `--rules classic\|hyperion`, `"rules"` | command line, `settings.json`'s `hyperion` | the rules a new game plays by; the setting applies when the option is absent |
| `--trainer` | command line | makes the game a trainer session (snapshots and timelines) |
| `"keepReplays"` | `settings.json`, `hyperion` | Settings' "Save a replay of every game"; on unless set to 0 |
| `"checkModFingerprint"` | `settings.json`, `hyperion` | off lets a replay or save play with another version of its mod's data |
| `HYPERION_SYNC_DUMP=<frame>,<folder>` | environment | each domain's bytes as hashed at that frame, from a game (`live.*`), its replay (`replay.*`) and a save (`saved.*`, `loaded.*`) |
| `HYPERION_SNAPTEST=<period>,<back>[,<diff frame>]`, `HYPERION_SNAPPROBE=<a>,<b>`, `HYPERION_TRAINERTEST=<period>,<back>` | environment | the snapshot self-test (`snaptest.txt`), the memory probe (`snapprobe.txt`) and the trainer's rewind self-test (`trainertest.txt`) |
| `HYPERION_TV=1` | environment | Replay TV on from the start of a run |
| `HYPERION_PATH_BUDGET=117` | environment | keeps 1.17's search budgets on a larger map, recorded as `HX_OPT_PATH_BUDGET_117` |
| `HYPERION_DLLS` | environment | set by the harness; `--record` then returns to the menus instead of exiting |

Logs: `hyperion_replay.log` (what a replay checked, and why one was refused),
`hyperion_save.log` (a load's comparison with the hash saved), `snaptest.txt`, `snapprobe.txt`.

## Integration points

- `lang/gameloop.cpp`, at a frame's end: `ReplayHxFrame` (the check, then the LAN game's
  per-player hashes, then the harness's frame hook, then the saves a replay makes again), then
  `PortSnapProbeFrame`, `PortSnapFrame` and `PortReplaySeekFrame`. While a replay is moved
  (`PortReplaySeeking`) the loop runs frames at once, past the replay's pause, without resting,
  and draws only when `PortReplaySeekPictureDue`; `main.cpp` calls `PortReplaySeekAfterGame` as
  a game ends.
- `lang/game.cpp`: `ReplayHxNoteGame` and `PortReplayKeep` as a game ends; `ReplayHxAssignSlots`,
  `ReplayHxColors`, `HxSaveColorsApply`, `ReplayHxGameStart` and `HxSaveLoaded` as one starts.
  `lang/replay.cpp`: `ReplayHxRead`/`ReplayHxForget` and `ReplayHxWrite`, the last also in
  `replay_live_parts`, which `port_lanfeed.cpp` uses to send a LAN match's replay as it is played.
  `lang/saveload.cpp`, `CUnitSave.cpp`, `SAI_Banker.cpp` and `sai_LoadSave.cpp` hold the save and
  load hooks.
- `port_observe.cpp` is the harness's whole view of an engine build (`OC_OBSERVE_VERSION` 11): the
  game state, the used units in 1.17's widths, every slot's numbers, the unit table's size, the
  check's status, the hash, the save and pinned seed, and the per-frame hook.
- `port_front_replays.cpp` (the Replays screen) and `port_front_trainer.cpp` read a
  file for a list with `ReplayHxPeek`. `port_control.cpp` publishes `status`'s `sync` object and
  the `hash` command, `port_trainer.cpp` builds its lines on `port_snap.h`, and `port_bench.cpp`
  makes a benchmark's replay.

## Gotchas

- A change to what a Hyperion game does needs `HX_SIM_REVISION` raised and a line in `hxsim.h`;
  every earlier Hyperion replay and save is then refused and the harness's set has to be recorded
  again. A change to how the hash is made needs `SYNC_HASH_VERSION` raised instead, which leaves
  older replays playable but unchecked, and `hxsuite` fails them for unmade checks.
- A classic game's replay is 1.17's byte for byte, so it has no rule record, no state checks and
  no place for a chosen RGB colour, and a trainer session watching one is held to nothing. A
  benchmark's replay (`PortBenchMaking`) carries neither `SYNC` nor `SAVE`, since its local slot
  plays as a computer.
- Every save Hyperion writes is in the port's container, even for a classic game, so 1.17 cannot
  read it. The reverse works: Hyperion reads 1.17's saves. Loading one into a game already
  running is not possible, in 1.17 either (the oracle crashes in `find_units_in_rect`).
- Saving perturbs the game, which is why `SAVE` and `HxSaveReplay` exist. A save taken on the very
  frame the local player gives orders still goes its own way on load.
- `PortSnapFree` drops every line's snapshots, since the heap blocks belong to one game, and a
  restore after the blocks changed returns `~0u` rather than corrupting the game.
- `gen/snapranges.h` follows the global blocks' layout: a global added to a game TU or a pool
  grown moves it, and a stale header breaks every restore. The build writes it again when it is
  stale; commit it with the change. The gate does not run `HYPERION_SNAPTEST`, so a snapshot
  that misses some state shows only in the self-test or a rewind.
- `ReplayHxPeek` caches the data fingerprint per game (Brood War or vanilla); a replay of the
  other game is checked in the list for its rules and revision only, and fully when it plays.

## Open items

- Memory over a long trainer session: a 64-player game on the largest maps changes about 16 MB of
  pages between snapshots, and a 16-player session of 6,000 frames over 5 lines held 197 MB, so
  older snapshots will want thinning or compressing.
- A save made at a point in a frame other than where the save menu runs would need its place noted
  in `SAVE`, not just its frame.
- Commands from the slots past 7 in a Hyperion replay wait on multiplayer for those slots; only
  computers sit there so far.
- The lobby's choice of rules, how a multiplayer game agrees on them, and later rule versions
  ("1.23.10" beside "1.17" and "hyperion"), each with recorded games of its own.

## Further reading

- `loop.md`: the state hash, its domains and versions, and the loop hooks this doc calls from.
- `../replay-test.md`: the harness, the suite, the Hyperion set, the observer's log and the save
  checks, in full.
- `../hyperion-exe.md`: the command line, the data folder, and where replays and saves live.
- `../hud-format.md`: the HUD layers a replay drives, the slider among them.
