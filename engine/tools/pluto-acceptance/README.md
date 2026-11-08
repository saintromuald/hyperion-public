# Pluto Phase 5 acceptance runner

`pluto_acceptance.py` plans and, only when explicitly invoked with `run`, runs a fresh sequential series of **30 natural 1v1 melee games**. It uses only the Python standard library and does not modify gameplay code.

The default map root is:

```text
/home/saintbellarmine/Games/cosmo-farm/trees/slot0-a/prefix/drive_c/Cosmonarchy/Starcraft/Maps/CMBW-Root/CMBW-maps/melee/1v1/ASL
```

The planner selects 30 content-distinct `(2)` `.scm`/`.scx` maps **only from a separate reviewed SHA256/settings manifest**. A suffix, filename, or plain-text file is not map evidence. Observer, fog, training, solo, sandbox, and duplicate-content variants are excluded. Three- and four-player maps are never substituted for a 1v1 map.

## Reviewed map manifest

`--map-manifest FILE` names the review; by default the runner reads `MAP_ROOT/pluto-acceptance-maps.json`. The JSON schema is `hyperion-pluto-acceptance-map-manifest-v1`. Its `review` object must identify the reviewer and review time and contain the exact ordinary-melee attestation exposed as `MAP_REVIEW_ATTESTATION` in `pluto_acceptance.py`. Every `maps` entry has this form:

```json
{
  "path": "S01/(2)ReviewedMap.scx",
  "sha256": "LOWERCASE_SHA256_OF_THE_REVIEWED_FILE",
  "players": 2,
  "start_locations": 2,
  "game_type": "melee",
  "ordinary_start": true
}
```

This is a human-review trust boundary, not a CHK parser: the reviewer must inspect the actual map and its CHK/player/start settings before attesting. The runner deliberately provides no command to manufacture a review from files it has not inspected. It verifies normalized paths, review settings, and every listed file's SHA256, then selects 30 unique contents. The manifest and every scheduled map are fingerprinted and fully rehashed before every launch and every results write; any post-review change aborts the series.

## Series contract

- Exactly 30 scheduled games: 10 Zerg, 10 Terran, and 10 Protoss Pluto.
- All nine race matchups occur three or four times. Every opponent is **Cheater: insane**.
- Requested Pluto seat alternates. Actual identity is established from the unique `PLuto` name **and** `Pluto` level in the resulting `summary.json`, never from the requested slot alone.
- `--bench-frames 30000`, two players, melee, ordinary starting state, no mutators, no stress armies, and no scaled pools.
- The requested Pluto/Opponent race order and `p,i`/`i,p` AI order are recorded in every exact command. Actual summary races, `pluto.log` `own_race` and `onStart` race, and `hyperion_pluto.log`'s actual PLuto slot must all agree.
- Inherited `BWRL_*` settings, `HX_DEBUG`, and every `HX_TEST_*` bridge/test override are removed. `HYPERION_PLUTO_DUMP=1` is set to retain raw replies.
- A fresh output directory is mandatory. Existing output, including a partial historical series, is refused; there is no resume mode and old results are never counted as new games.
- The engine, official and deployed DLLs, inference executable, weights, bridge, reviewed map manifest, and all 30 scheduled maps retain their preflight metadata and SHA256. Every one is checked again immediately before each launch and before/during every results write, including final results. A change is fatal; results from mixed inputs are never accepted.

A safety/integrity failure can stop later launches. In that case the remaining manifest entries are marked `not_run`, rather than pretending 30 games completed.

## Immutable baseline/target input

Both `manifest` and `run` require `--baseline-target FILE`. The file must already exist and must be outside the new output directory. The runner records its SHA256 and metadata, checks it before every game and around every results write, and never edits, restores, chmods, or replaces it.

For an observational series with no invented performance threshold:

```bash
python3 pluto_acceptance.py init-target \
  --output /tmp/opencode/pluto-target.json \
  --unmeasured
```

