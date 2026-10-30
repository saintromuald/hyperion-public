# gameplay: starting games, players, computers, triggers, the trainer and keys

What the port adds around 1.17's simulation so a game can be played: the ways a game is
started, the player slots past 1.17's eight, the boundary the engine reaches the computer
player through, the trigger actions run outside a trigger, the trainer's sessions and tape,
the keys, and the pool and map limits raised past 1.17's. The simulation itself is
`src117/lang/`, the reconstructed C++; everything here is in `src117/port/` and in the
headers that name the new limits. Nothing in this area replaces a rule of 1.17's: a game that
stays inside 1.17's limits and picks no Hyperion option plays as 1.17 does, to the frame, and
the replay suite is the gate for that (`../replay-test.md`).

## Status

All of it is on master and built: the launches (`port_launch.cpp`); players past eight, up to
64, with their tables, colours, saves and replays (`../maps.md`); pools sized per
game and unit ids past 2,047 (stage C); triggers that name the slots past 11
(simulation revision 10); the computer player's boundary and the levels Classic to
Cheater (stages B and C); the trainer; hotkeys K1 to K3
with Hyperion's own function keys; handicaps, StarCraft II's, in skirmishes and
rooms; mutators, one set for every player; and
a skirmish of computers watched, the player's own seat a computer's (v0.6.14-beta).
Not built: the computer's play styles, so `AI_STYLES` has one entry.

## Limits today

