# The Pluto protocol, as captured from the v2.0 binaries and the rig

Everything Hyperion needs to speak Pluto's language: the shared-memory layout of the
observation, the observation's field semantics as far as they are named, and the
action format the engine replies with. Captured by `engine/tools/pluto-rig/` running
the real v2.0 DLL against scripted state; the layout is read from the writer's code
and the field semantics from differential runs and the per-unit writer's
disassembly (base+0x307F0).

## Transport

Shared memory + two events (`PLUTO_SHM_REQ_EV`, `PLUTO_SHM_RESP_EV`), with a pipes
fallback. `pluto_infer.exe` owns the mapping in the rig's shape; the bot's client
(the shm client at base+0x54160) holds the view pointer at +0x18 and a send flag at
+0x1C. One request-reply pair per step, one step every 6 game frames.

The official v2.0 binaries use **PLSM**, version 4, declared size `0x85F80`, and
capacity 1024. The request serial is at `0x40`; the response serial at `0x80` and
echoes the serviced request. Publication of each serial is the completed-write
boundary. Do not use payload changes as a clock: consecutive requests or responses
can be byte-identical.

## The request (the observation)

The writer is base+0x34960. Its 48-byte header at `0xC0` contains N, W, H and nine
zero trailing dwords. These are the active request ranges (reserved capacity and
padding are not current payload):

| Offset | Active size | What |
|---|---|---|
| +0xC0 | 48 | request header: N, W, H, nine trailing dwords |
| +0x100 | 48*N | first request tensor, in visible-unit order |
| +0xC100 | 436*N | unit feature entries, in visible-unit order (the model currently caps N at 768) |
| +0x79100 | N | first per-slot unit mask |
| +0x79500 | N | second per-slot unit mask |
| +0x79900 | 20 | small vector |
| +0x79940 | 233 | small vector |
| +0x79A40 | 1168 | vector |
| +0x79F00 | 32768 | 128x128 u16 map plane |
| +0x81F00 | 16384 | 128x128 byte map plane |
| +0x85F00 | 32 | request tail |

The engine's globals log still sizes the model globals at **303 floats**. Field
names that have not been established by a controlled differential must not be
inferred from reserved capacity or stale bytes.

## The raw inference response

The complete response is exactly **40 bytes** at `0x85F40..0x85F68`: four
little-endian 64-bit values (`a`, `arg`, `pos`, `u` in the engine log), followed by
two float32 values. The engine writes the response, then publishes the response
serial at `0x80`. The DLL waits for that serial and only then consumes the record.
The bytes at `0x100` are request data, not a response action array; the tail after
`0x85F68` is padding, not more response data. The raw response and the later
classic TurnBuffer commands are different differential-capture boundaries.

### The differential pass (2026-11-03): what each region is

Six scripted variants (base / minerals 777+333 / supply 6->7 / +2 units /
fog r=24->40 / one drone moved), diffed dword-by-dword:

- **+0x79F00 (the 128x128 u16 plane) = the fog/visibility plane.** The fog
  variant's 1006 changed dwords are exactly the r=24..40 annulus tiles;
  a tile reads 0x03 (seen/explored) or 0xC3 (fogged) -- 0xC0 = the hidden
  bits, 0x03 = the seen+explored bits. Hyperion fills it from
  VisionHxTileSeen/Explored.
- **+0x81F00 (the 128x128 byte plane) = not fog**: unchanged across every
  variant (same map) -- terrain/height derived.
- **+0x79100/+0x79500 (the 32x32 byte planes)** change only in the units
  variant (two new p0 units: bytes 1 and 2 of each plane flip 0 -> 1) --
  unit-derived summaries, not fog and not a plain position grid. One more
  differential batch (single-unit adds, enemy-unit adds) finishes them.
