#!/usr/bin/env python3
"""Phase 5 Pluto acceptance runner.

Only the ``run`` command can start Hyperion.  ``manifest`` is a dry run and
``init-target`` only creates an immutable baseline/target input.  The module
uses the Python standard library exclusively.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import datetime as _datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import uuid

GAME_COUNT = 30
FRAME_CAP = 30000
DEFAULT_MAP_ROOT = Path(
    "/home/saintbellarmine/Games/cosmo-farm/trees/slot0-a/prefix/drive_c/"
    "Cosmonarchy/Starcraft/Maps/CMBW-Root/CMBW-maps/melee/1v1/ASL"
)
OFFICIAL_INFERENCE_SHA256 = "3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc"
OFFICIAL_DLL_SHA256 = "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc"
OFFICIAL_WEIGHTS_SHA256 = "c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86"
TARGET_SCHEMA = "hyperion-pluto-acceptance-target-v1"
MANIFEST_SCHEMA = "hyperion-pluto-acceptance-manifest-v1"
RESULTS_SCHEMA = "hyperion-pluto-acceptance-results-v1"
MAP_MANIFEST_SCHEMA = "hyperion-pluto-acceptance-map-manifest-v1"
MAP_REVIEW_ATTESTATION = (
    "The reviewer inspected these exact SHA256-pinned files and attests that each is "
    "an ordinary two-player melee map with two start locations and an ordinary starting state."
)
RACES = ("zerg", "terran", "protoss")
RACE_LETTER = {"zerg": "z", "terran": "t", "protoss": "p"}
RACE_NUMBER = {"zerg": 0, "terran": 1, "protoss": 2}
NUMBER_RACE = {value: key for key, value in RACE_NUMBER.items()}
OPPONENTS = {
    "zerg": ("terran", "protoss", "zerg", "terran", "protoss", "zerg", "terran", "protoss", "zerg", "terran"),
    "terran": ("protoss", "zerg", "terran", "protoss", "zerg", "terran", "protoss", "zerg", "terran", "protoss"),
    "protoss": ("zerg", "terran", "protoss", "zerg", "terran", "protoss", "zerg", "terran", "protoss", "zerg"),
}
REQUIRED_RUN_ARTIFACTS = ("summary.json", "settings.json", "run.rep")
REQUIRED_GLOBAL_ARTIFACTS = (
    "replies-raw.bin",
    "frames.bin",
    "hyperion_pluto.log",
    "pluto.log",
    "pluto_infer.log",
)
OPTIONAL_GLOBAL_ARTIFACTS = (
    "bridge.log",
    "pluto_infer_stdout.log",
    "pluto_infer_stderr.log",
    "bot-stdout.log",
)
TARGET_METRICS = ("wins_total", "win_rate_decided")


class AcceptanceError(RuntimeError):
    """A preflight or series integrity error."""


class BaselineChanged(AcceptanceError):
    """The explicit baseline/target file changed after planning."""


def utc_now() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def _no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key: %s" % key)
        result[key] = value
    return result


def json_load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_no_duplicate_keys)
    except (OSError, UnicodeError, ValueError) as exc:
        raise AcceptanceError("cannot read valid JSON %s: %s" % (path, exc)) from exc


def json_bytes(value) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_json_new(path: Path, value) -> None:
    try:
        with path.open("xb") as handle:
            handle.write(json_bytes(value))
            handle.flush()
            os.fsync(handle.fileno())
    except FileExistsError as exc:
        raise AcceptanceError("refusing to overwrite existing file: %s" % path) from exc


def write_json_atomic(path: Path, value) -> None:
    """Replace a generated result file, never a user-supplied input."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".%s." % path.name, dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(json_bytes(value))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def file_fingerprint(path: Path, with_hash: bool = True) -> dict:
    stat = path.stat()
    result = {
        "path": str(path.resolve()),
        "device": stat.st_dev,
        "inode": stat.st_ino,
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "mode": stat.st_mode,
    }
    if with_hash:
        result["sha256"] = sha256_file(path)
    return result