| What | 1.17 | Hyperion |
|---|---|---|
| players | 8 in the lobby; 12 slots (8 to 10 a map's own, 11 neutral) | 64 players in slots 0 to 10 and 12 to 64, 65 slots in all (`PLAYERS_MAX`, `PLAYER_SLOTS_MAX`, `src117/h/playerlimits.h`) |
| units | 1,700; a unit id holds its index in 11 bits, so 2,047 | 1,700 times k, k the players over 8 rounded up and at most 8, so 13,600; past 2,047 units the id's index is 24 bits (`POOL_UNITS_117`, `UNIT_ID_SHIFT_WIDE`, `src117/h/poollimits.h`) |
| sprites | 2,500 | 2,500 k, so 20,000. Images 5,000 k, bullets 100 k, thingies 500 k a pool, orders 2,000 k |
| map tiles | 256 a side | up to `MAP_SIDE_MAX`, 1,024 a side, which `maphdr.cpp` refuses above; 512 is as far as the pathing is tuned and tested, by the decision of 2026-10-15 |
| triggers | one trigger list a player, eight of them; a trigger's player codes 0 to 26 | one list a slot, 65 of them (`sgTriggers` is a player table of 8 in `victory.cpp`); the codes 0 to 26 keep their meaning and a slot from 12 up is named `0x100 + slot` (`TRIG_HX_CODE`, `src117/h/TrigEnum.h`) |

A game on a 1.17 map with no player past 7 keeps 1.17's sizes exactly. The sizes are not
recorded: they follow from the map's slots and the simulation revision, which both a save and
a replay carry (`src117/h/hxsim.h`).

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `../../src117/port/gameplay/port_launch.cpp` | every way into a game without the menus, each modelled on the menu steps it stands for, and the skirmish set-up's choices | `PortLaunch`, `LaunchReplay`, `LaunchMap`, `LaunchScenario`, `LaunchLoad`, `PortLaunchSkirmishGame`, `PortLaunchSkirmishChoices`, `PortLaunchTrainer`, `PortLaunchBench`, `PortLaunchMapFile`, `PortLaunchCanvas` |
| `../../src117/port/gameplay/port_watchgame.cpp`, `port_watchgame.h` | a skirmish watched: the game made, its replay played with the whole map shown at the speed chosen, and its end | `PortWatchBegin`, `PortWatchMaking`, `PortWatchPatchHeader`, `PortWatchFrame`, `PortWatchAfterGame`, `PortWatchLaunch`, `PortWatchReplayStarts`, `PortWatchLive` |
| `../../src117/port/gameplay/port_session.cpp`, `port_session.h` | the session: the choices a single-player game is launched with, kept from the launch until the game is left | `PortSession`, `Session`, `SessionClear`, `SessionAfterGame` |
| `../../src117/port/gameplay/port_players.cpp` | the slots past 1.17's: the flag, a map's `HXPL` and `HXRG` sections, the computers past seven, colours, the save of the extra table entries | `PlayersMapBegin`, `PlayersMapHxpl`, `PlayersMapHxrg`, `PlayersMapSlotCount`, `PlayersExtraComputers`, `PlayersSeatNote`, `PlayersSeatedComputer`, `PlayersChooseColour`, `PlayersChooseColourRgb`, `PlayersColourRamp`, `PlayersColorsApply`, `PlayerTablesSave`, `PlayerTablesLoad` |
| `../../src117/h/playerlimits.h` | the slot counts and the player table templates: 1.17's entries at the global's own address, the new slots' beside them | `PlayerSlots`, `PlayerLobbySlots`, `PlayerIsLobby`, `PlayerTable`, `PlayerGrid`, `PlayerMatrix` |
| `../../src117/port/gameplay/port_teams.cpp`, `port_teams.h` | the teams and start locations a set-up chooses, the alliances they make, and the team layouts and names the set-up screens offer | `TeamsChoose`, `TeamsChooseList`, `TeamsChooseStart`, `TeamsSeat`, `TeamsAlliances`, `TeamsStartFor`, `TeamsLayouts`, `TeamsShades`, `TeamsAdd` |
| `../../src117/port/gameplay/port_score.cpp`, `port_score.h`, `port_scoregame.h` | the score of a game for the score screen: who played and the four tabs' numbers, kept as the game ends, with the time each player went out; the screen's rank and count-up rules; stand-in games for checks | `PortScoreGameStart`, `PortScoreFrame`, `PortScoreLeft`, `PortScoreGameEnd`, `ScoreRead`, `ScoreStandIn`, `ScoreBefore`, `ScoreScaleOf`, `ScoreStep` |
| `../../src117/port/gameplay/port_ai.cpp`, `port_ai.h` | the computer player's boundary and the levels as knobs on 1.17's AI | `AiChoose`, `AiSeat`, `AiForGame`, `AiFrame`, `AiVisionRefresh`, `AiThinkFrames`, `AiScriptWait`, `AiAttackCount`, `AiAttackAllowed`, `AiWorkerTarget`, `AiHarvestExtra`, `AiProfilesSave` |
| `../../src117/port/gameplay/port_handicap.cpp`, `port_handicap.h`, `../../src117/h/handicap.h` | the players' handicaps: the choices, a launch's and a room's, the game's percent a slot, and the maximums the game's code asks for | `HandicapChoose`, `HandicapChooseList`, `HandicapChooseNet`, `HandicapForGame`, `HandicapLife`, `HandicapShields`, `HandicapOwnerChanged`, `HandicapsSave` |
| `../../src117/port/gameplay/port_mutators.cpp`, `port_mutators.h`, `../../src117/h/mutators.h` | the game's mutators: the choices and presets the set-up lists, a launch's, `--mutators` and a room's, the game's set, and the values the game's code asks for | `MutatorsChoose`, `MutatorsChooseList`, `MutatorsChooseNet`, `MutatorsForGame`, `MutatorResource`, `MutatorEndless`, `MutatorFoodMade`, `MutatorSupplyCap`, `MutatorWorkers`, `MutatorStartResources`, `MutatorsSave`, `MutatorsJson` |
| `../../src117/port/gameplay/port_richbases.cpp`, `port_richbases.h` | rich bases, the tenth mutator: the map's read into bases, mains, naturals and walking distances, the roll, the units that turned rich and the loads from them, the replay's record and the save's, and the look each gets | `RichBasesMapLoaded`, `RichBasesFrame`, `RichBasesResource`, `RichBasesCarry`, `RichBasesRecord`, `RichBasesRecorded`, `RichBasesSave`, `RichBasesLoad`, `RichBasesTable`, `MutatorTripOf`, `MutatorCarryOf` |
| `../../src117/port/gameplay/port_trigrun.cpp`, `port_trigrun.h` | 1.17's trigger actions run outside a trigger, through two scratch locations | `PortTrigCreate`, `PortTrigOrderNear`, `PortTrigOrderId`, `PortTrigRemoveAll`, `PortTrigRemoveId`, `PortTrigTransmission`, `PortTrigScratch`, `PortTrigUnit` |
| `../../src117/port/gameplay/port_eud.cpp`, `port_eud.h`, `../../src117/gen/eud_code116.inc` | EUD maps, played as StarCraft: Remastered plays them: the load-time test, the death counts through a map of 1.16.1's addresses onto Hyperion's data, the trigger lists and string section at 1.16.1 addresses, the pools a map's unlimiter grows, the command cards a map points at its own buttons; the 1.16.1-to-1.17 function pairs (`../../tools/eud_code116.py`) | `EudMapTrigger`, `EudMapTriggersRead`, `EudGameStart`, `EudDeaths`, `EudSetDeaths`, `EudRead`, `EudWrite`, `EudTriggersFirst`, `EudTriggerNext`, `EudTriggersDone`, `EudSavesBlocked` |
| `../../src117/port/gameplay/port_cutscene.cpp`, `port_cutscene.h` | cutscenes scripted over the live channel: steps run in turn on the motion's clock, moving the camera, the zoom, the picture and units (through `port_trigrun.cpp`); the player's input is held while one plays, and Escape ends it | `PortCutscenePlay`, `PortCutsceneTick`, `PortCutsceneAim`, `PortCutsceneStop` |
| `../../src117/port/gameplay/port_trainer.cpp`, `port_trainer.h` | a session's lines, the playhead, the seeks, the figures a game second, the marks, the state checks and the saved `.hxtrain` | `PortTrainerGameStart`, `PortTrainerFeed`, `PortTrainerCommand`, `PortTrainerFrameEnd`, `PortTrainerRetry`, `PortTrainerBranch`, `PortTrainerScrub`, `PortTrainerGoOn`, `PortTrainerLeave`, `PortTrainerFromMenus` |
| `../../src117/port/gameplay/port_trainerview.cpp` | the trainer's screen: the playing strip, the Tape, the sheet and the lane figures | `PortTrainerViewShown`, `PortTrainerViewKeyAs`, `PortTrainerViewTape`; a layer over the game (`render.md`) |
| `../../src117/port/gameplay/port_hotkeys.cpp` | the profile's `hotkeys.json`: the command card's keys, the accelerator commands, the control group keys | `PortHotkeysLoad`, `PortCardKey`, `PortCardTooltip`, `PortCardLayout`, `PortAccelRemap`, `PortKeyGroup` |
| `../../src117/port/gameplay/port_keys.cpp` | Hyperion's own function keys (art, view, zoom, follow, replay TV, the debug overlay) | `PortHotkey`, `PortHotkeySet`, `PortHotkeyFree`, `PortHotkeyPressed`, `PortHotkeyName` |
| `../../src117/port/loop/port_pools.cpp`, `../../src117/h/poollimits.h` | the pool sizes and the position width for the game being played | `PoolsForGame`, `PoolsWideIds`, `g_pools`, `g_posShift` |

Three files sit beside these but belong elsewhere: `port_veclen.cpp`, the simulation's integer
distance; `port_names.cpp`, which names the port's drawing and distance bodies for the game's
C++; and `port_pathstats.cpp`, the pathing's counters for the live channel and `pathbench`.

## How it works

### A skirmish, from the front end down to the game loop

`StartGame` on the skirmish screen (`port_front_skirmish.cpp`) does what Play Custom's Ok
does. It takes the set-up as a `SkirmishGame` record (`SkirmishRecord`), calls 1.17's
`CreateNewGame` with the map and the game type's key, and makes the record's choices with
`PortLaunchSkirmishChoices` (`port_launch.cpp`), which a trainer session's game
(`PortLaunchSkirmishGame`) makes the same way: `TeamsChoose` a team a row, `AiChoose` a level
a row, `TeamsChooseStart` a start location a row, `PlayersChooseColour` or
`PlayersChooseColourRgb` a colour, `ModRaceChoose` and `ModRaceChooseComputer` a mod's race,
`PlayersSetExtraComputers` the computers past seven, and `RulesChooseNext` the rules. Each
owner writes its choice into the session (below). Beside them `byte_526546_g` and
`byte_526547_g` take the races Play Custom keeps, the local player's and up to seven
computers'. A row is 0 for the person and `1 + k` for the k-th computer, the same numbering
everywhere in this area.

The menus then set game mode 1, `RunGame` makes the game, and `SinglePlayerMeleeInitGame`
(`game.cpp`) seats it: `PlayersSeatsBegin`, `seat_computer_player` for each of 1.17's seven
and once more for each `PlayersExtraComputers`, then `TeamsSeat` and `AiSeat`, which hand the
rows' choices to the seated slots. `game_init` then runs `RulesForGame`, `TeamsAlliances`,
`ModRacesForGame`, `AiForGame` and `PoolsForGame` in that order, before any pool is built, so
the rules and the sizes are settled before the first object exists.

`PortLaunch` is the same thing without menus. It runs once from `RunGlues` (`glues.cpp`) with
the options `port_paths.cpp` pulled out of the command line: `--replay`, `--map` with
`--race`, `--computers`, `--computer-race`, `--teams`, `--starts`, `--ai-levels`, `--rules`
and `--ums`, `--load`, or `--canvas`. The replay suite starts its Hyperion runs this way so a
run waits on no menu. The other entry points in the table above serve the front end, the
benchmark, the trainer and the live channel; every one of them ends in `LaunchDone`, which
either sets game mode 1 or frees the map, puts the expansion flag back and clears the session.

### Watching a skirmish of computers

1.17 has no observer. Its slot type 9 is handled in places and never given, the defeat
dialog's Become Observer is always hidden, and `IsTVBGame` is the Top vs Bottom game type.
A game that is not a replay always has a local person, whose defeat or victory ends it. So
Hyperion watches a game the way the benchmark does (`../../src117/port/tools/port_bench.cpp`):
it makes the game, keeps its replay with the local slot a computer's, and plays that. A
computer's play is part of the simulation, so a replay with no commands plays the whole game.

On the skirmish screen the player's own seat offers "You" or "Computer", in Skirmish's own
set-up only (not a room's or the trainer's, `WatchOffered`), and the Observers frame under the
players has the observer's slot, a row drawn as a player's (`ObserverRow`): "You" there makes
the seat a computer's, "Empty" gives it back (the user, 2026-10-25: "if the user is set to
observer mode/slot that it shows as such like in the other slots"). The seat
then has a computer's race, level and handicap, Melee plays as Free For All (a single-player
Melee puts every computer on one side, against the player's seat), Use Map Settings puts the
player back in the seat, and Game speed goes on past Fastest to Fastest ×16, the replay's own
steps. `SkirmishRecord` records it as `bObserve` with the multiple, and the player's row keeps
its level for `AiChoose` (`PortLaunchSkirmishChoices`); `AiForGame` gives row 0 to the local
slot as it does for any game.

`StartGame` calls `PortWatchBegin` and makes the game as usual. From there
`port_watchgame.cpp` steps through the benchmark's places, each hooked beside the benchmark's:

| Phase | Where | What |
|---|---|---|
| making | `gameloop.cpp` (`PortWatchFrame`) | at frame 1, `gameloop_init(4)`, as Quit Mission leaves; no tips (`gameloop.cpp`), no countdown (`port_intro.cpp`) |
| making | `replay.cpp` (`write_replay`) | the header's local slot made a computer's, no network id and type 1 (`PortBenchPatchLocal`), its length 0x7FFFFFF0 so 1.17's end of a replay never comes; no commands |
| making | `port_hxreplay.cpp` | no state checks or saves in it, no AutoSave copy (`PortReplayKeep`) |
| to watch | `main.cpp` (`PortWatchAfterGame`) | `LastReplay.rep` moved to the data folder's `cache\watch.rep`, which the watch plays, and the map's name kept |
| watching | `glues.cpp` (`PortWatchLaunch`), `game.cpp` (`PortWatchReplayStarts`) | the replay started in the menus' place, then Show All and `replay_set_speed` at the speed chosen |
| ended | `PortWatchFrame` | every 24 game frames, `TeamsSidesLeft`; at one side left, the replay pauses, the stats' side view opens, and `do_victory_dlg` raises the game menu's end box (`PortGameEndKind`'s `GM_END_WATCHED`): "Game Over", the winners and the time as the replay's readout counts it (`replay_dlg_elapsed_seconds`, `PortWatchResult`), Leave Game, which leaves as Exit Replay does (`PortQuitToMenu`), and Keep Watching, which closes it and plays on (`PortGameEndChoose`) |
| over | `main.cpp` (`PortWatchAfterGame`) | `LastReplay.rep` written from the watch's copy with its length the last frame the watch reached (`WriteWithLength`: the first compressed block's bytes, the header compressed again, the rest as it was), and kept in AutoSave under the map's name (`PortReplayKeep`), so the match plays again from the replays screen as any game's replay does |

The front end's skirmish flag stays set through the replay, so leaving it (the end box's Leave
Game, or F10, End Mission, Exit Replay) goes back to the skirmish screen with its set-up. Up to
v0.6.14-beta's first build the made replay stayed in `LastReplay.rep` with its open length, and
a copy in `maps\replays\Observed`, so the replays screen showed a game of some 3 years
(the user, 2026-10-25: "see latest reply with insane length reflected"); and the end was a
message line on the map. `HYPERION_FRONT_WATCH` starts a
watched game from the set-up for a check without a person (`front-end.md`). `TeamsSidesLeft` (`port_teams.cpp`) is the
benchmark's count of the sides with units, a team once, now shared by both; it also counts
1.17's teams in a team game.