- **The second batch solved the 32x32 blocks**: they are per-slot unit masks
  over the visible-unit list, only the first N bytes used -- +0x79100 =
  "the slot holds a real unit" (byte 0, the sentinel, is 0; base = bytes
  1..4 for the four zerg units; one more p0 unit flips byte 5; a p1 unit and
  a unit move change nothing, and fog changes nothing). +0x79500 mirrors it
  exactly (the per-slot visibility mask -- equal in the rig, where every own
  unit is visible). The map-u16 plane also carries a per-tile unit bit
  (0x0100) beside the fog bits.
- **+0x79A40 (the 1168-byte active vector, within a 1216-byte region stride)
  and the globals tail** move with every variant and, for the globals, even
  across runs of the same scenario: they
  hold the bot's own policy state (arm picks, deploy lambda, counts) mixed
  with the game scalars. A Hyperion-side builder cannot reproduce them from
  the game state alone -- the obs's globals are the fork's internal state,
  which is exactly why the bridge (the real DLL) is the architecture and a
  direct Hyperion-to-pluto_infer mode is not faithful.

## The unit entry (436 bytes, 109 floats)

Each field is a float32. Offsets are within the entry; values are from a live
capture with 8 scripted units (hatchery, drone, drone, overlord, command center,
SCV, SCV, marine — the rig's scenario 1) unless noted.

| Offset | Field (as named) |
|---|---|
| +0x00, +0x04 | position x, y, scaled by 1/4096 (verified by the teleport differential) |
| +0x08 | hit points / the unit's max hp (a half-hp hatchery reads 0.5; NOT 1/1600) |
| +0x30, +0x40, +0x48, +0x58, +0x70 | flags, all 1.0 in the captures |
| +0x3C | isMoving (0 -> 1 when the unit starts moving) |
| +0x50, +0x5C | a race bit (1.0 for zerg, 0.0 for terran) |
| +0x0C..+0x20, +0x2C..+0x3B, +0x54..+0x130 | zeros in the captures (cooldowns, shields, energy, orders) |
| +0x24, +0x28 | the facing pair: (sin, cos) of the facing byte (unit +0x21) times the scale at 0x63FDCB2C; the rig's scripted units face 0, so both read (0, 1) |
| +0x134..+0x1B0 | 32 floats: 8 blocks of 4, one per scale k = 0..7: (sin θk, cos θk, sin φk, cos φk) with θk = x·π/(16·2^k), φk = y·π/(16·2^k) -- the raw x and y as multi-scale positional encodings (a period of 32·2^k px). Solved and verified: see "the embedding formula" below |

No per-unit type one-hot has been established by these captures. The verified
unit-entry semantics are limited to x, y, hp/maxHP, race, the listed flags and
the positional embeddings. Any type/player encoding, cooldowns, shields,
energy, orders and targets in the remaining fields must be established from the
per-unit writer's disassembly (the `fstp [esi+off]` sequence from base+0x307F0)
or new controlled differentials, not inferred from zero or reserved regions.

## The reply (the actions)

Classic net commands appended to StarCraft's TurnBuffer (0x654880, byte count at
0x654AA0) — the same command language Hyperion's `net_cmds.cpp` parses:

- `0x09` Select: `{op, count, u16 unit ids[count]}` (the bot's own unit ids, echoed
  from the obs Hyperion builds)
- `0x0C` Placebox (8 bytes)
- `0x14` RightClick (10 bytes): `{op, u16 x, u16 y, u16 targetId, u16 targetType, u8 queued}`
- `0x15` Action (11 bytes): `{op, u16 x, u16 y, u16 targetId, u16 targetType, u8 order, u8 queued}`
- `0x1A` Stop and `0x2B` HoldPosition (2 bytes each)
- `0x1F` Build, `0x23` Unit Morph, and `0x35` UpgradeBldg (3 bytes each)
- Research, upgrade, burrow/cloak, transport, cancellation, and other classic
  commands use their native 1.16.1 opcode sizes and receivers

