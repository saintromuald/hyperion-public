# lab: the AI lab, its farm of games and watching them

## Overview

The AI lab is a mod for making computer players, for
Hyperion's own testing: since 2026-10-27 no package or install carries it or `lab_stream.py`
(`tools/package.py`'s `UNSHIPPED_MODS`), so it is run from a folder it is copied into. Its
screen sits in Hyperion's front end, as the trainer's and the benchmark's do. Its farm plays
games of computers in worker processes the lab starts and owns, hidden and with nothing drawn,
and shows each game on a wall as its map with its units moving on it; any game can be opened
and watched in the lab's own process.

A process plays one game at a time: 1.17's game is one set of globals (`g_data117`,
`g_wide117`), so 24 games at once are 24 workers, as a LAN room runs a process a seat.

Nothing here changes the simulation. A worker's game is the benchmark's game of computers, and
what the lab adds to the frame only reads.

## State

On master as v0.3.8-beta (2026-10-19): the mod and its screen, the worker, which plays one
game and tells the lab about it, the farm, which plays a project's games in workers, and
the wall, which shows each of them as its map with its units moving on it, at the pace
the screen sets, the games (S4), every game played listed, watching a game in the lab, from its
start (S5) or live (S6), its state checked against the worker's, workers that play game after
game in one process (S7), and the stream, which a program outside Hyperion subscribes to on the
live channel (S8); measured and documented for v0.3.8-beta (S9). The trained model is the later
milestones' (the take's README).

Not in yet: the mockup's admin sections (projects, data sets, training, the league,
publishing), which come with those milestones; a project editor, Edit Project handing
`project.json` to Windows to open, so that with no program set for `.json` it does nothing; and
the wall's map pictures are at most 128 pixels a side, stretched to their tiles.

Measured 2026-10-19 on 2-player games of Lost Temple: a worker plays about 26,000 to 37,000 game
frames a second in 75 MB of memory. Its process needs 1.4 s to its first game; after that a game
needs 0.13 s from the lab's `JOB` to its first frame, then its play (a 4-minute game 0.1 s, a
21-minute one about 1.3 s), and its memory holds flat (72 MB after 10 games, 61 to 75 MB through
200). The farm with 4 workers played 8 games of 20,000 frames in 3.3 s; when each worker was a
process a game, they took 12 s.

Games an hour by games at once, measured 2026-10-19 by `labcheck.py rate` on a machine of 32
logical cores: the first project's games (Lost Temple, Very Hard and Elite against each other,
Free For All, an hour of game at the most, 43 minutes of game each on average), at the most
pace with the wall shown, a minute counted after 10 s to fill the workers.

| games at once | games an hour | hours of game an hour |
|---|---|---|
| 1 | 780 | 588 |
| 2 | 1,680 | 1,200 |
| 4 | 3,000 | 2,243 |
| 8 | 6,000 | 4,270 |
| 12 | 7,920 | 5,685 |
| 16 | 9,420 | 6,827 |
| 24 | 11,520 | 8,316 |

It grows with the workers to 8 at once, then by less as they share the processor's cores:
24 at once play 14.8 times what one does. A worker holds 70 MB however many there are, so 24
are 1.7 GB.

## Key files

| File | What it does |
|---|---|
| `src117/port/lab/port_front_lab.cpp` | the lab's screen: its header, the farm and the games views, Back. Declared in `port_frontscreens.h` (`LabOpen`, `LabScreen`, `LabKeys`, `LabResult`, `LabCameBack`, `LabPanelId`) |
| `mods/lab/mod.json` | the mod: mode `lab`, whose home screen button, Open the Lab, opens the screen |
| `src117/port/lab/port_labproto.h` | what the lab and a worker say: each message a frame of the LAN host's connection, its kind and fields (`LabProto`, protocol 1) |
| `src117/port/lab/port_labfarm.cpp` | the farm: the project, the workers started and owned, their messages, the index (`PortLabFarmStart`, `PortLabFarmPump`, the `lab_` commands) |
| `src117/port/lab/port_labgames.cpp` | the games: the project's `games.jsonl` read as it grows, a table newest first, the picked game and what can be done with it (`LabGames`) |
| `src117/port/lab/port_labwall.cpp` | the wall: a tile a game, its map's picture, its resources and its units glided between samples (`LabWall`) |
| `src117/port/lab/port_labwatch.cpp` | a game watched in the lab: its replay started, the panel over it, its state checked at the worker's end frame, the way back (`PortLabWatchAsk`, `PortLabWatchLaunch`, `PortLabWatchFrame`, `lab_watch`) |
| `src117/port/lab/port_labworker.cpp` | the worker: its environment, its connection, and each game frame's samples, status and end (`PortLabWorkerStart`, `PortLabFrame`) |
| `tools/labcheck.py` | the lab's checks: `worker` plays the lab's part for one worker, `farm` runs the lab and its farm |

