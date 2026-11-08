# HXP4 late-state producer, codec, and static evidence

These are host-only tests. They do not load the official DLL or launch Wine,
StarCraft, a game, or inference. The public wire contract is in
`docs/pluto-protocol.md`.

## Producer and codec

`engine/tools/pluto-bridge/hxp_late.h` contains the shared little-endian
HXP2/HXPF/HXP3/HXP4 codec, HXP4 producer helpers, native layouts, and BWAPI
projections. Hyperion's `PlutoSendFrame` uses those helpers with the actual
native fields:

- `CUnit::wBuildQueue[5]` and `bBuildQueueSlot`;
- `wRemainingResearchTime` under orders 75/76;
- `bTechType` under order 75 and `bUpgradeType` under order 76;
- `get_tech_researched`, `get_upgrade_level`, `is_tech_in_progress`, and
  `is_upgrade_in_progress` for every native player slot.

`PopulateUnitProduction` preserves all five entries, checks the active slot,
and emits neutral research defaults outside those orders. Wide mod IDs fail
explicitly rather than being truncated. `ReorderPlayerTech` applies the exact
same player ordering as the existing HXP player records, leaving unoccupied and
neutral slots zeroed. The packed unit-extra layout is exactly 64+14=78 bytes;
the player-tech layout is 44+61+6+8=119 bytes.

HXP2/HXPF/HXP3 parsing and fallback behavior are unchanged. HXP3 trailing bytes
never become HXP4 fields. HXP4 retains the existing unit generation byte and is
generation-capable in worker lifecycle analysis.

## Static proof for omitted payloads

The accepted official module is pinned at SHA-256
`603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc`.
`audit_official.py` checks the complete PE for positive **and negated**
little-endian native addresses and anchors required fields in disassembly.

- Queue reads are visible at `0x63FB1BE4`, `0x63FB72E0..0x63FB741D`, and
  `0x63FBC29B..0x63FBC2D0`.
- Research reads are visible at `0x63FB1CAD` (`+0xC8`), `0x63FB1CDD`
  (`+0xC6`), and `0x63FB3D17` (`+0xC9`). Owner-level reads at
  `0x63FB3D48/73` are also required for `base + factor * currentLevel`.
- Terrain is built from `ActiveTileArray` at `0x63FBB516/5BC`.
- There are no references to `UnitOrderingCount/X/Y` (`0x66FF74/78`,
  `0x6769B8`), `MiniTileFlags` (`0x5993D0`), `MapTileArray` (`0x5993C4`),
  `TileSetMap` (`0x6D5EC8`), or `SAIPathing` (`0x6D5BFC`).
- The only BWAPI Game calls are enemy race, chat, leaveGame, and debug drawing;
  no rectangle/closest/best-unit or `isWalkable` query consumes client data.

Therefore HXP4 deliberately has no minitile or unit-search payload. That is a
statement about this official DLL, not about arbitrary BWAPI clients.

## Tests

Run:

```sh
engine/tools/pluto-bridge/late-state/run_tests.sh
```

The script writes artifacts to `/tmp/opencode/pluto-late-state-tests` (override
with `OUT`) and performs:

- six groups in a normal optimized build: round trips, native application,
  producer contract, malformed/strict framing, concatenated streams, and
  deterministic fuzz smoke;
- the same six groups with AddressSanitizer and UndefinedBehaviorSanitizer;
- real BWAPI 4.4.0 i686 layout `static_assert`s;
- 500 arbitrary-input cases and 250 single-bit HXP4 mutations;
- queue wraparound `{8,9,10,11,12}` from slot 2 to `{10,11,12,8,9}`;
- SC/BW researched and owner-level mappings, in-progress bitsets, neutral
  research arms, mod-ID rejection, and player-tech reordering;
- the static official-DLL audit and existing `verify_pe.py` reservation check.

`PLUTO_DLL`, `PLUTO_BRIDGE_EXE`, `MINGW`, `BWAPI`, and `SHIM` can be overridden
for another local installation. The layout-check PE is not executed; the BWAPI
layout proof is entirely compile-time. Official replay validation is separate.

## Receiver integration and limits

`ApplyImageState` parses and applies HXP4 before publication: all five queue
entries, the active research arm, SC/BW researched and current-level tables, and
unified in-progress bitsets. `receiver_smoke.py` checks actual values before
code 2 and malformed rejection; `replay_official_hxp4.py` validates a 54-frame
replay. The implementation follows `late-state/RECEIVER_INTEGRATION.md` and the
wire contract at the end of `docs/pluto-protocol.md`.

HXP4 does not serialize custom availability/max-level rules, mod IDs, arbitrary
pointer/hangar/cargo union arms, minitiles, or unit-search arrays. Existing
ordinary-melee availability/max defaults remain in force. Generic BWAPI
minitile/unit-search APIs remain incomplete for other clients even though the
official policy does not consume them.