## What Hyperion must do

1. Run the real DLL and its own `pluto_infer.exe` through `pluto-bridge.exe`;
   Pluto's observation globals contain policy state that a Hyperion-side builder
   cannot reproduce faithfully.
2. Send optional static HXPM pathing metadata once before frame 0, then one HXP4
   frame per game frame. The bridge owns the six-frame inference cadence and
   returns the classic net commands in each HXRP reply.
3. Apply those commands through Hyperion's native receivers. Replies are held
   for two additional game frames so their total action latency is three, as
   measured in the genuine BWAPI match.
4. The level: `AI_LEVEL_PLUTO` in `port_ai.h`, the skirmish row, `--ai-levels`, the
   replay/save record — and the room's computer seat rides the same level.

## The bridge protocol (Hyperion <-> pluto-bridge.exe)

The bridge (`engine/tools/pluto-bridge/`) runs the fork's DLL under Wine and owns
the obs build; Hyperion only feeds game frames and receives net commands, over the
bridge's stdin/stdout. All little-endian.

### Static map/region metadata (stdin, optional once before frame 0)

Before launching the bridge, Hyperion exports `HYPERION_PLUTO_MAP_FILE`,
`HYPERION_PLUTO_MAP_NAME`, and `HYPERION_PLUTO_STARTS`. The bridge reads the
user's local map (never a bundled asset), puts the leaf in `mapFileName`, the full
local path in `mapPathName`, the actual title in `mapName`, and BWAPI's SHA-1 in
`mapHash`. A declared map that cannot be read or hashed fails startup instead of
silently producing empty metadata. Start records are in tiles after subtracting
the depot-center `(64,48)` pixels. Every map start is published; an enemy player's
own exact start is hidden. The older `HyperionRig.scm` constant is not production
metadata.

`HXPM` is a separate static record, not a frame extension. HXP2, HXP3, and HXPF
legacy layouts are unchanged; HXP4 is a distinct frame generation, never a tail
appended to HXP3. Hyperion serializes the actual `ep_glob_t` once in `PlutoStart`,
before the first HXP4 frame. Maps over 256 tiles
on either axis, unavailable pathing, or invalid/oversized pathing omit HXPM rather
than changing the simulation.

The fixed 32-byte header is:

| Offset | Type | Field |
|---|---|---|
| +0 | 4 bytes | ASCII `HXPM` (`0x4D505848` as a little-endian u32) |
| +4 | u16 | version = 1 |
| +6 | u16 | fixed header bytes = 32 |
| +8, +10 | u16 | map width, map height in tiles; each 1..256 |
| +12 | u16 | region count, at most 5,000 |
| +14 | u16 | map start count, at most 8 |
| +16 | u32 | split-tile record count, at most 25,000 |
| +20 | u32 | map tile count; must equal width * height |
| +24 | u32 | total neighbor IDs across all regions, at most 1,275,000 |
| +28 | u32 | payload bytes after this header; must exactly match all counts |

The payload has four sections in this order, with no padding:

1. **Starts:** `startCount` records of `{u16 x, u16 y}` in map tiles. Every map
   start is present in native start-slot order; the unused `(0,0)` entries are
   omitted, matching the existing environment fallback.
2. **Tile IDs:** `tileCount` u16 values in row-major `[y * mapWidth + x]` order.
   Values below `0x2000` are region indices. Values at or above `0x2000` encode
   split index `value - 0x2000`, exactly as Hyperion's `wMap` does.
3. **Splits:** `splitCount` packed six-byte records
   `{u16 minitileMask, u16 region1, u16 region2}`, read from the actual
   `bScratch` records through `pScratch`, bounded by `EP_SPLIT_RECS`.
4. **Regions:** for each region in original index order, the following 34-byte
   fixed record and then `neighborCount` u16 neighbor IDs in their original order:

