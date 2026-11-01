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

Open (next session):
- The bot's set-returning Unit slots (63 getLoadedUnits / 65 getInterceptors /
  67 getLarva) crash its iteration — its set layout/convention is still to pin down
  (12-byte vector-style with an sret pointer is the current hypothesis; the empty-set
  shim is in place and being adjusted).
- After that: the observation builder completes per frame, `dump/` fills with
  BWRL_SHM captures, and the schema extraction begins (differential scenarios).

## State 2026-11-01 (session 2, continued)

- Slot audit (PMF encoding) corrected the shims: the set-returning Unit slots are
  64 getLoadedUnits / 66 getInterceptors / 68 getLarva (the vtable has one more
  leading entry than assumed; getHatchery=67, getTransport=63).
- fakehost now installs a vectored exception handler: every fault logs eip/esp/ret/
  eax/ecx/edx to fault.log (the caller address is the key to the bot's desynced
  disassembly regions).
- Remaining blocker: after a complete onStart (race, bandit, engine handshake, chat),
  the bot crashes in its engine-launch continuation: caller 0x63f8d9c8 -> its
  "probe" 0x63f815d0 -> the vt[1] mid-entry at 0x63f81490 (skips the null checks;
  ebx must be self — set by the desynced real caller code), finally executing a null
  function pointer. Next: single-step under winedbg (break 0x63f8d9c8, dump the
  real code there — objdump and llvm-objdump both desync in that region), or
  alternatively disassemble 0x63f8d9c8's real caller with ndisasm (present on the
  machine) to see what sets eax/ebx before the call.
- After that: the obs builder finishes per frame; BWRL_SHM dumps land in dump/ and
  the schema extraction starts.

## State 2026-11-01 (session 2, third pass)

The frame loop RUNS: after onStart the bot's onFrame executes hundreds of frames
("frame 0/100/200/300"), with a vectored handler redirecting the one null-call to a
ret stub so the bot limps on. The remaining crash shows the true scale of the ABI
gap: the bot's fork has different BY-VALUE TYPE LAYOUTS than public BWAPI 4.4.0
(UnitType, Race, Position, Order, ...). Those layouts are measurable directly: the
bot's own static type instances (UnitTypes::Terran_Marine etc., 233 consecutive
objects) live in the DLL's data — the spacing between consecutive statics gives each
type's size, and the fields follow by diffing against known values. Next: measure the
type layouts from the DLL's statics, then add shims for every by-value return, then
the obs builder completes and dump/ captures begin.

## State 2026-11-01 (session 2, fourth pass) — the game loop runs

- The rig now plays a complete 1200-frame game: onStart (race, bandit, engine, chat) and
  every onFrame, with only one absorbed null-call. The stack corruption was the
  empty-set shim zeroing 16 bytes where the bot's set is 12 ({begin,end,cap}); fixed.
- Slot-numbered call tracing confirms the bot calls the host's Unit/Player/Game slots
  exactly at the 4.4.0 layout (+2 shift from the header order — the CRTP base and the
  inline destructor), except the known fork slots (Player[4] race getter with an
  out-param, the set-returning Unit slots, the Game chat slot).
- Enum id tables found in the bot's .rdata (0x57080 and 0x57160, with the categorized
  unit-id subtables after them) — the fork's type data lives in the loaded image and
  is measurable there.
- Remaining for the obs capture: the one null-call is the bot's shm-send entry
  (ret=0x7b19d9cd, the wrapper's continuation) — find the gate that initialises its
  send pointer, then dump/ fills with the observation and the schema extraction starts.

## State 2026-11-01 (session 2, final pass of the day)

- The transient null-call is the bot's first engine-launch attempt failing and being
  retried (its own retry path probes the race before the engine is up); absorbing it is
  harmless — the launch succeeds on a later attempt.
- The bot's obs never ships: at run end "BWRL_SHM" does not exist in the host's
  namespace (err=2), so the bot's per-game shm client is never initialised in the rig.
  The remaining gate is the bot's send-side init (its client object / the engine's
  ready event path). Next: trace the onFrame's send path (the slot-numbered call log
  plus the fault handler's frame walk), find the gate, satisfy it, then dump/ captures
  the observation per frame and the schema extraction starts.

## State 2026-11-01 (final finding of the session)