The HUD knows the game as watched: `game.observer` and `replay.live` are true
(`PortWatchLive`), the position reads all of the game so far, and the stats show each
computer's level after its race (`../modules/hud.md`). `game.victory` and `game.defeat` do not
fire in a replay, so the recorded seat's result is not the watcher's.

### The session

The choices live in one record, `PortSession` (`port_session.h`), from the launch until the
game is left: a row's team, start, level, handicap, colour and mod's race, the computers past
seven, the rules and the mutators. The owners' `Choose` functions write it, and the game reads it as it starts:
`TeamsSeat` and `AiSeat` as the players are seated, `RulesForGame`, `ModRacesForGame`,
`AiForGame`, `HandicapForGame` and `MutatorsForGame` in `game_init`, `PlayersChosenColoursApply` in `new_game`. None of them clears
it, so a restart (game mode 5: `sub_443C70` runs `new_game` again) plays the same set-up.
`SessionAfterGame`, in `main_game_run` after `free_game`, clears it when the game ends any
other way, and `LaunchDone` when a launch fails, so the menus always hold an empty session
and a scenario, a campaign mission, a replay, a save or a LAN game starts from none. The
benchmark and the trainer choose colours or rules before they call their launch, and clear
the session themselves when it does not start.

What a game makes of the choices (each slot's team, level, race and colour) stays with the
owner that applies it, and so does what a replay or a save recorded; both are set again at
every game's start, from the session or from the file.

### Slots past eight

The flag is `g_hxPlayers`, and it is set only by a map whose `HXPL` section names players
past 7, or by a Hyperion save that carries one. While it is clear, every player table reads
1.17's twelve entries exactly as 1.17 did, out-of-range trigger reads included. With it set,
`g_hxSlots` is the game's slot count, one past the last slot the map gives an owner and never
fewer than twelve, and loops run to `PlayerSlots()` or `PlayerLobbySlots()` rather than to
the build's maximum, so a game plays and hashes the same in a build with more slots.

A per-player global keeps its 1.17 address and type: it is declared as a `PlayerTable`,
`PlayerGrid` or `PlayerMatrix`, whose `a` is 1.17's array in the data block and whose
`Extra()` is one static block a tag for the new slots. `PlayerExtraIndex` decides between
them, an index past the last slot still reads 1.17's block as 1.17 did, and each extra block
registers itself with the trainer's snapshots at its first use (`PortSnapStatic`).

Colours for the slots from 8 up come from `PlayersColorsApply`: `tunit.pcx`'s sixteen ramps
by number, then `kRamps`, built from palette entries that are the same in every tileset, with
a minimap entry each in `kMinimap`; a slot whose map names no colour takes one from
`kDefaultColors` in order, and `HXRG` or a chosen RGB value lays a full colour over the ramp
through `PortTeamColorSet`. The same 65 are what a set-up's row may choose
(`PlayersChooseColour`, `PLAYER_COLOURS`): `PlayersColourRamp` gives any of them as the game
takes it, 1.17's twelve with `tminimap.pcx`'s dots and the rest with their ramp's or
`kMinimap`'s, and `PlayersChosenColoursApply` lays it on the player's slot. A seat with a
colour in full RGB, a team's shade or a row's own colour, takes it through
`PlayersChooseColourRgb`, and the other seats keep theirs.

### The computer player's boundary

The engine never reaches into the AI. It calls `port_ai.h`, and each call is given 1.17's own
value and hands it back unchanged for a slot at Classic, so a game whose computers are all
Classic runs exactly 1.17's code. The seams are `AiFrame` (1.17's `sai_update`, from
`gameloop_update_game`), `AiMeleeScript`, `AiThinkFrames`, `AiDifficulty`, `AiScriptWait`,
`AiAttackCount`, `AiAttackAllowed`, `AiWorkerTarget`, `AiHarvestExtra` and
`AiVisionRefresh`.

A level is a row of `kKnobs`: how long a unit waits before it thinks again, how long the
script's waits run, attack size, worker target, the minutes before the first attack, whole
map vision, and a harvest ramp. Classic and Hard are 1.17's numbers, and Classic alone plays
under classic rules. The three Cheater levels play as Elite and cheat as StarCraft II's do:
vision takes the fog off the cheater's own bits after each sight refresh, which gives sight
and not detection, and the harvest ramp rides on top of the load, rounded by where the worker
stands and when, so trips average the multiplier with no counter to hash.

A game with any level but Classic plays by Hyperion's rules with the option
`HX_OPT_AI_LEVELS`, and each slot's level and style go into the replay (`AILV`) and the save
(`HXAI`) through `AiProfilesSave`, so the game plays back and loads as it was played.
`hyperion_ai.log` names the levels each game ran with.

### Handicaps

Handicaps bring StarCraft II's lobby option: a player
at a handicap has every unit and building at that share of its type's maximum life and shields,
100% down to 50% in steps of 10. Damage, energy, armour and speed stay the type's, as in
StarCraft II, and so does what a unit costs.