| Offset | Type | Region field / Hyperion source |
|---|---|---|
| +0 | u16 | access flags / `wGroup` (`0x1FFD` inaccessible, `0x1FF9` higher ground) |
| +2 | u16 | connected group / `wArea` |
| +4 | u16 | tile count / `wTiles` |
| +6 | u8 | path count / `bPathFirst` |
| +7 | u8 | neighbor count / `bPathCount` |
| +8 | u8 | defence priority / `bAreaLinks` |
| +9 | u8 | neighbor property / `nExtraPaths` byte |
| +10, +14 | i32 | center X/Y in pixels times 256 / `nX`, `nY` |
| +18, +22, +26, +30 | i32 | left, top, right, bottom in pixels / `nMinX`, `nMinY`, `nMaxX`, `nMaxY` |
| +34 | u16[] | variable neighbor IDs, copied from `pPaths[0..neighborCount)` |

`EP_STRIDE` selects each actual `wMap` row. A nonempty `pPaths` list is accepted
only when its complete range lies in that region's ten-word inline array or the
real `wPathPool`; raw pointer values never enter the record. Serialization and
parsing use bounded temporary vectors. The parser rejects inconsistent lengths,
counts, direct/split region IDs, neighbor IDs, truncation, trailing data, and
start locations outside the map before any SC or GameData allocation/write.

The bridge creates a genuine zeroed `BW::SAI_Paths`, including inline/pool neighbor
IDs and all split records, and points the stand-in SC global at `0x6D5BFC` to it.
Long lists use the genuine 10,000-word global buffer first, with a bounded stable
allocation if their total exceeds it. It also fills `GameData::regionCount`,
`regions`, `mapTileRegionId`, split tables, map dimensions, and start locations,
preserving region indices and neighbor order. As in BWAPI's own
`Server::onMatchStart`, only the first 5,000 splits fit the `GameData` split arrays;
the SC object retains all 25,000. BWAPI client lookups beyond those 5,000 remain
subject to the upstream `GameData` array limit. All metadata is committed before
the current `GameImpl::onMatchStart()` and `onStart()`; the bridge does not retain
the initial handoff pointer after the client may replace it.

Only one HXPM is accepted, and only before any frame. Duplicate/late records and
frames whose map differs from HXPM or earlier frames are rejected, so a second map
in one process cannot inherit stale metadata (the supported deployment remains
one match per process). A legacy stream without HXPM still runs, but the bridge
clears region metadata and logs **`HXPM absent: legacy region metadata is
INCOMPLETE`**; it does not invent a connected one-region graph.

There is **no unit-search index in HXPM**, and none is added to HXP4. Static
analysis of the pinned official v2.0 DLL (SHA-256
`603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc`)
found no positive or negated reference to native `UnitOrderingCount/X/Y` at
`0x66FF74`, `0x66FF78`, or `0x6769B8`. Its BWAPI Game calls are limited to
enemy race, chat, leaveGame, and debug drawing; there is no
`getUnitsInRectangle`, `getClosestUnitInRectangle`, or `getBestUnit` path.
Producing a dynamic sorted unit index would therefore be unused work for this
policy, not a fidelity improvement.

The same static audit found no reference to `MiniTileFlags` (`0x5993D0`),
`MapTileArray` (`0x5993C4`), `TileSetMap` (`0x6D5EC8`), or `SAIPathing`
(`0x6D5BFC`). The official spatial builder reads the four-byte `ActiveTileArray`
at `0x63FBB516` and `0x63FBB5BC`; it does not expand tiles into VF4 minitiles or
call BWAPI `isWalkable`. HXP4 consequently keeps the existing tile flags and
adds no minitile plane. These non-consumption proofs are specific to the pinned
official module: generic third-party BWAPI clients may still require minitile
walkability or dynamic unit-search data. `late-state/audit_official.py` repeats
the checks without loading the DLL.

