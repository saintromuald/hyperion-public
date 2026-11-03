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

## The request (the observation)

The view's layout, written by the writer (base+0x34960, this = the shm client,
args = the built obs objects and the global at base+0x73254):

| Offset | Size | What |
|---|---|---|
| +0x00 | 0x40 | the PLO1 header (magic at +0x80 in the rig's .data copy; the engine's header) |
| +0xC0 | 4 | N, the unit count |
| +0xC4 | 4 | W, the map width in tiles |
| +0xC8 | 4 | H, the map height in tiles |
| +0xCC | 40 | zeros (the header's tail) |
| +0xC100 | — | the unit feature block: one 436-byte entry a unit, in the visible-unit list order, up to 768 |
| +0x79100 | 1024 | a 32x32 byte feature plane |
| +0x79500 | 1024 | a 32x32 byte feature plane |
| +0x79900 | 64 | a small vector |
| +0x79940 | 256 | a small vector |
| +0x79A40 | 1216 | a vector |
| +0x79F00 | 32768 | a 128x128 u16 map plane |
| +0x81F00 | 16384 | a 128x128 byte map plane |
| +0x85F00 | — | the globals and the per-type counts |

The globals tail (2026-11-03, from the bridge's live captures): the engine's
own log sizes the vector at **303 floats** ("globals 303 incl. deploy lambda").
+0x85F00 = the player count (2); +0x85F40..+0x85F58 = integer counters
(3 / 229 / 1 in the 4-zerg-units scenario -- not yet individually named);
+0x85F60+ = float scalars; +0x86000+ = a map-name/checksum block. The full
field naming is the mechanical differential pass: run two scripted scenarios
that differ in one count at a time and diff this region.

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
- **+0x79A40 (the 1216-byte vector) and the globals tail** move with every
  variant and, for the globals, even across runs of the same scenario: they
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
| +0x28, +0x30, +0x40, +0x48, +0x58, +0x70 | flags, all 1.0 in the captures |
| +0x3C | isMoving (0 -> 1 when the unit starts moving) |
| +0x50, +0x5C | a race bit (1.0 for zerg, 0.0 for terran) |
| +0x0C..+0x20, +0x2C..+0x3B, +0x54..+0x134 | zeros in the captures (cooldowns, shields, energy, orders) |
| +0x24, +0x28 | the facing pair: (sin, cos) of the facing byte (unit +0x21) times the scale at 0x63FDCB2C; the rig's scripted units face 0, so both read (0, 1) |
| +0x134..+0x1B0 | 32 floats: 8 blocks of 4, one per scale k = 0..7: (sin θk, cos θk, sin φk, cos φk) with θk = x·π/(16·2^k), φk = y·π/(16·2^k) -- the raw x and y as multi-scale positional encodings (a period of 32·2^k px). Solved and verified: see "the embedding formula" below |

There is NO per-unit type one-hot: the net learns the unit identity from these
features plus the globals' per-type counts. The unit entries carry x, y,
hp/maxHP, the race, the flags and the embeddings only.
| the rest | the one-hots (unit type, player), cooldowns, shields, energy, order and target — to be read from the per-unit writer's disassembly (the fstp [esi+off] sequence from base+0x307F0) |

## The reply (the actions)

Classic net commands appended to StarCraft's TurnBuffer (0x654880, byte count at
0x654AA0) — the same command language Hyperion's `net_cmds.cpp` parses:

- `0x09` Select: `{op, count, u16 unit ids[count]}` (the bot's own unit ids, echoed
  from the obs Hyperion builds)
- `0x14` RightClick: `{op, u16 x, u16 y, u16 targetId, u16 targetType, u8 queued}`
- `0x15` Action: `{op, u16 x, u16 y, u16 targetId, u16 targetType, u8 order, u8 queued}`

## What Hyperion must do

1. Own `pluto_infer.exe` as a helper process (the AI lab's pattern), over the shm +
   events.
2. Every 6th game frame, build the observation from Hyperion's game state — the
   header, one 436-byte entry per visible unit (position/4096, hp/1600, the flags,
   the one-hots and the embeddings), the map planes and the globals — and signal
   the request event.
3. Read the reply's net commands and turn them into Hyperion's unit orders (the
   command layer is the classic one).
4. The level: `AI_LEVEL_PLUTO` in `port_ai.h`, the skirmish row, `--ai-levels`, the
   replay/save record — and the room's computer seat rides the same level.

## The bridge protocol (Hyperion <-> pluto-bridge.exe)

The bridge (`engine/tools/pluto-bridge/`) runs the fork's DLL under Wine and owns
the obs build; Hyperion only feeds game frames and receives net commands, over the
bridge's stdin/stdout. All little-endian.

### Frame in (stdin, per game frame)

| Field | Bytes | What |
|---|---|---|
| magic | 4 | `HXPF` = 0x50465848 |
| frame | 4 | the game frame number |
| nUnits | 4 | the unit count (<= 9999) |
| mapW, mapH | 8 | the map size in tiles |
| nPlayers | 4 | the player count (<= 12) |
| units | nUnits * 40 | one `{u32 id,type,player,x,y,hp,shields,energy,order; u8 visible,completed,pad[2]}` a unit |
| players | nPlayers * 28 | one `{u32 race,minerals,gas,supplyTotal,supplyUsed,startX,startY}` a player |
| seen | (mapW*mapH+7)/8 | one bit a tile in row order: the tile is seen by the Pluto slot |
| explored | (mapW*mapH+7)/8 | one bit a tile: the tile is explored by the Pluto slot |

Traps learned the hard way:

- `order` must be a real, alive order (the rig sends `PlayerGuard`). Sending 0
  (`Die`) makes the fork's engine drop every unit from its model: the obs stays at
  N=1 and no commands ever come back.
- **Fog of war is real, not a cheat**: `visible` per unit (the Pluto slot's own
  vision — `SpriteSeenBy(u->pSprite, slot)` on Hyperion's side) and the seen /
  explored tile planes (`VisionHxTileSeen/Explored`). The bridge writes the planes
  into the tile array and links only the visible units into the visible-unit
  list — the list is the local player's view, and the fork's engine keeps
  everything else hidden. Units that were never seen never reach the obs at all;
  the net's map planes carry the fog too.
- The unit `id`s are the positions in the list (0..n-1), sent fresh each frame.
  The bridge maps slot = id, so the fork's model index equals the Hyperion list
  position, and the commands' unit ids (9363 + index) map straight back.
- The bridge applies the frame to the fork's *SC image*, never to the fork's
  GameData: the fork's own server converts the image into the GameData every
  frame, and the bridge writing it concurrently broke the server's unit sync.
- Frame 0 is not applied: the start-up image is already complete before the
  fork's threads first read it.

### Reply out (stdout, per frame)

| Field | Bytes | What |
|---|---|---|
| magic | 4 | `HXRP` = 0x50525848 |
| frame | 4 | the frame the commands belong to |
| n | 4 | the command bytes that follow |
| cmds | n | the bot's classic net commands, appended to StarCraft's TurnBuffer; the bridge scans the self-describing opcode stream and sends the delta, because the bot's byte count does not land reliably in the harness |
| win | 4 | 0 for now |

`port_pluto.cpp` reads `12 + n + 4` per reply and re-syncs on the magic.

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
materialized at runtime. The bridge now traces them (`HX_DEBUG` +
`ArmSincosTrace`: the helpers' prologues are patched at runtime to stubs in the
bridge exe that log `(return address, angle)` into `dump/sincos.log`), which
named the call sites and their angle arguments directly:

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