1.17 reads a type's maximums straight from units.dat's tables, `gxUnitLife` (8.8 fixed point)
and `guwUnitEnergy` (the shields, whole points), wherever it needs one: a unit made, finished,
morphed, healed, repaired, regenerating, drawn, weighed by the AI or set by a trigger. Each of
those reads that concerns a player's unit, 74 in 26 of `lang/`'s files and two in the port's
(the HUD's and the benchmark's shields), goes through `HandicapLife` or `HandicapShields` with the unit's owner (`h/handicap.h`).
Both are inline and give the table's value back unless `g_hxHandicapOn`, so a game without a
handicap runs 1.17's arithmetic. The type-only reads, a type's cost to the AI with no unit, stay
the table's. A maximum at a handicap is rounded to whole points, at least one; 1.17's own 0x100
floor for life holds.

A unit handed to another player (Mind Control, a rescue, Give Units) keeps its share of the
maximums at the new owner's handicap: `CUnit::SetPlayer` calls `HandicapOwnerChanged` with the
owner it had, which scales life and shields by the new maximum over the old.

The choice is a row's, as a level is: `HandicapChoose` from the skirmish set-up and
`--handicaps` (`HandicapChooseList`), kept in the session's `bHandicap`. `HandicapForGame`, in
`game_init` after `AiForGame`, finds each slot's row wherever the starts were shuffled to
(row 0 the local player, a computer by `PlayersSeatedComputer` or, when nothing noted its seat,
its rank among the computers' lobby slots through `PlayersCameFrom`). A room's match names
them in its launch instead, each person's by name and each computer's in the order the launch
lists them (`HandicapChooseNet`, from `PortLaunchNetGame`), which every seat reads the same; the
net values count only in a game over the network, so a launch that failed leaves nothing behind.
A replay or a save takes the recorded ones. The neutral slot is always at 100%.

A game with any slot below 100% plays by Hyperion's rules with the option `HX_OPT_HANDICAP`
(`HandicapChosenThisGame`, `port_rules.cpp`), and each slot's percent goes into the replay
(`HCAP`) and the save (`HXHC`), so the game plays back and loads as it was played.
`hyperion_handicap.log` names each slot's handicap as a game starts.

One fault in 1.17's own AI code is fixed there, since it wrote past its array:
`add_request_to_town_queue` (`SAI_Build.cpp`) read a town's request slot before testing the
bound, so a town with all 100 requests taken read the next town's `pNext`, and when that
byte's top five bits were clear it wrote the request over it, and the next walk of that
player's towns crashed. The bound comes first now. Where 1.17 read a set byte it returned, as
the bound does, and the request it wrote was outside the queue the town reads, so a game
differs only where 1.17 broke a town's link; no rules revision goes with it. The AI lab found
it, in a game of Very Hard computers 54 minutes in.

By Hyperion's rules a computer's unit fights what it meets on the way (revision 18).
1.17's `CUnit::AIPickAttackTarget` gives a captain's soldier the captain's threat, the greatest
in the captain's region, wherever the soldier stands, and `ChangeTarget` asks it again rather
than search near the unit, so an army crossing a large map passed every enemy army until one
shot at it. `HxMeetOnTheWay` (`CUnitCombat.cpp`) puts `FindAttackTarget`'s pick against a
target more than 16 tiles off and out of range, through 1.17's `tai_pick_better_target`: an
armed enemy near wins, a building near loses to an armed target. The AI otherwise stays 1.17's;
a larger rework of it is planned.

### Mutators

The skirmish set-up's mutators play: one set for every player of a game, chosen with it on
the set-up's Mutators tab (`front-end.md`, "The Mutators tab").

| Mutator | Choices | 1.17's | Where the game asks |
|---|---|---|---|
| Mineral fields | ×0.5, ×1, ×1.5, ×2, ×3, ×4, ×8, Infinite | ×1 (1,500, the small ones 750) | a field made (`CUnitInit.cpp`) or placed with the map's amount (`maphdr.cpp`): `MutatorResource`; a harvest (`CUnit::GetHarvest`): `MutatorEndless` |
| Vespene geysers | the same | ×1 (5,000) | a geyser or refinery made or placed, and a harvest from a refinery, likewise |
| Start resources | 50 / 0, 500 / 200, 1,000 / 500, 5,000 / 5,000, 10,000 / 10,000 | 50 / 0 | `gametype_mutator_resources` over `gametype_init_resources`'s, from `game_init`: `MutatorStartResources` |
| Supply cap | 100, 200, 300, 400, 500, 600 | 200 | `count_init`: `MutatorSupplyCap` |
| Start workers | 2, 4, 6, 8, 10, 12 | 4 | `PlaceStartingUnits` and `TeamPlaceStartingUnits` (`placebox.cpp`): `MutatorWorkers` |
| Supply boost | None, +25%, +50%, +75%, +100% | none | the supply a unit gives as it counts (`count_unit_complete`), the computer's count of the depots it needs (`SAI_Build.cpp`) and the console's line (`statdraw.cpp`): `MutatorFoodMade` |
| Fog of war | Normal, Explored, None | Normal | the map's tiles as the map loads (`MutatorsMapLoaded`) and after each refresh of the players' sight (`MutatorsVisionRefresh`, `gameloop.cpp`) |
| Build time | ×0.25, ×0.5, ×0.75, ×1 | ×1 | the units' and buildings', upgrades' and techs' time tables as the map loads (`MutatorsMapLoaded`) |
| Tech and upgrades | Normal, Researched, No upgrades, No research | Normal | Researched: the players' tech and upgrades as the map loads; the other two: `UpgradeAllowed` and `ResearchAllowed` (`Allowed.cpp`): `MutatorUpgradesForbidden`, `MutatorResearchForbidden` |
| Rich resources | Off, Past the naturals, Outside a radius, Farthest only, Every base; a distance of 32, 48, 64 or 80 tiles or a quarter, third or half of the map; a chance of 25, 50, 75 or 100% | Off | the read and roll as the map loads (`RichBasesMapLoaded`, below); a harvest (`CUnit::GetHarvest`): `MutatorTrip`; a load taken (`SetHarvestInventory`): `MutatorCarry`; a unit's name (`CUnit::GetName`): `MutatorRichName` |

The unit limit is held with the mutators (`MutatorSet::nUnits`, the record's sixteenth value)
but chosen in Game Options, not on the tab: Automatic, or 3,400, 6,800, 13,600, 27,200, 54,400,
108,800 or 262,144 units, the table at least that (`loop.md`, "Caches for large
games"). No preset sets it, and choosing a preset keeps it. Automatic grows the table with the
supply cap past 200 (`MUTATOR_REVISION` 2).

The lists are the set-up's (the design's revision, 2026-10-26); a value they left, as Unlimited
supply, one worker or a build time of ×2, still plays from `--mutators`, a room or an older
replay, and the tab shows it as it is with no choice lit (`MutatorChoiceExact`,
`MutatorValueName`).

Each ask is inline (`h/mutators.h`) and gives 1.17's value back unless `g_hxMutatorsOn`,
so a game without a mutator runs 1.17's code. An amount is the map's or 1.17's times the
share, rounded, at least 1 and at most 65,535, the field's `WORD`. Infinite keeps the amount a
resource starts with, which the console shows, and a harvest takes its 8 and leaves it so; a
trigger that sets an amount sets it as written. The start resources are set for every slot in
any game type, and counted as gathered as 1.17's melee start counts its 50 minerals; at 50 / 0
the game type's own stand (none on a map's own settings). The start workers come where 1.17
puts its four, with or without a town hall; a map's own start units (Use Map Settings) and a
mod's race's own start units stay as they are. The cap is in halves, as 1.17 counts supply; a
team's captain has the team's slots times it, as 1.17 gives it 400 a slot, and Unlimited is
0x1000000 halves, past any game and with room for that product. The boost is rounded down to a
whole supply, and never gives less than without it: a depot's, a pylon's or an overlord's 8 is
10, 12, 14 or 16, a command center's 10 is 12, 15, 17 or 20, a nexus's 9 is 11, 13, 15 or 18, a
hatchery's 1 stays 1 to +75%. 0.6.9 rounded down to a half (a command center's 12.5, which the
console showed as 13, so a player could be refused a unit with a supply shown free); its
replays and saves have a record without the rules' revision and play by halves, as they were
played (`MUTATOR_REVISION`, `port_mutators.h`).

