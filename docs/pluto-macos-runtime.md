# Pluto runtime on macOS

## Supported prerequisite boundary

The macOS ARM64 package can **attempt** Pluto only when all of these are true:

1. The host is macOS 26.4 or later on Apple Silicon.
2. The signed MetalSharp Wine 6.0 app is installed intact. Its `wine --version` output must report
   Wine 11 and MetalSharp Wine, for example `wine-11.20 (MetalSharp Wine 6.0)`.
3. The selected executable is `MetalSharp-Wine.app/Contents/MacOS/wine`, and the app contains its
   wineserver, i386 Windows runtime, aarch64 Unix loader, and embedded cross-architecture
   provisioning profile.

Pluto needs both 32-bit and 64-bit execution: Hyperion's bridge and the official `pluto.dll` are
i386 PE images, while official `pluto_infer.exe` is an x86-64 PE image. MetalSharp Wine/FEX supplies
the required translation. Generic WineHQ with Rosetta is **not a supported alternative**. Rosetta
omits XSAVE/OSXSAVE in this environment, so Pluto's compiler-generated AVX2 guard returns false
even though a simpler CPUID/AVX probe can look promising.

The OS and Wine checks only permit an attempt; they do not claim inference works. Actual
bridge/inference readiness, strict command handling, replies, and clean shutdown are the runtime
proof.

Restart Hyperion after manually repairing or replacing Wine in place. The OS result and the Wine
probe for each resolved pathname are cached for the life of the native Hyperion process. The
one-click installer invalidates the Wine probe after dependency activation, then checks inference
capability again before replacing any Pluto payload.

Native Intel Mac behavior is unchanged: Pluto uses the native x86-64 AVX2 check, the existing
WineHQ Stable/Devel/Staging and `PATH` resolution, and the Wine 11 unified i386/x86-64 preflight.
The macOS 26.4 and MetalSharp requirements apply only to Apple Silicon. The ARM64 acceptance
result below is not evidence for every Intel/Wine combination.

## MetalSharp Wine installation, without Homebrew

Wine/FEX is a separate prerequisite and is not bundled. On Apple Silicon, **Download/Repair Pluto
AI** installs it automatically when the selected runtime is missing or invalid, then installs the
official Pluto payloads in the same action. A valid explicit override or system MetalSharp
installation is used as-is and is not replaced. A successful payload download is never reported as
a successful inference run.

Install the signed app from the official MetalSharp Wine 6.0 release, preserving the complete app
bundle and its provisioning profile. For an admin-free installation, name it
`$HOME/Library/Application Support/Hyperion/MetalSharp-Wine-6.0.app`. A system installation at
`/Applications/MetalSharp-Wine.app` is also supported:

| Field | Value |
|---|---|
| Archive | `MetalSharp-Wine-6.0-arm64.zip` |
| URL | `https://github.com/metalsharp/MetalSharp-Wine/releases/download/v6.0/MetalSharp-Wine-6.0-arm64.zip` |
| Exact size | `1356725682` bytes (1293 whole MiB displayed during download) |
| SHA-256 | `25f7dff91000c03c835b800980878acb12c1f952a4b7853f77de7f21fb87046f` |
| App-managed loader | `$HOME/Library/Application Support/Hyperion/MetalSharp-Wine-6.0.app/Contents/MacOS/wine` |
| System loader | `/Applications/MetalSharp-Wine.app/Contents/MacOS/wine` |

The automatic installer streams the archive into an owned fresh sibling staging directory, enforces
its exact byte count and SHA-256 before extraction, and extracts with `ditto -x -k` to preserve the
signed app's symlinks and layout. Before activation it requires all of these to pass:

1. `codesign --verify --strict --deep` on the staged app;
2. the embedded provisioning profile, i386 runtime, aarch64 loader, wineserver, and app-layout checks;
3. Wine 11 plus MetalSharp Wine identity from the staged loader.

Activation renames the verified app only to the versioned app-managed destination. Any previous
app-managed bundle is moved to a rollback backup first and restored if activation is cancelled or
fails. Failure never exposes a partially extracted active app; recovery bytes are retained if a
rollback itself cannot complete. Cancellation stops downloads and owned tool process groups, and
retry starts with fresh staging. The installer never targets `/Applications` or another Wine
installation.

