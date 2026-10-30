# The game loop, timing, threads and the state check

## Overview

Hyperion runs 1.17's game on one thread, in 1.17's order, with 1.17's one random generator:
that is what makes a replay play the same game every time. Around that thread the port adds
three things: a clock that says when the next game frame and the next drawn frame are due
and lets the loop rest in between; a pool of worker threads for work that only reads the
game while it stands still between frames; and a hash of the game's state that a Hyperion
replay or save is checked against. This doc covers all three, plus the measuring hooks
(`HYPERION_PERF`, `HYPERION_PROFILE`) and the caches that make large games fast.

## Status

On master since v0.3.6-beta (2026-10-18). The simulation is
unchanged by everything here: the full replay suite stays identical. What changed then:
the check's hash is version 9 (four chains a domain), the pool wakes only the
workers a batch needs, the game thread can be held on the large-cache cores, and the rest
ends as late as the timer allows.

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `src117/lang/gameloop.cpp` | 1.17's loop with the port's hooks marked `// Hyperion` | `RunGameLoop`, `gameloop_update` |
| `src117/port/loop/port_clock.cpp` | the game's clock, the speed multiplier, the rest between frames, Debug Tools' frame timing, the game time a frame counts on the clocks shown | `PortGameTicks`, `PortClockRest`, `PortFrameTiming`, `PortGameSpeedTimesSet`, `PortClockFrameMs` |
| `src117/port/render/port_present.cpp` | when a drawn frame is due (the frame rate setting, the vsync cap) | `PortFrameDue`, `PortFrameDueIn`, `FrameRateHz` |
| `src117/port/loop/port_workers.cpp`, `.h` | the worker pool; the game thread's cores | `PortWorkersFor`, `PortWorkersForInOrder`, `PortWorkersPost`/`Join`, `PortCoresFit`, `PortWorkersCount` |
| `src117/port/loop/port_synchash.cpp`, `h/port_synchash.h` | the state hash, one 64-bit hash a domain | `SyncHashCompute`, `SyncHashTimes`, `SyncHashDump`, `SyncSlotRead` |
| `src117/port/replays/port_hxreplay.cpp` | records hashes while a game is played, compares them on playback | `Check` (every 24 frames) |
| `src117/port/loop/port_vision.cpp` | vision past slot 7, its caches, the fog reads done ahead on workers | `VisionHxSpriteSet`, `ViewHxSpriteSeen`, `AheadMode` |
| `src117/port/loop/port_perf.cpp` | the per-part frame times and the sampling profiler | `PortPerfBegin`/`End`, `PortPerfGameFrame`, `PortPerfLine` |
| `src117/port/loop/port_pools.cpp`, `h/poollimits.h` | the unit, sprite and other pools sized for the game's players | (see the file's head) |

## How it works

### One pass of the loop

`RunGameLoop` (gameloop.cpp) goes round while the game runs. Each pass:

1. Steps the picture's motion and any cutscene, and pumps window messages
   (`RunEvtDispatcher`).
2. Applies a view switch asked for since the last pass (`PortViewApply`).
3. Reads the game's clock (`PortGameTicks`). The scroll keeps 1.17's `GetTickCount` timer so
   the scroll speed matches 1.17.
4. Runs one game frame (`gameloop_update`) when the clock has reached the frame's due time,
   or at once while something wants the game fast: the live channel's fast stepping, a
   benchmark, a trainer seek, a LAN watcher catching up.
5. Runs the UI's per-frame half.
6. Draws (`img_update`, which ends in `PortPresentFrame`). With a frame rate chosen in
   Settings, it draws only when `PortFrameDue` says a frame is due; at 1.17's rate, whenever
   anything changed.
7. If the pass neither ran a frame nor drew, rests (`PortClockRest`) until input arrives or
   the next thing is due.

### The clock

`PortGameTicks` is the game's millisecond clock. By default it reads the performance
counter, so a frame due every 42 ms runs every 42 ms; Settings' "Game frame timing" set to
1.17 uses `GetTickCount`, which steps every 15.6 ms. With DLLs injected (`HYPERION_DLLS`,
the replay harness) it is always 1.17's, because the harness's observer replaces that
timer. `PortGameSpeedTimesSet(n)` runs the clock n times as fast (up to 8) for watching
replays quickly. Only when a frame runs changes, never what it does.

