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

## The unit entry (436 bytes, 109 floats)

Each field is a float32. Offsets are within the entry; values are from a live
capture with 8 scripted units (hatchery, drone, drone, overlord, command center,
SCV, SCV, marine — the rig's scenario 1) unless noted.

| Offset | Field (as named) |
|---|---|
| +0x00, +0x04 | position x, y, scaled by 1/4096 |
| +0x08 | exists (1.0) |
| +0x28, +0x30, +0x40, +0x58, +0x70 | flags (completed and friends, all 1.0) |
| +0x48 | hit points, scaled by 1/1600, clamped to 1.0 (drone's 40 = 0.025) |
| +0x50, +0x5C | a race bit (1.0 for zerg, 0.0 for terran) |
| +0x78, +0xB0, +0xB4 | flags true only for the bot's first selected unit in the capture |
| +0x134..+0x190 | direction embeddings, sin/cos pairs (the ±0.707/±0.924/±0.383 family) |
| +0x194..+0x1B0 | normalized-vector embeddings (values like ±0.95..0.99) |
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
