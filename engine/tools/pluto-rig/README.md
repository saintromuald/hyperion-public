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