The last three change the game's own tables and map once, in `MutatorsMapLoaded`, from
`game_init` after `LoadMapData`: by then `maphdr_data_init` has read upgrades.dat and
techdata.dat again and reset the players' tech and upgrades, and `CUnit::InitUnits` has read
units.dat. Build time scales `guwUnitTimeToBuild`, which a unit's time to make and its life's
growth meanwhile (`AssignHPGain`) both read, the upgrades' base and per-level times and the
techs' times, each rounded, at least 1 frame and at most 65,535. Researched marks every tech a
player has available as researched and raises every upgrade to its top level, and gives the
units the map placed their upgrades as the upgrade cheat does; the start units take theirs as
they are made. No upgrades has `UpgradeAllowed` refuse every upgrade (as one at its top level),
and No research has `ResearchAllowed` refuse every tech as well (as one not available): the
buttons, a player's command and the computer's research (`SAI_Build.cpp`) all ask those two.
Explored clears every tile's unexplored bits for every player, and None its hidden bits too.
1.17 hides the tiles again every 100 frames (`sub_46C180`, the game loop's fog block), so
`MutatorsVisionRefresh` runs after each refresh; slots past 8 have Hyperion's own sight sets
(`VisionHxHidden`, `VisionHxUnexplored`). A save holds the tables (its `DATS`), the research and
the tiles, so a loaded game scales only a mod's unit types, which the mod gives back at each
start.

The presets are the design's (`lobby.js`'s `PRESETS`): Standard, Plenty (fields and geysers
×2; called Rich before rich bases, and `--mutators rich` still names it), Gold Rush (×4, 1,000 /
500 to start, and every base rich), Infinite (both endless) and Quick Start (1,000 / 500 and a
cap of 400); Custom is any other set. `MutatorsChoose` keeps the set in the session's
`mut`, from the skirmish set-up (`PortLaunchSkirmishChoices`) and `--mutators`
(`MutatorsChooseList`, a preset's name or values by name). A room's match carries the host's in
its `match` messages and its launch (`MutatorsJson`), and every seat takes the launch's
(`MutatorsChooseNet`, from `PortLaunchNetGame`); as with handicaps the net set counts only in a
game over the network. `MutatorsForGame`, in `game_init` after `HandicapForGame`, settles the
game's set before the caps and the map's units are made, and `gametype_mutator_resources` puts
the start resources over the ones `game_ui_init` gave a moment before.

A game with any mutator other than 1.17's plays by Hyperion's rules with the option
`HX_OPT_MUTATORS` (`MutatorsChosenThisGame`, `port_rules.cpp`), and the set goes into the replay
(`MUTS`) and the save (`HXMU`), so the game plays back and loads as it was played.
`hyperion_mutators.log` names the set as a game starts.

### Rich resources

Named Rich bases in the menus until v0.6.23 (the user, 2026-10-26); the code still says rich bases
(`port_richbases.cpp`, `MUT_RICH`).

The tenth mutator turns some of a
map's bases rich, as StarCraft II's gold bases: a trip to a rich mineral field gives 11
minerals and a trip to a rich geyser, or the refinery, assimilator or extractor on it, 16 gas,
where 1.17 gives 8. The amounts stay the map's, as StarCraft II keeps them, so a rich base
mines out sooner. The computer does not seek them out. `port_richbases.cpp` reads the map in
`RichBasesMapLoaded` (from `MutatorsMapLoaded`, a new game or a replay played back), in integer
maths so every machine reads the same:

1. The resources (mineral fields 176 to 178, geysers 188) in order of place, and every start
   the map has, played or not, so the set-up's picture is the game's and a base fair for every
   start is fair whichever the players get. A mineral field holding less than 50 at the
   fields' share (Heartbreak Ridge's walls hold 0) is a wall and left out.
2. Every resource within 14 tiles of a start is its main's. The other fields within 9 tiles of
   one another make a line, and a line wider than 16 tiles is split in two by its fields'
   nearer middle, again while a part is too wide (Diablo's middle). Each other geyser joins the
   line with the nearest field within 16 tiles. A base has four fields or a geyser.
3. A walk from each start over the map's minitiles (walkable as the tileset's VF4 flags say,
   8 ways, 10 straight and 14 across, never across a blocked corner) gives the distance to every
   base, in tenths of a tile; a base no walk reaches takes the straight line, as a drop goes.
   The search near a unit (`sai_get_ground_distance`) is not used: it has a budget and writes
   the computer's scratch.
4. Each start's natural is its nearest other base with a line within 60 tiles on foot that is
   nearer it than any other start (by air from an island start). Every base that is no main
   and no natural is a candidate; mains and naturals turn rich only with Every base.