This creates the file exclusively and makes it read-only. Its target is `null`; the report says `not_set`, not pass or fail.

To declare a numeric target, first provide prior measured results JSON with an `aggregate` containing positive `valid_games` and non-negative `wins`/`losses`:

```bash
python3 pluto_acceptance.py init-target \
  --output /tmp/opencode/pluto-target.json \
  --evidence /path/to/prior-baseline-results.json \
  --target-metric wins_total \
  --comparison '>=' \
  --target-value EXPLICIT_REVIEWED_VALUE
```

`win_rate_decided` is also supported. There is no default numeric target. A numeric target is rejected when baseline evidence is missing, changed, has no decided games for a rate, or lacks the corresponding measured value. Historical evidence used to define a baseline is not included in the new series' games.

## Official binaries

The deployed inference executable must be exactly:

```text
PLUTO_FOLDER/pluto/pluto_infer.exe
```

Its mandatory SHA256 is:

```text
3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc
```

`pluto/pluto_weights.bin` is also required, with SHA256
`c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86`.
The bridge executable is hashed and recorded, but a recorded hash alone is not
proof of its runtime behavior.

`--official-dll PATH` is required and must be the **official unmodified** `pluto.dll`, SHA256:

```text
603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc
```

The deployed `PLUTO_FOLDER/pluto.dll` must independently have the same official hash. A patched bridge DLL is not an acceptable substitute for either check. A missing or mismatching DLL aborts preflight before any output series or process launch.

## Dry-run manifest

`manifest` performs map, target, bandit, and binary checks and writes `manifest.json` plus a series-start bandit snapshot. It does **not** create `results.json` or `report.md`, does not take the live host lock, and never calls `Popen`.

```bash
python3 pluto_acceptance.py manifest \
  --engine /home/saintbellarmine/Games/hyperion/Hyperion.x86_64.pluto-final \
  --data-folder /home/saintbellarmine/Games/hyperion/Data \
  --pluto-folder /home/saintbellarmine/Games/hyperion/Data/pluto \
  --inference /home/saintbellarmine/Games/hyperion/Data/pluto/pluto/pluto_infer.exe \
  --official-dll /home/saintbellarmine/Github/pluto/v2.0-release/pluto.dll \
  --baseline-target /tmp/opencode/pluto-target.json \
  --output /tmp/opencode/pluto-manifest-NEW \
  --map-manifest '/home/saintbellarmine/Games/cosmo-farm/trees/slot0-a/prefix/drive_c/Cosmonarchy/Starcraft/Maps/CMBW-Root/CMBW-maps/melee/1v1/ASL/pluto-acceptance-maps.json' \
  --install-folder '/home/saintbellarmine/Games/battlenet/drive_c/Program Files (x86)/StarCraft'
```

Existing Hyperion/Pluto processes are reported as a dry-run warning. A live run refuses them. The dry-run does not signal or clean up processes it does not own.

## Live sequential run

**The following command is the only mode that can start Hyperion or inference:**

```bash
python3 pluto_acceptance.py run \
  --engine /home/saintbellarmine/Games/hyperion/Hyperion.x86_64.pluto-final \
  --data-folder /home/saintbellarmine/Games/hyperion/Data \
  --pluto-folder /home/saintbellarmine/Games/hyperion/Data/pluto \
  --inference /home/saintbellarmine/Games/hyperion/Data/pluto/pluto/pluto_infer.exe \
  --official-dll /home/saintbellarmine/Github/pluto/v2.0-release/pluto.dll \
  --baseline-target /tmp/opencode/pluto-target.json \
  --output /home/saintbellarmine/Games/hyperion/Data/pluto/acceptance-NEW \
  --map-manifest '/home/saintbellarmine/Games/cosmo-farm/trees/slot0-a/prefix/drive_c/Cosmonarchy/Starcraft/Maps/CMBW-Root/CMBW-maps/melee/1v1/ASL/pluto-acceptance-maps.json' \
  --install-folder '/home/saintbellarmine/Games/battlenet/drive_c/Program Files (x86)/StarCraft' \
  --timeout-seconds 1800 \
  --cleanup-timeout-seconds 15
```