Standalone coverage is in `engine/tools/pluto-bridge/metadata/` (roundtrip and
malformed native C++ tests). The bridge build and deterministic tests do not
establish every live-fork behavior: actual official-DLL consumption of
`SAIPathing`, startup-pipe timing with a large record, and overflow-neighbor behavior remain
unverified until a separately authorized live test. No replay or inference launch
is part of the HXPM tests.

### Legacy frame in (HXP2/HXPF/HXP3)

The following existing layouts are preserved byte-for-byte for old captures and
fallbacks. They do not gain HXP4 fields through trailing data.

| Field | Bytes | What |
|---|---|---|
| magic | 4 | `HXP3` = 0x33505848 (`HXP2` = 0x32505848 and legacy `HXPF` = 0x50465848 are also accepted) |
| frame | 4 | the game frame number |
| nUnits | 4 | the unit count (<= 9999) |
| mapW, mapH | 8 | the map size in tiles |
| nPlayers | 4 | the player count (<= 12) |
| units | nUnits * 40 | one `{u32 id,type,player,x,y,hp,shields,energy,order; u8 visible,completed,direction,generation}` a unit; `generation` reuses the former final `pad` byte and is native `GetUnitID() >> 11` (zero in legacy captures) |
| unit extras | nUnits * 64 | HXP3 only: full status flags, order/move/connected/build-unit slots, speed, current build and previous types, resources, order type, targets, remaining build time, queue slot/state, weapon/spell cooldowns, movement and facing bytes, order flags, detection, and the main order timer |
| players | nPlayers * 28 | one `{u32 race,minerals,gas,supplyTotal,supplyUsed,startX,startY}` a player |
| tiles | mapW*mapH*4 | HXP2/HXP3: the actual `gpSquareMap` four-byte tile flags (visibility, walkability, buildability, and terrain) |
| seen/explored | 2*((mapW*mapH+7)/8) | Legacy HXPF only: two visibility bitplanes |

### HXP4 frame in (stdin, per game frame)

HXP4 is selected only by its new magic. Its fixed **32-byte** header is:

| Offset | Type | Field |
|---|---|---|
| +0 | u32 | `HXP4` = 0x34505848 |
| +4 | u32 | game frame number |
| +8 | u32 | unit count, <= 9999 |
| +12, +16 | u32 | map width and height, each 1..256 |
| +20 | u32 | player count, <= 12 |
| +24 | u32 | **exactly 78** bytes per unit-extra record |
| +28 | u32 | **exactly 119** bytes per player-tech record |

Any other size words are an error, not an invitation to infer another layout.
The sections then appear with no padding:

1. **Units:** `nUnits * 40`, identical to HXP3 including the native generation
   byte. Nonzero HXP4 `(slot,generation)` identities have the same lifecycle
   semantics as HXP3.
2. **Unit extras:** `nUnits * 78`. The first 64 bytes are HXP3's exact extra;
   the appended 14 are, in order:
   - `u16 buildQueue[5]`, native slot order, including empty `228` entries;
   - `u16 remainingResearchTime` (the shared `CUnit+0xC6` timer);
   - `u8 techType` (`CUnit+0xC8`);
   - `u8 upgradeType` (`CUnit+0xC9`).

   `buildQueueSlot` must be 0..4 and `buildType` must equal the active queue
   entry. Every queue type must be 0..228. Under order 75 (`ResearchTech`),
   `remainingResearchTime` and `bTechType` are live and `upgradeType` is 61;
   under order 76 (`Upgrade`), the same timer and `bUpgradeType` are live and
   `techType` is 44. All other orders require timer zero, tech 44, and upgrade
   61. Pointer/hangar union aliases are not relabeled as research. Wide mod IDs
   fail production explicitly rather than being truncated to classic bytes.