Do not call `GetTickCount` in game-path code of our own: use `PortGameTicks`.

### The rest between frames

`PortClockRest` (Settings' "Rest between frames", `loopRest`, on by default) waits on a
high-resolution waitable timer with `MsgWaitForMultipleObjectsEx`, so window input ends the
wait at once. A slice is 1 ms at most. It ends a margin before what is due, and the loop
spins the rest, because a timer wakes late: about 0.5 ms as a rule on Windows, 1.3 ms at
the worst seen. The margin follows the timer: the latest of its last 256 wakes plus 0.2 ms,
held between 0.5 and 2 ms (`RestMargin`, `RestLate`). A 1v1 at normal speed costs about a
seventh of a core this way, where a loop with no rest costs a whole one. The loop never
rests under the replay harness, so the suites never see the rest.

### When a frame is drawn

`PortFrameDue` and `PortFrameDueIn` (port_present.cpp) pace drawn frames at Settings' frame
rate. With the GPU presenter and vertical sync on, the rate is capped at the display's
(`FrameRateHz` uses `DisplayHz`), since frames beyond it only queue and arrive later. How
many frames the processor may prepare ahead of the graphics card is "Frames ahead" (1 to
3, default 2); see `render.md`.

### The worker pool

The game stays on one thread. The pool takes work that only reads the game between frames:

- the state check's six domains and their parts (`PortWorkersForInOrder`, so the hash
  chain is mixed in order while later parts still run);
- the fog reads done ahead of `CThingy::CalcVisibility` in games of 4,096 units or more
  (`PortWorkersPost` then `PortWorkersJoin`). Below that they lose to the plain walk.

The pool's threads start on first use and sleep on a condition variable. A new batch wakes
only as many as it has items (`Wake(n)`), so a small batch does not wake 15 threads. Its
size is Settings' "Processor threads" (`workerThreads`, 0 Auto: half the processor's
threads, at most 16), or `HYPERION_WORKERS`. With 1, or in the browser, everything runs in
order on the calling thread.

### The game's cores

On a processor whose cores do not share one L3 cache (a Ryzen X3D's two halves, 96 MB and
32 MB), `PortCoresFit` holds the game thread on the larger cache's cores. Settings' "Game's
cores" (`gameCores`): Auto (the default) holds it from 2,000 units and lets go below 1,500;
Large cache always; Any never. `PortPerfGameFrame` counts units every 64 frames and calls
it. On a uniform processor, or off Windows, nothing changes. Large games run 7 to 14% faster
on the 7950X3D; small ones are unchanged.

### The state check