Host safety uses a non-blocking `fcntl` lock under the Pluto folder and read-only `/proc` checks before every game. Each engine gets its own process group. Wall timeout or interruption triggers owned-group cleanup, followed by TERM/KILL escalation only as needed. The next game cannot start if owned children remain, another host appears, or bandit continuity fails. The runner does not kill unrelated processes by name.

### Bandit continuity

There must be one unambiguous active `pluto_bandit_Bravo.txt` in the Pluto folder's `bwapi-data/read`, `bwapi-data/write`, or root. `--bandit` can name it explicitly, but multiple active candidates are still rejected. Pluto's own log must also name internal opponent `Bravo`, tying the log to that state file. The file must have one sequence, `format 2`, a parseable arm table and JSON records, and exactly one final `end`.

The clean file present before game 1 is the **original series snapshot**. If a particular pristine starting state is desired, stage that state before preflight and pass its live path explicitly. The runner itself never imports, resets, truncates, or edits priors. It:

1. hashes and copies the original to `bandit-before-series.txt`;
2. copies the live state to `bandit-before.txt` and `bandit-after.txt` for every game;
3. requires game N's before-hash to equal game N-1's after-hash and rejects backward sequence numbers;
4. carries the live state forward without intervention, including after the final game.

It never restores the series-start snapshot at series end. Incomplete/misconfigured games are not converted into bandit wins or losses by this tool.

## Outcome acceptance

Configuration and artifact validation happen before outcome classification. Missing/stale `summary.json`, `settings.json`, `run.rep`, `replies-raw.bin`, `frames.bin`, `hyperion_pluto.log`, `pluto.log`, or `pluto_infer.log` is not a valid result.

`replies-raw.bin` is scanned with foreign-byte tolerance, but an `HXRP` candidate is accepted only after a complete bounded header/payload/win parse, a full strict native **in-game** command parse, a zero in-game `win` field, and an exact next expected frame. Lobby-only `StartGame` (0x3C), `UpdateSlot` (0x3E), `RequestChangeRace` (0x41), and `ChangeSlot` (0x44) are rejected even though BWAPI defines their layouts; Hyperion's in-game dispatcher stops on them. The selected current run must be the unique longest contiguous run beginning at frame zero; garbage-only, magic-only, truncated, unknown-command, nonzero-win, ambiguous, and non-expected-frame captures cannot support a result. `frames.bin` receives a separate strict HXP2/HXP3/HXP4/HXPF parse with no foreign-byte resynchronization, and the reply run must cover its selected dumped-frame prefix. HXP4 additionally requires its exact 32-byte header and size words 78/119, all five queue entries with a valid active slot, a neutral research arm outside orders 75/76, and complete per-player 119-byte tech records; queue, tech, truncation, trailing, and mixed-version regressions exercise these checks without launching a game.

The raw dump proves transport receipt only. It does **not** prove native command application. Nonempty command replies old enough to reach Hyperion's genuine three-frame application point are correlated with explicit `Pluto: emitted E received R dispatched D received_bytes M accepted_bytes N` records (and available command-receipt records). The required timing is **R = E + 1** and **D = E + 3**. M must equal the raw HXRP command-byte count; N is the post-translation count sent to native dispatch and must satisfy `0 <= N <= M`. Correct stale filtering is preserved without byte/timing mismatches: N=0 is valid wholly filtered transport/filter evidence and 0<N<M is valid partially dropped evidence when the command, timing, and counts match. Raw-count mismatches, post-translation overcounts, equal emission/receipt, and old ambiguous `bytes N` logs are rejected rather than guessed.