## How it plugs in

- The mod's mode is read by `port_mods.cpp` (`Installed`), and the home screen's button opens
  `SCREEN_LAB` (`port_front_home.cpp`, `Hero`).
- The front end runs the screen as it runs the trainer's: `port_front.cpp` names it (`lab`),
  draws it, gives it the keys, takes its result (`FRONT_LAB_HOME`, `FRONT_LAB_WATCH`) and comes
  back to it after a game watched from it (`LabCameBack`).
- `HYPERION_FRONT_OPEN=lab` opens it on the farm, `lab-games` on the games, while the lab is
  the active mod (`--mod lab`).
- A game watched starts as the lab mode's game (`PortFrontStartMode`, `PortLabWatchLaunch`). In
  the lab's own process `PortLabFrame` (`gameloop.cpp`) hands each frame to the watch
  (`PortLabWatchFrame`); the game loop runs it as fast as it can with nothing drawn while
  `PortLabWatchCatching` says so, and holds its next turn while `PortLabWatchHold` does, as it
  does for a LAN match catching up and waiting for its feed.
- After the game, `PortFrontCampaign` (`PortLabWatchAfterGame`) or the main menu
  (`PortLabWatchCameBack`, through `LabCameBack`) opens the lab again.

## The farm

A project is a folder, `<data>lab\projects\<name>\`: `project.json` says what its games are,
and is made the first time with the defaults (Lost Temple; Elite against Elite, Elite against
Very Hard, Very Hard against Very Hard, Elite against Hard; an hour of game at the most):

```json
{"name": "First Project", "maps": ["(4)Lost Temple.scm"], "players": 2,
 "levels": ["7,7", "7,6", "6,6", "7,5"], "races": [], "type": "ffa",
 "first_seed": 1, "frames": 86400}
