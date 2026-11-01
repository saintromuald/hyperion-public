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

## The rig (tools/pluto-rig) — status 2026-11-01

A Wine-hosted 32-bit harness now runs the real pluto.dll against scripted game state:
the host compiles BWAPI 4.4.0's client-side GameImpl/PlayerImpl/UnitImpl over a
GameData block it fills, patches a copy of the DLL so its StarCraft-global writes land
in stand-in buffers (it writes SC's speed global at 0x5124d8 and friends by absolute
address), and drives the module through its real vtable ([1] onStart, [2] onEnd(bool),
[3] onFrame — not BWAPI 4.4.0's AIModule order).

Reached: onStart runs, pluto_infer.exe launches and handshakes over BWRL_SHM
("inference server up ... shm transport"), the model loads (int8mv, 6 threads,
768 max_units).

Blocked on: the bot's compiled Game/Player/Unit ABIs differ from the public 4.4.0
headers, so its state reads hit the wrong vtable slots (own_race reads as -1; crash at
onStart's tail). Next: align the host's vtables to what the bot calls (trace all
impl methods / follow the worker), then the shm dumps become meaningful and the
observation schema extraction begins.