`SyncHashCompute` hashes the game a domain at a time (`SYNC_UNITS`, `SYNC_OBJECTS`,
`SYNC_PLAYERS`, `SYNC_MAP`, `SYNC_SPRITES`, `SYNC_AI`), so a mismatch names where the games
went apart. It reads and never writes. Pointers are hashed as what they point at (a unit's
slot, a captain's nation and area), never as addresses, so two runs hash alike wherever the
program was loaded. Records are hashed as runs of their fields in 1.17's form, so a layout
change that keeps the values keeps the hash.

While a Hyperion game is played, `port_hxreplay.cpp` records a hash every 24 frames (a
second at fastest) into the replay; on playback it compares at the same frames. A save
holds the hash of its moment and is checked on load. The version is `SYNC_HASH_VERSION`,
9 since 2026-10-18: each domain's words go to four chains in turn, folded at the end, so one
thread mixes four words side by side. A replay or save with another version still plays,
but is not checked. 1.17's own replays carry no hashes; the replay harness compares them
against the original exe instead (`replays.md`).

Changing what the hash reads, or how, needs a version bump, a note in `h/port_synchash.h`'s
history, and the harness's Hyperion references re-recorded (`replaytest.py hxrecord`).

### Caches for large games

- `g_spriteViewSeen` (port_vision.cpp): one byte a sprite saying whether a replay's viewers
  past slot 7 can see it, kept current where the sets change, so the draw asks one byte
  where it used to walk 32. `HYPERION_VIEW_SEEN_CHECK=1` compares it with the full answer.
- The AI domain reuses a captain's hashed stream when it equals the previous captain's but
  for its region, patching the region in (`CaptainsAlike`).
- The pools are sized per game (`port_pools.cpp`): a 1.17 game keeps 1.17's sizes exactly.
  The unit table: a game past 1.17's limits, or with a supply cap past 200, holds
  the map's own units (its first pass counts the `UNIT` records, `PoolsMapUnit`) and, for each
  player, its supply cap in units and 100 for its buildings, rounded up to 1,700s, at least
  1,700 for each 8 slots; the unit limit (Game Options, `MutatorSet::nUnits`) takes it to at
  least its count, up to 262,144 (`POOL_UNITS_MAX`). The other pools follow in 1.17's
  proportions (`PoolsForUnits`), and the AI's six with them (`h/aipools.h`). A replay (`UNTS`)
  and a save (`HXUN`) record a table other than the slots', since a save does not read its map
  again. A game past 1.17's limits or table has 25,600 paths, or one a unit past that, where
  1.17 has 1,024 (revision 15); `sai_path_init` makes the pool before `game_init` sizes the
  pools, so `SaiPathPoolForGame` makes it again after `PoolsForGame` when the count differs.
- Storage on 64 bits is for the most, 262,144 units (`gen/wide117.h`, the AI's in
  `port_aipools.cpp`); memory is taken only as a game fills it. A pool's slot index is 4 bytes
  in a save past 65,535 slots (`PoolSaveBytes`), and the snapshots copy each grown pool only
  as far as the game's count (`kSnapGrown`, `PORT_SNAP_KEEP_POOL`).
- The finder's sides (`h/CUnitBounds.h`): beside each entry of `g_pos_sort_x` and
  `g_pos_sort_y`, its unit's extent on the other axis and whether it is the unit's high edge,
  moved with the entries and written again where a unit's coordinates change.
  `get_units_in_rect` and the pathing's contour walks read them instead of the unit's bound
  indices.
- The finder's grid: the same units in squares of 128 pixels by their top left
  corner. `get_units_in_rect` answers a walk of more than 96 entries from it
  (`FinderGridRect`), sorting the units it keeps by the entry the walk would meet each at
  first, so the list is the walk's. The pathing's contour walks (`sai_Contours.cpp`,
  `ContourEntries`) take their entries from it too (`FinderGridEdges`), where none of the
  16-bit sums their checks make can wrap. `find_units_in_rect`, the search collision and the
  AI's threat searches use, answers its walks of more than 192 entries from it
  (`FinderGridEntries`): the units with an entry in each of its two spans, by index, in the
  order of the first in the x span. Its walks also mark every unit of the x span 0, which
  the grid does not; those marks are read only against `g_find_flag`'s count, which passes
  every mark left once it is 3, so the grid answers from its third call on.
  `HYPERION_FINDER_CHECK=1` checks all of it against the walks and the units.
- The map's contour lists in rows (`sai_Contours.cpp`, `ContourRowsWalk`). The
  local path search's `sub_488600` walks each of the four lists, which hold the contours of
  the map's whole width or height, from `find_contour`'s binary search over every contour in
  a band as wide as its scan box, for the few that reach across the box. The lists are the
  map's and the game does not change them (`g_nContourLists` counts the times they are
  made, read from a save or freed, and the rows are made again after). Their rows give the
  first row at each coordinate, the cells of 32 pixels each row's contours cover and, for
  each contour, the furthest end of those up to it in its row. A walk passes a row with no
  cell in the span a kept contour must meet for any of the unit's origins, enters the others
  at the first contour that can reach the box and leaves at the first past it, testing those
  between as the walk does, in its order. `HYPERION_FINDER_CHECK=1` walks as well and compares the contours kept.
- The local search's entries by their point (`sai_PathFindLocal.cpp`,
  `LocalPointSlot`). `sub_491F50` looked each contour mark's point up by walking its entries
  so far, every mark against every entry, up to 1,000 on a large map. No two entries past
  the first have the same point, so a table of their points, filled as each is made, gives
  the walk's entry. `HYPERION_FINDER_CHECK=1` walks as well and compares.
- The local search's strips from the last point's (`sai_PathFindUtil.cpp`,
  `BinsEdge`). `sub_4930B0` and `sub_492DE0` find the strip of the bins' table that holds a
  point by stepping from the first edge, up to 128. A search's points are near each other, so
  the steps start from the strip the last point was in, while the edges are in order:
  `ResetBins` says they are and a split writing one out of order with its neighbours says they
  are not. `HYPERION_FINDER_CHECK=1` walks as well and compares.