def same_fingerprint(left: dict, right: dict) -> bool:
    keys = ("device", "inode", "size", "mtime_ns", "mode", "sha256")
    return all(left.get(key) == right.get(key) for key in keys if key in left or key in right)


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def _finite_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _nonnegative_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def create_target_file(
    output: Path,
    evidence: Path | None,
    unmeasured: bool,
    metric: str | None,
    comparison: str,
    value: float | None,
) -> dict:
    """Create a new target file; never infer a numeric target."""
    if output.exists():
        raise AcceptanceError("target already exists; create a new file instead of editing it: %s" % output)
    if (evidence is None) == (not unmeasured):
        raise AcceptanceError("choose exactly one of --evidence or --unmeasured")
    target_requested = metric is not None or value is not None
    if target_requested and (metric is None or value is None):
        raise AcceptanceError("--target-metric and --target-value must be supplied together")
    if target_requested and not _finite_number(value):
        raise AcceptanceError("target value must be finite")
    if target_requested and unmeasured:
        raise AcceptanceError("a numeric target requires measured baseline evidence")
    if metric is not None and metric not in TARGET_METRICS:
        raise AcceptanceError("unsupported target metric: %s" % metric)

    if unmeasured:
        baseline = {"status": "unmeasured", "measured_games": 0, "metrics": {}}
    else:
        evidence = evidence.resolve()
        evidence_doc = json_load(evidence)
        if not isinstance(evidence, Path) or not isinstance(evidence_doc, dict):
            raise AcceptanceError("baseline evidence must be a JSON object")
        aggregate = evidence_doc.get("aggregate")
        if not isinstance(aggregate, dict):
            raise AcceptanceError("baseline evidence needs an aggregate object")
        measured_games = aggregate.get("valid_games", aggregate.get("measured_games"))
        if not _nonnegative_int(measured_games) or measured_games == 0:
            raise AcceptanceError("baseline evidence has no positive aggregate.valid_games")
        wins = aggregate.get("wins")
        losses = aggregate.get("losses")
        if not _nonnegative_int(wins) or not _nonnegative_int(losses):
            raise AcceptanceError("baseline evidence needs non-negative aggregate wins and losses")
        decided = wins + losses
        metrics = {
            "wins_total": wins,
            "win_rate_decided": (wins / decided if decided else None),
        }
        baseline = {
            "status": "measured",
            "evidence_path": str(evidence),
            "evidence_sha256": sha256_file(evidence),
            "measured_games": measured_games,
            "metrics": metrics,
        }

    target = None
    if target_requested:
        baseline_value = baseline["metrics"].get(metric)
        if not _finite_number(baseline_value):
            raise AcceptanceError("baseline evidence does not measure target metric %s" % metric)
        target = {"metric": metric, "comparison": comparison, "value": value}
    document = {
        "schema": TARGET_SCHEMA,
        "created_utc": utc_now(),
        "baseline": baseline,
        "target": target,
        "immutability": "The runner verifies content and metadata before every game and before every results write.",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    write_json_new(output, document)
    # init-target may make its new file read-only.  The runner never chmods or
    # otherwise modifies a target supplied to it.
    output.chmod(0o444)
    return document


def validate_target_document(document) -> None:
    if not isinstance(document, dict) or document.get("schema") != TARGET_SCHEMA:
        raise AcceptanceError("baseline/target has the wrong schema")
    if not isinstance(document.get("created_utc"), str) or not document["created_utc"]:
        raise AcceptanceError("baseline/target needs created_utc")
    baseline = document.get("baseline")
    if not isinstance(baseline, dict) or baseline.get("status") not in ("measured", "unmeasured"):
        raise AcceptanceError("baseline/target needs a measured or explicitly unmeasured baseline")
    measured_games = baseline.get("measured_games")
    if not _nonnegative_int(measured_games):
        raise AcceptanceError("baseline measured_games must be a non-negative integer")
    target = document.get("target")
    if baseline["status"] == "unmeasured":
        if measured_games != 0 or target is not None:
            raise AcceptanceError("an unmeasured baseline cannot have a numeric target")
        return
    if measured_games == 0:
        raise AcceptanceError("a measured baseline needs measured_games > 0")
    evidence_path = baseline.get("evidence_path")
    evidence_hash = baseline.get("evidence_sha256")
    if not isinstance(evidence_path, str) or not evidence_path or not re.fullmatch(r"[0-9a-f]{64}", evidence_hash or ""):
        raise AcceptanceError("measured baseline needs evidence path and SHA256")
    evidence = Path(evidence_path)
    if not evidence.is_file() or sha256_file(evidence) != evidence_hash:
        raise AcceptanceError("measured baseline evidence is missing or changed: %s" % evidence)
    metrics = baseline.get("metrics")
    if not isinstance(metrics, dict):
        raise AcceptanceError("measured baseline needs metrics")
    if target is None:
        return
    if not isinstance(target, dict):
        raise AcceptanceError("target must be an object or null")
    if target.get("metric") not in TARGET_METRICS or target.get("comparison") not in (">=", ">", "<=", "<", "=="):
        raise AcceptanceError("target metric or comparison is unsupported")
    if not _finite_number(target.get("value")):
        raise AcceptanceError("target value must be finite")
    if not _finite_number(metrics.get(target["metric"])):
        raise AcceptanceError("numeric target lacks corresponding measured baseline evidence")


class BaselineGuard:
    def __init__(self, path: Path):
        self.path = path.resolve()
        if not self.path.is_file():
            raise AcceptanceError("explicit baseline/target file does not exist: %s" % self.path)
        self.initial = file_fingerprint(self.path)
        self.document = json_load(self.path)
        validate_target_document(self.document)
        self.check()

    def check(self) -> None:
        try:
            current = file_fingerprint(self.path)
        except OSError as exc:
            raise BaselineChanged("baseline/target disappeared or is unreadable: %s" % self.path) from exc
        if not same_fingerprint(self.initial, current):
            raise BaselineChanged(
                "baseline/target changed after acceptance planning; refusing to alter or replace it: %s" % self.path
            )

    def public_record(self) -> dict:
        return {"file": self.initial, "document": self.document, "unchanged_checks": "required throughout series"}


def _excluded_map(relative: Path) -> bool:
    if any(part.casefold() == "sandbox" for part in relative.parts[:-1]):
        return True
    name = relative.name.casefold()
    if re.search(r"\((?:ob|obfog|fog|tr|solo)\)", name):
        return True
    if "sandbox" in name or re.search(r"_2$", relative.stem.casefold()):
        return True
    return False


def load_map_manifest(root: Path, manifest: Path | None = None) -> dict:
    """Read the separate human-reviewed, SHA256-pinning melee-map manifest.

    A filename or a map suffix is not evidence of melee settings.  The runner
    deliberately has no command that creates this review attestation from whatever
    files happen to be present.
    """
    root = root.resolve()
    manifest = (manifest or root / "pluto-acceptance-maps.json").resolve()
    document = json_load(manifest)
    if not isinstance(document, dict) or document.get("schema") != MAP_MANIFEST_SCHEMA:
        raise AcceptanceError("reviewed map manifest has the wrong schema: %s" % manifest)
    review = document.get("review")
    if (
        not isinstance(review, dict)
        or not isinstance(review.get("reviewer"), str)
        or not review["reviewer"].strip()
        or not isinstance(review.get("reviewed_utc"), str)
        or not review["reviewed_utc"].strip()
        or review.get("attestation") != MAP_REVIEW_ATTESTATION
    ):
        raise AcceptanceError("map manifest lacks a complete ordinary-melee review attestation: %s" % manifest)
    entries = document.get("maps")
    if not isinstance(entries, list) or not entries:
        raise AcceptanceError("reviewed map manifest contains no maps: %s" % manifest)
    seen_paths = set()
    normalized = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise AcceptanceError("reviewed map manifest entries must be objects")
        value = entry.get("path")
        digest = entry.get("sha256")
        if not isinstance(value, str) or not value or not re.fullmatch(r"[0-9a-f]{64}", digest or ""):
            raise AcceptanceError("each reviewed map needs a relative path and lowercase SHA256")
        relative = Path(value)
        if relative.is_absolute() or relative.as_posix() != value or any(part in ("", ".", "..") for part in relative.parts):
            raise AcceptanceError("reviewed map path is not a normalized relative path: %s" % value)
        if relative.suffix.casefold() not in (".scm", ".scx") or not relative.name.startswith("(2)") or _excluded_map(relative):
            raise AcceptanceError("reviewed map is not an eligible ordinary (2) ASL map: %s" % value)
        if (
            entry.get("players") != 2
            or entry.get("start_locations") != 2
            or entry.get("game_type") != "melee"
            or entry.get("ordinary_start") is not True
        ):
            raise AcceptanceError("reviewed map lacks explicit two-player ordinary melee settings: %s" % value)
        folded = value.casefold()
        if folded in seen_paths:
            raise AcceptanceError("duplicate reviewed map path: %s" % value)
        seen_paths.add(folded)
        normalized.append((relative, digest))
    document["_normalized_maps"] = normalized
    return document


def _discover_reviewed_maps(root: Path, document: dict) -> list[tuple[Path, str, str]]:
    root = root.resolve()
    if not root.is_dir():
        raise AcceptanceError("ASL map root is not a directory: %s" % root)
    candidates = []
    for relative, expected_hash in document["_normalized_maps"]:
        path = (root / relative).resolve()
        if not _is_within(path, root) or not path.is_file():
            raise AcceptanceError("reviewed map is missing or escapes the ASL root: %s" % relative)
        actual_hash = sha256_file(path)
        if actual_hash != expected_hash:
            raise AcceptanceError("reviewed map SHA256 mismatch for %s" % relative)
        candidates.append((relative.as_posix().casefold(), relative.as_posix(), path, relative, actual_hash))
    candidates.sort(key=lambda item: (item[0], item[1]))
    selected = []
    seen_hashes = set()
    for _, _, path, relative, digest in candidates:
        if digest in seen_hashes:
            continue
        seen_hashes.add(digest)
        selected.append((path, relative.as_posix(), digest))
        if len(selected) == GAME_COUNT:
            break
    if len(selected) != GAME_COUNT:
        raise AcceptanceError("need exactly 30 distinct reviewed ordinary two-player ASL maps; found %d" % len(selected))
    return selected


def discover_maps(root: Path, manifest: Path | None = None) -> list[tuple[Path, str, str]]:
    """Return 30 content-distinct maps from the reviewed hash/settings manifest."""
    return _discover_reviewed_maps(root, load_map_manifest(root, manifest))


@dataclasses.dataclass(frozen=True)
class RunnerConfig:
    engine: Path
    data_folder: Path
    pluto_folder: Path
    inference: Path
    bandit: Path | None
    output: Path
    baseline_target: Path
    install_folder: Path | None = None
    official_dll: Path | None = None
    map_root: Path = DEFAULT_MAP_ROOT
    map_manifest: Path | None = None
    lock_file: Path | None = None
    wine: Path | None = None
    mod: str = "bw"
    timeout_seconds: float = 1800.0
    cleanup_timeout_seconds: float = 15.0
    seed_base: int = 730000

    def resolved(self) -> "RunnerConfig":
        values = dataclasses.asdict(self)
        for key in (
            "engine",
            "data_folder",
            "pluto_folder",
            "inference",
            "bandit",
            "output",
            "baseline_target",
            "install_folder",
            "official_dll",
            "map_root",
            "map_manifest",
            "lock_file",
            "wine",
        ):
            if values[key] is not None:
                values[key] = Path(values[key]).expanduser().resolve()
        if values["map_manifest"] is None:
            values["map_manifest"] = values["map_root"] / "pluto-acceptance-maps.json"
        if values["lock_file"] is None:
            values["lock_file"] = values["pluto_folder"] / ".pluto-acceptance.host.lock"
        return RunnerConfig(**values)


@dataclasses.dataclass(frozen=True)
class GameSpec:
    game: int
    map_path: Path
    map_relative: str
    map_sha256: str
    pluto_race: str
    opponent_race: str
    requested_pluto_slot: int
    seed: int

    def public(self) -> dict:
        return {
            "game": self.game,
            "map": str(self.map_path),
            "map_relative": self.map_relative,
            "map_sha256": self.map_sha256,
            "pluto_race": self.pluto_race,
            "opponent_race": self.opponent_race,
            "requested_pluto_slot": self.requested_pluto_slot,
            "seed": self.seed,
            "frame_cap": FRAME_CAP,
            "opponent_level": "Cheater: insane",
        }


def build_schedule(config: RunnerConfig, map_document: dict | None = None) -> list[GameSpec]:
    maps = (
        _discover_reviewed_maps(config.map_root, map_document)
        if map_document is not None
        else discover_maps(config.map_root, config.map_manifest)
    )
    within_race = {race: 0 for race in RACES}
    schedule = []
    for index in range(GAME_COUNT):
        pluto_race = RACES[index % len(RACES)]
        race_index = within_race[pluto_race]
        within_race[pluto_race] += 1
        opponent_race = OPPONENTS[pluto_race][race_index]
        map_path, relative, digest = maps[index]
        schedule.append(
            GameSpec(
                game=index + 1,
                map_path=map_path,
                map_relative=relative,
                map_sha256=digest,
                pluto_race=pluto_race,
                opponent_race=opponent_race,
                requested_pluto_slot=index % 2,
                seed=config.seed_base + index,
            )
        )
    return schedule


def command_for(spec: GameSpec, config: RunnerConfig) -> list[str]:
    races = [RACE_LETTER[spec.opponent_race], RACE_LETTER[spec.opponent_race]]
    levels = ["i", "i"]
    races[spec.requested_pluto_slot] = RACE_LETTER[spec.pluto_race]
    levels[spec.requested_pluto_slot] = "p"
    command = [str(config.engine), "--bench"]
    if config.mod:
        command += ["--mod", config.mod]
    if config.install_folder is not None:
        command += ["--install-folder", str(config.install_folder)]
    command += [
        "--data",
        str(config.data_folder),
        "--map",
        str(spec.map_path),
        "--players",
        "2",
        "--bench-races",
        ",".join(races),
        "--ai-levels",
        ",".join(levels),
        "--bench-type",
        "melee",
        "--bench-frames",
        str(FRAME_CAP),
        "--bench-speed",
        "fastest",
        "--bench-shown",
        "none",
        "--seed",
        str(spec.seed),
    ]
    return command


def environment_removals() -> list[str]:
    return sorted(
        key for key in os.environ
        if key.startswith("BWRL_") or key.startswith("HX_TEST_") or key == "HX_DEBUG"
    )


def environment_for(config: RunnerConfig) -> dict:
    environment = os.environ.copy()
    # Acceptance uses Pluto's natural defaults, not a developer's inherited
    # inference overrides or bridge test hooks.  Binary paths and dumping are
    # set explicitly below; all BWRL_* policy/runtime tuning is cleared.
    for key in environment_removals():
        environment.pop(key, None)
    environment["HYPERION_PLUTO_FOLDER"] = str(config.pluto_folder)
    environment["HYPERION_PLUTO_DUMP"] = "1"
    if config.wine is not None:
        environment["HYPERION_PLUTO_WINE"] = str(config.wine)
    return environment


def command_record(spec: GameSpec, config: RunnerConfig) -> dict:
    argv = command_for(spec, config)
    environment = {"HYPERION_PLUTO_FOLDER": str(config.pluto_folder), "HYPERION_PLUTO_DUMP": "1"}
    if config.wine is not None:
        environment["HYPERION_PLUTO_WINE"] = str(config.wine)
    return {
        "argv": argv,
        "shell_escaped": shlex.join(argv),
        "cwd": str(config.engine.parent),
        "environment_overrides": environment,
        "environment_removals": environment_removals(),
        "timeout_seconds": config.timeout_seconds,
        "frame_cap": FRAME_CAP,
    }


def read_bandit(path: Path) -> dict:
    """Validate one complete original bandit snapshot without modifying it."""
    fingerprint = file_fingerprint(path)
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise AcceptanceError("cannot read bandit snapshot %s: %s" % (path, exc)) from exc
    lines = text.splitlines()
    nonempty = [line.strip() for line in lines if line.strip()]
    if not nonempty or nonempty[-1] != "end" or nonempty.count("end") != 1:
        raise AcceptanceError("bandit snapshot is not cleanly terminated by exactly one 'end': %s" % path)
    seq_values = []
    format_values = []
    records = 0
    arms = 0
    arm_pattern = re.compile(
        r"^([ZTP?])\s+([ZTP?])\s+(\S+)\s+([0-9]+(?:\.[0-9]+)?)\s+"
        r"([0-9]+(?:\.[0-9]+)?)(?:\s+#.*)?$"
    )
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line == "end":
            continue
        if line.startswith("seq "):
            try:
                seq_values.append(int(line.split()[1]))
            except (ValueError, IndexError) as exc:
                raise AcceptanceError("invalid bandit seq in %s" % path) from exc
            continue
        if line.startswith("format "):
            try:
                format_values.append(int(line.split()[1]))
            except (ValueError, IndexError) as exc:
                raise AcceptanceError("invalid bandit format in %s" % path) from exc
            continue
        if line.startswith("{"):
            try:
                record = json.loads(line, object_pairs_hook=_no_duplicate_keys)
            except ValueError as exc:
                raise AcceptanceError("unparseable bandit JSON record in %s: %s" % (path, exc)) from exc
            if not isinstance(record, dict) or record.get("t") not in ("start", "end", "event"):
                raise AcceptanceError("invalid bandit record type in %s" % path)
            records += 1
            continue
        match = arm_pattern.fullmatch(line)
        if not match:
            raise AcceptanceError("unparseable bandit line in %s: %s" % (path, line))
        arms += 1
    if len(seq_values) != 1 or seq_values[0] < 0 or format_values != [2] or arms == 0:
        raise AcceptanceError("bandit snapshot lacks one valid seq, format 2, and arm table: %s" % path)
    return {"file": fingerprint, "seq": seq_values[0], "format": 2, "records": records, "arms": arms}


def discover_bandit(pluto_folder: Path, explicit: Path | None) -> Path:
    candidates = []
    for parent in (
        pluto_folder / "bwapi-data" / "read",
        pluto_folder / "bwapi-data" / "write",
        pluto_folder,
    ):
        path = parent / "pluto_bandit_Bravo.txt"
        if path.is_file():
            candidates.append(path.resolve())
    candidates = list(dict.fromkeys(candidates))
    if explicit is not None:
        explicit = explicit.resolve()
        if not _is_within(explicit, pluto_folder):
            raise AcceptanceError("live bandit must be inside HYPERION_PLUTO_FOLDER: %s" % explicit)
        if not explicit.is_file():
            raise AcceptanceError("live bandit does not exist: %s" % explicit)
        if explicit not in candidates:
            candidates.append(explicit)
        if len(candidates) != 1:
            raise AcceptanceError("need one unambiguous active Bravo bandit, found: %s" % candidates)
        return explicit
    if len(candidates) != 1:
        raise AcceptanceError("need exactly one original Bravo bandit in read/write/root; found %d" % len(candidates))
    return candidates[0]


def verify_binary(path: Path, expected: str | None, label: str) -> dict:
    if not path.is_file():
        raise AcceptanceError("%s does not exist: %s" % (label, path))
    record = file_fingerprint(path)
    record["label"] = label
    record["expected_sha256"] = expected
    record["verified"] = expected is None or record["sha256"] == expected
    if expected is not None and record["sha256"] != expected:
        raise AcceptanceError("%s SHA256 mismatch: got %s, expected %s" % (label, record["sha256"], expected))
    return record


class SeriesIntegrity:
    """Recheck every pinned executable and scheduled map throughout a series."""

    def __init__(self, binaries: dict, schedule: list[GameSpec], map_manifest: dict):
        self.files = {label: dict(record) for label, record in binaries.items()}
        self.files["reviewed_map_manifest"] = dict(map_manifest["file"])
        self.maps = []
        for spec in schedule:
            record = file_fingerprint(spec.map_path)
            # discover_maps() hashed the reviewed bytes earlier.  Do not let a
            # change during preflight become the new accepted initial state.
            if record["sha256"] != spec.map_sha256:
                raise AcceptanceError("scheduled map changed during preflight: %s" % spec.map_path)
            self.maps.append({"game": spec.game, "relative": spec.map_relative, **record})
        self.checks_completed = 0
        self.last_checked_utc = None
        self.check()

    @staticmethod
    def _changed(label: str, initial: dict) -> None:
        path = Path(initial["path"])
        try:
            current = file_fingerprint(path)
        except OSError as exc:
            raise AcceptanceError("series-critical input disappeared or is unreadable (%s): %s" % (label, path)) from exc
        if not same_fingerprint(initial, current):
            raise AcceptanceError(
                "series-critical input changed after preflight (%s): %s; aborting rather than launching/evaluating with mixed inputs"
                % (label, path)
            )

    def check(self) -> None:
        for label, initial in self.files.items():
            self._changed(label, initial)
        for initial in self.maps:
            self._changed("scheduled map game %d (%s)" % (initial["game"], initial["relative"]), initial)
        self.checks_completed += 1
        self.last_checked_utc = utc_now()

    def public_record(self) -> dict:
        return {
            "policy": "full metadata+SHA256 recheck before every launch and every results write, including final results",
            "tracked_binary_labels": sorted(self.files),
            "scheduled_map_count": len(self.maps),
            "checks_completed_at_manifest": self.checks_completed,
            "last_checked_utc": self.last_checked_utc,
        }


class ProcessInspector:
    """Read-only /proc checks for other Hyperion/Pluto hosts."""

    def conflicts(self, engine: Path) -> list[dict]:
        conflicts = []
        engine = engine.resolve()
        proc = Path("/proc")
        if not proc.is_dir():
            raise AcceptanceError("/proc is required for host exclusivity checks")
        own_pid = os.getpid()
        for entry in proc.iterdir():
            if not entry.name.isdigit() or int(entry.name) == own_pid:
                continue
            pid = int(entry.name)
            try:
                comm = (entry / "comm").read_text(encoding="utf-8", errors="replace").strip()
                try:
                    executable = str((entry / "exe").resolve())
                except OSError:
                    executable = ""
                raw = (entry / "cmdline").read_bytes()
                argv = [part.decode("utf-8", errors="replace") for part in raw.split(b"\0") if part]
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                continue
            except OSError:
                continue
            argv0 = argv[0] if argv else ""
            windows_argv0 = argv0.replace("\\", "/").rsplit("/", 1)[-1]
            names = (comm.casefold(), Path(executable).name.casefold(), windows_argv0.casefold())
            is_pluto = any("pluto_infer" in name or "pluto-bridge" in name for name in names)
            is_hyperion = any(name.startswith("hyperion") for name in names)
            same_engine = bool(executable) and Path(executable).resolve() == engine
            if is_pluto or is_hyperion or same_engine:
                conflicts.append({"pid": pid, "comm": comm, "exe": executable, "argv0": argv0})
        conflicts.sort(key=lambda item: item["pid"])
        return conflicts


@dataclasses.dataclass
class Preflight:
    schedule: list[GameSpec]
    target: BaselineGuard
    bandit: dict
    binaries: dict
    process_conflicts: list[dict]
    map_manifest: dict
    integrity: SeriesIntegrity


def preflight(config: RunnerConfig, inspector: ProcessInspector | None = None) -> Preflight:
    config = config.resolved()
    if not config.engine.is_file():
        raise AcceptanceError("Hyperion executable does not exist: %s" % config.engine)
    if not config.data_folder.is_dir() or not config.pluto_folder.is_dir():
        raise AcceptanceError("data and Pluto folders must already exist")
    expected_inference = (config.pluto_folder / "pluto" / "pluto_infer.exe").resolve()
    if config.inference.resolve() != expected_inference:
        raise AcceptanceError("verified inference must be the deployed %s" % expected_inference)
    if config.timeout_seconds <= 0 or config.cleanup_timeout_seconds <= 0:
        raise AcceptanceError("timeouts must be positive")
    if _is_within(config.baseline_target, config.output):
        raise AcceptanceError("baseline/target must be outside the fresh results directory")
    if config.output.exists():
        raise AcceptanceError("output must be new; refusing to resume or count historical results: %s" % config.output)
    target = BaselineGuard(config.baseline_target)
    map_manifest_file = verify_binary(config.map_manifest, None, "reviewed ordinary-melee map manifest")
    map_document = load_map_manifest(config.map_root, config.map_manifest)
    schedule = build_schedule(config, map_document)
    bandit_path = discover_bandit(config.pluto_folder, config.bandit)
    bandit = read_bandit(bandit_path)
    if config.official_dll is None:
        raise AcceptanceError("--official-dll is required; a patched bridge DLL is not acceptable")
    deployed_dll = (config.pluto_folder / "pluto.dll").resolve()
    binaries = {
        "engine": verify_binary(config.engine, None, "Hyperion executable"),
        "inference": verify_binary(config.inference, OFFICIAL_INFERENCE_SHA256, "official pluto_infer.exe"),
        "official_unmodified_dll": verify_binary(
            config.official_dll, OFFICIAL_DLL_SHA256, "official unmodified pluto.dll"
        ),
        "deployed_dll": verify_binary(deployed_dll, OFFICIAL_DLL_SHA256, "deployed unmodified pluto.dll"),
        "bridge": verify_binary(config.pluto_folder / "pluto-bridge.exe", None, "Pluto bridge executable"),
        "weights": verify_binary(config.pluto_folder / "pluto" / "pluto_weights.bin",
                                 OFFICIAL_WEIGHTS_SHA256, "official pluto_weights.bin"),
    }
    map_manifest = {
        "file": map_manifest_file,
        "review": map_document["review"],
        "listed_maps": len(map_document["_normalized_maps"]),
        "scheduled_maps": GAME_COUNT,
    }
    integrity = SeriesIntegrity(binaries, schedule, map_manifest)
    inspector = inspector or ProcessInspector()
    conflicts = inspector.conflicts(config.engine)
    return Preflight(schedule, target, bandit, binaries, conflicts, map_manifest, integrity)


def make_manifest(config: RunnerConfig, checked: Preflight, dry_run: bool) -> dict:
    games = []
    for spec in checked.schedule:
        item = spec.public()
        item["command"] = command_record(spec, config)
        games.append(item)
    return {
        "schema": MANIFEST_SCHEMA,
        "series_id": uuid.uuid4().hex,
        "created_utc": utc_now(),
        "mode": "dry-run" if dry_run else "live",
        "historical_results_used_as_games": False,
        "resume_allowed": False,
        "game_count": GAME_COUNT,
        "frame_cap": FRAME_CAP,
        "map_root": str(config.map_root),
        "map_manifest": checked.map_manifest,
        "series_integrity": checked.integrity.public_record(),
        "config": {
            "engine": str(config.engine),
            "data_folder": str(config.data_folder),
            "pluto_folder": str(config.pluto_folder),
            "inference": str(config.inference),
            "official_dll": str(config.official_dll) if config.official_dll else None,
            "map_manifest": str(config.map_manifest),
            "live_bandit": str(config.bandit),
            "baseline_target": str(config.baseline_target),
            "lock_file": str(config.lock_file),
            "timeout_seconds": config.timeout_seconds,
            "cleanup_timeout_seconds": config.cleanup_timeout_seconds,
        },
        "baseline_target": checked.target.public_record(),
        "original_bandit": checked.bandit,
        "bandit_policy": "one live original snapshot; copied for evidence, never reset or edited; carried between games",
        "binaries": checked.binaries,
        "host_process_preflight": {
            "conflicts": checked.process_conflicts,
            "exclusive": not checked.process_conflicts,
            "dry_run_will_not_launch": dry_run,
        },
        "games": games,
    }


def _map_path(value) -> Path | None:
    if not isinstance(value, str) or not value:
        return None
    normalized = value.replace("\\", "/")
    if re.match(r"^[A-Za-z]:/", normalized):
        normalized = normalized[2:]
    return Path(normalized)


def _level_key(value) -> str:
    return re.sub(r"[^a-z]", "", str(value).casefold())


def _stat_signature(path: Path) -> tuple | None:
    try:
        stat = path.stat()
        return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns
    except OSError:
        return None


def _fresh_source(path: Path, before: tuple | None, launched_epoch_ns: int) -> bool:
    try:
        stat = path.stat()
    except OSError:
        return False
    # A two-second clock-resolution allowance is not a freshness substitute:
    # an existing file must also have changed its stat signature.
    changed = before is None or _stat_signature(path) != before
    return changed and stat.st_mtime_ns >= launched_epoch_ns - 2_000_000_000


def _copy_artifact(source: Path, destination: Path, required: bool, fresh: bool = True) -> dict:
    record = {"source": str(source), "required": required, "exists": source.is_file(), "fresh": fresh}
    if not source.is_file():
        if required:
            record["error"] = "missing"
        return record
    shutil.copy2(source, destination)
    record.update(file_fingerprint(destination))
    record["fresh"] = fresh
    if required and not fresh:
        record["error"] = "stale"
    if required and destination.stat().st_size == 0:
        record["error"] = "empty"
    return record


def _new_benchmark_dir(data_folder: Path, before: set[Path]) -> tuple[Path | None, list[str]]:
    root = data_folder / "benchmarks"
    if not root.is_dir():
        return None, []
    current = {path.resolve() for path in root.iterdir() if path.is_dir()}
    new = sorted(current - before, key=lambda path: str(path).casefold())
    if len(new) == 1:
        return new[0], [str(path) for path in new]
    return None, [str(path) for path in new]


def _process_group_alive(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _signal_process_group(pgid: int, sig: int) -> None:
    with contextlib.suppress(ProcessLookupError, PermissionError):
        os.killpg(pgid, sig)


def cleanup_process_group(process: subprocess.Popen, pgid: int, timeout: float, immediate: bool = False) -> dict:
    """Clean only the session/group created for this game."""
    record = {"pgid": pgid, "term_sent": False, "kill_sent": False, "group_gone": False}
    deadline = time.monotonic() + (0.0 if immediate else timeout)
    while time.monotonic() < deadline:
        if not _process_group_alive(pgid):
            record["group_gone"] = True
            return record
        time.sleep(0.05)
    record["term_sent"] = True
    _signal_process_group(pgid, signal.SIGTERM)
    deadline = time.monotonic() + min(timeout, 5.0)
    while time.monotonic() < deadline:
        if not _process_group_alive(pgid):
            record["group_gone"] = True
            break
        time.sleep(0.05)
    if not record["group_gone"]:
        record["kill_sent"] = True
        _signal_process_group(pgid, signal.SIGKILL)
        deadline = time.monotonic() + min(timeout, 5.0)
        while time.monotonic() < deadline:
            if not _process_group_alive(pgid):
                record["group_gone"] = True
                break
            time.sleep(0.05)
    with contextlib.suppress(subprocess.TimeoutExpired):
        process.wait(timeout=1.0)
    return record


def execute_command(
    command: list[str],
    config: RunnerConfig,
    game_dir: Path,
    timeout: float,
    popen_factory=subprocess.Popen,
) -> dict:
    """Execute one game, or no game when the caller is a dry run (which never calls this)."""
    engine_log = game_dir / "engine.log"
    started_utc = utc_now()
    started_epoch_ns = time.time_ns()
    started = time.monotonic()
    process = None
    pgid = None
    timed_out = False
    interrupted = False
    launch_error = None
    with engine_log.open("wb") as output:
        try:
            process = popen_factory(
                command,
                cwd=str(config.engine.parent),
                env=environment_for(config),
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            pgid = process.pid
            try:
                return_code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                return_code = None
            except KeyboardInterrupt:
                interrupted = True
                return_code = None
        except KeyboardInterrupt:
            interrupted = True
            return_code = None
        except (OSError, subprocess.SubprocessError) as exc:
            launch_error = str(exc)
            return_code = None
        cleanup = None
        if process is not None and pgid is not None:
            cleanup = cleanup_process_group(process, pgid, config.cleanup_timeout_seconds, immediate=timed_out or interrupted)
            if return_code is None and not timed_out and process.poll() is not None:
                return_code = process.returncode
    return {
        "started_utc": started_utc,
        "finished_utc": utc_now(),
        "launched_epoch_ns": started_epoch_ns,
        "wall_time_seconds": max(0.0, time.monotonic() - started),
        "return_code": return_code,
        "timed_out": timed_out,
        "interrupted": interrupted,
        "launch_error": launch_error,
        "cleanup": cleanup,
    }


def collect_artifacts(
    spec: GameSpec,
    config: RunnerConfig,
    game_dir: Path,
    benchmark_before: set[Path],
    global_before: dict[str, tuple | None],
    execution: dict,
) -> dict:
    run_dir, candidates = _new_benchmark_dir(config.data_folder, benchmark_before)
    artifacts = {}
    if run_dir is not None:
        for name in REQUIRED_RUN_ARTIFACTS:
            artifacts[name] = _copy_artifact(run_dir / name, game_dir / name, required=True)
    else:
        for name in REQUIRED_RUN_ARTIFACTS:
            artifacts[name] = {"source": None, "required": True, "exists": False, "fresh": False, "error": "run folder not identified"}
    sources = {
        "replies-raw.bin": config.pluto_folder / "replies-raw.bin",
        "hyperion_pluto.log": config.data_folder / "hyperion_pluto.log",
        "pluto.log": config.pluto_folder / "pluto.log",
        "pluto_infer.log": config.pluto_folder / "pluto_infer.log",
        "frames.bin": config.pluto_folder / "frames.bin",
        "bridge.log": config.pluto_folder / "bridge.log",
        "pluto_infer_stdout.log": config.pluto_folder / "pluto_infer_stdout.log",
        "pluto_infer_stderr.log": config.pluto_folder / "pluto_infer_stderr.log",
        "bot-stdout.log": config.pluto_folder / "bot-stdout.log",
    }
    for name in REQUIRED_GLOBAL_ARTIFACTS + OPTIONAL_GLOBAL_ARTIFACTS:
        source = sources[name]
        fresh = _fresh_source(source, global_before.get(name), execution["launched_epoch_ns"])
        artifacts[name] = _copy_artifact(source, game_dir / name, name in REQUIRED_GLOBAL_ARTIFACTS, fresh=fresh)
    # engine.log is already in the per-game folder: hash it in place rather
    # than attempting a self-copy.  Empty output is valid when launch fails;
    # command.json and the execution record still retain the launch error.
    engine_log = game_dir / "engine.log"
    artifacts["engine.log"] = {
        "source": str(engine_log),
        "required": True,
        "exists": engine_log.is_file(),
        "fresh": engine_log.is_file(),
    }
    if engine_log.is_file():
        artifacts["engine.log"].update(file_fingerprint(engine_log))
        if engine_log.stat().st_size == 0:
            artifacts["engine.log"]["note"] = "empty engine output"
    else:
        artifacts["engine.log"]["error"] = "missing"
    return {
        "run_dir": str(run_dir) if run_dir else None,
        "new_run_dir_candidates": candidates,
        "artifacts": artifacts,
    }


class HXPParseError(ValueError):
    """Malformed HXP/HXRP transport or classic command bytes."""


# BWAPI 4.4.0 packed native AI command layouts, restricted to Hyperion's
# net_process_game_turn in-game grammar. Unknown opcodes are not resynchronized
# or skipped: the entire candidate HXRP record is rejected.
_CLASSIC_LAYOUTS = {
    0x08: ("RestartGame", "", ()),
    0x0C: ("MakeBuilding", "BhhH", ("order", "tile_x", "tile_y", "unit_type")),
    0x0D: ("SetVision", "H", ("vision",)),
    0x0E: ("SetAllies", "I", ("allies",)),
    0x10: ("PauseGame", "", ()),
    0x11: ("ResumeGame", "", ()),
    0x12: ("UseCheat", "I", ("flags",)),
    0x14: ("RightClick", "hhHHB", ("x", "y", "target_id", "target_type", "queued")),
    0x15: ("Attack", "hhHHBB", ("x", "y", "target_id", "target_type", "order", "queued")),
    0x18: ("CancelConstruction", "", ()),
    0x19: ("CancelUnitMorph", "", ()),
    0x1A: ("Stop", "B", ("queued",)),
    0x1B: ("CarrierStop", "", ()),
    0x1C: ("ReaverStop", "", ()),
    0x1E: ("ReturnCargo", "B", ("queued",)),
    0x1F: ("TrainUnit", "H", ("unit_type",)),
    0x20: ("CancelTrain", "bB", ("slot", "unknown")),
    0x21: ("Cloak", "B", ("unused",)),
    0x22: ("Decloak", "B", ("unused",)),
    0x23: ("UnitMorph", "H", ("unit_type",)),
    0x25: ("Unsiege", "B", ("unused",)),
    0x26: ("Siege", "B", ("unused",)),
    0x27: ("TrainFighter", "", ()),
    0x28: ("UnloadAll", "B", ("unused",)),
    0x29: ("UnloadUnit", "H", ("target_id",)),
    0x2A: ("MergeArchon", "", ()),
    0x2B: ("HoldPosition", "B", ("queued",)),
    0x2C: ("Burrow", "B", ("unused",)),
    0x2D: ("Unburrow", "B", ("unused",)),
    0x2E: ("CancelNuke", "", ()),
    0x2F: ("Lift", "I", ("unknown",)),
    0x30: ("Invent", "B", ("tech",)),
    0x31: ("CancelResearch", "", ()),
    0x32: ("Upgrade", "B", ("upgrade",)),
    0x33: ("CancelUpgrade", "", ()),
    0x34: ("CancelAddon", "", ()),
    0x35: ("BuildingMorph", "H", ("unit_type",)),
    0x36: ("UseStimPack", "", ()),
    0x57: ("LeaveGame", "B", ("type",)),
    0x58: ("MinimapPing", "hh", ("x", "y")),
    0x5A: ("MergeDarkArchon", "", ()),
}
# These have BWAPI layouts and native sizes, but net_process_game_turn stops on
# them. They belong to net_mgr_process_lobby_turns, not in-game HXRP evidence.
_LOBBY_ONLY_COMMAND_NAMES = {
    0x3C: "StartGame",
    0x3E: "UpdateSlot",
    0x41: "RequestChangeRace",
    0x44: "ChangeSlot",
}
_SELECT_NAMES = {0x09: "Select", 0x0A: "SelectAdd", 0x0B: "SelectRemove"}
_REPLY_HEADER = struct.Struct("<4sII")
_FRAME_HEADER = struct.Struct("<4s5I")
_HXP4_HEADER = struct.Struct("<4s7I")
_FRAME_UNIT = struct.Struct("<9I4B")
_FRAME_EXTRA = struct.Struct("<5I2i4I5H10B")
_LATE_EXTRA = struct.Struct("<6H2B")
_FRAME_PLAYER = struct.Struct("<7I")
_PLAYER_TECH = struct.Struct("<44s61s6s8s")
_MAX_REJECTION_DETAILS = 32


def parse_classic_commands(data: bytes) -> list[dict]:
    """Parse a complete native in-game AI command batch with no ignored tail."""
    commands = []
    offset = 0
    while offset < len(data):
        opcode = data[offset]
        if opcode in _SELECT_NAMES:
            name = _SELECT_NAMES[opcode]
            if len(data) - offset < 2:
                raise HXPParseError("truncated %s selection count at command byte %d" % (name, offset))
            count = data[offset + 1]
            if count > 12:
                raise HXPParseError("%s count exceeds 12 at command byte %d" % (name, offset))
            length = 2 + count * 2
            if len(data) - offset < length:
                raise HXPParseError("truncated %s at command byte %d" % (name, offset))
            ids = list(struct.unpack_from("<" + "H" * count, data, offset + 2)) if count else []
            fields = {"count": count, "unit_ids": ids}
        else:
            if opcode in _LOBBY_ONLY_COMMAND_NAMES:
                raise HXPParseError(
                    "%s opcode 0x%02x is lobby-only, not in-game command evidence at command byte %d"
                    % (_LOBBY_ONLY_COMMAND_NAMES[opcode], opcode, offset)
                )
            layout = _CLASSIC_LAYOUTS.get(opcode)
            if layout is None:
                raise HXPParseError("unknown classic opcode 0x%02x at command byte %d" % (opcode, offset))
            name, payload_format, field_names = layout
            command_format = "<B" + payload_format
            length = struct.calcsize(command_format)
            if len(data) - offset < length:
                raise HXPParseError("truncated %s at command byte %d" % (name, offset))
            values = struct.unpack_from(command_format, data, offset)
            fields = dict(zip(field_names, values[1:]))
        commands.append({"offset": offset, "opcode": opcode, "name": name, "length": length, "fields": fields})
        offset += length
    return commands


def _strict_reply_candidate(data: bytes, offset: int):
    if len(data) - offset < _REPLY_HEADER.size:
        return None, 0, "truncated HXRP header", None
    magic, frame, command_size = _REPLY_HEADER.unpack_from(data, offset)
    assert magic == b"HXRP"
    if command_size >= 0x10000:
        return None, 0, "HXRP command size exceeds the native 0x10000 bound", frame
    size = _REPLY_HEADER.size + command_size + 4
    if offset + size > len(data):
        return None, size, "truncated HXRP commands or win field", frame
    command_bytes = data[offset + _REPLY_HEADER.size:offset + _REPLY_HEADER.size + command_size]
    win = struct.unpack_from("<I", data, offset + _REPLY_HEADER.size + command_size)[0]
    try:
        commands = parse_classic_commands(command_bytes)
    except HXPParseError as exc:
        return None, size, str(exc), frame
    if win != 0:
        return None, size, "HXRP win field must be zero for an in-game reply", frame
    return {
        "frame": frame,
        "command_byte_count": command_size,
        "commands": commands,
        "win": win,
        "byte_offset": offset,
        "byte_end": offset + size,
    }, size, None, frame


def _unique_longest_run(runs: list[list[dict]], what: str):
    if not runs:
        return [], "%s contains no contiguous current run beginning at frame zero" % what
    longest = max(len(run) for run in runs)
    candidates = [run for run in runs if len(run) == longest]
    if len(candidates) != 1:
        return [], "%s has ambiguous equally long frame-zero runs" % what
    return candidates[0], None


def parse_hxrp_capture(data: bytes) -> tuple[list[dict], dict]:
    """Select a current reply run from raw bytes, tolerating only foreign noise.

    Every accepted candidate has a complete header, bounded and fully parsed
    classic command payload, present zero win field, and the exact next expected
    frame.  Magic alone and valid replies with unexpected frame numbers are never
    promoted into evidence.
    """
    runs = []
    current = []
    expected = 0
    offset = 0
    candidates = 0
    valid_candidates = 0
    accepted = 0
    rejected = 0
    rejections = []

    def close_run():
        nonlocal current
        if current:
            runs.append(current)
        current = []

    while True:
        found = data.find(b"HXRP", offset)
        if found < 0:
            break
        candidates += 1
        reply, size, error, candidate_frame = _strict_reply_candidate(data, found)
        if reply is None:
            rejected += 1
            if len(rejections) < _MAX_REJECTION_DETAILS:
                rejections.append({
                    "byte_offset": found,
                    "candidate_frame": candidate_frame,
                    "expected_frame": expected if current else 0,
                    "error": error,
                })
            offset = found + 1
            continue
        valid_candidates += 1
        if reply["frame"] == 0:
            close_run()
            current = [reply]
            expected = 1
            accepted += 1
            offset = found + size
            continue
        if current and reply["frame"] == expected:
            current.append(reply)
            expected += 1
            accepted += 1
            offset = found + size
            continue
        rejected += 1
        if len(rejections) < _MAX_REJECTION_DETAILS:
            rejections.append({
                "byte_offset": found,
                "candidate_frame": candidate_frame,
                "expected_frame": expected if current else 0,
                "error": "valid HXRP record has a non-expected frame",
            })
        offset = found + 1
    close_run()
    selected, selection_error = _unique_longest_run(runs, "replies-raw.bin")
    evidence = {
        "valid": selected != [],
        "magic_candidates_seen": candidates,
        "complete_valid_candidates": valid_candidates,
        "strict_replies_accepted": accepted,
        "candidates_rejected": rejected,
        "rejection_details": rejections,
        "rejection_details_truncated": max(0, rejected - len(rejections)),
        "valid_contiguous_runs": [
            {
                "start_byte": run[0]["byte_offset"],
                "end_byte": run[-1]["byte_end"],
                "record_count": len(run),
                "last_frame": run[-1]["frame"],
            }
            for run in runs
        ],
        "selected_record_count": len(selected),
        "selected_first_frame": selected[0]["frame"] if selected else None,
        "selected_last_frame": selected[-1]["frame"] if selected else None,
        "selection_rule": "unique longest complete strict contiguous run beginning at frame zero",
        "acceptance_rule": "complete HXRP, full classic command parse, zero win, and exact expected frame",
        "selection_error": selection_error,
    }
    return selected, evidence


def _parse_frame_at(data: bytes, offset: int):
    if len(data) - offset < _FRAME_HEADER.size:
        raise HXPParseError("truncated HXP frame header at byte %d" % offset)
    magic, frame, unit_count, width, height, player_count = _FRAME_HEADER.unpack_from(data, offset)
    if magic not in (b"HXP2", b"HXP3", b"HXP4", b"HXPF"):
        raise HXPParseError("invalid HXP frame magic at byte %d" % offset)
    if unit_count > 9999 or player_count > 12 or not (1 <= width <= 256 and 1 <= height <= 256):
        raise HXPParseError("invalid HXP counts or map dimensions at byte %d" % offset)
    header_size = _HXP4_HEADER.size if magic == b"HXP4" else _FRAME_HEADER.size
    if magic == b"HXP4":
        if len(data) - offset < header_size:
            raise HXPParseError("truncated HXP4 section sizes at byte %d" % offset)
        extra_size, player_tech_size = struct.unpack_from("<II", data, offset + _FRAME_HEADER.size)
        if extra_size != 78 or player_tech_size != 119:
            raise HXPParseError("invalid HXP4 section sizes at byte %d" % offset)
    cursor = offset + header_size
    units_offset = cursor
    extra_size = _FRAME_EXTRA.size if magic == b"HXP3" else 78
    extras_offset = units_offset + unit_count * _FRAME_UNIT.size
    players_offset = extras_offset + (unit_count * extra_size if magic in (b"HXP3", b"HXP4") else 0)
    player_tech_offset = players_offset + player_count * _FRAME_PLAYER.size
    tile_count = width * height
    size = unit_count * _FRAME_UNIT.size + player_count * _FRAME_PLAYER.size
    if magic in (b"HXP3", b"HXP4"):
        size += unit_count * extra_size
    if magic == b"HXP4":
        size += player_count * _PLAYER_TECH.size
    size += tile_count * 4 if magic in (b"HXP2", b"HXP3", b"HXP4") else 2 * ((tile_count + 7) // 8)
    if cursor + size > len(data):
        raise HXPParseError("truncated %s frame payload at byte %d" % (magic.decode("ascii"), offset))

    unit_extras = None
    if magic in (b"HXP3", b"HXP4"):
        unit_extras = []
        for index in range(unit_count):
            unit = _FRAME_UNIT.unpack_from(data, units_offset + index * _FRAME_UNIT.size)
            extra_at = extras_offset + index * extra_size
            extra = _FRAME_EXTRA.unpack_from(data, extra_at)
            decoded = {"build_type": extra[7], "build_queue_slot": extra[16]}
            if magic == b"HXP4":
                late = _LATE_EXTRA.unpack_from(data, extra_at + _FRAME_EXTRA.size)
                queue = tuple(late[:5])
                slot = extra[16]
                if slot >= 5 or any(unit_type > 228 for unit_type in queue):
                    raise HXPParseError("invalid HXP4 build queue at unit %d" % index)
                if extra[7] != queue[slot]:
                    raise HXPParseError("HXP4 active buildType/queue mismatch at unit %d" % index)
                order = unit[8]
                tech, upgrade, remaining = late[6], late[7], late[5]
                if order == 75:
                    if tech > 44 or upgrade != 61:
                        raise HXPParseError("invalid HXP4 ResearchTech arm at unit %d" % index)
                elif order == 76:
                    if tech != 44 or upgrade > 61:
                        raise HXPParseError("invalid HXP4 Upgrade arm at unit %d" % index)
                elif remaining != 0 or tech != 44 or upgrade != 61:
                    raise HXPParseError("invalid HXP4 non-research union arm at unit %d" % index)
                decoded.update(
                    build_queue=queue,
                    remaining_research_time=remaining,
                    tech_type=tech,
                    upgrade_type=upgrade,
                )
            unit_extras.append(decoded)
    player_tech = None
    if magic == b"HXP4":
        player_tech = [
            {
                "has_researched": tuple(record[0]),
                "upgrade_level": tuple(record[1]),
                "tech_in_progress": tuple(record[2]),
                "upgrade_in_progress": tuple(record[3]),
            }
            for record in _PLAYER_TECH.iter_unpack(
                data[player_tech_offset : player_tech_offset + player_count * _PLAYER_TECH.size]
            )
        ]
    cursor += size
    return {
        "protocol": magic.decode("ascii"),
        "frame": frame,
        "unit_count": unit_count,
        "player_count": player_count,
        "map_width": width,
        "map_height": height,
        "unit_extras": unit_extras,
        "player_tech": player_tech,
        "byte_offset": offset,
        "byte_end": cursor,
    }, cursor


def parse_hxp_capture(data: bytes) -> tuple[list[dict], dict]:
    """Strictly parse frames.bin and select one contiguous frame-zero run."""
    records = []
    try:
        offset = 0
        while offset < len(data):
            record, offset = _parse_frame_at(data, offset)
            records.append(record)
    except HXPParseError as exc:
        return [], {"valid": False, "errors": [str(exc)], "total_records": len(records)}
    if not records or records[0]["frame"] != 0:
        return [], {"valid": False, "errors": ["frames.bin must begin with frame zero"], "total_records": len(records)}
    starts = [index for index, record in enumerate(records) if record["frame"] == 0]
    runs = []
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(records)
        run = records[start:end]
        if all(record["frame"] == index for index, record in enumerate(run)):
            runs.append(run)
    selected, selection_error = _unique_longest_run(runs, "frames.bin")
    evidence = {
        "valid": selected != [],
        "errors": [selection_error] if selection_error else [],
        "total_records": len(records),
        "valid_contiguous_runs": [
            {"record_count": len(run), "last_frame": run[-1]["frame"]} for run in runs
        ],
        "selected_record_count": len(selected),
        "selected_first_frame": selected[0]["frame"] if selected else None,
        "selected_last_frame": selected[-1]["frame"] if selected else None,
        "selection_rule": "unique longest strict contiguous run beginning at frame zero; no foreign-byte resync",
    }
    return selected, evidence


def command_application_evidence(reply_records: list[dict], hyperion_text: str, summary: dict | None) -> dict:
    """Correlate raw command frames with actual Hyperion receipt/dispatch lines.

    HXRP bytes alone prove only transport receipt.  They are never described as
    applied commands.  Nonempty command frames old enough to have reached the
    native three-frame dispatch point need an explicit received_bytes/
    accepted_bytes log record. PlutoFrame receives at emitted+1 and applies at
    emitted+3. received_bytes must equal the raw count; accepted_bytes may be
    smaller after stale filtering. Only an eligible accepted_bytes>0 dispatch
    establishes natural-policy application; wholly filtered proof stays separate.
    Legacy ambiguous byte counts are rejected.
    """
    receipts = [
        {"frame": int(match.group(1)), "bytes": int(match.group(2))}
        for match in re.finditer(r"Pluto:\s+frame\s+(\d+)\s+has\s+(\d+)\s+cmd bytes", hyperion_text)
    ]
    dispatches = [
        {
            "emitted": int(match.group(1)),
            "received": int(match.group(2)),
            "dispatched": int(match.group(3)),
            "received_bytes": int(match.group(4)),
            "accepted_bytes": int(match.group(5)),
        }
        for match in re.finditer(
            r"Pluto:\s+emitted\s+(\d+)\s+received\s+(\d+)\s+dispatched\s+(\d+)"
            r"\s+received_bytes\s+(\d+)\s+accepted_bytes\s+(\d+)",
            hyperion_text,
        )
    ]
    legacy_dispatches = [
        {
            "emitted": int(match.group(1)),
            "received": int(match.group(2)),
            "dispatched": int(match.group(3)),
            "ambiguous_bytes": int(match.group(4)),
        }
        for match in re.finditer(
            r"Pluto:\s+emitted\s+(\d+)\s+received\s+(\d+)\s+dispatched\s+(\d+)\s+bytes\s+(\d+)",
            hyperion_text,
        )
    ]
    raw_commands = [
        {"frame": record["frame"], "bytes": record["command_byte_count"]}
        for record in reply_records if record["command_byte_count"] > 0
    ]
    raw_by_frame = {item["frame"]: item["bytes"] for item in raw_commands}
    receipt_mismatches = [
        item for item in receipts if raw_by_frame.get(item["frame"]) != item["bytes"]
    ]
    dispatch_mismatches = [
        item for item in dispatches
        if raw_by_frame.get(item["emitted"]) != item["received_bytes"]
        or item["received"] != item["emitted"] + 1
        or item["dispatched"] != item["emitted"] + 3
        or item["accepted_bytes"] > item["received_bytes"]
    ]
    end_frame = summary.get("end_frame") if isinstance(summary, dict) else None
    eligible = [
        item for item in raw_commands
        if not _nonnegative_int(end_frame) or item["frame"] + 3 <= end_frame
    ]
    matched_dispatches = {
        (item["emitted"], item["received"], item["dispatched"], item["received_bytes"])
        for item in dispatches if item not in dispatch_mismatches
    }
    eligible_dispatch_keys = {
        (item["frame"], item["frame"] + 1, item["frame"] + 3, item["bytes"])
        for item in eligible
    }
    missing = [
        item for item in eligible
        if (item["frame"], item["frame"] + 1, item["frame"] + 3, item["bytes"]) not in matched_dispatches
    ]
    sufficient = not legacy_dispatches and not receipt_mismatches and not dispatch_mismatches and not missing
    valid_dispatches = [item for item in dispatches if item not in dispatch_mismatches]
    positive_native_dispatches = sum(
        (item["emitted"], item["received"], item["dispatched"], item["received_bytes"]) in eligible_dispatch_keys
        and item["accepted_bytes"] > 0
        for item in valid_dispatches
    )
    if legacy_dispatches:
        status = "ambiguous_legacy_dispatch_evidence"
    elif not raw_commands:
        status = "no_nonempty_command_replies"
    elif not eligible:
        status = "commands_too_near_match_end_to_require_dispatch"
    elif sufficient and not positive_native_dispatches:
        status = "filtering_corroborated_without_nonzero_application"
    elif sufficient:
        status = "dispatch_corroborated"
    else:
        status = "insufficient_dispatch_evidence"
    return {
        "sufficient": sufficient,
        "status": status,
        "transport_receipt_only": "HXRP parsing alone never establishes native command application",
        "expected_dispatch_timing": "received=emitted+1; dispatched=emitted+3",
        "byte_count_policy": "received_bytes equals raw HXRP bytes; 0 <= accepted_bytes <= received_bytes after stale filtering",
        "raw_nonempty_command_frames": len(raw_commands),
        "dispatch_eligible_command_frames": len(eligible),
        "receipt_log_records": receipts,
        "dispatch_log_records": dispatches,
        "legacy_dispatch_records": legacy_dispatches,
        "receipt_mismatches": receipt_mismatches,
        "dispatch_mismatches": dispatch_mismatches,
        "missing_dispatch": missing[:_MAX_REJECTION_DETAILS],
        "missing_dispatch_truncated": max(0, len(missing) - _MAX_REJECTION_DETAILS),
        "fully_filtered_dispatch_frames": sum(item["accepted_bytes"] == 0 for item in valid_dispatches),
        "partially_filtered_dispatch_frames": sum(
            0 < item["accepted_bytes"] < item["received_bytes"] for item in valid_dispatches
        ),
        "positive_native_dispatch_frames": positive_native_dispatches,
        "application_established": sufficient and positive_native_dispatches > 0,
    }


def parse_logs(pluto_text: str, hyperion_text: str, infer_text: str) -> dict:
    own_matches = []
    start_matches = []
    for line in pluto_text.splitlines():
        match = re.search(
            r"own_race=(-?\d+)\s+opp_idx=(\d+)\s+opp=\"([^\"]*)\"\s*->\s+BO\s+"
            r"(.*?)\s+\(mask=(0x[0-9A-Fa-f]+),\s*(\d+)\s+arms\)",
            line,
        )
        if match:
            own_matches.append(
                {
                    "own_race": int(match.group(1)),
                    "opponent_index": int(match.group(2)),
                    "opponent_name": match.group(3),
                    "build_order": match.group(4),
                    "build_order_mask": match.group(5),
                    "arms": int(match.group(6)),
                    "line": line,
                }
            )
        start = re.search(r"onStart\s+\(player\s+(\d+),\s+opp race\s+(-?\d+)\)", line)
        if start:
            start_matches.append({"player": int(start.group(1)), "opponent_race": int(start.group(2)), "line": line})
    slots = [int(value) for value in re.findall(r"Pluto:\s+slot\s+(\d+)\s+plays Pluto", hyperion_text)]
    return {
        "own_records": own_matches,
        "start_records": start_matches,
        "hyperion_slots": slots,
        "inference_server_up": "inference server up" in pluto_text,
        "expected_model_in_pluto_log": "v2.0 md09b_1331000" in pluto_text,
        "inference_ready": "[pluto_infer] ready:" in infer_text,
        "expected_model_in_infer_log": "v2.0 md09b_1331000" in infer_text,
        "pluto_relevant_lines": [line for line in pluto_text.splitlines() if "own_race" in line or "onStart" in line or "inference server up" in line],
        "hyperion_relevant_lines": [line for line in hyperion_text.splitlines() if "plays Pluto" in line],
    }


def _read_artifact_text(game_dir: Path, name: str) -> str:
    path = game_dir / name
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def validate_actual_game(spec: GameSpec, game_dir: Path, artifacts: dict) -> dict:
    errors = []
    summary = None
    settings = None
    try:
        summary = json_load(game_dir / "summary.json")
        if not isinstance(summary, dict):
            errors.append("summary.json is not an object")
            summary = None
    except AcceptanceError as exc:
        errors.append(str(exc))
    try:
        settings = json_load(game_dir / "settings.json")
        if not isinstance(settings, dict):
            errors.append("settings.json is not an object")
            settings = None
    except AcceptanceError as exc:
        errors.append(str(exc))

    for name, record in artifacts.items():
        if record.get("required") and (not record.get("exists") or record.get("error") in ("missing", "stale", "empty")):
            errors.append("required artifact %s: %s" % (name, record.get("error", "not fresh")))

    pluto = opponent = None
    players = []
    if isinstance(summary, dict):
        players = summary.get("players")
        if not isinstance(players, list) or len(players) != 2 or not all(isinstance(p, dict) for p in players):
            errors.append("actual summary must contain exactly two player objects")
            players = []
        else:
            named = [p for p in players if p.get("name") == "PLuto"]
            pluto_level = [p for p in players if _level_key(p.get("level")) == "pluto"]
            if len(named) != 1 or len(pluto_level) != 1 or named[0] is not pluto_level[0]:
                errors.append("actual players must have one PLuto-labelled player whose level is Pluto")
            else:
                pluto = named[0]
                opponent = next(p for p in players if p is not pluto)
                if _level_key(opponent.get("level")) != "cheaterinsane":
                    errors.append("actual opponent is not Cheater: insane")
                slots = [p.get("slot") for p in players]
                if not all(_nonnegative_int(slot) for slot in slots) or len(set(slots)) != 2:
                    errors.append("actual player slots are missing or not unique")
                if pluto.get("race") != RACE_NUMBER[spec.pluto_race]:
                    errors.append("actual PLuto race does not match manifest")
                if opponent.get("race") != RACE_NUMBER[spec.opponent_race]:
                    errors.append("actual opponent race does not match manifest")
                numeric = (
                    "minerals",
                    "gas",
                    "minerals_gathered",
                    "gas_gathered",
                    "units",
                    "units_made",
                    "units_lost",
                    "units_killed",
                )
                for player in players:
                    for key in numeric:
                        if not _nonnegative_int(player.get(key)):
                            errors.append("player slot %s has invalid %s" % (player.get("slot"), key))
            for key in ("game_frames", "end_frame"):
                if not _nonnegative_int(summary.get(key)):
                    errors.append("summary has invalid %s" % key)
            if not _finite_number(summary.get("seconds")) or summary.get("seconds", 0) <= 0:
                errors.append("summary has invalid duration")
            if not isinstance(summary.get("ended"), str) or not _nonnegative_int(summary.get("last_player")) and summary.get("last_player") != -1:
                errors.append("summary has invalid ended/last_player")

    if isinstance(settings, dict):
        expected_races = [RACE_LETTER[spec.opponent_race], RACE_LETTER[spec.opponent_race]]
        expected_levels = ["i", "i"]
        expected_races[spec.requested_pluto_slot] = RACE_LETTER[spec.pluto_race]
        expected_levels[spec.requested_pluto_slot] = "p"
        actual_levels = re.sub(r"[,\s]", "", str(settings.get("ai_levels", ""))).casefold()
        actual_races = re.sub(r"[,\s]", "", str(settings.get("races", ""))).casefold()
        if actual_levels != "".join(expected_levels):
            errors.append("settings ai_levels do not match the requested two-seat configuration")
        if actual_races != "".join(expected_races):
            errors.append("settings races do not match the requested two-seat configuration")
        if settings.get("players") != 2 or settings.get("frames") != FRAME_CAP or settings.get("game_type") != "melee":
            errors.append("settings are not natural two-player melee capped at 30000 frames")
        if settings.get("seed") != spec.seed:
            errors.append("settings seed does not match manifest")
        if settings.get("army", 0) or settings.get("pools", 0) or "mutators" in settings:
            errors.append("settings contain stress armies, scaled pools, or mutators")
        settings_map = _map_path(settings.get("map"))
        if settings_map is None or not settings_map.is_absolute() or settings_map.resolve() != spec.map_path.resolve():
            errors.append("settings map does not match manifest")

    pluto_text = _read_artifact_text(game_dir, "pluto.log")
    hyperion_text = _read_artifact_text(game_dir, "hyperion_pluto.log")
    logs = parse_logs(
        pluto_text,
        hyperion_text,
        _read_artifact_text(game_dir, "pluto_infer.log"),
    )
    if len(logs["own_records"]) != 1:
        errors.append("pluto.log must contain exactly one own_race/build-order record")
        own = None
    else:
        own = logs["own_records"][0]
        if own["own_race"] != RACE_NUMBER[spec.pluto_race]:
            errors.append("pluto.log own_race does not match manifest PLuto race")
        if pluto is not None and own["own_race"] != pluto.get("race"):
            errors.append("pluto.log own_race does not match actual PLuto summary race")
        if own["opponent_name"] != "Bravo":
            errors.append("pluto.log opponent does not match the live Bravo bandit")
        if not own["build_order"]:
            errors.append("pluto.log has no natural build-order label")
    if len(logs["start_records"]) != 1 or logs["start_records"][0]["opponent_race"] != RACE_NUMBER[spec.opponent_race]:
        errors.append("pluto.log onStart opponent race is missing or mismatched")
    if pluto is None or set(logs["hyperion_slots"]) != {pluto.get("slot")}:
        errors.append("hyperion_pluto.log does not corroborate the actual PLuto-labelled slot")
    if not logs["inference_server_up"] or not logs["expected_model_in_pluto_log"]:
        errors.append("pluto.log does not prove the expected inference server started")
    if not logs["inference_ready"] or not logs["expected_model_in_infer_log"]:
        errors.append("pluto_infer.log does not prove the expected inference engine was ready")
    raw = game_dir / "replies-raw.bin"
    try:
        reply_records, reply_evidence = parse_hxrp_capture(raw.read_bytes() if raw.is_file() else b"")
    except OSError as exc:
        reply_records, reply_evidence = [], {"valid": False, "selection_error": str(exc)}
    if not reply_records:
        errors.append("replies-raw.bin has no complete valid current-run HXRP replies")

    frames_path = game_dir / "frames.bin"
    try:
        frame_records, frame_evidence = parse_hxp_capture(frames_path.read_bytes() if frames_path.is_file() else b"")
    except OSError as exc:
        frame_records, frame_evidence = [], {"valid": False, "errors": [str(exc)]}
    if not frame_records:
        errors.append("frames.bin has no complete valid current-run HXP frames")
    for error in frame_evidence.get("errors", []):
        errors.append("frames.bin: %s" % error)

    coverage = {
        "frame_zero_contiguous": bool(reply_records),
        "dumped_frame_prefix_checked": bool(reply_records and frame_records),
        "dumped_frame_prefix_covered": bool(
            reply_records and frame_records
            and reply_records[-1]["frame"] >= frame_records[-1]["frame"]
        ),
        "full_match_terminal_coverage_proven": False,
        "terminal_coverage_note": (
            "frames.bin is a bounded prefix capture; contiguous replies and summary/logs are retained, "
            "but this tool does not claim byte-level proof that every later match frame was replied to"
        ),
    }
    if reply_records and frame_records and not coverage["dumped_frame_prefix_covered"]:
        errors.append("HXRP current run does not cover the selected frames.bin current-run prefix")
    if reply_records and isinstance(summary, dict) and _nonnegative_int(summary.get("end_frame")):
        if reply_records[-1]["frame"] > summary["end_frame"]:
            errors.append("HXRP current run extends beyond summary end_frame")
    reply_evidence["coverage"] = coverage
    application = command_application_evidence(reply_records, hyperion_text, summary)

    return {
        "valid": not errors,
        "errors": list(dict.fromkeys(errors)),
        "summary": summary,
        "settings": settings,
        "players": players,
        "pluto": pluto,
        "opponent": opponent,
        "actual_pluto_slot": pluto.get("slot") if pluto else None,
        "requested_slot_matches_actual": bool(pluto and pluto.get("slot") == spec.requested_pluto_slot),
        "logs": logs,
        "natural_build": own,
        "reply_evidence": reply_evidence,
        "frame_evidence": frame_evidence,
        "command_application": application,
    }


def corroboration(summary: dict, winner: dict, loser: dict, require_combat: bool = True) -> dict:
    game_frames = summary.get("game_frames")
    end_frame = summary.get("end_frame")
    seconds = summary.get("seconds")
    checks = {
        "production": winner.get("units_made", 0) > 0 and loser.get("units_made", 0) > 0,
        "resources": winner.get("minerals_gathered", 0) > 0 and loser.get("minerals_gathered", 0) > 0,
        "duration": (
            _finite_number(seconds)
            and seconds > 0
            and _nonnegative_int(game_frames)
            and _nonnegative_int(end_frame)
            and 0 < game_frames <= FRAME_CAP
            and 0 < end_frame <= FRAME_CAP
            and 0 <= end_frame - game_frames <= 1
        ),
        "kills": winner.get("units_killed", 0) > 0 and loser.get("units_lost", 0) > 0,
    }
    return {
        "checks": checks,
        "missing": [key for key, passed in checks.items() if not passed and (key != "kills" or require_combat)],
        "combat_required": require_combat,
    }


def classify_outcome(spec: GameSpec, execution: dict, validation: dict) -> tuple[str, list[str], dict]:
    """Conservative acceptance from actual summary, never DLL onEnd."""
    if execution.get("interrupted"):
        return "incomplete", ["runner interrupted; owned process group cleaned up"], {}
    if execution.get("timed_out"):
        return "incomplete", ["wall timeout"], {}
    if execution.get("launch_error"):
        return "incomplete", ["launch failed: %s" % execution["launch_error"]], {}
    if execution.get("return_code") != 0:
        return "incomplete", ["Hyperion did not exit cleanly: %s" % execution.get("return_code")], {}
    if not isinstance(validation.get("summary"), dict):
        return "incomplete", ["missing or invalid summary.json"], {}
    if not validation.get("valid"):
        return "misconfigured", list(validation.get("errors", [])), {}
    application = validation.get("command_application") or {}
    if not application.get("sufficient", False):
        reasons = ["native command receipt/application evidence is insufficient"]
        for key in ("receipt_mismatches", "dispatch_mismatches", "missing_dispatch", "legacy_dispatch_records"):
            if application.get(key):
                reasons.append("%s: %s" % (key, application[key]))
        return "incomplete", reasons, {"command_application": application}
    if not application.get("application_established", False):
        return (
            "incomplete",
            ["no genuine nonempty in-game command has corroborated native handling; not a natural-policy metric outcome"],
            {"command_application": application},
        )
    summary = validation["summary"]
    pluto = validation["pluto"]
    opponent = validation["opponent"]
    ended = summary.get("ended")
    last = summary.get("last_player")
    if ended == "last":
        if last == pluto["slot"]:
            survivors = pluto["units"] > 0 and opponent["units"] == 0
            evidence = corroboration(summary, pluto, opponent, require_combat=True)
            evidence["survivors"] = {"pluto": pluto["units"], "opponent": opponent["units"], "valid": survivors}
            if survivors and not evidence["missing"]:
                return "win", [], evidence
            reasons = [] if survivors else ["PLuto is last but survivor counts do not prove elimination"]
            reasons += ["insufficient %s corroboration" % key for key in evidence["missing"]]
            return "incomplete", reasons, evidence
        if last == opponent["slot"]:
            survivors = opponent["units"] > 0 and pluto["units"] == 0
            evidence = corroboration(summary, opponent, pluto, require_combat=True)
            evidence["survivors"] = {"pluto": pluto["units"], "opponent": opponent["units"], "valid": survivors}
            if survivors and not evidence["missing"]:
                return "loss", [], evidence
            reasons = [] if survivors else ["opponent is last but survivor counts do not prove elimination"]
            reasons += ["insufficient %s corroboration" % key for key in evidence["missing"]]
            return "incomplete", reasons, evidence
        return "incomplete", ["ended=last but last_player is neither actual player"], {}
    if ended == "finished":
        at_cap = FRAME_CAP - 1 <= summary["game_frames"] <= FRAME_CAP and summary["end_frame"] == FRAME_CAP
        survivors = pluto["units"] > 0 and opponent["units"] > 0 and last == -1
        evidence = corroboration(summary, pluto, opponent, require_combat=False)
        evidence["survivors"] = {"pluto": pluto["units"], "opponent": opponent["units"], "valid": survivors}
        evidence["frame_cap_reached"] = at_cap
        if at_cap and survivors and not evidence["missing"]:
            return "draw", ["30000-frame cap reached with both sides alive"], evidence
        return "incomplete", ["finished before a valid cap draw or without two survivors"], evidence
    return "incomplete", ["unaccepted end reason: %s" % ended], {}


def _player_metrics(player: dict) -> dict:
    return {
        "survivors": player["units"],
        "units_made": player["units_made"],
        "units_lost": player["units_lost"],
        "units_killed": player["units_killed"],
        "minerals": player["minerals"],
        "gas": player["gas"],
        "minerals_gathered": player["minerals_gathered"],
        "gas_gathered": player["gas_gathered"],
    }


def compact_game_record(spec: GameSpec, execution: dict, collection: dict, validation: dict, outcome: str, reasons: list[str], evidence: dict, bandit: dict) -> dict:
    summary = validation.get("summary") if isinstance(validation.get("summary"), dict) else {}
    pluto = validation.get("pluto")
    opponent = validation.get("opponent")
    return {
        **spec.public(),
        "command_file": "command.json",
        "execution": {key: value for key, value in execution.items() if key != "launched_epoch_ns"},
        "outcome": outcome,
        "reasons": reasons,
        "accepted_as_win": outcome == "win",
        "ended": summary.get("ended"),
        "last_player": summary.get("last_player"),
        "game_frames": summary.get("game_frames"),
        "end_frame": summary.get("end_frame"),
        "engine_seconds": summary.get("seconds"),
        "wall_time_seconds": execution.get("wall_time_seconds"),
        "actual_pluto_slot": validation.get("actual_pluto_slot"),
        "requested_slot_matches_actual": validation.get("requested_slot_matches_actual"),
        "config_valid": validation.get("valid", False),
        "config_errors": validation.get("errors", []),
        "pluto": _player_metrics(pluto) if pluto else None,
        "opponent": _player_metrics(opponent) if opponent else None,
        "natural_build": validation.get("natural_build"),
        "reply_evidence": validation.get("reply_evidence"),
        "frame_evidence": validation.get("frame_evidence"),
        "command_application": validation.get("command_application"),
        "race_log": {
            "own_records": validation.get("logs", {}).get("own_records", []),
            "start_records": validation.get("logs", {}).get("start_records", []),
            "hyperion_slots": validation.get("logs", {}).get("hyperion_slots", []),
        },
        "win_evidence": evidence,
        "bandit": bandit,
        "run_dir": collection.get("run_dir"),
        "new_run_dir_candidates": collection.get("new_run_dir_candidates", []),
        "artifacts": collection.get("artifacts", {}),
        "include_in_performance_metrics": outcome in ("win", "loss", "draw"),
    }


def _bandit_public(snapshot: dict | None) -> dict | None:
    if snapshot is None:
        return None
    return {"sha256": snapshot["file"]["sha256"], "seq": snapshot["seq"], "records": snapshot["records"], "arms": snapshot["arms"]}


def run_one(
    spec: GameSpec,
    config: RunnerConfig,
    previous_bandit: dict,
    inspector: ProcessInspector,
    integrity: SeriesIntegrity,
) -> tuple[dict, dict | None, bool]:
    """Run one game. Returns (record, after_bandit, safe_to_continue)."""
    integrity.check()
    game_dir = config.output / ("game-%02d" % spec.game)
    game_dir.mkdir()
    command = command_for(spec, config)
    write_json_new(game_dir / "command.json", command_record(spec, config))
    shutil.copy2(config.bandit, game_dir / "bandit-before.txt")
    before_bandit = read_bandit(game_dir / "bandit-before.txt")
    if before_bandit["file"]["sha256"] != previous_bandit["file"]["sha256"]:
        raise AcceptanceError("bandit changed between games; refusing to reset or continue from ambiguous state")
    conflicts = inspector.conflicts(config.engine)
    if conflicts:
        raise AcceptanceError("another Hyperion/Pluto host is running before game %d: %s" % (spec.game, conflicts))
    benchmark_root = config.data_folder / "benchmarks"
    benchmark_before = {path.resolve() for path in benchmark_root.iterdir() if path.is_dir()} if benchmark_root.is_dir() else set()
    global_sources = {
        "replies-raw.bin": config.pluto_folder / "replies-raw.bin",
        "hyperion_pluto.log": config.data_folder / "hyperion_pluto.log",
        "pluto.log": config.pluto_folder / "pluto.log",
        "pluto_infer.log": config.pluto_folder / "pluto_infer.log",
        "frames.bin": config.pluto_folder / "frames.bin",
        "bridge.log": config.pluto_folder / "bridge.log",
        "pluto_infer_stdout.log": config.pluto_folder / "pluto_infer_stdout.log",
        "pluto_infer_stderr.log": config.pluto_folder / "pluto_infer_stderr.log",
        "bot-stdout.log": config.pluto_folder / "bot-stdout.log",
    }
    global_before = {name: _stat_signature(path) for name, path in global_sources.items()}
    # Keep the final check adjacent to Popen: none of the verified binaries or
    # reviewed scheduled map bytes may change between earlier setup and launch.
    integrity.check()
    execution = execute_command(command, config, game_dir, config.timeout_seconds)
    collection = collect_artifacts(spec, config, game_dir, benchmark_before, global_before, execution)

    after_bandit = None
    bandit_record = {"before": _bandit_public(before_bandit), "after": None, "continuity": "unchanged or advanced live state"}
    safe_to_continue = not execution.get("interrupted")
    cleanup = execution.get("cleanup")
    if cleanup is not None and not cleanup.get("group_gone"):
        safe_to_continue = False
        bandit_record["cleanup_error"] = "owned process group did not exit"
    try:
        # Preserve the raw post-game state even if a crash left it malformed;
        # validate the copy so its hash and seq describe the same bytes.
        shutil.copy2(config.bandit, game_dir / "bandit-after.txt")
        after_bandit = read_bandit(game_dir / "bandit-after.txt")
        bandit_record["after"] = _bandit_public(after_bandit)
        bandit_record["changed"] = before_bandit["file"]["sha256"] != after_bandit["file"]["sha256"]
        if after_bandit["seq"] < before_bandit["seq"]:
            raise AcceptanceError("bandit seq moved backwards during game %d" % spec.game)
    except (AcceptanceError, OSError) as exc:
        bandit_record["continuity_error"] = str(exc)
        safe_to_continue = False

    validation = validate_actual_game(spec, game_dir, collection["artifacts"])
    outcome, reasons, evidence = classify_outcome(spec, execution, validation)
    if not safe_to_continue:
        integrity_reason = (
            bandit_record.get("continuity_error")
            or bandit_record.get("cleanup_error")
            or "runner interrupted; series will not continue"
        )
        reasons = list(reasons) + [integrity_reason]
        if outcome not in ("misconfigured", "incomplete"):
            outcome = "incomplete"
    record = compact_game_record(spec, execution, collection, validation, outcome, reasons, evidence, bandit_record)
    # A separate immutable race/build record is easy to inspect without
    # trusting the requested slot or replaying the game.
    race_record = {
        "game": spec.game,
        "expected": spec.public(),
        "actual_pluto_slot": validation.get("actual_pluto_slot"),
        "actual_pluto": validation.get("pluto"),
        "actual_opponent": validation.get("opponent"),
        "natural_build": validation.get("natural_build"),
        "log_evidence": validation.get("logs", {}),
        "reply_evidence": validation.get("reply_evidence"),
        "frame_evidence": validation.get("frame_evidence"),
        "command_application": validation.get("command_application"),
        "config_valid": validation.get("valid"),
        "config_errors": validation.get("errors"),
    }
    write_json_new(game_dir / "race-record.json", race_record)
    hashes = {"command.json": sha256_file(game_dir / "command.json"), "race-record.json": sha256_file(game_dir / "race-record.json")}
    for name in ("bandit-before.txt", "bandit-after.txt"):
        if (game_dir / name).is_file():
            hashes[name] = sha256_file(game_dir / name)
    for name, artifact in collection["artifacts"].items():
        if artifact.get("exists") and (game_dir / name).is_file():
            hashes[name] = artifact.get("sha256", sha256_file(game_dir / name))
    write_json_new(game_dir / "hashes.json", hashes)
    record["hashes"] = hashes

    conflicts_after = inspector.conflicts(config.engine)
    if conflicts_after:
        record["post_game_process_conflicts"] = conflicts_after
        safe_to_continue = False
    return record, after_bandit, safe_to_continue


def placeholder_record(spec: GameSpec, reason: str) -> dict:
    return {
        **spec.public(),
        "outcome": "not_run",
        "reasons": [reason],
        "accepted_as_win": False,
        "include_in_performance_metrics": False,
        "artifacts": {},
    }


def _sum(records: list[dict], side: str, key: str):
    values = [record[side][key] for record in records if record.get(side)]
    return sum(values) if values else None


def _mean(values: list[float]):
    return sum(values) / len(values) if values else None


def aggregate_results(games: list[dict], series_status: str) -> dict:
    outcomes = ("win", "loss", "draw", "incomplete", "misconfigured", "not_run")
    counts = {outcome: sum(record.get("outcome") == outcome for record in games) for outcome in outcomes}
    attempted = sum(record.get("outcome") != "not_run" for record in games)
    metric_games = [record for record in games if record.get("include_in_performance_metrics")]
    races = {}
    for race in RACES:
        race_games = [record for record in games if record.get("pluto_race") == race]
        race_metrics = [record for record in metric_games if record.get("pluto_race") == race]
        race_counts = {outcome: sum(record.get("outcome") == outcome for record in race_games) for outcome in outcomes}
        decided = race_counts["win"] + race_counts["loss"]
        races[race] = {
            **race_counts,
            "scheduled": len(race_games),
            "win_rate_decided": race_counts["win"] / decided if decided else None,
            "win_rate_all_attempted": race_counts["win"] / (len(race_games) - race_counts["not_run"]) if len(race_games) != race_counts["not_run"] else None,
            "metrics": performance_metrics(race_metrics),
        }
    builds = {race: {} for race in RACES}
    for record in games:
        build = record.get("natural_build")
        if not build or not record.get("config_valid"):
            continue
        race = record["pluto_race"]
        name = build.get("build_order", "unknown")
        builds[race][name] = builds[race].get(name, 0) + 1
    decided = counts["win"] + counts["loss"]
    return {
        "series_status": series_status,
        "scheduled": len(games),
        "attempted": attempted,
        **counts,
        "wins": counts["win"],
        "losses": counts["loss"],
        "draws": counts["draw"],
        "valid_games": len(metric_games),
        "wins_by_race": {race: races[race]["win"] for race in RACES},
        "races": races,
        "win_rate_decided": counts["win"] / decided if decided else None,
        "win_rate_all_attempted": counts["win"] / attempted if attempted else None,
        "metrics": performance_metrics(metric_games),
        "wall_time_seconds_all_attempted": sum(record.get("wall_time_seconds") or 0 for record in games if record.get("outcome") != "not_run"),
        "natural_builds_valid_config": builds,
        "natural_build_record_games": sum(sum(builds[race].values()) for race in RACES),
        "metric_policy": "performance totals include only accepted wins/losses/draws; incomplete, misconfigured, and not_run are reported separately",
    }


def performance_metrics(records: list[dict]) -> dict:
    keys = ("survivors", "units_made", "units_lost", "units_killed", "minerals", "gas", "minerals_gathered", "gas_gathered")
    result = {"games": len(records)}
    for side in ("pluto", "opponent"):
        for key in keys:
            result["%s_%s" % (side, key)] = _sum(records, side, key)
    frames = [record["game_frames"] for record in records if _nonnegative_int(record.get("game_frames"))]
    wall = [record["wall_time_seconds"] for record in records if _finite_number(record.get("wall_time_seconds"))]
    result.update(
        {
            "game_frames_total": sum(frames) if frames else None,
            "game_frames_mean": _mean(frames),
            "wall_time_seconds_total": sum(wall) if wall else None,
            "wall_time_seconds_mean": _mean(wall),
        }
    )
    return result


def series_observation_complete(aggregate: dict) -> bool:
    """A complete observation has 30 freshly accepted, classifiable games."""
    return (
        aggregate.get("series_status") == "completed"
        and aggregate.get("scheduled") == GAME_COUNT
        and aggregate.get("attempted") == GAME_COUNT
        and aggregate.get("valid_games") == GAME_COUNT
        and aggregate.get("incomplete") == 0
        and aggregate.get("misconfigured") == 0
        and aggregate.get("not_run") == 0
    )


def evaluate_target(target_doc: dict, aggregate: dict) -> dict:
    complete = series_observation_complete(aggregate)
    target = target_doc.get("target")
    if target is None:
        return {
            "status": "not_set",
            "meets_target": None,
            "observation_complete": complete,
            "acceptance_passed": None,
            "note": "completed observation is not a numeric acceptance pass; no performance target was invented",
        }
    observed = aggregate["wins"] if target["metric"] == "wins_total" else aggregate["win_rate_decided"]
    if not complete or not _finite_number(observed):
        return {
            "status": "not_evaluable",
            "observed": observed,
            "meets_target": None,
            "observation_complete": False,
            "acceptance_passed": None,
            "target": target,
        }
    comparison = target["comparison"]
    meets = {
        ">=": observed >= target["value"],
        ">": observed > target["value"],
        "<=": observed <= target["value"],
        "<": observed < target["value"],
        "==": observed == target["value"],
    }[comparison]
    return {
        "status": "evaluated",
        "observed": observed,
        "meets_target": meets,
        "observation_complete": True,
        "acceptance_passed": meets,
        "target": target,
        "baseline": target_doc["baseline"],
    }


def acceptance_exit_code(aggregate: dict, evaluation: dict) -> int:
    """0 means a complete observation, or a passed explicitly set target.

    A targetless complete observation is deliberately not labelled an acceptance
    pass.  A false target returns 1; aborted, incomplete, or not-evaluable series
    return 2.  Thus a clean process exit can never hide numeric target failure.
    """
    if not series_observation_complete(aggregate):
        return 2
    if evaluation.get("status") == "not_set":
        return 0
    if evaluation.get("status") == "evaluated":
        return 0 if evaluation.get("meets_target") is True else 1
    return 2


def render_report(results: dict) -> str:
    aggregate = results["aggregate"]
    lines = [
        "# Pluto Phase 5 acceptance report",
        "",
        "Series `%s` (%s), generated %s." % (results["series_id"], results["status"], results["finished_utc"]),
        "",
        "This report contains only the fresh games in this series. No historical result is counted as a new game.",
        "The baseline/target file was read-only input and was never edited by the runner.",
        "Observation complete: %s; numeric acceptance pass: %s." % (
            results["target_evaluation"].get("observation_complete"),
            results["target_evaluation"].get("acceptance_passed"),
        ),
        "",
        "## Outcomes",
        "",
        "| Scope | Wins | Losses | Draws | Incomplete | Misconfigured | Not run | Win rate (decided) |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    def rate(value):
        return "n/a" if value is None else "%.1f%%" % (100 * value)
    lines.append(
        "| All Pluto | %d | %d | %d | %d | %d | %d | %s |"
        % (aggregate["wins"], aggregate["losses"], aggregate["draws"], aggregate["incomplete"], aggregate["misconfigured"], aggregate["not_run"], rate(aggregate["win_rate_decided"]))
    )
    for race in RACES:
        item = aggregate["races"][race]
        lines.append(
            "| %s Pluto | %d | %d | %d | %d | %d | %d | %s |"
            % (race.title(), item["win"], item["loss"], item["draw"], item["incomplete"], item["misconfigured"], item["not_run"], rate(item["win_rate_decided"]))
        )
    lines += [
        "",
        "Attempted %d of %d scheduled games; %d accepted games contribute performance totals." % (aggregate["attempted"], aggregate["scheduled"], aggregate["valid_games"]),
        "Frame-limit results are draws only with both sides alive; timeouts, bad configurations, contradictory end states, and missing evidence remain separate.",
        "",
        "## Performance totals (accepted wins/losses/draws only)",
        "",
        "| Metric | Pluto | Opponent |",
        "|---|---:|---:|",
    ]
    metrics = aggregate["metrics"]
    labels = (
        ("units_killed", "Kills"),
        ("survivors", "Survivors"),
        ("units_made", "Units produced"),
        ("units_lost", "Units lost"),
        ("minerals", "Minerals remaining"),
        ("gas", "Gas remaining"),
        ("minerals_gathered", "Minerals gathered"),
        ("gas_gathered", "Gas gathered"),
    )
    for key, label in labels:
        lines.append("| %s | %s | %s |" % (label, metrics["pluto_" + key], metrics["opponent_" + key]))
    lines += [
        "",
        "Game frames: total %s, mean %s. Wall time (accepted games): total %s s, mean %s s." % (
            metrics["game_frames_total"], metrics["game_frames_mean"], metrics["wall_time_seconds_total"], metrics["wall_time_seconds_mean"]
        ),
        "Wall time across every attempted game, including incomplete: %.3f s." % aggregate["wall_time_seconds_all_attempted"],
        "",
        "## Natural build-order records",
        "",
    ]
    for race in RACES:
        builds = aggregate["natural_builds_valid_config"][race]
        rendered = ", ".join("%s (%d)" % (name, count) for name, count in sorted(builds.items())) or "none validated"
        lines.append("- **%s:** %s" % (race.title(), rendered))
    lines += [
        "",
        "## Target evaluation",
        "",
        "```json",
        json.dumps(results["target_evaluation"], indent=2, sort_keys=True, allow_nan=False),
        "```",
        "",
        "## Games",
        "",
        "| Game | Map | Matchup | Actual PLuto slot | Outcome | Frames | Wall s | Build | Reason |",
        "|---:|---|---|---:|---|---:|---:|---|---|",
    ]
    for game in results["games"]:
        build = (game.get("natural_build") or {}).get("build_order", "—")
        reason = "; ".join(game.get("reasons", [])) or "—"
        lines.append(
            "| %02d | %s | %s vs %s | %s | %s | %s | %s | %s | %s |"
            % (
                game["game"], game["map_relative"].replace("|", "\\|"), game["pluto_race"], game["opponent_race"],
                game.get("actual_pluto_slot", "—"), game["outcome"], game.get("game_frames", "—"),
                ("%.3f" % game["wall_time_seconds"]) if _finite_number(game.get("wall_time_seconds")) else "—", build,
                reason.replace("|", "\\|"),
            )
        )
    lines += [
        "",
        "Per-game `hashes.json`, `race-record.json`, exact `command.json`, bandit before/after snapshots, replay, settings, summary, raw replies, and logs are preserved beside this report.",
        "",
    ]
    return "\n".join(lines)


def _results_document(manifest: dict, games: list[dict], status: str, finished_utc: str) -> dict:
    aggregate = aggregate_results(games, status)
    evaluation = evaluate_target(manifest["baseline_target"]["document"], aggregate)
    return {
        "schema": RESULTS_SCHEMA,
        "series_id": manifest["series_id"],
        "created_utc": manifest["created_utc"],
        "finished_utc": finished_utc,
        "status": status,
        "manifest_sha256": None,  # filled by caller after manifest is written
        "historical_results_used_as_games": False,
        "baseline_target": manifest["baseline_target"],
        "binaries": manifest["binaries"],
        "map_manifest": manifest["map_manifest"],
        "series_integrity": manifest["series_integrity"],
        "games": games,
        "aggregate": aggregate,
        "observation_complete": evaluation["observation_complete"],
        "acceptance_passed": evaluation["acceptance_passed"],
        "target_evaluation": evaluation,
    }


def _write_results(config: RunnerConfig, guard: BaselineGuard, results: dict, integrity: SeriesIntegrity) -> None:
    integrity.check()
    guard.check()
    write_json_atomic(config.output / "results.json", results)
    integrity.check()
    guard.check()
    (config.output / "report.md").write_text(render_report(results), encoding="utf-8")
    integrity.check()
    guard.check()


@contextlib.contextmanager
def host_lock(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+b")
    try:
        try:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise AcceptanceError("another acceptance host holds lock: %s" % path) from exc
        handle.seek(0)
        handle.truncate()
        handle.write(("pid=%d started=%s\n" % (os.getpid(), utc_now())).encode("ascii"))
        handle.flush()
        yield
    finally:
        with contextlib.suppress(OSError):
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        handle.close()


def dry_run(config: RunnerConfig, checked: Preflight) -> Path:
    config = dataclasses.replace(config.resolved(), bandit=Path(checked.bandit["file"]["path"]))
    checked.integrity.check()
    manifest = make_manifest(config, checked, dry_run=True)
    config.output.mkdir(parents=True, exist_ok=False)
    checked.target.check()
    write_json_new(config.output / "manifest.json", manifest)
    checked.integrity.check()
    shutil.copy2(config.bandit, config.output / "bandit-before-series.txt")
    if sha256_file(config.output / "bandit-before-series.txt") != checked.bandit["file"]["sha256"]:
        raise AcceptanceError("original bandit snapshot changed during dry-run setup")
    checked.target.check()
    # Deliberately no results.json/report.md: a plan is not an acceptance run.
    return config.output / "manifest.json"


def live_run(config: RunnerConfig, checked: Preflight) -> int:
    config = dataclasses.replace(config.resolved(), bandit=Path(checked.bandit["file"]["path"]))
    if checked.process_conflicts:
        raise AcceptanceError("another Hyperion/Pluto host is running: %s" % checked.process_conflicts)
    with host_lock(config.lock_file):
        # Close the preflight-to-launch race under the host lock.
        checked.integrity.check()
        conflicts = ProcessInspector().conflicts(config.engine)
        if conflicts:
            raise AcceptanceError("another Hyperion/Pluto host appeared after preflight: %s" % conflicts)
        manifest = make_manifest(config, checked, dry_run=False)
        config.output.mkdir(parents=True, exist_ok=False)
        checked.target.check()
        write_json_new(config.output / "manifest.json", manifest)
        manifest_hash = sha256_file(config.output / "manifest.json")
        shutil.copy2(config.bandit, config.output / "bandit-before-series.txt")
        if sha256_file(config.output / "bandit-before-series.txt") != checked.bandit["file"]["sha256"]:
            raise AcceptanceError("original bandit snapshot changed during series setup")
        games = []
        previous_bandit = checked.bandit
        status = "completed"
        abort_reason = None
        inspector = ProcessInspector()
        for index, spec in enumerate(checked.schedule):
            try:
                checked.target.check()
                record, after_bandit, safe = run_one(spec, config, previous_bandit, inspector, checked.integrity)
                games.append(record)
                if after_bandit is not None:
                    previous_bandit = after_bandit
                if not safe:
                    status = "aborted"
                    abort_reason = "integrity or host cleanup failure after game %d" % spec.game
            except (AcceptanceError, OSError) as exc:
                status = "aborted"
                abort_reason = "game %d aborted series: %s" % (spec.game, exc)
                games.append(placeholder_record(spec, abort_reason))
            for remaining in checked.schedule[index + 1:]:
                if status == "aborted":
                    games.append(placeholder_record(remaining, abort_reason))
            if status == "aborted":
                break
            results = _results_document(manifest, games, "in_progress", utc_now())
            results["manifest_sha256"] = manifest_hash
            _write_results(config, checked.target, results, checked.integrity)
        if len(games) < GAME_COUNT:
            status = "aborted"
            abort_reason = abort_reason or "series interrupted"
            for spec in checked.schedule[len(games):]:
                games.append(placeholder_record(spec, abort_reason))
        if any(record["outcome"] == "not_run" for record in games):
            status = "aborted"
        results = _results_document(manifest, games, status, utc_now())
        results["manifest_sha256"] = manifest_hash
        if abort_reason:
            results["abort_reason"] = abort_reason
        _write_results(config, checked.target, results, checked.integrity)
        # A completed targetless observation may exit zero without claiming an
        # acceptance pass.  False numeric targets and unevaluable series cannot.
        return acceptance_exit_code(results["aggregate"], results["target_evaluation"])


def _path(value: str) -> Path:
    return Path(value).expanduser()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    target = sub.add_parser("init-target", help="create a new immutable baseline/target file; never edits one")
    target.add_argument("--output", type=_path, required=True)
    source = target.add_mutually_exclusive_group(required=True)
    source.add_argument("--evidence", type=_path, help="prior results JSON used as measured baseline evidence")
    source.add_argument("--unmeasured", action="store_true", help="explicitly unmeasured baseline; numeric targets forbidden")
    target.add_argument("--target-metric", choices=TARGET_METRICS)
    target.add_argument("--target-value", type=float)
    target.add_argument("--comparison", choices=(">=", ">", "<=", "<", "=="), default=">=")
    target.set_defaults(handler=_init_target_command)

    for name, dry in (("manifest", True), ("run", False)):
        command = sub.add_parser(name, help="dry-run 30-game manifest" if dry else "sequentially run the fresh 30-game series")
        command.add_argument("--engine", type=_path, required=True)
        command.add_argument("--data-folder", type=_path, required=True)
        command.add_argument("--pluto-folder", type=_path, required=True)
        command.add_argument("--inference", type=_path, required=True)
        command.add_argument("--official-dll", required=True, type=_path, help="required official UNMODIFIED pluto.dll reference")
        command.add_argument("--bandit", type=_path, help="live Bravo bandit (auto-discovered only when unambiguous)")
        command.add_argument("--baseline-target", type=_path, required=True)
        command.add_argument("--output", type=_path, required=True, help="new directory; existing dirs are always refused")
        command.add_argument("--install-folder", type=_path)
        command.add_argument("--map-root", type=_path, default=DEFAULT_MAP_ROOT)
        command.add_argument(
            "--map-manifest",
            type=_path,
            help="reviewed SHA256/settings manifest (default: MAP_ROOT/pluto-acceptance-maps.json)",
        )
        command.add_argument("--lock-file", type=_path)
        command.add_argument("--wine", type=_path)
        command.add_argument("--mod", default="bw")
        command.add_argument("--timeout-seconds", type=float, default=1800.0)
        command.add_argument("--cleanup-timeout-seconds", type=float, default=15.0)
        command.add_argument("--seed-base", type=int, default=730000)
        command.set_defaults(handler=_run_command, dry_run=dry)
    return parser


def _init_target_command(args) -> int:
    document = create_target_file(args.output.resolve(), args.evidence, args.unmeasured, args.target_metric, args.comparison, args.target_value)
    print("created immutable baseline/target %s (target: %s)" % (args.output, document["target"] or "not set"))
    return 0


def _config_from_args(args) -> RunnerConfig:
    return RunnerConfig(
        engine=args.engine,
        data_folder=args.data_folder,
        pluto_folder=args.pluto_folder,
        inference=args.inference,
        bandit=args.bandit,
        output=args.output,
        baseline_target=args.baseline_target,
        install_folder=args.install_folder,
        official_dll=args.official_dll,
        map_root=args.map_root,
        map_manifest=args.map_manifest,
        lock_file=args.lock_file,
        wine=args.wine,
        mod=args.mod,
        timeout_seconds=args.timeout_seconds,
        cleanup_timeout_seconds=args.cleanup_timeout_seconds,
        seed_base=args.seed_base,
    ).resolved()


def _run_command(args) -> int:
    config = _config_from_args(args)
    checked = preflight(config)
    if args.dry_run:
        path = dry_run(config, checked)
        print("dry-run manifest: %s" % path)
        print("30 games planned; no matches or inference were started; no results.json was created.")
        if checked.process_conflicts:
            print("warning: live run would be refused while these host processes exist: %s" % checked.process_conflicts)
        return 0
    return live_run(config, checked)


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (AcceptanceError, OSError) as exc:
        print("pluto-acceptance: error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
