# The Pluto rig: run pluto.dll against scripted game state

A harness for reverse-engineering Pluto v2.0 (the closed-source BWAPI bot) so its
inference protocol can be reimplemented on Hyperion's side. Everything here runs under
Wine with a user-space llvm-mingw i686 toolchain — no StarCraft, no BWAPI.dll.

## What it does

- `fakehost.exe` (32-bit, built by `build.sh`) compiles BWAPI 4.4.0's own client-side
  `GameImpl`/`PlayerImpl`/`UnitImpl` (from a local copy of github.com/bwapi/bwapi at
  v4.4.0, a few include-order patches applied) over a `GameData` block it fills with
  scripted state, and loads pluto.dll, calling `newAIModule` and `gameInit` as the
  real framework does.
- pluto.dll expects to run *inside StarCraft 1.16.1*: it writes StarCraft's own globals
  by absolute address (the speed uncap and friends) and assumes SC's memory layout.
  `patch_pluto.py` rewrites every absolute memory operand in a copy of the DLL onto two
  stand-in buffers the host reserves, so the writes land harmlessly.
- The bot's module vtable is NOT BWAPI 4.4.0's AIModule layout (no virtual destructor):
  from the disassembly, `[1]` = onStart (probes `Game::[70]` and the result's `[4]`
  against event-type mask 0x147, then calls the real worker), `[2]` = onEnd(bool),
  `[3]` = onFrame. The host drives them directly.
- On start the bot launches `pluto/pluto_infer.exe` (kept beside it) and speaks to it
  over `BWRL_SHM` + events; the host dumps that mapping every frame into `dump/` for
  schema work. `pluto.log` and `pluto_infer.log` report the bot's and engine's view.

## Getting it to run (current state)

1. `tools/fetch` the release into `rig/../v2.0-release/` (pluto.dll, pluto/).
2. `python patch_pluto.py ../v2.0-release/pluto.dll pluto.patched.dll`
3. `./build.sh`  (needs llvm-mingw i686 at the path in the script)
4. `wine fakehost.exe 1`

Current: the bot's `onStart` runs, launches the engine, the engine loads the model and
handshakes over shm (`BWRL: inference server up ... shm transport`). Open items below.

## Open items (the next RE steps)

- **The ABI mismatch.** The bot's compiled BWAPI interface (Game/Player/Unit vtables)
  does not match the public 4.4.0 headers: its calls into the host's `GameImpl` hit
  different slots than the headers claim. Consequence: the bot logs `own_race=-1` and
  crashes at the end of onStart. Map each slot the bot calls (trace all host-side
  `GameImpl`/`PlayerImpl`/`UnitImpl` methods, or follow the calls in the worker at
  `pluto.dll + 0x85cd0`) and align the host's vtable to the bot's expectations.
- Then capture `BWRL_SHM` per frame across scripted scenarios (units moved, resources
  changed, fog changed) and diff to name every field of the observation.
- The action side: read the bot's `UnitCommand`s out of `GameData.unitCommands` while
  the engine's responses arrive — each `(id, arg, pos, unit)` action lands there via
  the host's `UnitImpl::issueCommand`.

## The protocol so far (see docs/pluto-ai.md)

magic `PLO1`, version 4, frames_per_step 6, max_units 768; request = obs (N units,
W×H map, globals), reply = per-unit actions + win estimate; transport = `BWRL_SHM`
+ events, pipes fallback; engine args `--bench --blob --seed --threads --backend ...`.

## State 2026-11-01 (session 2)

The bot now completes its entire startup in the rig:
- onStart runs (its vtable is not BWAPI's AIModule: [1] = a race probe calling
  self()->[4](&out) then the real worker, [2] = onEnd(bool), [3] = onFrame).
- Its fork-ABI differences are shimmed by patching vtable slots on the objects handed
  out: Player slot 4 = a race getter (out-param + eax), Game's vPrintf slot = (int,
  const char*) chat printer. patch_pluto.py now remaps every SC-address constant (SIB
  displacements included) onto the stand-in buffers.
- Verified end to end: `own_race=0 ... BO free/UltraLing~tr (22 arms)`, the chat
  announcement ("Pluto online (v2.0 md09b_1331000). gl hf!"), the engine's shm
  handshake ("inference server up ... shm transport").

## State 2026-11-01 (session 4) — the full pipeline runs; the schema is captured

The miniature 1.16.1 memory image in the stand-in buffers unblocked everything:

- The emulation fills the UnitNodeTable (0x59CCA8, 8 scripted CUnit entries linked
  through UnitNodeList_VisibleUnit_First at 0x628430, the rest type 228 = None), the
  Players array (0x57EEE0), playerStatusArray, and the BWGame fields the bot reads:
  minerals/gas (+0x00/+0x30), mapTileSize (+0xE4), frameCount (+0x14C, per frame),
  mapFileName (+0xC4C), supplies (+0x3054), unitCounts (+0x3234), NetMode/gwGameMode.
  The ActiveTileArray at 0x6D1260 is a POINTER, so it points at a rig tile buffer
  (visible, explored, walkable). The bot's call into SC's sendTurn (0x485A40) gets a
  ret stub in the stand-in.
- With that, the bot plays: 200 inference steps over 1200 frames (33 ms a step),
  issues orders (raw net commands into SC's TurnBuffer at 0x654880, byte count at
  0x654AA0), and even credits its bandit at the end.
- The obs is captured from the bot's own BWRL_SHM mapping (the client's view at
  base+0x54160+0x18, set by the bot's own handshake inside onStart — do NOT inject
  over it). The layout: request header at +0xC0 (N, W, H), the per-unit feature
  block from +0xC100 (one 436-byte entry a unit, 9 used of 768), 32x32 feature
  planes at +0x79100/+0x79500, small vectors at +0x79900/+0x79940/+0x79A40, a
  128x128 u16 map plane at +0x79F00, a 128x128 byte plane at +0x81F00, and the
  globals/counts from +0x85F00.
- The unit entry (436 bytes = 109 floats) starts with x, y scaled by 1/4096, then
  float flags (isMoving, isIdle, ...). A differential run (the marine teleported at
  frame 600) names the position fields and the flags; the rest is read straight
  from the feature writer's disassembly (base+0x307F0, the fstp [esi+off] list).
- The actions decode as the classic net commands: 0x09 Select (count, u16 unit ids),
  0x14 RightClick (x, y, target, type, queued), 0x15 Action (x, y, target, type,
  order, queued) — the same command language Hyperion's net_cmds.cpp parses. The
  bot's unit ids (0x2494..) are its own; Hyperion just echoes them.

Open (next session):

- Read the feature writer's disassembly end to end for the exact per-unit field
  list (the fstp [esi+off] sequence), then the Hyperion-side obs builder is
  mechanical: the header, the unit features, the map planes, the globals.
- The action executor: the reply's Select/RightClick/Action commands translate to
  Hyperion's unit orders (the command layer is the same as a classic replay's).
