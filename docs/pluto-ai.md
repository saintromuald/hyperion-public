# Pluto AI as a computer level

Goal: Pluto (github.com/tscmoo/pluto — the self-play RL Brood War bot, CoG 2026 winner)
as a new skirmish computer level (`AI_LEVEL_PLUTO`, beside Classic ... Cheater Insane in
`port_ai.h`), playing 1v1 melee through `pluto_infer.exe` with Hyperion as its BWAPI.

## Shape

- The skirmish set-up's computer level pulldown gains "Pluto" (and `--ai-levels` a letter);
  the replay and save record it like any other level (`AiProfilesSave`).
- Hyperion builds Pluto's observation each 6th game frame from its own game state, sends it
  to `pluto_infer.exe` (the released 64-bit CPU engine), and turns the answer — one action a
  unit, `(id, arg, pos, unit)` — into Hyperion's unit orders.
- The bot's binaries ship beside Hyperion (`pluto/pluto_infer.exe`,
  `pluto/pluto_weights.bin`), started and owned by Hyperion as the AI lab starts its workers.
  No BWAPI, no StarCraft.exe, no 32-bit DLL in process: Hyperion replaces pluto.dll's half
  of the protocol.

## What the binaries are (v2.0, model md09b_1331000)

| file | what | sha256 |
|---|---|---|
| `pluto.dll` | 32-bit BWAPI 4.4.0 module (closed source, stripped) | 603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc |
| `pluto/pluto_infer.exe` | 64-bit CPU inference engine (closed source) | 3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc |
| `pluto/pluto_weights.bin` | int8 weights, 348 MB | c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86 |

Requirements: x86-64 with AVX2 (AVX-VNNI doubles the step rate); a step every 6 game
frames, ~20-60 ms on a current CPU; the game slows rather than the bot playing worse.

## The protocol, as read from the binaries

Transport: shared memory + events, with a pipes fallback.

- Shared objects: `BWRL_SHM` (the name overridable by the `BWRL_SHM` environment variable,
  in both binaries), `PLUTO_SHM_PARENT`, `PLUTO_SHM_REQ_EV`, `PLUTO_SHM_RESP_EV`,
  `PLUTO_SHM_SEC`. Made with CreateFileMappingA / MapViewOfFile / CreateSemaphoreA.
- The header, as `pluto_infer.exe` prints it running alone: magic `PLO1`, version **4**,
  `frames_per_step` **6**, a field 3, the model step **1331000**, the build string
  ("v2.0 md09b_1331000"), the backend string ("int8mv 6t Sep 23 2026 L:b1d0v3"), then the
  bandit arm table: 28 arms, a line each — race (`Z`/`T`/`P`), an id (`0x00020000`,
  id 0 with flags 0x20000), three float priors, and the build's name ("9PoolSpeed", "free",
  "free+hint"). The exe refuses an arm table or max_units past protocol caps, and a magic
  or version mismatch ("bad handshake", "shm header mismatch ... using pipes").
- Model: GRU with qk_norm, 303 globals, max_units **768**, a deployment lambda `b1d0v3`;
  `--bench` picks the thread count and writes `pluto/pluto_config.json`.
- A request: the observation — "bad request header (N=%d W=%d H=%d)" names N units and the
  map's W×H; frames carry ids ("obs F%d flushed F%d (slip %d, infer %.1f ms), next obs F%d").
  The reply: "step %ld N=%d a=%d arg=%d pos=%lld u=%d lp=%.3f win=%.3f" — N actions, each a
  code, an argument, a position, a unit; the step's log-probability and win estimate.
- Engine args: `--argmax --backend --bench --blob --log --prof --prof-action
  --prof-gap --prof-ready --prof-secs --ptx --seed --threads --verbose`.
- Environment: `BWRL_DRAW` (win-probability graph), `BWRL_RESIGN_THRESHOLD`,
  `BWRL_FRAME_BUDGET_MS`, `BWRL_STRADDLE`, `BWRL_STDOUT` (the header dump), `BWRL_ENGINE_THREADS`,
  `BWRL_DUMP_RESID`, `BWRL_LOCKSTEP`, `BWRL_NO_VNNI`, and more.

