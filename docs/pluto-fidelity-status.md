# Pluto/Hyperion fidelity status — 2026-11-07

This page separates evidence from unresolved acceptance work. It is not a claim
that Pluto is fully replicated or that Phase 5 is complete.

## What is tested now

- **Twenty pure packet/fixture/regression tests and 13 deterministic native
  scenarios pass (33 total).** The regressions cover stale-target rejection at
  apply time, no-target sentinels, lobby/native-player mapping, sandbox Storm-ID
  safety, old/new Storm-owner mappings, and emission/receipt/dispatch timing.
  The scenarios cover mining and
  return, repeated worker replacement, larva
  association and spawning, larva-to-drone including the incomplete-drone stage,
  egg completion, hatchery-to-lair, building placement/completion, supply caps,
  army combat, Follow/Patrol/Attack, cooldown/facing/velocity/targets, alliances,
  and visibility versus cloaked detection. Evidence is in
  `/tmp/opencode/pluto-native-run-34` through `/tmp/opencode/pluto-native-run-41`;
  the latest rebuilt-engine run is `/tmp/opencode/pluto-native-run-42` (33/33,
  no skips). That engine's SHA-256 is
  `a22c06908703906357b9b4811ad4666d8211e22fbc894f372b1c4f8ee4ffeb27`.
  These are native UMS tests with a fixed seed, not Pluto matches or BWAPI
  differentials.
- **106 differential-tool tests pass**, including six divergence classes, strict
  classic/HXP/HXRP parsers with an explicit in-game command profile, PLSM live
  serial/payload/serial capture, structural trace validation, generation-aware
  worker-stall analysis, and implementation/bandit identity checks.
- **57 acceptance-runner tests pass**, including conservative victory checks,
  actual `PLuto` race/slot validation, immutable targets, real cross-process
  lock contention/release, bandit continuity, strict HXP/HXRP evidence,
  emission/receipt/dispatch timing, separate received/accepted byte counts,
  positive nonzero native-application evidence, recurring binary/map provenance
  checks, reviewed-map requirements, and acceptance exit status.
- **10 lifecycle tests and six host-logic tests pass**, covering generation
  reuse, fog evasion, death, morph, hidden-frame-zero completion, renegade,
  initial neutral accessibility, burrowed visibility, binary command framing,
  publication generations, and late/absent clients. HXPM
  serialization/malformed-input tests also pass; all three suites pass with
  AddressSanitizer and UndefinedBehaviorSanitizer.
- The general engine suite passes notes, HUDs, maps, and mods. Its catalog check
  fails on the already-committed `PT_("Pluto")` in unchanged `port_ai.cpp`,
  which is absent from `hyperion.pot`; this working tree did not introduce that
  source string or modify the catalogs.

These results are meaningful host-contract and native-receiver tests, not proof
of natural competitiveness or genuine BWAPI equivalence.

## What is only partially demonstrated

- The bridge loads and executes the **official unmodified** `pluto.dll` (SHA-256
  `603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc`).
  Two integrated two-frame HXP3 runs in `/tmp/opencode/pluto-official-integrated`
  are byte-identical and contain empty HXRP replies for input frames 0 and 1
  plus the final empty flush labelled 2. They reach official inference readiness,
  resolve `Bravo` and Terran race 1, and shut down without a page fault, join
  timeout, or surviving child. Genuine SC addresses are supplied by a checked
  PE `.game` section at `0x401000..0x800000`, with host code above it and a
  validated high worker-stack allocation. Real synchronous/asynchronous client
  ownership, HXPM, malformed-input, frame-sequence, and finalization tests also
  pass with the official DLL. Three fresh 54-frame replays produced 210, 231,
  and 182 valid classic command bytes respectively, demonstrating real official
  policy output while also showing that exact model-response determinism cannot
  be assumed. Complete pinned BSS protection, all required SC-range checks, and
  strict PE/TLS validation are enforced; 14 PE verifier valid/mutation groups
  pass. The current integrated bridge SHA-256 is
  `2ec07692981eda9b7da6c67fbf1068e358c65efd9cde47f7c4794ff615d23100`.
  Matching HXP4 artifacts are in `/tmp/opencode/pluto-hxp4-official`: a valid
  two-frame replay and a 54-frame replay both complete with ordered replies,
  and the latter has 168 classic command bytes in five nonempty replies. A live
  120-frame HXP4 match produced valid commands and closed Hyperion, the bridge,
  and inference cleanly, including the explicit result/shutdown handshake and
  final process exit. These remain startup, transport, ownership, shutdown,
  replay, and controlled-match evidence, not genuine BWAPI equivalence or
  natural-series evidence.
- HXP2/HXP3/HXP4/HXPF remain binary-safe and readable. HXP4 adds all five
  queue entries, current research/upgrade state, and per-player tech records.
  Static official-DLL analysis proves that minitile walkability and dynamic
  unit-search are not consumed by this policy, so no such payloads were added.
  HXPM is optional static
  metadata, not a new frame version. Codec tests do not prove live-fork region
  consumption, startup-pipe timing, or all late-game behavior.
- The native command path now uses emission-frame generation IDs and the real
  classic dispatcher. A genuine stock-BWAPI match confirms issued F6 applied F9
  (emitted+3), and Hyperion's pending reply timing now applies at emitted+3 as
  well. This is not full latency validation for every production and combat command.