Which candidates may turn rich is the choice: Past the naturals, every one; Outside a radius,
those at least the distance from every start in a straight line (32, 48, 64 or 80 tiles, or a
quarter, a third or half of the map's size, its width and height averaged; 48 tiles by
default); Farthest only, those whose walk from their nearest start is at least 800 thousandths
of the way from the nearest candidate's to the farthest's. Each turns rich at the chance as
chosen (the user, 2026-10-26, "Why not simplify the formula to a radius formula?"; 0.6.20 and
before had a Middle ground in the radius's place and a chance that grew with the distance).
With Fair on (the default) the bases roll in sets: a base and the fewest others that bring every
start's walk to its nearest base of the set within 20% of the others'; a base no such set makes
fair joins the fair set it leaves fairest when that stays within 20%. Each set rolls once, so
the players get rich bases alike or none. A base still unfair rolls alone: being far enough
from every start is what makes a base eligible (the user, 2026-10-26: on Big Game Hunters 3x3,
32 starts ringing the edge, no set of its 93 bases came within 20%, and the middle, far from
every start, should be fair game; 0.6.22 and before left such a base plain). With At least one
(the default) the set farthest out turns rich when the roll gave none. The roll is a hash of the game's seed
(`net_get_fixed_seed`, which every seat of a room shares) and the set, never the game's own
random numbers; Same every game (`richseed=map`) hashes the map's resources in its place.
Every base marks every field and geyser rich with no read. The game's first frame says what
came of it on the message line, "Rich resources: 2 bases turned rich." or why none did ("No base is
48 tiles or more from every start."), unless the set's Announce at the start is off
(`MUTATOR_RICH_QUIET`, `announce=off`; the user, 2026-10-26, "so the user or host has option to
turn it off or on"); `richbases` and `hyperion_mutators.log` keep the read either way.

A unit's byte (`g_unitRich`, kept by the trainer's snapshots) holds whether it is a rich
resource and whether a worker's load came from one. A refinery is the geyser's own unit
transformed, so it keeps the byte; a unit made in the slot clears it (`MutatorUnitMade`, from
`CUnit::sub_425710`). `CUnit::GetHarvest` gives the rich trip, and below it what is left, as
1.17 does; a dry rich geyser gives 2 a trip, as 1.17's. The table is in the sync hash, a
Hyperion save's `HXST` holds it, and a replay's `RICH` chunk records the resources that turned
rich, which playback marks in place of a read, so a later change to the read never changes an
old replay. `units` on the live channel says `"rich": true`, and `richbases` gives the whole
read: the bases, mains, naturals, distances, shares, sets, chances and the outcome.

A rich field draws gold and a rich geyser, the building on it and a worker's rich load violet,
through the looks (`render.md`, "Looks"): `RichLookOf` is the looks' provider. The
minimap's dot of a rich resource takes the look's pen, and a dry geyser draws as 1.17 draws it.
The names are "Rich Mineral Field" and "Rich Vespene Geyser"; a refinery keeps its own.

### Triggers

Two separate things carry the name. A map's own triggers past 1.17's eight players live in
`victory.cpp`: `TriggersMapHxte` reads the `HXTE` section, a quad word
a trigger naming the slots past 11 it runs for; `TriggerHxCodes` turns a condition's,
action's or Give Units' `0x100 + slot` code into a tagged slot so slot 13 is not read as the
group 13; and `TryAddTriggerHx` adds the trigger to those slots' lists. A 1.17 game reads no
Hyperion map and so never sees a code.

`port_trigrun.cpp` is the other: it runs 1.17's trigger actions from outside a trigger, so
the live channel's spawn, order and clear (`port_control.cpp`) and the cutscenes'
steps (`port_cutscene.cpp`) make the changes a trigger would. It places the map's last two
locations, 254 and 255, as scratch regions, names the player as the current player when the
slot is past 11 (`TriggerAs`), calls `act_create_unit`, `act_order` or `act_remove`, and
tells the live channel the game changed. Single player only; the callers check.

### EUD maps

An EUD map's Deaths and Set Deaths reach memory through the death table's
unchecked index, `0x58A364 + 4 * (player + 12 * unit)` in 1.16.1's address space. Hyperion
serves that space from its own data, as Remastered does, in `port_eud.cpp`:

- `maphdr_TRIG` hands each trigger to `EudMapTrigger`, which tests it as Remastered does (a
  Deaths condition or Set Deaths action with player 12 or past 26, or unit past 232); a map
  with one makes a new game an EUD game, `HX_OPT_EUD` in its rule record, so its replay and
  save play it the same way. `EudGame()` is the test everywhere else.
- In an EUD game `death_count` and `set_deaths` (`TrigEnum.cpp`, `TrigAct.cpp`) call
  `EudDeaths` and `EudSetDeaths`, every count, in range or not; `cond_deaths` and
  `act_set_deaths` set the mask a condition's or action's `SC` mark carries.
- The address map is a sorted list of items, built at each game's start and again once the
  map's triggers are loaded: twins (a 1.16.1 range and the 1.17 global at the same place in
  1.17's layout, read and written through `PortDataField`, so a 64-bit field or pointer is
  translated), plain ranges, buffers at new 1.16.1 addresses from 0x6DE000 (the trigger
  nodes, the string section, GRPs, the map's tiles, the scripts), and ranges kept for the
  map. 1.16.1's memory as a game starts, which Remastered serves for 64 ranges, is read from
  the stream in the player's Remastered `x86_64\StarCraft.exe` (`StreamRead`, logged in
  `hyperion_eud.log`); without one those ranges are zeros, read only.
- `triggers_loop` (`victory.cpp`) walks each player's list by 1.16.1 addresses
  (`EudTriggersFirst`, `EudTriggerNext`) and runs each trigger in place
  (`trigger_parse_eud`), then calls `EudTriggersDone`, which applies what Remastered applies
  once the triggers have run: a pool's free list rebuilt (euddraft's unlimiter grows the
  pools to Hyperion's storage), a card's buttons rebuilt (`StatcmdEudButtons`,
  `statcmd.cpp`).
- An access no item serves stops the triggers for the rest of the game (`g_eudHalted`) and
  is logged; tables a map wrote are put back at the next game's start (`StaticRestore`):
  the requirement trees, cards and panels, every .dat table and `iscript.bin`, the ones 1.17
  loads once for the program's run, saved as the map's triggers mark the game an EUD game
  (`EudMapTriggersRead`).
- An image on a frame its GRP does not have is neither drawn nor clicked (`GroupHasFrame`,
  `grp.h`), where 1.17 reads the bytes past the frame table as a frame; an EUD game logs
  the image once (`EudFrameMissing`).
- `CanSaveGame` (`save_dlg.cpp`) is false in an EUD game whose map writes memory, so Save
  Game is greyed out, as in Remastered.

### The trainer: sessions, snapshots and the tape

A session is one game played as a set of lines. A line is the commands its game was given,
frame by frame, from frame 0, the same commands the replay recorder takes, caught in
`PortTrainerCommand` from `net_record.cpp`. The game is always at the playhead. Going back
calls `PortSnapRestore`, which puts the line's latest snapshot at or before the frame back in
place in the same game, and the loop then plays the line's own commands on, frame after frame
with nothing drawn, which is the `TRAINER_SEEK` state. `PortTrainerFeed` gives each frame's
commands at the top of the loop as a replay's turns are given; `PortTrainerFrameEnd` closes
the frame, snapshots every `TRAINER_SNAP_EVERY` frames, samples the player's figures each
game second (`TrainerSecond`), makes the automatic marks, and holds the frame to its state
hash every `TRAINER_CHECK_EVERY` frames.

A line played before its end is `TRAINER_WATCH`: its commands are given at their frames, and
the first order of the player's starts a new attempt from that frame. At a line's end the
game is `TRAINER_LIVE` and the player's commands are the line's. A branch copies its parent's
commands up to the split and shares its snapshots to there (`PortSnapShare`), so each line
plays on its own, and the parent is kept to draw the tree.

A session set up on the trainer's entry screen (`port_front_trainer.cpp`, which hosts the
skirmish set-up) arrives as a `TrainerSetup` through `PortTrainerFromMenus`, which arms it
and launches its game or its replay. It is saved as the game ends, to
`Trainer\Sessions\<name>.hxtrain` in the data folder, as JSON: the set-up with its seed and
rules, and each line's commands, marks, figures, build order and checks. Leave asks what to
save: every line, retries too, the timelines only, or nothing; a game ended any other way
saves every line. Opened again, the same game is made with the same seed, every line starts
from its snapshot at frame 0, and the recorded hashes say whether this build plays it as the
build that made it (`PortTrainerDiffered`, `hyperion_trainer.log`).

The screen is `port_trainerview.cpp` and draws after the game in the GPU presenter. While the
game runs the keys take Ctrl, so 1.17's own command keys stay the player's: Ctrl+Space pauses,
Ctrl+R retries, Ctrl+Q sets a retry point, Ctrl+K keeps, Ctrl+B branches, Ctrl+M marks,
Ctrl+T takes over a watched line. Paused, the Tape is up and the keys take no modifier.

### Hotkeys and keys

`port_hotkeys.cpp` reads the profile's `hotkeys.json` as a game starts (`PortHotkeysLoad`,
from the game loop), in three parts. K1 is the command card: a button's key is the profile's
own for that `stat_txt` string, or with the Grid layout the key of the button's place on the
card (`Q W E / A S D / Z X C`), or 1.17's first character; `PortCardTooltip` moves the
highlight when the key is not the string's own. K2 is the accelerator commands by name:
`accel_apply_profile` in `event_Win.cpp` rebuilds 1.17's three tables through
`PortAccelRemap`, keeping 1.17's entries for every command the profile does not bind. K3 is
the keys 1.17 compares in its own code, so far the ten control group keys through
`PortKeyGroup`.

`port_keys.cpp` is separate: Hyperion's own function keys, kept as numbers in the `hyperion`
section of `settings.json` with `PORT_KEY_CTRL`, `_SHIFT` and `_ALT` folded in above the
virtual key. Only the keys 1.17 leaves free can be taken, which `PortHotkeyFree` holds to F5 to
F9, F11, F12 and Tab (not Alt+Tab). Four of them belong to Hyperion's selection
(`hud.md`) and are rows of Settings' Hotkeys tab with the source "Hyperion": the next subgroup
(Tab, with Shift the one before), the minimap's terrain (Ctrl+Tab), the idle worker (Shift+F11)
and the army (Shift+F12). `event_Win.cpp` takes them before 1.17's keys, not while a message is
typed and not again while a key is held. The subgroup key acts only under Hyperion's selection;
there 1.17's own Tab toggle of the terrain is off, and in a classic game Tab toggles the terrain
as in 1.17.

