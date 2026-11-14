# Pluto known-good snapshots

`engine/tools/pluto-acceptance/pluto_snapshot.py` preserves a reviewed set of source, runtime, bandit, and evidence bytes without rebuilding Hyperion or launching StarCraft/Pluto. It uses only the Python standard library.

The four commands have deliberately different mutation boundaries:

| Command | What it does | What it never does |
|---|---|---|
| `snapshot` | Copies supplied inputs into one **new**, private, content-addressed directory and writes `manifest.json` atomically. | Never overwrites an existing snapshot, moves/deletes originals, uploads files, or creates a Git tag. |
| `verify` | Checks schema, paths, layout, symlink targets, sizes, SHA256, the immutable directory-name hash, and available Git commit/tree metadata. | Never rebuilds, launches games/inference, or repairs a mismatch. |
| `restore` | Verifies and then materializes a snapshot into an **entirely new** private directory with `RECOVERY.md`. | Never resets a worktree or uses an existing destination, even an empty one. |
| `forward-port` | Compares OG scoped source hashes with a current worktree and prints commands to review for a **new** worktree. | Never cherry-picks, merges, checks out, creates a worktree, edits a repository, builds, or claims compatibility. |

> **Current status warning:** this tooling does **not** designate the current Pluto installation as OG. Repository/Git OG tags and the human decision to confirm a particular installation are a later, separate step. Do not use `--user-confirmed` merely to make a candidate verify or to enable forward-port planning.

## Trust and privacy boundary

Official/proprietary DLLs, inference executables, weights, maps, logs, and evidence remain local. The tool has no upload command and performs no network operation. Git is invoked only for local read-only queries with optional locks disabled. Recorded repository URLs and original local paths are metadata, not upload destinations.

Use a caller-selected durable root on a private local filesystem, for example `/durable/private/pluto-snapshots`, and retain an independent backup. The staging/manifest temporary file never becomes the sole copy. A destination under `/tmp` or `/var/tmp` produces an explicit warning; a temporary test snapshot is not a durable known-good archive.

Snapshot directories are mode `0700`; copied regular files retain only owner permission bits. Existing caller-provided root directories are not chmodded. Restored files are likewise private. This is not encryption: filesystem permissions and backups remain the operator's responsibility.

## Manifest and immutable layout

The schema is `hyperion-pluto-known-good-v1`. A snapshot has this layout:

```text
STATUS-YYYYMMDDTHHMMSSZ-UNIQUE_ID-FULL_MANIFEST_SHA256/
  manifest.json
  source/...                 # only explicitly scoped source files/links
  runtime/engine
  runtime/sdl
  runtime/bridge
  runtime/official_dll
  runtime/inference
  runtime/weights
  bandit/before
  bandit/after
  evidence/NNN-NAME/...      # when evidence is supplied
```

The full SHA256 of the exact `manifest.json` bytes is the final directory-name component. The manifest includes every payload file's SHA256 and size, so changing payload bytes, a manifest hash, a symlink target, or the directory-name digest is detected. Restores may use an arbitrary new directory name; their copied manifest remains authoritative for payload verification.

The manifest contains:

- `source`: original repository path/name, branch, full commit and tree IDs, whether Git metadata was present, scoped dirty status, and scoped file/link records;
- `runtime`: required engine, SDL, bridge, official unmodified DLL, inference, and weights records;
- `bandit`: independent before/after records and hashes, plus whether they differ;
- `provenance`: build JSON, toolchain, Wine, BWAPI, and shim fields, each present but nullable when not supplied;
- `protocol`: protocol name and a JSON timing object;
- `evaluation`: map names/paths, settings JSON, and explicit seeds;
- `evidence`: supplied paths and recursively copied file/directory/link records;
- `designation`, `limitations`, and free-form recovery `notes`.

Regular-file hashes cover file content. Symlink hashes cover the link-target text and are marked `hash_subject: symlink-target-text`; links are never dereferenced while walking evidence or copying. A symlink to an external file does **not** implicitly archive that target. Supply the target separately if it must be recoverable. Empty evidence directories are retained as directory records. Other special file types are rejected.

A candidate does not need optional build/toolchain/Wine/BWAPI/shim values. Missing provenance is represented by `null`, not invented. Candidates and `user_confirmed_og` snapshots are never silently promoted or edited in place. A later confirmation requires a **new** snapshot with both:

```bash
--user-confirmed \
--confirmation 'REVIEWER: Pluto AI is properly working — reviewed evidence IDENTIFIER on DATE'
```

The confirmation must contain the exact case-sensitive phrase `Pluto AI is properly working`. This is an attestation gate, not automated proof that the statement is true.

## Create a candidate

Run from the acceptance directory. Replace every example path and metadata value with the actual reviewed local inputs; the command below is an interface example, not an instruction to archive the current installation now.

```bash
cd /path/to/hyperion/engine/tools/pluto-acceptance
python3 pluto_snapshot.py snapshot \
  --root /durable/private/pluto-snapshots \
  --source /path/to/OG-source-worktree \
  --source-file engine/src/Pluto/pluto.cpp \
  --source-file engine/src/Pluto/pluto.h \
  --engine /path/to/runtime/Hyperion.x86_64 \
  --sdl /path/to/runtime/libSDL2-2.0.so.0 \
  --bridge /path/to/runtime/pluto-bridge.exe \
  --official-dll /path/to/official/pluto.dll \
  --inference /path/to/runtime/pluto_infer.exe \
  --weights /path/to/runtime/pluto_weights.bin \
  --bandit-before /path/to/evidence/bandit-before.txt \
  --bandit-after /path/to/evidence/bandit-after.txt \
  --evidence /path/to/reviewed-acceptance-output \
  --evidence /path/to/reviewed-pluto-acceptance-maps.json \
  --protocol 'HXP4/HXRP; native in-game command dispatch' \
  --timing '{"receipt_frames_after_emit":1,"dispatch_frames_after_emit":3}' \
  --map '(2)ReviewedMap.scx' \
  --settings '{"players":2,"game_type":"melee","frame_cap":30000}' \
  --seed 730000 \
  --seed 730001 \
  --build-provenance '{"command":"reviewed build command","output":"engine binary identifier"}' \
  --toolchain 'REVIEWED COMPILER/LINKER VERSIONS' \
  --wine 'REVIEWED WINE BUILD/VERSION' \
  --bwapi 'REVIEWED BWAPI VERSION/COMMIT' \
  --shim 'REVIEWED BRIDGE/SHIM COMMIT/PATCH' \
  --limitation 'Compatibility and acceptance are limited to the retained evidence.' \
  --notes 'Reviewer, local recovery location, and other useful context' \
  --json
```

Repeat `--source-file`, `--map`, `--seed`, `--evidence`, and `--limitation` as needed. All six runtime arguments and both bandit states are required so an incomplete runtime cannot silently look complete. The source scope is explicit: the tool never archives an entire repository by default. Source paths must be normalized relative POSIX paths; absolute paths, `..`, symlinked parent directories, and directories passed as `--source-file` are rejected.

Normally `--source` is a Git worktree top level. For intentionally non-Git source directories, add `--allow-missing-source`; copied source hashes are still mandatory, but branch/commit/tree remain `null`. `--source-repo` can supply a repository name when no origin exists.

The source and every selected source/runtime path must be outside `--root`. Evidence is rejected there too, preventing recursive self-archiving. Existing snapshots are never reused. The tool tries Linux `FICLONE` copy-on-write and otherwise truncates only the new destination file and performs a checked regular copy. Source and destination hashes must match the pre-copy hash; a source changing during capture aborts publication. If an I/O failure occurs after the new directory is created, it remains private and manifest-less for diagnosis rather than deleting anything that might be mistaken for an older snapshot.

## Verify

For a Git-backed snapshot whose original repository is still at the recorded commit:

```bash
python3 pluto_snapshot.py verify \
  --snapshot /durable/private/pluto-snapshots/candidate-YYYYMMDDTHHMMSSZ-UNIQUE_ID-FULL_MANIFEST_SHA256 \
  --json
```

Use `--source /path/to/another/worktree` to check the recorded commit/tree at another local checkout. For an intentionally non-Git source directory, add:

```bash
  --allow-missing-source
```

That option does **not** waive schema, payload hash, size, path, symlink, or layout validation. If the original source path is entirely unavailable, verification reports a warning and still checks the self-contained payload. If it exists but lacks expected Git metadata, verification fails unless the option is explicit. An available Git-backed source must have the recorded full `HEAD` commit and `HEAD^{tree}` IDs.