## Status and the open items

- **Author contact**: issues and discussions are disabled on tscmoo/pluto; the commits'
  email is GitHub's noreply. The workable channel is the SSCAIT Discord (the BW AI
  community's; PurpleWave pointed there in the tl.net thread). The ask: the obs/action wire
  format, or the DLL's protocol layer, under a permissive license; and a LICENSE file, since
  nothing licenses redistributing the binaries. Until that lands, a released package cannot
  carry the weights.
- **The field schema is the unknown**: the exact observation encoding (the map features
  W×H, the per-unit tensor layout, the globals) and the action codes live only in the
  stripped DLL. Plan of record if the author stays quiet:
  1. BWAPI 4.4.0 + StarCraft 1.16.1 under Wine with pluto.dll as the bot;
  2. capture `BWRL_SHM` once a frame (a Wine-visible file mapping), with `BWRL_DUMP_RESID=1`;
  3. differential RE: change one thing in the game (spawn a unit, move it, spend minerals)
     and diff the request bytes to name each field;
  4. reimplement the sender and the action executor on Hyperion's side;
  5. the level: `AI_LEVEL_PLUTO` in `port_ai.h`, its letter, the skirmish row, `--ai-levels`,
     and the replay/save record (all patterned on the existing levels).
- Hyperion-side pieces, all present: full game state for the observation, unit orders for
  the actions, an AI boundary made for another level, and the lab's pattern of starting and
  owning a helper process.

## The rig (tools/pluto-rig) — status 2026-11-01, session 3

The obs path is mapped end to end; the schema extraction is one gate away.
New this session, read from the binaries and the running rig:

- **The writer runs with a real writable view.** The writer (this = the shm client at
  base+0x54160: view +0x18, send flag +0x1C, frame counter +0x20; args = the two obs
  objects and the global at base+0x73254) writes the request header at view+0xC0
  (N, W, H, zeros) and the obs at fixed offsets — +0xC100, +0x79100, +0x79500,
  +0x79900, +0x79940, +0x79A40, +0x79F00, +0x81F00, +0x85F00 — each a (begin,end)
  span of the obs objects. The rig's view had pointed at the bot's .data page, past
  which the writes hit read-only .rdata; a 1 MB writable buffer seeded with the
  header page fixes it.
- **The build gates are three globals**, read by the driver at base+0xD01B..0xD04E:
  base+0x72654 (dword, the next obs frame), base+0x72658 (byte, already sent; the
  writer's success sets it, the engine's reply pump clears it) and base+0x72661
  (byte, engine ready). The driver builds the obs, walks the record list (the walk,
  base+0x30130) and calls the writer (base+0x34960).
- **The record node shape is {next, b..g}, 28 bytes** (from the walk's disassembly);
  the obs unit entry is the six fields {b..g}, 24 bytes, appended by base+0x2E110.
  A second per-unit writer (base+0x307F0/0x30850) turns a record (ecx) into the
  float features ([esi]): position x/y scaled by the map size, an id, and more.
- **The obs-state object** (record list head at +0x1EC) is a static at base+0x73120
  and a copy owned by the module object (module+0x10F4, seen beside the module in a
  tap). The static's head reads 0x0F000009 in the rig, and the module's walker runs
  with edi=0x0F000009 without faulting — the head's representation is the last
  unknown (a tagged pointer, or a field read one word off).
- **The harness** gained the writable view buffer, the BSS zero-fill tail made
  writable (Wine maps base+0x72000.. read-only), fault logging with the fault address
  and code, and guard-page taps (PAGE_GUARD + the vectored handler) on the driver,
  walk and writer pages that log every execution with the register context.

Open (next session): the module's obs-state (module+0x10F4) is the live one, and its
record list — a forward_list {head(+0x1E4), size(+0x1E8)} — holds one all-zero record:
the builder runs but reads zeros from the host's units. The remaining gap is the
fork-ABI unit getters (the slot audit covered the set-returning slots; the builder's
type/position/hp getters need the same audit). Once the records fill, the walk
completes, the writer fills view+0xC0 with N, and the differential scenarios (move a
unit, change resources) name every field.
