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