Human-readable output is the default. `--json` returns `valid`, `errors`, `warnings`, and check counts. Valid input exits 0; integrity/schema/source mismatches exit 1; command-line or operational failures exit 2. Verification never launches a build, game, Wine, or inference process.

## Restore to a new directory

```bash
python3 pluto_snapshot.py restore \
  --snapshot /durable/private/pluto-snapshots/candidate-YYYYMMDDTHHMMSSZ-UNIQUE_ID-FULL_MANIFEST_SHA256 \
  --destination /durable/private/recoveries/pluto-NEW-UNIQUE \
  --json
```

The destination must not already exist; an existing empty directory, symlink, or nonempty directory is refused. Restore first performs full schema/payload verification. Normalized paths and parent-collision checks reject `../`, absolute paths, and attempts to place files beneath a symlink. No existing worktree is reset. Restored payload hashes are checked again.

`RECOVERY.md` records the original snapshot, manifest digest, designation, source provenance, notes, limitations, privacy warning, and the need to run tests. It does not convert a candidate into OG.

## Read-only forward-port plan

Only a `user_confirmed_og` snapshot is accepted for this OG-specific workflow. The command checks the current worktree bytes against the OG scoped source records; `--base-commit` selects the base for the **printed** new-worktree command.

```bash
python3 pluto_snapshot.py forward-port \
  --snapshot /durable/private/pluto-snapshots/og-YYYYMMDDTHHMMSSZ-UNIQUE_ID-FULL_MANIFEST_SHA256 \
  --repo /path/to/current-hyperion-worktree \
  --base-commit FULL_REVIEWED_BASE_COMMIT \
  --json
```

The JSON groups every scoped path as `unchanged`, `changed`, `missing`, or `conflicting` (for example, file versus symlink). It also records scoped dirty status and differences from the base, the resolved base and current HEAD, warnings about differing bases, and commands containing `NEW-WORKTREE`/`NEW-RESTORE` placeholders. Those commands are text for separate human review. The tool does not execute them.

`repository_modified` and `mutations_performed` are `false`; `tests_run` and `compatibility_proven` are always `false`. Even all-unchanged hashes prove only those bytes, not source/API/runtime compatibility. Changed/missing/conflicting paths exit 1; an unchanged comparison exits 0; invalid input or operational errors exit 2. Run the relevant build, synthetic tests, and separate acceptance protocol in a new worktree before making any forward-port claim. There is intentionally no `--apply-in-new-worktree` mutation option.

## Tests

The tests use temporary directories, tiny synthetic files, and synthetic temporary Git repositories only:

```bash
cd /path/to/hyperion/engine/tools/pluto-acceptance
python3 -m unittest -v test_pluto_snapshot.py
python3 -m unittest -v test_pluto_acceptance.py
```

They cover snapshot/verify/restore round trips, private layout, content and manifest tampering, symlink preservation, path traversal and symlink-parent rejection, existing destination/snapshot refusal, OG confirmation gating, missing Git metadata, source commit/tree mismatch, changed/missing/conflicting forward-port plans, and original-input non-mutation. No real Pluto assets, builds, games, Wine, inference, refs, or running processes are used.

## Design limitations

- SHA256 and content-addressed names provide integrity evidence, not a digital signature or a remote trust anchor. Protect the local archive and its backup from unauthorized replacement.
- A Git commit/tree identifies the repository commit, while scoped files are captured from the worktree as supplied. A dirty scoped source is allowed and explicitly recorded; OG confirmation must not be interpreted as a clean-tree assertion.
- Git commit/tree verification is a current-checkout comparison, not proof that the original binaries were produced by that commit. Build provenance is optional supplied metadata, not a reproducible-build proof.
- Runtime acceptance is established only by the separate acceptance evidence and human review. Snapshot creation never runs that evidence, and forward-port planning never claims compatibility.
- Maps and settings are metadata unless their files/reviewed manifests are also supplied as evidence. The snapshot tool does not re-review maps or validate acceptance outcomes.
- External symlink targets, unscoped source files, and unspecified runtime dependencies are not automatically included. Review the recovered layout and record such limitations explicitly.
- Reflink support depends on the filesystem. The regular-copy fallback is safe but uses additional space. Neither form replaces an independent durable backup.