- The captains' unit slots (`TAI_Captain.cpp`, `CaptainSlotsMark`). A unit's removal
  cleared it from the four unit slots of every area of every computer's; a set for each
  nation of the areas whose slots may hold a unit, marked in the four places that store one,
  lets it walk those alone. The tables made, read, grown or freed, or a snapshot restored
  (`CaptainSlotsForget`), have the next removal walk them all and make the set again.
  `HYPERION_FINDER_CHECK=1` looks at every area for a unit the set leaves out.
- A medic's searches for a unit to restore (`AIFindCureTarget`, `AIFindParasiteCureTarget`)
  first look down its player's list for a unit under an effect; with none, neither
  search can find anything and both filters only read, so the walk is left out.
  `HYPERION_FIND_CHECK=1` walks anyway and logs a difference.

Each of these gives 1.17's answer to the bit, so the replay suite is their gate; their checks
were run over the full suite, hxsuite and the 64-player bench.

## Measuring

- `HYPERION_PERF=1` writes `hyperion_perf.log` once a second: frames shown, game frames, and
  ms per game frame for each part (`PERF_EVENTS` to `PERF_DRAW`), plus in a Hyperion replay
  each check's wait and each domain's ms and words.
- `HYPERION_PROFILE=1` samples the game thread about once a ms and writes
  `hyperion_profile.log` every 10 s, named from `Hyperion.pdb` beside the exe, with each
  function's share alone and with what it calls.
- `HYPERION_SYNC_SERIAL=1` runs the check's parts one after another on the game thread, so
  the profile shows each one's own cost.
- `engine/tools/hxbench.py` runs builds and settings against each other and appends to
  `engine/testrun/bench/results.jsonl` (`tools.md`).

## Settings and environment variables

| Name | Effect |
|---|---|
| Settings "Game frame timing" (`frameTiming`) | 1 even on the precise counter (default), 0 as 1.17 |
| Settings "Rest between frames" (`loopRest`), `HYPERION_LOOP_REST=0/1` | the loop rests between frames (default on) |
| `HYPERION_LOOP_MARGIN_US=<us>` | fixes the rest's margin, for measuring |
| Settings "Processor threads" (`workerThreads`), `HYPERION_WORKERS=<n>` | the pool's threads, the caller included |
| Settings "Game's cores" (`gameCores`), `HYPERION_CORES=cache/other/any` | where the game thread runs on a split-cache processor |
| `HYPERION_VISION_AHEAD=0` | no fog reads ahead |
| `HYPERION_VISION_AHEAD_FEWEST=<units>` | the unit count the reads ahead start at (4,096) |
| `HYPERION_VISION_AHEAD_CHECK=1` | compares every read ahead with the plain walk |
| `HYPERION_VIEW_SEEN_CHECK=1` | compares the sprite-seen byte with the full answer, logs to `hyperion_view_seen.log` |
| `HYPERION_SYNC_SERIAL=1` | the check's parts in order on the game thread |
| `HYPERION_SYNC_CAPTAINS_WALK=1` | turns the captains' reuse off |
| `HYPERION_PERF=1`, `HYPERION_PROFILE=1` | the logs above |

## Gotchas

- Anything run on a worker must only read game state, and only between frames. A worker
  that writes, or reads during `gameloop_update`, breaks determinism in a way the suite
  may only catch much later.
- Do not use `GetTickCount` on the game path; `PortGameTicks` is the game's clock.
- A drawing thread is not possible with SDL's GPU API: it hands out the swapchain image only
  on the window's thread, which is the game's.
- Gains from threads are easy to mismeasure on a busy machine. Compare builds by
  alternating runs, several each, and keep each build in `testrun/bench/builds/`.
- Hash changes invalidate the recorded Hyperion references: keep the old ones aside before
  re-recording, and confirm with hxsuite that every replay still plays its game.

## Open items

- How evenly frames reach the display on a quiet machine, with the adaptive margin.
- Measurements on the ARM board and the Linux machine.
- Further multi-core candidates are ranked but not built.

## Further reading

- `../hyperion-exe.md`: every environment variable of the program.
- `replays.md`, `render.md`, `tools.md`: the modules this one feeds.