```

`type` is `ffa`, every computer for itself, unless it says `melee`, 1.17's Melee, where a
single-player game puts every computer on one side (`port_bench.h`), so they never fight.

Game n plays the next map, levels and races of each list, and the seed `first_seed + n - 1`; a
map is a name or a path, found as the live channel's `map_load` finds one
(`PortControlMapPath`). Its folder is `games\NNNNNN\` (the benchmark's `run.rep`,
`settings.json` and `summary.json`), and when it ends it is a line of `games.jsonl`: what it was
asked (map, players, levels, races, seed, worker, when), how it ended (`finished` at its length,
`last` with one player or team left, `lost` with why, `stopped`), its last frame, the winners,
its seconds, its game frames a second, its state (`end_state`), and each seat's race, team,
level, name, units and gathering.

`PortLabFarmStart` plays them, so many at once (the cores less two, at most 24, unless told)
and so many in all (until stopped, unless told). The lab listens on 127.0.0.1 at a port the
system gives (`LanNet::ListenLocal`); each worker is started with its data folder
`<data>lab\workers\wNN` and a token, and its connection is the one whose `HELLO` carries the
token. The lab answers `HELLO` with the project's next game (`JOB`: its number, map, players,
levels, races, type, seed, length and folder), and each `GAME_END` with the next, or with
`STOP` when it wants no more of that worker, which then ends; so a worker plays game after game
in one process, and the answer is also what tells it its last message was read. Processes are
started only for games still to give. A worker that says nothing for 30 s is ended, and a game
given to a worker that ended before that game's end is lost.

On Windows the workers are in one Job object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), started
suspended and put in it before they run, so they end with the lab however it ends; on Linux a
worker asks for `SIGKILL` when its parent ends (`PR_SET_PDEATHSIG`); everywhere a worker ends
when its connection goes.

The farm runs on Windows and Linux. The Linux x64 build in WSL played the farm check's 8 games
as Windows did, each to the same end frame with the same winners, and its workers went within
1.1 s of the lab being killed. In a browser, which starts no processes, `PortLabFarmStart`
refuses with "the lab's farm needs Hyperion on a desktop"; the lab's screen is not reached there
anyway, a browser's Hyperion being a LAN room's seat.

A Hyperion process can be opened by its user only for `SYNCHRONIZE`, `PROCESS_TERMINATE`,
`PROCESS_QUERY_INFORMATION` and two more, once 1.17's `InitACL` (`main.cpp`) has run, which a
script that watches workers has to ask for (`labcheck.py`'s `alive` and `Memory`).

The farm is pumped beside the live channel each frame the front end shows (`port_front.cpp`)
and each frame the presenter shows (`port_present.cpp`), so it plays on while the lab shows a
game.

## The farm's screen and the wall

Above the wall: games at once, the pace (Max, x64, x32, x16), Hide the Games (H), Pause (P) and
Start or Stop (S), and how many games are played, playing and so many an hour.

The pace is the workers': `PACE` carries a multiple of Fastest, 0 for as fast as each goes, and
paused. A worker keeps it by the clock between its frames (`Pace` in `port_labworker.cpp`):
frame n is due n times 42 ms over the multiple after the pace was set, and the worker waits
for it, pumping its connection; paused, it waits until told otherwise. Waiting only puts off
its next frame, so the game is the same at any pace. 1.17's own replay speeds would not do:
their turn rates are whole milliseconds, so x16 of Fastest is 2 ms, really x21, and x64 is
0 ms. At x16 a worker runs at 381 game frames a second, Fastest's 23.8 sixteen times. A worker
waiting on its pace, or on its watcher, still sends `STATUS` twice a second, so a farm paused
past the lab's 30 s watchdog keeps its workers.

The wall (`port_labwall.cpp`) is a tile a worker, as many across as makes them largest. A tile
is its game's map, fitted below a strip with the game's time, its number and how many times
real time it runs (from `STATUS`). On the map are its resources, from the map's scenario, and
its units, from the samples: a dot in its player's colour (1.17's, as the benchmark's minimap
has them), a building larger and square. The wall draws 50 ms behind the last sample and glides
each unit from the sample before to the last one by its id (each sample's units are sorted by
id as they come); a unit new in the last one comes in, and one gone flashes where it was for
0.35 s. A game's end is a band across its tile for 1.6 s, the winner's colour and name or
"Length limit", and the game's time; then the tile dims under "Next game starting" until its
worker's next game starts. Hidden, a tile is its strip and how far its game has gone, and the
workers send no samples: the farm asks for 30 a second only while the wall draws them
(`PortLabFarmWallShown`), and none from half a second after it stops, the lab's screen left
too.

## The games

The Games view (G) reads the project's `games.jsonl` as it grows, every half second from where
it last stopped, a whole line at a time, and lists the games in the order they ended, the last
first, drawing only the rows that fit (the wheel scrolls). A row is the game's number, map,
players (race and level each), result (the winner's colour, "Length limit", "Stopped" or
"Lost"), game time, real time and game frames a second. The game picked shows its map, each
player with its colour, race, level, units at the end and gathering, how it ended and in how
long, its seed, worker, when, and its state at its end; and Watch from the Start (W, below),
Open Folder (its `games\NNNNNN\`), and Copy to My Replays, which copies its `run.rep`
into `maps\replays\Lab\` of the data folder as "<project> game <n>.rep".

The foot names the project, and Edit Project opens its `project.json` in the editor the system
gives .json files (made with the defaults first when it is not there); the farm reads it when
it starts. Picking among projects and editing a project's pool and mix in the lab wait for later.

The maps' pictures are read once each (`MapPictureRead` with no slot, and `MapMinimapRgba` at
128 pixels at most) and kept nine to a slot in the picture atlas's slots 26 and 27, so 18 maps
have pictures; a map past those is drawn as a plain field.

The live channel has the farm's commands, added by `PortControlCommand`: `lab_status` (the
farm, its pace, and each worker's state, process, game, map, frame, speed, units and
samples), `lab_start {at_once?, games?, project?}`, `lab_pace {multiple?, paused?}`, `lab_stop`,
and `lab_games {since?, max?}` (lines of `games.jsonl`). `python tools/labcheck.py farm` runs the lab with a project of its own, plays 8
games 4 at once and checks each folder and line, checks that x16 holds each game to about 381
game frames a second and that Pause holds them still and Resume sets them going, then checks
that `lab_stop` ends every worker and that killing the lab alone ends them too.

## Watching a game

A game's Watch from the Start, or a tile of the wall clicked, plays that game in the lab's own
process (`port_labwatch.cpp`), from its start or, for a tile of a game still playing, live. A farm game has no commands, so its `run.rep` is the map, the
players and the seed, and played again it is the same game, whether its worker has finished it
or is still playing it. The lab copies it to `lab\watching.rep` in the data folder and the
front end starts it through the launch a watched LAN match uses (`PortLaunchWatchReplay`,
`port_launch.cpp`), in SD art for as long as it is watched (the HD setting comes back after),
at Fastest, with the replay panel's Show All, and with Replay TV, which points the camera at
fighting and leaves it where it is when there is none.

A panel over the game's top left names the game, its map and its time, and each side with its
colour, race, level, bank, supply and units. The benchmark's own seat, which made the replay
and plays no side, has no race and is left out.

At the frame the worker's game ended (its line in `games.jsonl`, read as soon as the farm has
the worker's `GAME_END`) the lab folds the state as the worker did (`SyncHashFold`) and
compares the two. The panel says "The same game as the farm's" or that it differs, the game
holds there with its result, and Esc goes back to the lab (B at any time), on the view the game
was watched from. A game whose worker did not finish it (stopped, or lost) is watched with no
check. While the farm still plays the game and its end is not known, the watcher holds before
a turn past the worker's last frame (`PortLabWatchHold`), so it never passes an end it has not
heard of.

### Live

The lab tells the game's worker at once that it is watched (`WATCHED`, with the frame it may
play to), so it stops where it is while the game starts in the lab; the watcher then catches up
with nothing drawn to 48 frames behind it and plays on at its own speed. Each frame the watcher
allows the worker its own frame and 48 more (`PortLabFarmWatch`): the worker plays as fast as it
can to there and waits, whatever the farm's pace. So the worker follows the watcher at Fastest,
at x16 or paused, with no speed passed between them. A watcher more than 240 frames behind
catches up again.

While watched the worker sends `STATUS` every 50 ms and its state every 240 frames (`HASH`).
The watcher compares each with its own at that frame, and the panel, marked LIVE, says how many
were the same, or where one differed. Leaving gives the worker back to the farm's pace.

Measured 2026-10-19 with four games at x16: a game joined at frame 4,135, 2.4 s after asking;
the watcher and its worker then ran at 23.7 frames a second, 47 or 48 frames apart, while the
other three stayed at x16; every state compared was the same, and a game followed live to its
end checked the same there. Back in the lab its worker was at 379 frames a second again.

### On the live channel

`lab_watch {game?, live?, to_end?, leave?}` asks for a game while the lab's screen shows, from
its start or live, and answers what is watched: its frame, its worker's, the frame it is
checked at, the check (`waiting`, `same`, `differs`, `none`), both states, and the live checks
(`same`, `differ`, `differ_at`). With `to_end` the game runs with nothing drawn until its end,
the farm pumped from the frame meanwhile; `to_end` alone does it for the game watched, and
`leave` goes back to the lab. Five games of 6,000 frames came back `same` with `to_end`, about
2.7 s each from asking to the check, most of it the game's loading; 24,000 frames take about a
second more. `lab_status` says which worker is watched.

## The stream

A program outside Hyperion follows the farm on the live channel: `subscribe {topics}` and every
message on those topics comes to it as `{"topic": ..., "data": ...}` until it closes
(`PortControlPublish`, `port_control.cpp`). A client more than 8 MB behind misses messages until
it catches up, and a client gone takes its subscriptions with it. The lab publishes three:

- `lab.workers`, once a second: the farm as `lab_status` has it.
- `lab.games`, as each game starts (`event: start`, its map, seed, levels and seats) and ends
  (`event: end`, its line as `games.jsonl` has it, a lost or stopped game's too).
- `lab.samples`, each worker's last sample at most 10 times a second: its game and frame, its
  players' banks, supply and units, and its units in base64, 10 bytes each little-endian (the
  unit's id, x and y in map pixels, player, and kind: building 1, air 2, worker 4, resource 8).
  Workers sample 10 times a second at least while the stream has a subscriber, as they do for
  the wall.

The lab makes a topic's messages only while a client takes it (`PortControlSubscribed`).
`python tools/lab_stream.py [--port] [--topics] [--seconds] [--raw]` subscribes and prints what
comes, the units decoded; its `Stream` is the client for a script of one's own. With four
workers at x16 and the wall not shown, a client took 12 `lab.workers`, 8 `lab.games` and 468
`lab.samples` in 12 s; before it subscribed and after it dropped, the workers sent no samples.

## The worker

A farm's worker is `Hyperion.exe --data <its folder> --lab-worker <port>:<token>:<n>`. Its games
come from the lab: at each `JOB` it makes and plays the benchmark's game of computers as a run
from the benchmark's set-up does (`PortBenchLabGame`, `port_bench.cpp`), with nothing drawn at
the most speed, its run folder the one the job names; but the game is left at its end rather
than held under its summary, and the program goes back to the glue loop, where it waits for the
next job (`PortLabWorkerNext`, `glues.cpp`). It is never started with `--seed`, which would
outrank each game's (`PortObserveSeed`).

The benchmark's own options with `--lab-game <id>` and `--lab-out <folder>` make a worker of one
game, which ends with it (`labcheck.py worker` checks that one).

- With no game of its own to start, a worker skips the intro films (`main.cpp`), and it plays
  no music: 1.17 fades the music out for a second as each game ends (`stop_music`), twice a
  farm game, the game made and then played.

- `PortLabWorkerStart` (`hyperion.cpp`, before SDL starts) makes it hidden, silent, drawn with
  SDL's software renderer (no graphics card a worker), one thread, and quiet when it crashes
  (`HYPERION_QUIET`, `port_crash.cpp`). It connects to the lab on 127.0.0.1 and says `HELLO`
  with the token; with no lab there it ends.
- `PortLabFrame` (`gameloop.cpp`, after the frame) pumps the connection every 5 ms and reads what
  the lab sent (`SAMPLING`, `PACE`, `WATCHED`, `STOP`). On the run's first frame it sends
  `GAME_START`; then `SAMPLE`s by the clock (30 a second unless the lab says otherwise, none
  while 1 MB waits to go) and `STATUS` twice a second, or every 50 ms with `HASH` every 240
  frames while it is watched. When the run ends it sends `GAME_END`, with the state folded into
  one number (`SyncHashFold`, the benchmark summary's `end_state`), the winners when one player
  or team is left, and each seat's units and gathering, and waits up to 1 s for it to go; a
  worker of one game then waits for the lab to close the connection, and a farm's for its next
  job.
- A unit in a sample is its `GetUnitID` (the same unit from sample to sample, so the wall can
  glide it), its place, its player and its kind (building, air, worker, resource). The neutral
  player's units are left out.
- The glue loop's 1.5 s wait between leaving a game and the next (`glues.cpp`) is skipped for a
  worker.

`python tools/labcheck.py worker` checks one: it listens, starts a worker on a game of
computers, checks the messages in their order and the game's folder, plays the replay again in
a fresh process (`--bench-replay`) and checks it ends in the state `GAME_END` gave, and prints
the times and memory. `--levels 7,1 --frames 200000` gives a game won before its length.

`python tools/labcheck.py soak` checks a farm's worker: one process given `--games` games one
after another (20 unless it says, the levels varying), each of which, played again in a fresh
process, must end in the state the worker's did, and it prints each game's time from its job and
the worker's memory after it. 20 games of 6,000 frames were all the same, 0.23 s each; 12 FFA
games of 30,000 frames, three of them won before the length, were all the same; and 200 games
left the worker at 61 MB.

`python tools/labcheck.py rate [--at-once 1,2,4,8,12,16,24] [--warm 10] [--seconds 60]` measures
games an hour: the lab plays the first project's games at each `--at-once` in turn and counts
the games that end in `--seconds`, with how they ended and the workers' memory. A game that
neither reaches its length nor has one player left is a problem it reports; the first run lost
game 81 at 8 at once and more, a crash in 1.17's AI that a fresh process playing the game had
too (`gameplay.md`, the computer player's boundary), fixed before the table above.
