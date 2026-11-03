# pluto-bridge: Pluto's brain as a server for Hyperion

The bridge runs the real pluto.dll (patched) + pluto_infer.exe under Wine against
the game state Hyperion sends it, and answers with the commands the bot issued.
Hyperion's Pluto level is a thin client: one HxFrame a game frame in, one HxReply
out.

## The protocol (stdin/stdout, binary, little-endian)

- HxFrame: `u32 'HXPF'` | `u32 frame` | `u32 nUnits` | `u32 mapW` | `u32 mapH` |
  `u32 nPlayers` | nUnits x HxUnit | nPlayers x HxPlayer
  - HxUnit: `u32 id, type, player, x, y, hp, shields, energy, order` |
    `u8 visible, completed, pad[2]` (40 bytes)
  - HxPlayer: `u32 race, minerals, gas, supplyTotal, supplyUsed, startX, startY`
- HxReply: `u32 'HXRP'` | `u32 frame` | `u32 nCmd` | `u8 cmd[nCmd]` | `u32 win`
  - cmd = the classic net commands (0x09 Select, 0x14 RightClick, 0x15 Action),
    exactly what Hyperion's net_cmds.cpp parses.

## Building and running

- `./build.sh` (llvm-mingw i686; see the script's paths) makes pluto-bridge.exe.
- Beside it: pluto.patched.dll (patch_pluto.py from the release), pluto/ with
  pluto_infer.exe + pluto_weights.bin (tscmoo's v2.0 release).
- `wine pluto-bridge.exe` serves stdin/stdout; `bridge-test.py` drives it with
  scripted frames.

## Status 2026-11-03 (the sincos trace)

The embedding formulas are solved (docs/pluto-protocol.md, "the embedding
formula"): the 32 floats at entry +0x134..+0x1B0 are 8 blocks of
`[sin θk, cos θk, sin φk, cos φk]` with θk = x·π/(16·2^k), φk = y·π/(16·2^k)
-- multi-scale positional encodings of the raw x/y, period 32·2^k px. The
proof came from a runtime trace of the fork's sincos helpers (their callers
are materialized at runtime and invisible statically): with HX_DEBUG the
bridge patches the helpers' prologues to stubs in its own exe that log
`(return address, angle)` into `dump/sincos.log` (rings in the LOW stand-in,
pointer slots 0x2103FFF0/0x2103FFF4). `HX_PROBE` additionally probes the
preferred-base constant region after the first frame.

## Status 2026-11-01 (session 4, evening)

The rig's whole pipeline (docs/pluto-ai.md) runs in the bridge: the bot starts,
launches the engine ("inference server up"), and plays in free-build mode against
the fed state. One 90-frame test produced 919 bytes of replies (the commands flow
end to end). Two instabilities remain, both in the harness, not the protocol:

- The bot's engine-poll probe calls through a garbage pointer (an exec fault at a
  stack address); the rig absorbed it as a null call, the bridge answers "ready"
  (eax=1) via a stub, and the poll loop then settles — but the obs does not ship
  in every run yet (the record walk limps after the absorbed faults).
- The bot's own_race read gives -1 under the per-frame GameData replacement
  (free build, no bandit — the bot still plays); the rig read 0 with its one-time
  fill. Next: keep the GameData's unit slots stable by Hyperion id across frames
  instead of re-memsetting them in frame order.

The protocol and the emulation are the proven parts; the remaining work is the
frame-replacement policy and the fault stubs, then the Hyperion-side client
(the obs and action halves are already captured in docs/pluto-protocol.md).