3. **Players:** `nPlayers * 28`, identical to HXP3.
4. **Player tech:** `nPlayers * 119` in **exactly the same reordered player
   index space as the player records**: Pluto first, then other occupied slots,
   with unused and neutral slots zeroed. Each record is 44 researched bytes,
   61 current upgrade-level bytes, six tech-in-progress bytes, and eight
   upgrade-in-progress bytes. These are read from `get_tech_researched`,
   `get_upgrade_level`, `is_tech_in_progress`, and `is_upgrade_in_progress`.
   Owner levels are required because official upgrade progress uses
   `base[upgrade] + factor[upgrade] * currentLevel`, not just the CUnit timer.
5. **Tiles:** `mapW*mapH*4`, the same actual `gpSquareMap` flags as HXP3. No
   minitile or unit-search section follows.

Strict parsers reject wrong sizes, inconsistent queue/research arms, truncation,
and trailing bytes. An HXP3 frame with a plausible 14-byte suffix is rejected;
there is no silent HXP3-to-HXP4 reinterpretation. HXP2/HXPF keep their old
fallbacks, and HXP3 keeps its 64-byte extras and one-active-entry queue
projection. The logical BWAPI training queue starts at `buildQueueSlot`, rotates
modulo five, and stops at the first `228` or after five entries: raw
`{8,9,10,11,12}` with slot 2 becomes `{10,11,12,8,9}`.

HXP4 carries dynamic researched/level/progress state only. Existing ordinary
melee availability and max-level defaults are unchanged; custom availability,
mod IDs, and arbitrary pointer/hangar/cargo union arms remain outside this
format. Official reads own production internals, while visible owner upgrade
levels remain intentional BWAPI/native state. Generic `UnitData` projections
must continue to hide enemy production.

**Receiver integration.** Hyperion produces HXP4 and the
`pluto-diff`/acceptance/late-state codecs validate it. The bridge's
`ApplyImageState` parses the new header and sections, copies all five queue
entries and the active research arm into `ScUnit`, and populates native
`BWGame` researched/current-level/progress tables before the existing code-2
publication. `UnitData`/`PlayerData` receive the corresponding BWAPI projections
with enemy production masked. The opt-in `HX_TEST_LATE_STATE_DUMP` receiver
regression records these values immediately before `s_publication.Publish()`;
normal execution is unchanged.

For the receiver implementation, `ScUnit` must expose the native union arm at
`+0xC6` (`u16 upgradeResearchTime`), `+0xC8` (`u8 techType`), and `+0xC9`
(`u8 upgradeType`) without changing its `0x150` size. Initialize those to zero,
tech 44, and upgrade 61, then apply the live arm only under orders 75/76. Copy
all five queue words at `+0x98` and the slot at `+0xA4`; retain the existing
HXP3 one-active-entry and HXP2/HXPF empty-queue behavior.

For each reordered player `p` and HXP4 record `tech`, with `gg = Sc(SC_GAME)`:

- tech `t < 24`: `gg[0xDE54 + p*24 + t] = tech.hasResearched[t]`;
  otherwise `gg[0x10050 + p*20 + (t-24)]` (absolute bases `0x58CF44` and
  `0x58F140`).
- upgrade `u < 46`: `gg[0xE1C0 + p*46 + u] = tech.upgradeLevel[u]`;
  otherwise `gg[0x1023C + p*15 + (u-46)]` (absolute bases `0x58D2B0` and
  `0x58F32C`).
- copy six bytes to `gg + 0x10140 + p*6` (`0x58F230`) and eight to
  `gg + 0x102F0 + p*8` (`0x58F3E0`). These are the unified tech/upgrade
  in-progress bitsets. Clear the same dynamic fields for legacy frames rather
  than leaking prior HXP4 state, and preserve existing availability/max setup.