### The score

The score screen (`front-end.md`, `port_front_score.cpp`) shows every player of a game, and
1.17's own score dialog keeps eight rows, so the game's numbers are read for it here
(`port_score.cpp`), by 1.17's formulas:

- Overview: the units score (`glGameCounts_g` [5] and [6]), the structures score ([12] and
  [13]), the resources gathered (gas and minerals); its score their sum.
- Units: produced ([2]), killed ([4]), lost ([3]); its score [5] and [6]. Structures:
  constructed ([9]), razed ([11]), lost ([10]); its score [12] and [13]. These follow
  `sub_462C10` and `sub_462B20`, whose comments in the source have the two tabs swapped.
- Resources: gas, minerals, and spent, what was gathered less what is on hand, at least 0
  (`sub_462BB0`); its score what was gathered.

`PortScoreGameEnd`, in `main_game_run` before `free_game`, keeps the players of the lobby's
slots (types 1, 2, 10 and 11, neutral left out) with their names, races, colours (the chosen
RGB, or 1.17's colour through the palette), computer levels and teams, and the numbers. A
team comes from the set-up's teams (`HX_OPT_TEAMS`), else 1.17's team game (its captains'
numbers, `GetTeamName`), else a scenario's forces while one holds more than one player.
`ScoreRead` adds what `do_score_dlg` knows at the end (`ScoreEnd`): the result of each slot
from `gbVictoryState` (3 a win, 2 a loss, 5 a draw, 1 dropped), the local player's as
`sub_477270` gives it, the elapsed time, and whether Save Replay is allowed.

1.17 does not keep when a player went out. `PortScoreFrame`, each frame in `gameloop.cpp`,
notes the game's clock the first time a slot's victory state leaves 0, and `PortScoreLeft`
that the local player left (Quit Mission), whose time `PortScoreGameEnd` reads: 1.17's clock
takes a pause back out only when the pause ends, so read in Quit Mission's menu it would
count the menu's own pause. `PortScoreGameStart` forgets them. These only read the game, so
they change no frame.

The screen's order is 1.17's (`score_row_rank`): winners, then the rest, then losers, each by
the tab's score and then by slot (`ScoreBefore`). The count-up is 1.17's too (`sub_4629F0`,
`sub_462F70`): a value counts up at 16 times itself up to 28, 8 times up to 128 and once above
(`ScoreScaleOf`), so a small number does not finish at once, and every 20 ms the count moves
an eighth of what is left, at least 4 (`ScoreStep`).

## Simulation or presentation

Changes what a game frame does, and so plays by Hyperion's rules and is carried in the
replay and the save:

- Players past 7, with the pools, positions and tables that follow: revisions 1 to 9 in
  `src117/h/hxsim.h`. Turned on only by a map's `HXPL` or a Hyperion save. The pools sized
  for the game and unit ids past 2,047 are revisions 4 to 6. A game takes its players over 8,
  rounded up, times 1.17's pools, 8 times for 64 players; the 64-bit build's storage holds 13
  times (`POOL_SCALE_MAX`: 22,100 units and 65,000 images, the most the save's 16-bit counts of
  a pool's slots hold), which only a benchmark's stress run takes (`--bench-pools`,
  `HX_OPT_STRESS`).
- Maps past 256 tiles a side, with their pathing and search budgets: revisions 9, 11 and 13.
- 25,600 paths in a game past 1.17's limits, where 1.17 has 1,024 (`POOL_PATHS_HX`,
  `SaiPathPoolForGame`): revision 15. Past 1,024 units moving at once a unit found no
  path, gave its order up and stood.
- Triggers that name the slots past 11, the leaderboard a record a slot, objectives as a
  player table, Give Units and Set Alliance: revision 10.
- Chosen teams: the option `HX_OPT_TEAMS`. The players are allied by `nTeam` as the game
  starts, with allied victory and shared vision; the replay keeps `nTeam` in its slot records
  and the option in its rule record.
- Computer levels other than Classic: the option `HX_OPT_AI_LEVELS`, with each slot's level
  recorded. Classic is 1.17's AI to the frame.
- Handicaps: the option `HX_OPT_HANDICAP`, with each slot's percent recorded. A game
  where every slot is at 100% records neither and plays 1.17's maximums.
- Mutators: the option `HX_OPT_MUTATORS`, with the game's set recorded. A game with
  1.17's values records neither.
- EUD maps: the option `HX_OPT_EUD`, revision 16. A new game on a map whose triggers reach
  memory takes it; every death count then goes through the address map.

Changes the game that is played but is not a rule, because the result is set up before frame
0 and is already recorded where a replay reads it: the chosen start locations (`--starts`, the
skirmish screen's by-slot starts, which steer `ShuffleStartSlots`), and which races and how
many computers a launch seats.

Presentation only, and compatible with vanilla replays:

- Player colours: 1.17's twelve, the ramps past them and full RGB colours (`HXRG`, `COLR`
  version 2, `PlayersChooseColourRgb`).
- The hotkeys of all three kinds, since a key picks a button or a command and the command is
  what the replay records; and Hyperion's own function keys for art, view width, zoom,
  following a unit, replay TV and the debug overlay.
- The whole trainer. A line is a command stream, so each line is a game Hyperion plays
  ordinarily; the snapshots are raw copies of state put back into the same game, and the
  replay the game records is the line it ends on. The screen draws over the frame.
- `port_pathstats.cpp`'s counters, which the game never reads.
- The score's numbers and the times players went out (`port_score.cpp`), read from the game
  and never written into it.

## Settings and environment variables

| Name | Where | What |
|---|---|---|
| `rules` | `settings.json`, `hyperion` | `classic` (or `1.17`) makes new games play by 1.17's rules; anything else, or none, Hyperion's, the default since 2026-10-22. `--rules` overrides it; a game past 1.17's limits plays by Hyperion's whatever it says, and a game over the network by the rules its room's launch names (`src117/h/hxrules.h`) |
| `keyArt`, `keyView`, `keyZoomOut`, `keyZoomIn`, `keyZoomReset`, `keyZoomFit`, `keyFollow`, `keyTv`, `keyDebugView` | `settings.json`, `hyperion` | Hyperion's own function keys, as numbers; the defaults are F5, Ctrl+F5, F7, F8, F9, Shift+F9, F11, F12 and F6 (`port_keys.cpp`) |
| `layout`, `card`, `accel`, `keys` | `profiles\<name>\hotkeys.json` | the command card's layout (`classic` or `grid`) and per-button keys, the accelerator commands' keys by name, and the control group keys (`port_hotkeys.cpp`) |
| `HYPERION_TRAINERTEST=<period>,<back>` | environment | the trainer's self-test: at every period'th frame first reached, go back that many frames and watch the line again; every frame played twice must hash alike (`trainertest.txt`) |
| `HYPERION_TRAINER_SEAT=<slot>` | environment | with `--trainer --replay`, the recorded seat the player takes over |
| `HYPERION_EUD=continue`, `trace`, `map`, `dat` | environment | in an EUD game: go on past an access no item serves, dropping it (`continue`), also log each trigger node run (`trace`), log each item of the address map and any bytes of it Hyperion's data does not cover (`map`), or read every .dat field as the map would as its triggers first run and log those that differ from their files (`dat`: a map's unit settings, 1.17's own rewrites such as units.dat's target range, the sound and portrait tables 1.17 turns into pointers; a column of ids Hyperion keeps in 16 bits reads as its low byte); `hyperion_eud.log` |
| `HYPERION_PATH_BUDGET=117` | environment | keeps 1.17's search budgets on a map past 256 tiles, for `pathbench`'s comparison; recorded as `HX_OPT_PATH_BUDGET_117` |

