# Deterministic native tests (Phase 2)

`engine/tools/pluto-native-tests` tests Hyperion directly. It does **not** start Pluto,
`pluto-bridge`, an inference process, or a match. The only process an engine scenario
starts is the explicitly named Hyperion executable, with `--ums`, a fixed seed, a generated
HXM map, a hidden window, and a fresh local control port.

## Run

```bash
# Always-on packet and fixture tests; the 13 engine scenarios are explicit skips.
python3 engine/tools/pluto-native-tests/run_tests.py

# Pure tests only.
python3 engine/tools/pluto-native-tests/run_tests.py --pure-only

# Run everything against an engine containing this Phase 2 port_control.cpp.
python3 engine/tools/pluto-native-tests/run_tests.py \
  --engine /path/to/Hyperion.x86_64 \
  --data /path/to/Data \
  --install-folder '/path/to/licensed/StarCraft' \
  --output /tmp/opencode/pluto-native-results

# A subset, by unittest method-name pattern.
python3 engine/tools/pluto-native-tests/run_tests.py \
  --engine /path/to/Hyperion.x86_64 --only 'larva*'
```

There is deliberately no engine auto-detection. Without an explicit `--engine`, only the
pure tests run and engine tests say why they skipped. A supplied path that is missing,
not executable, or lacks `turn` is a failure, never a skip or a pass. `--data` defaults to
the engine's folder. An explicit output folder is not reused: each scenario must be able
to create a new directory beneath it.

## Sandbox command and unit evidence

The live-control sandbox command is:

```json
{"cmd":"turn", "player":0, "bytes":[9,1,1,0,35,41,0]}
```

It accepts an occupied lobby player and at most 65,535 integral bytes in `0..255`. It saves
and restores active nation/human/Storm IDs, `gbInPacketProcess`, and the random-enable
flag. As in Trainer `GiveCommands`, it enables packet processing and networked randomness,
sets the selected player's active IDs, and passes a mutable packet copy to
`net_process_game_turn(..., 1)`. It does not advance a frame. The answer includes the
actual `frame`, `player`, and byte count.

The related setup-only `passive {player}` command keeps the opponent's seat and units but
changes it to a non-computer record so the native AI cannot overwrite the orders under test.
It changes no production AI level, policy, or match. Fixtures also use the real pathing glob,
known-buildable start sites, actual prerequisites (including a Spawning Pool for Lair), and
the resource trigger's applied frame 2 state. The base template is selected by
`HYPERION_NATIVE_BASE_MAP`; the default is the local `Big Game Hunters 2x.hxm`. Generated
fixtures contain every required terrain/metadata asset and are byte-identical across runs.

`units {ids:[...]}` remains backward compatible and adds numeric fields for exact tests:

- `id`: stable unit-table index used by the command and by all link fields;
- `unitId`: generation-packed classic ID needed by raw `Select`/target packets;
- `orderState`, `orderTargetId`, `orderTarget`, `moveTargetId`, `nextWaypoint`, and
  `destination`, alongside the original `order` and `to` fields;
- `groundCooldown`, `airCooldown`, `spellCooldown`, `direction`, `currentDirection`, and
  signed `speedX`/`speedY`;
- `statusFlags`, `completed`, and `previousType`;
- `connectedId`, `currentBuildUnitId`, `buildType`, all five raw `buildQueue` entries,
  `buildQueueSlot`, and `remainingBuildTime`;
- `carriedResource`, `carriedAmount`, and the resource container's `resourceAmount`;
- `larvaTimer` and `creepTimer`, used to diagnose production cadence;
- `team` and `alliedMask`, alongside per-player race, bank, supply, and vision state;
- `visibleMask` and `detectedMask`, computed independently with `SpriteSeenBy` and
  `UnitDetectedBy`. These are the classic low 32 player bits. In particular, visibility
  does not imply cloaked detection.

## Determinism and evidence

Every game action is a raw classic packet through `turn`. Command and transition boundaries
advance exactly one frame. Long untested intervals may advance in a single bounded `step`
to the next asserted frame, without skipping an assertion. Assertions name explicit
absolute or scenario-base frame numbers, including frames before, at, and after transitions.
There are no screenshot checks, sleeps standing in for game frames, retry loops, recovery
orders, or expected values updated to agree with a failure.

Fixtures use standard unit data: drone 50 minerals/300 build time, overlord 100/600, Lair
150 minerals + 100 gas/1500, and supply depot 100/600. Measured intermediate states are
asserted explicitly: egg completion state 2 and an incomplete drone before completion;
Lair order states 4, 5, and 6 before the completed type; and HP 0 before unit removal.
Marine cooldown and facing checks use exact values at the measured attack frame.

Each scenario directory contains:

- `fixture.hxm`: the self-contained map actually launched;
- `command.json`: exact engine argv, cwd, seed and port;
- `engine.log`: that child engine's output;
- `evidence.jsonl`: numeric state at every frame and every raw command, including dead
  stable table IDs after removal;
- `result.json`: only after a completely successful scenario;
- `first-mismatch.json`: on the first failure, its frame, assertion, expected and actual
  values, and the last state. A later failure never overwrites the first report.

## Scenarios

The 13 engine scenarios cover:

1. one SCV mining, carrying eight minerals, returning and depositing;
2. two worker losses and successive replacements before a surviving third drone;
3. larva-to-drone morph state and raw production queue;
4. hatchery/larva links, exact spawn frames 361 and 715, and the three-larva cap;
5. egg timer zero, completion state 2, incomplete drone at frame 321, and completed drone at frame 336;
6. hatchery-to-lair cost, previous type, and completion;
7. SCV building placement, worker/building links, and supply depot completion;
8. hatchery-only supply rejection, overlord production, and admission at the new cap;
9. an eight-marine attack killing an SCV;
10. allied `follow`, `patrol`, and hostile `attack_unit` order IDs and targets;
11. signed velocity, facing, marine cooldown, order targets, and damage;
12. force/team membership, shared vision, and alliance-dependent orders;
13. cloaked targets that are visible/undetected, visible/detected, and outside vision.