`UnitData.trainingQueueCount/trainingQueue` follows the modulo-five projection;
`tech` and `remainingResearchTime` are set only for order 75, while `upgrade`
and `remainingUpgradeTime` are set only for order 76. Populate
`PlayerData.upgradeLevel`, `hasResearched`, `isResearching`, and `isUpgrading`,
then apply the existing enemy-inside-access masks. Commit native units,
BWGame, UnitData, and PlayerData atomically under the current publication lock,
before code 2 and before frame-zero `onStart`. Do not add minitile or
unit-search writes to this integration.

Traps learned the hard way:

- `order` must be a real, alive order (the rig sends `PlayerGuard`). Sending 0
  (`Die`) makes the fork's engine drop every unit from its model: the obs stays at
  N=1 and no commands ever come back.
- **Fog of war is real, not a cheat**: `visible` per unit (always for Pluto's
  own units, otherwise `SpriteSeenBy(u->pSprite, slot)`) and the actual tile
  flags, including visibility remapped from Hyperion's slot order to BWAPI's
  player order. Detection is transported separately, so visible-but-undetected
  units remain visible without becoming detected. `completed` comes from the
  unit's real status flag, and start locations are transported in pixels then
  converted to tiles. Invisible enemies do not become accessible initial units.
  The bridge writes the planes
  into the tile array and links only the visible units into the visible-unit
  list — the list is the local player's view, and the fork's engine keeps
  everything else hidden. Units that were never seen never reach the obs at all;
  the net's map planes carry the fog too.
- The unit `id` is Hyperion's stable unit-table slot, not its changing position
  in the used list. The bridge preserves that slot in the SC image and writes
  the former `pad` byte to `CUnit::uniquenessIdentifier` at `0xA5`. Hyperion
  fills it with `GetUnitID() >> 11`; this preserves HXP3 and is carried
  unchanged by HXP4. Captures whose byte is zero remain accepted. The bridge
  assigns a permanent packed BWAPI id to each logical `(slot, generation)`
  lifetime. Command ids (9363 + slot) map back to the packed id captured at the
  reply's emission frame. Before native dispatch, a targeted command is removed
  whole unless that exact emission id still resolves to the same live unit;
  stale targets never become location orders or retarget a replacement. A
  genuine target id zero remains a position order. A changed generation retires
  the old accessible identity with
  Hide/Evade/Destroy before Create/Discover for its replacement, never reusing
  the old packed id. An all-zero legacy stream retains slot-based behavior, so
  replacement without an intervening absent frame is inherently indistinguishable
  there; an observed death followed by zero-generation reuse still gets a new id.
- The bridge applies live frames to the fork's *SC image*. A state-publication
  lock and client-driven publication/acknowledgement sequence keep the pipe from
  observing half-written unit links, dynamic UnitData, or event records: complete
  state is published before code 2, and the next write waits for the client's
  code 1 acknowledgement. Every present unit is refreshed each frame; deaths,
  fog evades, and slot replacements clear their old records and `unitArray`
  entries. Invisible enemy records are sanitized, enemy production internals and
  resources are masked, and neutral units retain BWAPI's initial-discovery
  accessibility exception.
- The standalone lifecycle tracker in `engine/tools/pluto-bridge/lifecycle/`
  follows genuine BWAPI 4.4.0 `GameUnits.cpp`: Create, Complete when appropriate
  (including a newly created already-completed accessible unit), Discover,
  Show/Hide; Hide/Evade on fog; Hide/Evade/Destroy for an accessible death; then
  Morph/Renegade. Frame 0 still emits only the existing initial Discover events.
  Initial neutral units are accessible even outside current vision. Detection
  remains independent of visibility. Event writes
  are bounded by `GameData::MAX_EVENTS`; overflow is counted and logged rather
  than writing past the shared-memory array.
- Frame 0 is applied. `onStart` runs only after the scripted startup scaffold
  has been replaced by the actual match's players, units, resources, and map.

### Reply out (stdout, per frame)