Trainer sessions are written to `Trainer\Sessions` in the data folder; the logs are
`hyperion_ai.log`, `hyperion_trainer.log`, `hyperion_keys.log`, `trainertest.txt` and, for
an EUD game, `hyperion_eud.log`.

## Integration points

- `glues.cpp`, `RunGlues`: `PortLaunch`, once, where 1.17's mission skip starts a campaign
  mission. `game.cpp`, `game_init`: `RulesForGame`, `TeamsAlliances`, `ModRacesForGame`,
  `AiForGame`, `PoolsForGame`, before the pools are built; `game_minimap_init` calls
  `PoolsForGame` too.
- `main.cpp`, `main_game_run`: `PortScoreGameEnd` before `free_game`, and `SessionAfterGame`
  after it, before the other after-game hooks. `game.cpp`: `PortScoreGameStart` after
  `sub_461E00`. `gameloop.cpp`: `PortScoreFrame` after `PortLabFrame`. `opt_dlgs.cpp`,
  `quit_to_menu_respond`: `PortScoreLeft`.
- `game.cpp`, `SinglePlayerMeleeInitGame`: `PlayersSeatsBegin`, the extra computers,
  `TeamsSeat`, `AiSeat`. `uiSingle.cpp` notes each computer's seat with `PlayersSeatNote`,
  `net_slot.cpp` the start shuffle with `PlayersShuffleNote` and `TeamsStartFor`.
- `gameloop.cpp`: `PortTrainerFeed` at the top of each turn, `AiFrame` inside
  `gameloop_update_game`, and at a frame's end `PortSnapFrame` then `PortTrainerFrameEnd`;
  `PortTrainerSeeking` is what makes the loop run frames at once and draw nothing.
  `net_record.cpp`: `PortTrainerCommand`, one call a recorded command.
- `maphdr.cpp`: the `HXPL`, `HXRG` and `HXTE` handlers, and the size refusal above
  `MAP_SIDE_MAX`. `event_Win.cpp`: `accel_apply_profile` and the trainer's key handler.
  `statcmd.cpp`: `PortCardKey`. `game_evt.cpp`: `PortKeyGroup`.
- `port_rules.cpp`: `TeamsThisGame`, `AiChosenThisGame`, `HandicapChosenThisGame` and
  `MutatorsChosenThisGame` decide the rules and the options a new game records.
  `port_hxsave.cpp` and `port_hxreplay.cpp`: `PlayersSaveSetUp`, `PlayerTablesSave`,
  `AiProfilesSave`, `HandicapsSave`, `MutatorsSave` and the trigger lists of the slots past 7.
- `game.cpp`, `game_init`: `HandicapForGame` after `AiForGame`. `CUnitInit.cpp`,
  `CUnit::SetPlayer`: `HandicapOwnerChanged`. Every read of a player's unit's maximum life or
  shields in `lang/` (marked `Hyperion: a player's handicap` at its include):
  `HandicapLife`, `HandicapShields`.
- `game.cpp`, `game_init`: `MutatorsForGame` after `HandicapForGame`, then for a new game or a
  replay `gametype_mutator_resources`, since `game_ui_init` sets the start resources before the
  rules and the mutators are known. The mutators' reads, each marked ``: `count.cpp`
  (`count_unit_complete`, `count_init`), `SAI_Build.cpp`, `statdraw.cpp`, `placebox.cpp`,
  `gametype.cpp` (`gametype_mutator_resources`), `CUnitInit.cpp`,
  `maphdr.cpp` and `CUnitHarvest.cpp` (`CUnit::GetHarvest`).

## Gotchas

- `HXTE` has one quad word a trigger, bit n for slot n, so a trigger can be given slots up to
  63. Slot 64, the last of `PLAYERS_MAX`, cannot be named there, and `TryAddTriggerHx` skips
  it; it is still reached through the groups.
- The set-up's rows are not slots. Row 0 is the person and row `1 + k` the k-th computer
  seated, and the start shuffle moves the records afterwards, so every lookup from a slot back
  to a row goes through `PlayersSeatedComputer`, which returns -1 in a multiplayer team game
  because that seats by team.
- `port_session.h` includes `playerlimits.h` and nothing else, so `main.cpp` can include it:
  `main.cpp` declares 1.17's `fopen`, `fclose` and `fputc` itself, which clash with the
  standard headers `port_ai.h` brings. A row keeps its computer's level and style as two bytes
  for that reason, not as an `AiProfile`.
- `--computers` accepts up to `PLAYERS_MAX - 1`, that is 63, on a map with the slots for
  them; `../hyperion-exe.md` still says 15, which was true when `PLAYERS_MAX` was 16. With no
  `--computers` at all, `PlayersSetExtraComputers(-1, ...)` means one computer in each slot
  past 7 the map opens, of the race the map gives it, which is not the same as asking for
  zero.
- `AiVisionRefresh` clears the square map's bits for slots 0 to 7 and the port's hidden and
  unexplored sets for the slots past 7; the two halves are separate code paths, so a change to
  one needs the other. `AiAttackAllowed` measures the first-attack delay in
  `g_dwElapsedTime` ticks, which are 16 frames, converted with the Fastest frame time, so at
  another speed the delay is the same number of game frames, not the same minutes.
- A trainer seek drops what the player ordered while it runs (`PortNetKeepSyncs` keeps only
  the sync bytes), and `PortTrainerCommand` ignores commands during a seek. Code that assumes
  every local order reaches the game must check `PortTrainerSeeking`.
- The trainer's snapshots cover the port's own game state only where a static has been
  registered with `PORT_SNAP_KEEP` or `PortSnapStatic`. New port state that the simulation
  reads has to be registered, or a rewind will not put it back.
- `port_trigrun.cpp` writes map locations 254 and 255, so a map that uses all 255 loses those
  two. `PoolsForGame` sets `g_posShift` with the pool sizes, so the position width and the
  pools change together.

## Open items

- Play styles for the computer player (stage D) and the improvements of stage E; the
  menus list styles but `AI_STYLES` has one entry.
- Text, sounds, the view and the other trigger actions aimed at a person still act for slots
  0 to 7 only, briefings run for those slots, and the leaderboard dialog shows its first eight
  records ("Not done"); the alliance and vision dialogs keep eight rows, still to come.
  The score screen shows every player.
- Maps past 1,024 tiles, and the pathing that would scale rather than coarsen, are not
  planned; 512 tiles and 32 players are the scope
  held today. The AI's areas stay 2,500 a nation. Its six pools grow with the unit table in a
  game past 1.17's limits or table (`h/aipools.h`), and each AI frame plans a nation
  and serves 333 captains for each 8 slots (`PlayerAiTurns`, revision 17), so a computer keeps
  1.17's pace with up to 64; its decisions are 1.17's.
- Multiplayer is rebuilt separately; nothing here crosses to another client.
- EUD maps: all 794 of StarCraft: Remastered's ranges are served; for 7 of them,
  Remastered's own state that Hyperion has no counterpart for, a map's bytes are kept for it
  and may read differently than in Remastered. 1.16.1's memory as a game
  starts needs a Remastered install. Map text in UTF-8 and CP949 is drawn, in the game and on
  the skirmish screen (`render.md`, map text).

## Further reading

- Engine docs: `../maps.md` (maps and players, and what differs past 256 tiles),
  `../hyperion-exe.md` (the program and every command-line option), `../replay-test.md` (the
  gate), `front-end.md` (the screens that set a game up).