- **The shm is the bot's own .data**: the PLO1 header sits at loaded address
  0x7b190000+0x54080 (found by scanning for the magic; `shm-header-reference.bin` is
  the first 8KB: magic PLO1, version 4, the -0.9f resign threshold, the section
  offsets 0xCC/0xA4/0xA8/0xB0/0xB4/0xB8, and the arm-table region). No file mapping
  named BWRL_SHM exists — the send writes into this in-process region (shared with
  the engine via a mapping of it).
- The obs region after the header is byte-static across all 1200 frames: the bot's
  per-frame obs serializer runs (helpers 0x63fb0130 etc. called each frame) but the
  commit to the region is gated on the engine's ready-reply pump, which the rig does
  not yet service. Next: single-step the commit gate (the reply event), then the
  region fills and the schema extraction is mechanical.

## State 2026-11-01 (session 2, closing)

- The obs-send gate found and opened: the client is a static object at base+0x54160;
  the handshake leaves its view pointer (+0x18) and send flag (+0x1C) unset in the
  rig — setting view=0x7b1e4000 (the .data page holding the PLO1 header) and flag=1
  makes the bot's writer (0x63fb4960) run: it writes the request header at
  [view+0xC0] (N units) and serializes the obs at [view+0x100].
- The obs serialization now crashes on the bot's unit-record walk — the last
  unknown is the fork's per-unit record format (the serializer's node shape
  {next, a..e} at 0x63fb0130). Next: measure the record format from the serializer's
  disassembly + the client object, then the schema is complete and the Hyperion-side
  sender can be written.


## State 2026-11-01 (session 3)

The obs path is now mapped end to end; the schema extraction is one gate away.

- The writer runs with a real writable view: a 1 MB buffer seeded with the bot's .data
  page, instead of the .data page itself. The writer writes the obs at
  view+0xC0..+0x85F1C, past .data's end (base+0x54678) into read-only .rdata, which
  crashed it before.
- The writer's this is the shm client at base+0x54160 (view +0x18, send flag +0x1C,
  a frame counter +0x20); its three args are the obs objects: the local build
  ([arg1+0x54] = N units), a second object (W/H at +0x18/+0x1C), and the global at
  base+0x73254.
- The obs layout the writer produces: the request header at view+0xC0 (N, W, H, then
  zeros), then fixed-offset spans: +0xC100, +0x79100, +0x79500, +0x79900, +0x79940,
  +0x79A40, +0x79F00, +0x81F00, +0x85F00 — each a (begin,end) span copied from the
  obs objects.
- The build is gated by three globals, all read by the driver at base+0xD01B..0xD04E:
  base+0x72654 (dword, the next obs frame), base+0x72658 (byte, already sent) and
  base+0x72661 (byte, engine ready). The driver builds the obs, walks the record list
  (the walk, base+0x30130) and calls the writer (base+0x34960); the writer's success
  sets "already sent", and the engine's reply pump clears it in a real game.
- The record node shape, from the walk's disassembly: {next(+0), b(+4), c(+8), d(+0xC),
  e(+0x10), f(+0x14), g(+0x18)} — 28 bytes; the obs unit entry is the six fields
  {b..g}, 24 bytes, appended by base+0x2E110.
- The walk's this is the obs-state object: a static at base+0x73120, and a copy owned
  by the module object (a tap caught the module at 0x7B20202C walking its copy at
  0x7B203120, module+0x10F4). The record-list head field is +0x1EC in both.
- The per-unit obs-entry writer (base+0x307F0/0x30850, floats written to [esi]) reads
  the unit record at ecx: position x/y scaled by the map size, an id, and more.
- The harness now has: the writable view buffer, the BSS zero-fill tail made writable
  (Wine maps it read-only; base+0x72000..0x74000 covers the gates and the static
  obs-state), fault logging with the fault address and code, and guard-page taps
  (PAGE_GUARD + the vectored handler) on the driver, walk and writer pages that log
  every execution with the full register context.

Open (next session):

- Read the module-owned obs-state (module+0x10F4) instead of the static: its +0x1EC
  head is the real record list; in the rig the static's head reads 0x0F000009 and the
  module's walker runs with edi=0x0F000009 without faulting, so the head's
  representation is the last unknown (a tagged pointer, or a field read one word off).
- Once the head is pinned down, the walk completes, the writer's header (view+0xC0)
  fills with N, and dump/ captures the observation; the differential scenarios
  (move a unit, change resources) then name every field.
