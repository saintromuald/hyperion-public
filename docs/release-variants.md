# Hyperion Zero Private and Hyperion Shield Zero releases

Hyperion has two package variants. They use the same engine build and the same
`engine/tools/package.py`; the difference is what may be distributed with it.

| Variant | Audience | Game data | Publishing |
| --- | --- | --- | --- |
| **Hyperion Zero Private** | Private testing | A complete, maintainer-supplied StarCraft: Remastered install under `Game/` | Never upload publicly. Build and share only through an authorized private channel. |
| **Hyperion Shield Zero** | Public | None. The player supplies Remastered or classic Brood War files. | The manual `Hyperion Shield Zero release` workflow creates a prerelease or final release. |

Hyperion Shield Zero is the engine package HyperShield bundles and launches; Zero identifies
where the engine runs. Hyperion Zero Private is the separate maintainer-only test bundle.

Both packages contain `release.json` and `EDITION.txt`, recording the product,
version, `prerelease` or `release` channel, visibility, platform, and whether game
files are bundled. Shield has no `Game/` directory and no Zero launcher. Zero's
launcher passes `--install-folder <the extracted Game directory>` to Hyperion and
forwards any additional arguments.

## Build Shield locally

On Windows:

```bat
cd engine
python tools/build_engine_x64.py --no-install
python tools/package.py --edition shield --channel prerelease --out testrun/share/variants
```

On Linux:

```sh
cd engine
python tools/build_engine_linux.py
python tools/package.py --linux x64 --edition shield --channel prerelease --out testrun/share/variants
```

On an Apple-silicon Mac:

```sh
cd engine
python3 tools/build_engine_linux.py
python3 tools/package.py --mac --edition shield --channel prerelease --out testrun/share/variants
```

## Build Zero privately

Zero **requires** a legally supplied Remastered install. The packager refuses to
make Zero without one, and refuses to let Shield accidentally include one.

```sh
python tools/package.py --linux x64 \
  --edition zero --channel prerelease \
  --game-files '/path/to/StarCraft' \
  --out testrun/share/variants
```

The source install needs a `Data` directory and at least one file. Its contents
are copied recursively under `Game/`; local source paths are not written into the
archive or metadata. Use the platform's launcher after extraction:

- Windows: `Play Hyperion Zero Private.cmd`
- Linux: `./play-hyperion-zero-private.sh`
- Mac: `"./Play Hyperion Zero Private.command"`

For a non-GUI check on Linux:

```sh
./play-hyperion-zero-private.sh --mod-check bw
```

Do not attach Hyperion Zero Private to a GitHub release, an issue, a public artifact, or a shared
scratch directory. There is deliberately no Hyperion Zero Private upload workflow.

## Canonical ladder rules and team sizes

Canonical manifests support 1v1, 2v2, and 3v3 with authenticated per-slot
`teamId` values and 2/4/6-player rosters. The seven independent matchmaking
identities are:

| Format | Rule 1: classic BW | Rule 2: Hyperion anti-meta | Rule 3 |
| --- | --- | --- | --- |
| 1v1 | `hyperion1v1rule1` | `hyperion1v1rule2` | `hyperion1v1rule3` (disabled) |
| 2v2 | `hyperion2v2rule1` | `hyperion2v2rule2` | — |
| 3v3 | `hyperion3v3rule1` | `hyperion3v3rule2` | — |

Rule 2 uses Hyperion rules with exactly `mutators.antiMeta = 1`. From the shared
manifest seed, every seat independently rolls the same start: 4–8 workers,
100–300 minerals, and 0–125 gas, each in steps of five. No other mutator object
is accepted. Replays record the rolled values rather than rerolling them.

Rule 3 is retained in type and result schemas but is never queueable, including
through schedule administration and apply-to-all schedule changes. A configured
Hyperion install cannot divert other ladder modes or lobby formats into the
native sidecar.

## ShieldBattery results and replays

For a server-tracked canonical match, an eligible tracked human seat receives
both `HYPERION_SB_RESULT_CODE` and `HYPERION_SB_SERVER_URL`. The result code is
not written into the native manifest, command line, lifecycle events, or logs.
The server URL is supplied only to enable authenticated rehome. Observers,
replays, legacy launches, and untracked seats receive neither value.

At the end of play, Hyperion sends a raw v2 result report through the relay's
reliable control stream before announcing a clean leave. The report contains the
authenticated roster's assigned races and BW/Storm/user IDs, victory and
alliance evidence, network drop/quit rows, and disconnect type. The coordinator
and ShieldBattery apply their existing result, departure, and reconciliation
rules. Observers, replays, legacy launches, and seats without a result code do
not arm or send a report.

Replays remain local. Classic native games use classic `reRS` recordings with
Hyperion playback; Hyperion QoL and anti-meta games use `hxRS`. There is no
Hyperion replay-upload workflow yet; BroodRep format and seed compatibility,
map/roster/rules/simulation identity, and server-confirmed game linkage must be
proven before one is enabled.

Canonical tracked participants can use ShieldBattery-mediated rehome through the
existing rehome endpoint. Requests use the game client's `shieldbattery://game`
origin and require HTTPS except for literal loopback development servers. The
bridge keeps result credentials out of launch files and logs, pins each
replacement relay's certificate, and gives each bounded request margin beyond
the server's coordinator deadline. Observers, legacy rooms, replay processes,
and untracked seats do not receive rehome credentials. Same-relay reconnect and
replacement-relay rehome are separate acceptance cases.

The acceptance harness supports `--engine` for an exact executable and
`--rallypoint-target`/`--no-build` for prebuilt services. Every seat must agree
on 600 meaningful-input turns and produce a replay. `--classic` checks ordinary
Classic rules; `--negative` exercises rollback, roster/profile mismatch, and bad
map hashes/bytes through the native fail-closed parser.

## Pluto is a mandatory release gate

Pluto AI must be functional in every release and every prerelease, without exception. A package
is incomplete if its Pluto level can be selected but its runtime cannot start. The package must
include Hyperion's bridge and either the complete, unmodified runtime or the built-in
**Download Pluto AI** setup on the first-run screen and in Settings. Do not bundle or mirror the
official DLL, inference executable, or weights without the author's redistribution permission;
the built-in setup downloads them directly from the official Pluto release and verifies their
published hashes. Run a real packaged-engine Pluto match and a two-client multiplayer match with
a Pluto slot before publication or promotion. Multiplayer needs one authoritative inference
owner, deterministic commands on every peer, consistent results, and valid replays. Native
scenario tests without that runtime are necessary but not sufficient.

## Prerelease, then release

Run **Hyperion Shield Zero release** manually with the ref, tag, and `prerelease`
channel. It packages Windows, Linux, and macOS, then creates or updates a public
prerelease. Test those exact packages before promoting them. The Mac build-input
archive is only a workflow artifact; it is not attached to the release.

To promote the same release after testing:

```sh
gh release edit hyperion-shield-zero-vX.Y.Z-beta --prerelease=false
```

For a final release built from another ref, run the workflow with the `release`
channel instead. The workflow has no scheduled or push-triggered publishing.