| Field | Bytes | What |
|---|---|---|
| magic | 4 | `HXRP` = 0x50525848 |
| frame | 4 | the frame the commands belong to |
| n | 4 | the command bytes that follow |
| cmds | n | the bot's classic net commands, appended to StarCraft's TurnBuffer; the bridge scans the self-describing opcode stream and sends the delta, because the bot's byte count does not land reliably in the harness |
| win | 4 | 0 for now |

`port_pluto.cpp` reads `12 + n + 4` per reply and re-syncs on the magic.

At match end Hyperion sends a 12-byte `HXPE` record (`magic`, `winner`, zero)
before closing transport. `winner` is 1 only when Pluto is the last side alive;
otherwise it is 0. The bridge calls `onEnd` only after receiving this record, so
benchmark setup and aborted runs cannot fabricate a win or loss for the bandit
history. Match acceptance uses Hyperion's game-state summary, never `onEnd`.

## The obs architecture, mapped (2026-11-03)

- The writer at base+0x34960 is a serializer: it memcpys the fork's prebuilt
  vectors into the view (the units at +0xC100, the planes at +0x79100/+0x79500,
  the small vectors at +0x79900/+0x79940/+0x79A40).
- The floats are computed in the feature builders (x87 chains) that push into
  those vectors; the whole DLL holds only 27 float-store sites.
- The direction embeddings are sin/cos pairs of computed angles: the fork's
  sincos helpers are at 0x2C210 (double out) and 0x2C250 (float out), called
  indirectly from the builders (no direct call sites).
- Six marine-position scenarios (1/4/5/6/7/8) are captured in the rig's
  dump-s*/ for the angle-solving pass; the embedding angles quantize to the
  22.5-degree bucket family (1, 0.707, 0.924, 0.383, 0.556, 0.831, 0.981,
  0.195), and the pairs do not fit any fixed anchor -- the target per pair is
  still to be solved.

## The embedding formula, solved (2026-11-03, the sincos trace)

The sincos helpers have no static callers in the DLL -- the calls are
materialized at runtime. A research-only bridge traced them by patching the
helpers' prologues to log `(return address, angle)`, which named the call sites
and their angle arguments directly (that instrumentation is not in the live
bridge):

- `0x30A94`/`0x30AD8` -- the facing pair at entry +0x24/+0x28:
  angle = facing byte (unit +0x21) × the scale at 0x63FDCB2C.
- `0x3153F`/`0x31587` -- the embedding loop, 8 iterations k = 0..7, each
  storing four floats at entry +0x134+16k:
  `[sin θk, cos θk, sin φk, cos φk]`.

The traced angles are exact multiples: for the scenario units
(608,928), (672,1064), (640,864), (512,1224) the k=0 angles were
119.3805, 131.9469, 125.6637, 100.5310 (= x·π/16) and
182.2124, 208.9159, 169.6460, 240.3318 (= y·π/16), each halving per k.
So:

- θk = x·π/(16·2^k), φk = y·π/(16·2^k) -- sin/cos of the raw pixel
  coordinates scaled by 2^-k; period 32·2^k px (one tile at k=0).
- The "bucket family" the captures showed (multiples of 11.25° = π/16, with
  5.625° = π/32 appearing at k=1) is the coordinate folding itself -- not a
  lookup table. Every captured pair from the rig's dumps matches the formula
  exactly (e.g. the hatchery's pairs: sin 38π = 0, cos 38π = 1, sin 58π = 0,
  cos 58π = 1, then cos 19π = -1 at k=1, ...).
- The two scale constants at 0x63FDCB3C/0x63FDCB40 cancel out of the result
  (their ratio is 1); they live between the DLL's sections at its preferred
  base and the fork resolves them through its own fault-tolerant path under
  the harness, so a Hyperion-side port should use the formula, not the
  storage.

The loop's helper call (`0x1CB0` with args `(2.0f, k)`) is the 2^k factor.
The facing pair's scale constant reads 0 in the harness (the scripted units
face 0 anyway); the byte source is the CUnit-facing at +0x21.