- The worker-stall tool separates insufficient resources from missing requests.
  Only provable nonzero HXP3/HXP4 slot+generation identities can close a production
  chain as completed; legacy and generation-zero ambiguities remain inconclusive
  and non-actionable. It cannot classify requests outside numeric state coverage.
  A historical `pluto.log` latency warning is not a substitute for native timing
  evidence.

## Genuine BWAPI capture established on 2026-11-07

- `/home/saintbellarmine/Games/pluto-reference/capture-original-013` and its
  `capture-original-013-export` are a complete natural-match capture and
  validated export, not a synthetic fixture. The official DLL, inference,
  weights, capture module, map, replay, logs, and bandit snapshots all pass
  post-exit provenance. The match ended with `onEnd(isWinner=1)` and contains
  12,558 consecutive numeric frames, 12,557 actual sends, 734,216 unit records,
  and no frame/send sequence issues, orphaned sends, locator failures, or PLSM
  publication gaps. Its export reports `complete_capture` and provenance reports
  `provenance_complete=true`.
- The capture module's SHA-256 is
  `cd9faf33e5b1cd5b03836ea245f79068b2cd7ec4f58eb9e02e41d408f56f694f`.
  Anonymous PLSM was found by bounded same-process handle duplication and exact
  header validation; 63 nonmatching duplicate views were rejected and cleaned
  up. The raw-send hook restored cleanly. The 100,000-record cap and continued
  single-delivery fixture also pass. No proxy, synthetic responder, patched
  Pluto binary, visibility-flag change, or forced opening was used.
- Genuine issued-F6/applied-F9 evidence again confirms three-frame command
  latency. The uncaptured stock control and captured match both completed with
  zero inference slips, but their seeds, openings, and durations differ. This is
  useful coarse timing evidence, not a behavioral-neutrality proof. Native
  receive/acceptance boundaries remain outside this capture, and its exported
  differential status therefore remains explicitly incomplete.

## Blockers to full acceptance

1. **The genuine baseline does not capture native receive/acceptance
   boundaries.** It proves genuine per-frame public numeric state, PLSM
   request/response payloads, actual sends, and clean natural completion. It
   does not prove every command's native receive or acceptance result, complete
   differential equivalence, or tournament neutrality. The official DLL does
   not expose an inference-seed or argmax launch option, so exact model-response
   determinism cannot be pinned through its normal unmodified launch path
   without violating the no-wrapper constraint.
2. **Late-state and stock-control comparison remain open.** HXP4 represents and
   tests all five build-queue entries and current research/upgrade progress
   through the official DLL. Static analysis proves that exact minitile
   walkability and dynamic BWAPI unit-search are not read by this official
   policy, so those payloads are intentionally absent. Generic third-party
   BWAPI client completeness, every late-game event, and a controlled
   stock-versus-capture neutrality comparison are still not established.
3. **There is no current natural 30-game acceptance series or measured target.**
   The historical controlled 12HatchFE win is not natural-policy proof. The
   later ASL series is historical and cannot be relabeled as current
   acceptance; it contains configuration problems and no natural wins. A
   no-launch natural-baseline manifest now passes with 30 reviewed maps and
   official binaries, but no games have started. Live execution still requires
   host exclusivity, followed by a measured immutable performance target before
   any separate acceptance series. The staged engine SHA-256 is
   `fff3e7f2d2f1cfd65389d6e7268f507bc0f8e34967d81d244011d54440b86fdc`;
   the rebuilt bridge is
   `609f6c367f9d5332c791946a4fa61b3205f630b9f2ef71013fddc9a46f969ad3`.
   The new reviewed map manifest is
   `/home/saintbellarmine/Games/pluto-acceptance/pluto-acceptance-maps.json`
   (SHA-256 `fbde3ce13da6fd06bf19e1ec2a3ef2c5b028a744f2606e9b34be064efb4e9aaf`),
   and its unmeasured target is
   `/home/saintbellarmine/Games/pluto-acceptance/natural-baseline-target.json`.

## Final review and remaining acceptance work

- Independent final reviews resolved every defect they identified in the scoped
  bridge, native-dispatch, differential, and acceptance paths, including real
  synchronous/asynchronous client ownership and deterministic finalization.
  This is source/evidence review, not proof of complete BWAPI equivalence.
- The exact rebuilt engine and bridge hashes are recorded above.
- Full acceptance still requires the missing native receive/acceptance and
  stock-control differential coverage, an explicit immutable performance target
  from natural Hyperion baseline games, and a fresh 30-game acceptance series
  with complete artifacts. The genuine capture baseline is established, but the
  remaining requirements are still open.

## Distribution and licensing

Hyperion's MIT notice remains intact. BWAPI is LGPLv3: distribution requires
notices, corresponding source, and relink/replacement support. The local build
uses a BWAPI checkout, but that alone does not satisfy a distribution package.
Pluto's documentation does not grant clear permission to redistribute patched
binaries or weights. Historical patched-DLL smokes were not acceptance evidence;
the current bridge enforces the official unmodified DLL. No DLLs, inference
binaries, or weights are added to this repository. A licensed
user-installed StarCraft copy is required. Copyright, EULA obligations, and
distribution rights are separate issues; BWAPI's own StarCraft EULA note is not
treated as resolved.