Manual installation remains supported. After verifying the archive and extracting it with macOS
`ditto`, the app's signature can be checked before installation:

```sh
codesign --verify --strict --deep /path/to/MetalSharp-Wine.app
/path/to/MetalSharp-Wine.app/Contents/MacOS/wine --version
```

Approve the hash-verified official app through macOS's normal Gatekeeper flow if the system asks.
Hyperion does not strip quarantine attributes or bypass that user decision. Homebrew is not used or
required.

On Apple Silicon, `HYPERION_PLUTO_WINE` may explicitly name an absolute executable or a bare name on
`PATH`. An explicitly set but invalid override fails rather than silently selecting another Wine.
Without an override, Hyperion checks these locations in order:

```text
$HOME/Library/Application Support/Hyperion/MetalSharp-Wine-6.0.app/Contents/MacOS/wine
/Applications/MetalSharp-Wine.app/Contents/MacOS/wine
```

The app-managed installation is preferred, needs no admin access, and discovery never moves or
replaces an existing system Wine. Hyperion does not fall back to WineHQ app bundles or a generic
`wine` on `PATH`. An app anywhere else requires the explicit override. The selected name must resolve
to a regular executable file, and that same absolute path is used for the version probe and
`posix_spawn` launch.

Apple Silicon preflight invokes the resolved file with `--version`, requires Wine 11 and the
MetalSharp Wine identity, and checks this layout relative to the selected loader:

- the selected `.app/Contents/MacOS/wine` and `Contents/MacOS/wineserver`, both executable;
- `Contents/lib/wine/i386-windows/ntdll.dll`;
- `Contents/lib/wine/aarch64-unix/wine`, executable;
- `Contents/embedded.provisionprofile`.

An executable directory, a script reporting the wrong version or identity, a WineHQ layout, and an
incomplete MetalSharp app fail inference preflight. The download action is allowed to repair a
missing or invalid dependency; a persistent failure after dependency setup stops before Pluto
payload replacement. Both the dependency installer and release acceptance verify the app's code
signature. These checks reject invalid prerequisites; they do not substitute for the real runtime
acceptance run.

The default Linux `/usr/bin/wine` resolution and native Windows direct-process behavior are
unchanged; the macOS preflight is not imposed on those platforms.

## Runtime and launch checks

The combined Apple Silicon action checks macOS 26.4+, installs or reuses MetalSharp Wine, reruns
inference preflight, and only then enters the existing Pluto payload commit path. The Pluto phase
still verifies the official v2.0 ZIP's outer hash and every DLL/engine/weight hash, atomically
replaces only those three payloads, and preserves the bridge, logs, configuration, and bandit data.
Dependency failure therefore leaves working Pluto payloads untouched. Linux, Windows, and native
Intel Mac setup remains Pluto-only.

Progress reports separate honest byte totals for MetalSharp Wine (1293 whole MiB) and Pluto AI
(295 whole MiB), followed by explicit checking, extraction, signature/layout validation, activation,
and Pluto verification stages. File installation is evidence of integrity, not a substitute for
runtime acceptance.

The POSIX process launcher gives the bridge its own runtime directory. macOS uses
`posix_spawn_file_actions_addchdir_np`; failure to apply that directory refuses the spawn instead of
letting the bridge search for `pluto.dll` in Hyperion's directory. The launch command uses the exact
Wine path selected by preflight. Apple Silicon no longer sets `ROSETTA_ADVERTISE_AVX`, because the
supported runtime is MetalSharp Wine/FEX rather than Rosetta.

## ARM64 runtime acceptance gate

`pluto-macos-runtime` in `.github/workflows/shield-release.yml` uses the `fex-probe` backend by
default on the ARM64 `macos-26` image and is in `publish.needs`. Publishing is restricted to that
backend. The `rosetta` choice is diagnostic-only, requires `runtime-only=true`, and cannot publish or
establish production readiness.

The MetalSharp job:

