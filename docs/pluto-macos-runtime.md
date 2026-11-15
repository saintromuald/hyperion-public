# Pluto runtime on macOS

## Supported prerequisite boundary

The macOS ARM64 package can **attempt** Pluto only when all of these are true:

1. The host is macOS 15 or later on Apple Silicon.
2. Rosetta 2 is installed and can execute an x86-64 system binary.
3. A regular executable Wine 11 installation reports `wine-11.* --version` and contains its unified
   32-bit/64-bit runtime. Pluto needs both: Hyperion's bridge and the official `pluto.dll` are i386
   PE images, while official `pluto_infer.exe` is an x86-64 PE image. A `wine64`-only installation
   is insufficient.

Check the OS and Rosetta installation with:

```sh
test "$(/usr/sbin/sysctl -n kern.osproductversion | cut -d. -f1)" -ge 15
sudo /usr/sbin/softwareupdate --install-rosetta --agree-to-license # only if Rosetta is absent
/usr/bin/arch -x86_64 /usr/bin/true
```

macOS 15 Rosetta can execute AVX/AVX2 even when default CPUID or `hw.optional.avx2_0` omits it.
That sysctl bit is therefore not a necessary capability test and Hyperion does not use it. Hyperion
sets `ROSETTA_ADVERTISE_AVX=1` for translated Wine consumers so official Windows binaries can see
the translator's AVX support. The OS/Rosetta check and Wine preflight only permit an attempt; they
do not claim inference works. Actual bridge/inference readiness, replies, and clean shutdown are the
runtime proof.

Restart Hyperion after installing Rosetta or repairing Wine in place. The OS/Rosetta result and the
Wine probe for each resolved pathname are cached for the life of the native Hyperion process. If a
prerequisite check fails, installing or repairing the payload ZIP cannot override that refusal.

Intel Macs use the native x86-64 AVX2 check and the same Wine 11 requirement. The release acceptance
job described below is specifically Apple Silicon evidence, not evidence for every Intel/Wine
combination.

## Wine installation, without Homebrew

Wine is a prerequisite, as it is on Linux. Hyperion neither bundles Wine nor downloads it during
Pluto repair, and a successful payload download is never reported as a successful inference run.

The release job pins the official WineHQ macOS package published by `Gcenx/macOS_Wine_builds`:

| Field | Value |
|---|---|
| Version | Wine Devel 11.21 |
| URL | `https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.21/wine-devel-11.21-osx64.tar.xz` |
| SHA-256 | `9909b486657e10ab66c7279371ae1f990345b596b3b1b3a0cf21f28af24fa0ce` |
| Loader in the archive | `Wine Devel.app/Contents/Resources/wine/bin/wine` |

The same archive can be installed manually without Homebrew. This example keeps it in the user's
Applications folder and does not modify a system Wine installation:

```sh
archive="$HOME/Downloads/wine-devel-11.21-osx64.tar.xz"
curl --proto '=https' --tlsv1.2 --retry 3 -fL \
  'https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.21/wine-devel-11.21-osx64.tar.xz' \
  -o "$archive"
printf '%s  %s\n' \
  '9909b486657e10ab66c7279371ae1f990345b596b3b1b3a0cf21f28af24fa0ce' "$archive" \
  | shasum -a 256 -c -
mkdir -p "$HOME/Applications"
tar -xJf "$archive" -C "$HOME/Applications"
export HYPERION_PLUTO_WINE="$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"
```

Approve the hash-verified official app through macOS's normal Gatekeeper flow if the system asks.
Hyperion does not strip quarantine attributes or bypass that user decision. Homebrew's disabled Wine
casks are not used or required.

`HYPERION_PLUTO_WINE` may name an absolute executable or a bare name on `PATH`. An explicitly set but
invalid override fails rather than silently selecting another Wine. Without an override, macOS looks
for the official Stable, Devel, and Staging app-bundle loaders under `/Applications`, then looks for
`wine` on `PATH`. The selected name must resolve to a regular executable file, not a directory, and
that same absolute path is used for the version probe and `posix_spawn` launch.

Preflight actually invokes that resolved file with `--version`, requires `wine-11.*`, and checks for:

- `../lib/wine/i386-windows/ntdll.dll` relative to the `bin/wine` loader;
- `../lib/wine/x86_64-windows/ntdll.dll`;
- `../lib/wine/x86_64-unix/wine`.

Consequently `/bin/true`, an executable directory, a script reporting the wrong Wine version, and an
incomplete Wine layout fail before any Pluto download. These checks reject invalid prerequisites;
they do not substitute for the real runtime acceptance run.

The default Linux `/usr/bin/wine` resolution and native Windows direct-process behavior are
unchanged; the Wine 11/macOS preflight is not imposed on those platforms.

## Runtime and launch checks

The existing installer still downloads only the official Pluto v2.0 ZIP and verifies its outer hash
and every DLL/engine/weight hash before installation. On macOS that installer cannot start unless
the macOS 15/Rosetta and Wine 11 unified-runtime checks pass. File installation is evidence of
integrity, not a substitute for runtime acceptance.

The POSIX process launcher must also give the bridge its own runtime directory. macOS uses
`posix_spawn_file_actions_addchdir_np`; failure to apply that directory refuses the spawn instead of
letting the bridge search for `pluto.dll` in Hyperion's directory. Apple Silicon launches set
`ROSETTA_ADVERTISE_AVX=1` in the Wine child's environment.

## ARM64 runtime acceptance gate

`pluto-macos-runtime` in `.github/workflows/shield-release.yml` runs on the ARM64 `macos-15` image
and is in `publish.needs`. A release cannot publish if this job is skipped or fails. The job:

1. asserts macOS 15+ on ARM64, installs Rosetta if necessary, and proves an x86-64 system binary can
   execute; it does not gate on `hw.optional.avx2_0`;
2. exports `ROSETTA_ADVERTISE_AVX=1`, downloads and SHA-256-checks the pinned Gcenx archive directly
   without Homebrew, and verifies its Wine 11 version and unified runtime files;
3. compiles the production runtime capability code and requires its real current-host probe to pass;
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

The harness is `engine/tools/pluto-bridge/macos_runtime_acceptance.py`. It uses direct-pipe mode for
a deterministic game-free transport test. Its JSON summary contains counts, hashes, Wine identity,
and failure reasons. The workflow has no artifact-upload step for the official ZIP, Wine archive,
extracted payloads, runtime directory, or any other official binary.

To run the real harness on an already prepared macOS ARM64 host:

```sh
python3 engine/tools/pluto-bridge/macos_runtime_acceptance.py \
  --wine "$HYPERION_PLUTO_WINE" \
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
  -f channel=prerelease
run_id=$(gh run list --workflow=shield-release.yml --event=workflow_dispatch \
  --branch BRANCH --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$run_id" --exit-status
```

This is the release workflow, not a dry-run command: if every job succeeds, its final job publishes
the named prerelease. Inspect the `pluto-macos-runtime` job's summary before treating macOS as
validated. This document describes the gate and required evidence; it does not claim that the gate
has passed on a local or hosted runner.

Host-only harness tests, which use synthetic executables rather than a real Wine installation and
do not use official payloads, are:

```sh
python3 engine/tools/pluto-bridge/test_macos_runtime_acceptance.py
```