Every performance-accepted win/loss/draw requires `application_established`: at least one valid eligible dispatch with **accepted_bytes > 0**, tied to a genuine nonempty in-game command. Partial filtering with N>0 qualifies; wholly filtered proof alone does not prove any nonzero native application. A game or series with only wholly filtered commands therefore remains `incomplete`/separate, as does a production game containing only empty HXRP replies. Commands too near match end to have an eligible dispatch record likewise do not establish natural-policy application. Missing or contradictory evidence is never invented from command bytes.

**Coverage limitation:** `frames.bin` is a bounded prefix capture. Contiguous replies, summary/log evidence, and prefix cross-checks are retained, but the runner does not claim byte-level proof of a reply for every later match frame. `reply_evidence.coverage.full_match_terminal_coverage_proven` is therefore explicitly false.

A **win** requires all of the following:

- clean engine exit and valid actual configuration;
- positive `application_established` evidence with at least one eligible `accepted_bytes > 0` dispatch;
- `ended == "last"`;
- `last_player` equals the actual `PLuto` player's slot;
- PLuto survivors > 0 and opponent survivors == 0;
- both players produced units and gathered minerals;
- winner kills > 0 and loser units lost > 0;
- positive engine duration and consistent positive frame counts within the 30000 cap.

A **loss** applies the same conservative evidence rule with the opponent as winner. Contradictory last-player/survivor states or missing corroboration are `incomplete`, not wins/losses.

A **draw** requires the frame cap (`game_frames` 29999/30000 and `end_frame == 30000`), `last_player == -1`, both sides alive, and production/resource/duration corroboration. Early `finished`, `stopped`, timeout, crash, and missing summary are `incomplete`. Actual race/level/map/settings/log/artifact mismatches are `misconfigured`, separately aggregated.

## Evidence and reports

Each `game-NN/` retains, when produced:

- `summary.json`, `settings.json`, `run.rep`;
- `replies-raw.bin`, `frames.bin`, `hyperion_pluto.log`, `pluto.log`, `pluto_infer.log`;
- `engine.log`, and optional bridge/inference stdout/stderr captures;
- exact `command.json` (argv, shell-escaped form, cwd, environment overrides/removals, timeout);
- `bandit-before.txt`, `bandit-after.txt`, `race-record.json`, `hashes.json`.

`results.json` and `report.md` include wins by race, losses, draws, incomplete, misconfigured, and not-run counts; kills, survivors, production/losses, current and gathered resources, frames, wall time, and natural build-order frequencies. Performance totals include only accepted wins/losses/draws. Other games remain visible separately and cannot silently inflate rates or totals. A numeric target is evaluated only for a complete 30-game series without incomplete/misconfigured/not-run entries.

Process status distinguishes observation from acceptance. A complete targetless observation exits 0 with `acceptance_passed: null`; this is not a numeric pass claim. An explicitly set target exits 0 only when it evaluates true, and exits 1 when it evaluates false. Aborted, incomplete, misconfigured, not-run, or otherwise not-evaluable series exit 2. `observation_complete`, `acceptance_passed`, and `target_evaluation.status` preserve those distinctions in JSON and the report.

## Tests

```bash
cd engine/tools/pluto-acceptance
python3 -m unittest -v test_pluto_acceptance.py
```

The suite uses an explicit reviewed synthetic map manifest, strict binary HXP/HXRP fixtures, summaries, logs, bandits, and mocked game execution. It tests valid win/loss/draw acceptance, false-win and garbage-reply rejection, expected-frame/current-run coverage, command-application corroboration, actual PLuto slot identity, race/config rejection, immutable targets, all critical binary/map rechecks, exit-code honesty, clean bandit validation and continuity, timeout cleanup, environment sanitization, aggregate honesty, and dry-run's no-game-`Popen` guarantee. A separate short-lived Python child performs real `fcntl` lock contention/release in a temporary directory. The suite does not launch matches or inference.