1. asserts macOS 26.4+ on ARM64 without installing or requiring Rosetta;
2. downloads and SHA-256-checks the pinned MetalSharp archive directly without Homebrew, verifies the
   signed app with `codesign --verify --strict --deep`, and checks its provisioning profile and
   i386/aarch64 runtime files;
3. compiles the production runtime capability code and requires its real current-host probe plus
   synthetic MetalSharp installer, validation, cancellation, rollback, and combined-setup regressions
   to pass;
4. downloads the official Pluto v2.0 ZIP only after those checks, verifies its outer SHA-256 and all
   three inner payload hashes, and extracts only the pinned files;
5. checks that the gated bridge and DLL are i386 and the inference engine is x86-64;
6. runs the real bridge and official binaries in a fresh isolated Wine prefix with 54 generated,
   public HXPF frames (no StarCraft installation, map, replay, or other proprietary game file);
7. requires the expected inference-ready model handshake, a nonzero official engine step count in
   its clean `shutdown after N steps; exiting` record, contiguous HXRP replies 0 through 54, at least
   one strictly parsed classic command, zero `win` in every in-game reply, exit status zero, and
   `[bridge] done (clean EOF)`. Optional per-observation timings are summarized when present but are
   not required from a release build that omits them;
8. permits no owned surviving inference/bridge process. Ownership is limited to the acceptance
   process group or a Wine process naming the isolated `Z:` runtime, never a global substring match.
   Every failure, including a survivor discovered after normal bridge exit, triggers cleanup of owned
   survivors before the error is raised. Unrelated Wine/Pluto processes are not flagged or signalled.

Workflow run **43758320180** passed this complete bridge, inference, strict-command, and clean-shutdown
acceptance on **macOS 26.7.2**, using signed MetalSharp Wine 6.0 ARM64/FEX at
`MetalSharp-Wine.app/Contents/MacOS/wine`. That run is the real ARM64 runtime evidence; synthetic
preflight tests are not a replacement for it. It predates the combined one-click dependency
installer and is not evidence of a real app-managed installation. A real installer acceptance must
separately exercise fresh installation and repair on macOS without touching another Wine.

The harness is `engine/tools/pluto-bridge/macos_runtime_acceptance.py`. It uses direct-pipe mode for
a deterministic game-free transport test. Its JSON summary contains counts, hashes, Wine identity,
and failure reasons. The workflow has no artifact-upload step for the official ZIP, Wine archive,
extracted payloads, runtime directory, or any other official binary.

To run the real harness on an already prepared macOS ARM64 host:

```sh
python3 engine/tools/pluto-bridge/macos_runtime_acceptance.py \
  --wine "$HYPERION_PLUTO_WINE" \
  --mac-backend fex \
  --bridge engine/tools/pluto-bridge/pluto-bridge.exe \
  --pluto-zip /path/to/pluto-v2.0.zip \
  --work /tmp/pluto-macos-runtime-acceptance
```

`--work` must be new or empty. A non-Darwin or non-ARM64 host is rejected, so another platform's
run cannot be presented as macOS evidence.

The exact manual CI dispatch for a branch containing this workflow is:

```sh
gh workflow run shield-release.yml \
  --ref BRANCH \
  -f ref=BRANCH \
  -f tag=UNIQUE-TEST-TAG \
  -f channel=prerelease \
  -f mac-backend=fex-probe
run_id=$(gh run list --workflow=shield-release.yml --event=workflow_dispatch \
  --branch BRANCH --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$run_id" --exit-status
```

This is the release workflow, not a dry-run command: if every job succeeds, its final job publishes
the named prerelease unless `publish=false` or `runtime-only=true` is supplied. Inspect the
`pluto-macos-runtime` job's summary before treating macOS as validated.

Host-only tests use tiny synthetic archives, fake `ditto`/`codesign` tools, and synthetic executables
and app bundles. They cover hash/size refusal, signature/layout/identity failures, symlink retention,
cancellation, activation rollback, and Pluto preservation without network access, official payloads,
a real code signature, or a real installation:

```sh
python3 engine/tools/pluto-native-tests/test_native.py PortRegressionTests
python3 engine/tools/pluto-bridge/test_macos_runtime_acceptance.py
```
