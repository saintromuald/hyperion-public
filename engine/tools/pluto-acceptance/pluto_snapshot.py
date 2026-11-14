#!/usr/bin/env python3
"""Create, verify, restore, and plan forward-ports of Pluto known-good snapshots.

Only ``snapshot`` and ``restore`` create files, and each writes exclusively to a
new private destination.  ``verify`` and ``forward-port`` are read-only.  No
command builds Hyperion, launches a game, changes Git refs, or creates a tag.
The module uses the Python standard library exclusively.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import datetime as _datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import subprocess
import sys
import tempfile
import uuid

MANIFEST_SCHEMA = "hyperion-pluto-known-good-v1"
VERIFY_SCHEMA = "hyperion-pluto-known-good-verification-v1"
FORWARD_PORT_SCHEMA = "hyperion-pluto-known-good-forward-port-v1"
OG_CONFIRMATION = "Pluto AI is properly working"
RUNTIME_LABELS = ("engine", "sdl", "bridge", "official_dll", "inference", "weights")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
OID_RE = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})")
SNAPSHOT_ID_RE = re.compile(r"[0-9]{8}T[0-9]{6}Z-[0-9a-f]{12}")
FICLONE = 0x40049409  # Linux ioctl; unsupported filesystems safely fall back.
COPY_BLOCK_SIZE = 1024 * 1024


class SnapshotError(RuntimeError):
    """A snapshot integrity, input, or destination safety error."""


@dataclasses.dataclass(frozen=True)
class PlannedEntry:
    source: Path
    record: dict


def utc_now() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def _absolute(path: Path) -> Path:
    """Make a path absolute without dereferencing its final symlink."""
    return Path(os.path.abspath(os.path.expanduser(os.fspath(path))))


def _lexists(path: Path) -> bool:
    return os.path.lexists(os.fspath(path))


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except (OSError, ValueError):
        return False


def _no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key: %s" % key)
        result[key] = value
    return result


def _reject_json_constant(value):
    raise ValueError("non-finite JSON number: %s" % value)


def json_bytes(value) -> bytes:
    # ASCII output also represents unusual local symlink/filename surrogates
    # without ever encoding them directly as invalid UTF-8.
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True, allow_nan=False) + "\n").encode("utf-8")


def _read_json_bytes(data: bytes, description: str):
    try:
        return json.loads(
            data.decode("utf-8"),
            object_pairs_hook=_no_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeError, ValueError) as exc:
        raise SnapshotError("cannot read valid JSON %s: %s" % (description, exc)) from exc


def read_json(path: Path):
    try:
        st = path.lstat()
        if not stat.S_ISREG(st.st_mode):
            raise SnapshotError("JSON path is not a regular file: %s" % path)
        return _read_json_bytes(path.read_bytes(), str(path))
    except OSError as exc:
        raise SnapshotError("cannot read %s: %s" % (path, exc)) from exc


def _parse_json_object(value: str, label: str) -> dict:
    document = _read_json_bytes(value.encode("utf-8"), label)
    if not isinstance(document, dict):
        raise SnapshotError("%s must be a JSON object" % label)
    return document


def _fsync_directory(path: Path) -> None:
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    try:
        fd = os.open(path, flags)
    except OSError:
        return
    try:
        os.fsync(fd)
    except OSError:
        pass
    finally:
        os.close(fd)


def write_bytes_atomic_new(path: Path, data: bytes, mode: int = 0o600) -> None:
    """Atomically publish a generated file inside a new private directory.

    Callers create the parent exclusively.  The explicit existence check and
    private parent ensure this helper is never used to revise an old snapshot.
    """
    if _lexists(path):
        raise SnapshotError("refusing to overwrite existing file: %s" % path)
    fd, temporary = tempfile.mkstemp(prefix=".%s." % path.name, dir=str(path.parent))
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        if _lexists(path):
            raise SnapshotError("refusing to overwrite existing file: %s" % path)
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def _normalized_relative(value) -> str | None:
    if not isinstance(value, str) or not value or "\\" in value or "\x00" in value:
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return None
    normalized = path.as_posix()
    return normalized if normalized == value else None


def _private_mode(mode: int) -> int:
    # Preserve owner read/write/execute bits but never expose proprietary bytes
    # to group/other.  A read-only original remains read-only after restoration.
    return stat.S_IMODE(mode) & 0o700


def _hash_fd(fd: int) -> str:
    digest = hashlib.sha256()
    os.lseek(fd, 0, os.SEEK_SET)
    while True:
        block = os.read(fd, COPY_BLOCK_SIZE)
        if not block:
            break
        digest.update(block)
    return digest.hexdigest()


def hash_regular_file(path: Path) -> str:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise SnapshotError("not a regular file: %s" % path)
        return _hash_fd(fd)
    finally:
        os.close(fd)


def _symlink_record(path: Path) -> tuple[str, str, int]:
    target = os.readlink(path)
    raw = os.fsencode(target)
    return target, hashlib.sha256(raw).hexdigest(), len(raw)


def _inspect_entry(source: Path, destination: str, original_path: Path | None = None) -> PlannedEntry:
    source = _absolute(source)
    normalized = _normalized_relative(destination)
    if normalized is None:
        raise SnapshotError("unsafe internal snapshot path: %s" % destination)
    try:
        st = source.lstat()
    except OSError as exc:
        raise SnapshotError("cannot inspect supplied path %s: %s" % (source, exc)) from exc
    record = {
        "path": normalized,
        "original_path": str(original_path if original_path is not None else source),
        "mode": stat.S_IMODE(st.st_mode),
        "mtime_ns": st.st_mtime_ns,
    }
    if stat.S_ISLNK(st.st_mode):
        target, digest, size = _symlink_record(source)
        record.update(
            kind="symlink",
            target=target,
            sha256=digest,
            hash_subject="symlink-target-text",
            size=size,
        )
    elif stat.S_ISREG(st.st_mode):
        try:
            digest = hash_regular_file(source)
        except OSError as exc:
            raise SnapshotError("cannot hash supplied file %s: %s" % (source, exc)) from exc
        record.update(kind="file", sha256=digest, hash_subject="file-content", size=st.st_size)
    elif stat.S_ISDIR(st.st_mode):
        record.update(kind="directory")
    else:
        raise SnapshotError("only regular files, directories, and symlinks are supported: %s" % source)
    return PlannedEntry(source=source, record=record)


def _check_source_parents(root: Path, relative: str) -> None:
    current = root
    parts = PurePosixPath(relative).parts
    for part in parts[:-1]:
        current = current / part
        try:
            st = current.lstat()
        except OSError as exc:
            raise SnapshotError("cannot inspect source parent %s: %s" % (current, exc)) from exc
        if stat.S_ISLNK(st.st_mode) or not stat.S_ISDIR(st.st_mode):
            raise SnapshotError("source scope has a symlink/non-directory parent; name the link itself instead: %s" % current)


def _plan_evidence(path: Path, index: int) -> list[PlannedEntry]:
    source = _absolute(path)
    base = re.sub(r"[^A-Za-z0-9_.-]+", "_", source.name).strip(".") or "evidence"
    destination_root = "evidence/%03d-%s" % (index, base)
    entries: list[PlannedEntry] = []

    def walk(current: Path, destination: str, original_root: Path) -> None:
        planned = _inspect_entry(current, destination, original_path=original_root)
        entries.append(planned)
        if planned.record["kind"] != "directory":
            return
        try:
            children = sorted(os.scandir(current), key=lambda item: os.fsencode(item.name))
        except OSError as exc:
            raise SnapshotError("cannot enumerate evidence directory %s: %s" % (current, exc)) from exc
        for child in children:
            child_relative = _normalized_relative(child.name)
            if child_relative is None:
                raise SnapshotError("evidence filename is not a safe single path component: %r" % child.name)
            walk(Path(child.path), destination + "/" + child_relative, original_root)

    walk(source, destination_root, source)
    return entries


def _git(repo: Path, arguments: list[str], *, allow_failure: bool = False) -> str:
    environment = os.environ.copy()
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    command = ["git", "--no-optional-locks", "-C", str(repo)] + arguments
    try:
        completed = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise SnapshotError("cannot run read-only Git query in %s: %s" % (repo, exc)) from exc
    if completed.returncode != 0:
        if allow_failure:
            return ""
        detail = completed.stderr.strip() or completed.stdout.strip() or "exit %d" % completed.returncode
        raise SnapshotError("read-only Git query failed in %s: %s" % (repo, detail))
    return completed.stdout.strip()


def _has_git_metadata(repo: Path) -> bool:
    return _lexists(repo / ".git")


def _git_source_record(source: Path, scoped_paths: list[str], allow_missing: bool, repo_name: str | None) -> dict:
    result = {
        "original_path": str(source),
        "repository_root": str(source),
        "repo": repo_name or str(source),
        "branch": None,
        "commit": None,
        "tree": None,
        "git_metadata_present": False,
        "dirty": None,
    }
    if not _has_git_metadata(source):
        if not allow_missing:
            raise SnapshotError(
                "source has no .git metadata: %s; pass --allow-missing-source to preserve scoped files without commit/tree proof" % source
            )
        return result
    top = _git(source, ["rev-parse", "--show-toplevel"])
    if not top or Path(top).resolve() != source.resolve():
        raise SnapshotError("--source must be the Git worktree top level (Git reported %r)" % top)
    commit = _git(source, ["rev-parse", "HEAD"])
    tree = _git(source, ["rev-parse", "HEAD^{tree}"])
    branch = _git(source, ["symbolic-ref", "--short", "-q", "HEAD"], allow_failure=True) or "DETACHED"
    remote = _git(source, ["config", "--get", "remote.origin.url"], allow_failure=True)
    status = _git(source, ["status", "--porcelain=v1", "--untracked-files=normal", "--"] + scoped_paths)
    result.update(
        repo=repo_name or remote or str(source),
        branch=branch,
        commit=commit,
        tree=tree,
        git_metadata_present=True,
        dirty=bool(status),
    )
    return result


def _validate_oid(value) -> bool:
    return isinstance(value, str) and OID_RE.fullmatch(value) is not None


def _validate_entry(entry, prefix: str, label: str, errors: list[str]) -> None:
    if not isinstance(entry, dict):
        errors.append("%s is not an object" % label)
        return
    normalized = _normalized_relative(entry.get("path"))
    if normalized is None or not normalized.startswith(prefix):
        errors.append("%s has unsafe or out-of-section path: %r" % (label, entry.get("path")))
    kind = entry.get("kind")
    if kind not in ("file", "symlink", "directory"):
        errors.append("%s has unsupported kind: %r" % (label, kind))
        return
    if not isinstance(entry.get("original_path"), str) or not entry["original_path"]:
        errors.append("%s needs original_path" % label)
    if (
        not isinstance(entry.get("mode"), int)
        or isinstance(entry.get("mode"), bool)
        or not 0 <= entry["mode"] <= 0o7777
    ):
        errors.append("%s needs an integer mode in the permission-bit range" % label)
    if not isinstance(entry.get("mtime_ns"), int) or isinstance(entry.get("mtime_ns"), bool):
        errors.append("%s needs integer mtime_ns" % label)
    if kind == "directory":
        if prefix != "evidence/":
            errors.append("%s directory entries are only valid for evidence" % label)
        return
    size = entry.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        errors.append("%s needs non-negative integer size" % label)
    if not isinstance(entry.get("sha256"), str) or not SHA256_RE.fullmatch(entry["sha256"]):
        errors.append("%s needs lowercase SHA256" % label)
    if kind == "file":
        if entry.get("hash_subject") != "file-content" or "target" in entry:
            errors.append("%s has inconsistent regular-file hash metadata" % label)
    else:
        if entry.get("hash_subject") != "symlink-target-text":
            errors.append("%s has inconsistent symlink hash metadata" % label)
        if not isinstance(entry.get("target"), str) or not entry["target"] or "\x00" in entry["target"]:
            errors.append("%s needs a nonempty symlink target without NUL" % label)


def _manifest_entries(document: dict) -> list[tuple[str, dict]]:
    entries: list[tuple[str, dict]] = []
    source = document.get("source")
    if isinstance(source, dict) and isinstance(source.get("files"), list):
        entries.extend(("source", item) for item in source["files"])
    runtime = document.get("runtime")
    if isinstance(runtime, dict):
        for label, item in runtime.items():
            if isinstance(item, dict):
                entries.append(("runtime", item))
    bandit = document.get("bandit")
    if isinstance(bandit, dict):
        for side in ("before", "after"):
            if isinstance(bandit.get(side), dict):
                entries.append(("bandit", bandit[side]))
    evidence = document.get("evidence")
    if isinstance(evidence, dict) and isinstance(evidence.get("files"), list):
        entries.extend(("evidence", item) for item in evidence["files"])
    return entries


def _validate_path_collisions(document: dict, errors: list[str]) -> None:
    paths: dict[str, str] = {}
    for section, entry in _manifest_entries(document):
        if not isinstance(entry, dict):
            continue
        path = entry.get("path")
        if not isinstance(path, str):
            continue
        if path in paths:
            errors.append("duplicate snapshot path: %s" % path)
        paths[path] = entry.get("kind", "invalid")
    for path in paths:
        parts = PurePosixPath(path).parts
        for length in range(1, len(parts)):
            parent = PurePosixPath(*parts[:length]).as_posix()
            if parent in paths and paths[parent] != "directory":
                errors.append("snapshot path traverses a file/symlink: %s -> %s" % (parent, path))


def validate_manifest(document) -> list[str]:
    errors: list[str] = []
    if not isinstance(document, dict):
        return ["manifest root must be an object"]
    if document.get("schema") != MANIFEST_SCHEMA:
        errors.append("wrong manifest schema: %r" % document.get("schema"))
    if not isinstance(document.get("snapshot_id"), str) or not SNAPSHOT_ID_RE.fullmatch(document["snapshot_id"]):
        errors.append("snapshot_id has invalid immutable timestamp/id form")
    if not isinstance(document.get("created_utc"), str) or not document["created_utc"]:
        errors.append("created_utc is required")
    if document.get("local_only") is not True:
        errors.append("local_only must be true")
    content = document.get("content_address")
    if not isinstance(content, dict) or content.get("algorithm") != "sha256" or content.get("input") != "manifest.json bytes":
        errors.append("content_address must describe SHA256 of manifest.json bytes")

    designation = document.get("designation")
    if not isinstance(designation, dict) or designation.get("status") not in ("candidate", "user_confirmed_og"):
        errors.append("designation must distinguish candidate from user_confirmed_og")
    else:
        status = designation["status"]
        confirmed = designation.get("user_confirmed")
        confirmation = designation.get("confirmation")
        confirmed_utc = designation.get("confirmed_utc")
        if status == "candidate":
            if confirmed is not False or confirmation is not None or confirmed_utc is not None:
                errors.append("candidate cannot carry an OG confirmation")
        elif (
            confirmed is not True
            or not isinstance(confirmation, str)
            or OG_CONFIRMATION not in confirmation
            or not isinstance(confirmed_utc, str)
            or not confirmed_utc
        ):
            errors.append("user-confirmed OG lacks the explicit flag, required confirmation text, or timestamp")

    source = document.get("source")
    if not isinstance(source, dict):
        errors.append("source object is required")
    else:
        for key in ("original_path", "repository_root", "repo"):
            if not isinstance(source.get(key), str) or not source[key]:
                errors.append("source.%s is required" % key)
        if not isinstance(source.get("git_metadata_present"), bool):
            errors.append("source.git_metadata_present must be boolean")
        if source.get("branch") is not None and not isinstance(source.get("branch"), str):
            errors.append("source.branch must be text or null")
        for key in ("commit", "tree"):
            value = source.get(key)
            if value is not None and not _validate_oid(value):
                errors.append("source.%s must be a full Git object ID or null" % key)
        if source.get("git_metadata_present") is True and (not _validate_oid(source.get("commit")) or not _validate_oid(source.get("tree"))):
            errors.append("Git-backed source needs full commit and tree IDs")
        if source.get("git_metadata_present") is True and not isinstance(source.get("dirty"), bool):
            errors.append("Git-backed source needs a boolean scoped dirty status")
        if source.get("git_metadata_present") is False and any(
            source.get(key) is not None for key in ("branch", "commit", "tree", "dirty")
        ):
            errors.append("non-Git source cannot claim branch/commit/tree/dirty provenance")
        if source.get("dirty") is not None and not isinstance(source.get("dirty"), bool):
            errors.append("source.dirty must be boolean or null")
        files = source.get("files")
        if not isinstance(files, list) or not files:
            errors.append("source.files must contain at least one scoped file")
        else:
            for index, entry in enumerate(files):
                _validate_entry(entry, "source/", "source.files[%d]" % index, errors)

    runtime = document.get("runtime")
    if not isinstance(runtime, dict):
        errors.append("runtime object is required")
    else:
        for label in RUNTIME_LABELS:
            if label not in runtime:
                errors.append("runtime.%s is required" % label)
            else:
                _validate_entry(runtime[label], "runtime/", "runtime.%s" % label, errors)
                if isinstance(runtime[label], dict) and runtime[label].get("path") != "runtime/" + label:
                    errors.append("runtime.%s has a noncanonical path" % label)
        for label in runtime:
            if label not in RUNTIME_LABELS:
                errors.append("unexpected runtime label: %s" % label)

    bandit = document.get("bandit")
    if not isinstance(bandit, dict):
        errors.append("bandit object is required")
    else:
        for side in ("before", "after"):
            if side not in bandit:
                errors.append("bandit.%s is required" % side)
            else:
                _validate_entry(bandit[side], "bandit/", "bandit.%s" % side, errors)
                if isinstance(bandit[side], dict) and bandit[side].get("path") != "bandit/" + side:
                    errors.append("bandit.%s has a noncanonical path" % side)
        if not isinstance(bandit.get("changed"), bool):
            errors.append("bandit.changed must be boolean")
        elif (
            isinstance(bandit.get("before"), dict)
            and isinstance(bandit.get("after"), dict)
            and isinstance(bandit["before"].get("sha256"), str)
            and isinstance(bandit["after"].get("sha256"), str)
            and bandit["changed"] != (bandit["before"]["sha256"] != bandit["after"]["sha256"])
        ):
            errors.append("bandit.changed does not match before/after SHA256 records")

    provenance = document.get("provenance")
    if not isinstance(provenance, dict):
        errors.append("provenance object is required")
    else:
        for key in ("build", "toolchain", "wine", "bwapi", "shim"):
            if key not in provenance:
                errors.append("provenance.%s must be present (null when not supplied)" % key)
        for key in ("toolchain", "wine", "bwapi", "shim"):
            if provenance.get(key) is not None and not isinstance(provenance.get(key), str):
                errors.append("provenance.%s must be text or null" % key)

    protocol = document.get("protocol")
    if not isinstance(protocol, dict) or not isinstance(protocol.get("name"), str) or not protocol.get("name") or not isinstance(protocol.get("timing"), dict):
        errors.append("protocol needs a nonempty name and timing object")
    evaluation = document.get("evaluation")
    if not isinstance(evaluation, dict):
        errors.append("evaluation object is required")
    else:
        maps = evaluation.get("maps")
        if not isinstance(maps, list) or not maps or any(not isinstance(item, str) or not item for item in maps):
            errors.append("evaluation.maps must be a nonempty list of names/paths")
        if not isinstance(evaluation.get("settings"), dict):
            errors.append("evaluation.settings must be an object")
        seeds = evaluation.get("seeds")
        if not isinstance(seeds, list) or not seeds or any(not isinstance(item, int) or isinstance(item, bool) or item < 0 for item in seeds):
            errors.append("evaluation.seeds must be a nonempty list of non-negative integers")

    evidence = document.get("evidence")
    if not isinstance(evidence, dict) or not isinstance(evidence.get("supplied_paths"), list) or not isinstance(evidence.get("files"), list):
        errors.append("evidence needs supplied_paths and files lists")
    else:
        for index, original in enumerate(evidence["supplied_paths"]):
            if not isinstance(original, str) or not original:
                errors.append("evidence.supplied_paths[%d] must be text" % index)
        for index, entry in enumerate(evidence["files"]):
            _validate_entry(entry, "evidence/", "evidence.files[%d]" % index, errors)
    limitations = document.get("limitations")
    if not isinstance(limitations, list) or not limitations or any(not isinstance(item, str) or not item for item in limitations):
        errors.append("limitations must be a nonempty list of explicit statements")
    if not isinstance(document.get("notes"), str):
        errors.append("notes must be text (possibly empty)")
    _validate_path_collisions(document, errors)
    return errors


def _read_manifest(snapshot: Path) -> tuple[dict, bytes]:
    manifest_path = snapshot / "manifest.json"
    try:
        st = manifest_path.lstat()
        if not stat.S_ISREG(st.st_mode):
            raise SnapshotError("snapshot manifest is not a regular file: %s" % manifest_path)
        data = manifest_path.read_bytes()
    except OSError as exc:
        raise SnapshotError("cannot read snapshot manifest %s: %s" % (manifest_path, exc)) from exc
    document = _read_json_bytes(data, str(manifest_path))
    if not isinstance(document, dict):
        raise SnapshotError("snapshot manifest root must be an object: %s" % manifest_path)
    return document, data


def _content_address_error(snapshot: Path, document: dict, manifest_bytes: bytes) -> str | None:
    # Recognize the directory shape independently of the potentially tampered manifest.
    # A restored directory may have an arbitrary new name, but a content-shaped name
    # must prove every part of that address rather than opting out on an ID mismatch.
    match = re.fullmatch(
        r"(candidate|og)-([0-9]{8}T[0-9]{6}Z-[0-9a-f]{12})-([0-9a-f]{64})",
        snapshot.name,
    )
    if match is None:
        if snapshot.name.startswith(("candidate-", "og-")):
            return "malformed content-addressed snapshot directory name"
        return None
    snapshot_id = document.get("snapshot_id")
    if not isinstance(snapshot_id, str) or match.group(2) != snapshot_id:
        return "snapshot ID does not match immutable directory name"
    expected_prefix = "og" if document.get("designation", {}).get("status") == "user_confirmed_og" else "candidate"
    if match.group(1) != expected_prefix:
        return "snapshot directory prefix does not match its designation"
    actual = hashlib.sha256(manifest_bytes).hexdigest()
    if match.group(3) != actual:
        return "manifest content hash does not match immutable directory name: got %s, expected %s" % (actual, match.group(3))
    return None


def _scan_layout(snapshot: Path) -> tuple[dict[str, str], list[str]]:
    actual: dict[str, str] = {}
    errors: list[str] = []

    def walk(directory: Path, prefix: str = "") -> None:
        try:
            children = sorted(os.scandir(directory), key=lambda item: os.fsencode(item.name))
        except OSError as exc:
            errors.append("cannot enumerate snapshot directory %s: %s" % (directory, exc))
            return
        for child in children:
            relative = prefix + child.name
            if relative == "manifest.json":
                continue
            path = Path(child.path)
            try:
                st = path.lstat()
            except OSError as exc:
                errors.append("cannot inspect snapshot path %s: %s" % (path, exc))
                continue
            if relative == "RECOVERY.md" and stat.S_ISREG(st.st_mode):
                continue
            if stat.S_ISLNK(st.st_mode):
                actual[relative] = "symlink"
            elif stat.S_ISREG(st.st_mode):
                actual[relative] = "file"
            elif stat.S_ISDIR(st.st_mode):
                actual[relative] = "directory"
                walk(path, relative + "/")
            else:
                actual[relative] = "special"
                errors.append("special file in snapshot layout: %s" % relative)

    walk(snapshot)
    return actual, errors


def _expected_layout(document: dict) -> dict[str, str]:
    expected: dict[str, str] = {}
    for _, entry in _manifest_entries(document):
        path = entry["path"]
        expected[path] = entry["kind"]
        parts = PurePosixPath(path).parts
        for length in range(1, len(parts)):
            parent = PurePosixPath(*parts[:length]).as_posix()
            expected.setdefault(parent, "directory")
    return expected


def _verify_payload(snapshot: Path, document: dict) -> tuple[list[str], dict]:
    errors: list[str] = []
    actual, scan_errors = _scan_layout(snapshot)
    errors.extend(scan_errors)
    expected = _expected_layout(document)
    for path in sorted(set(expected) - set(actual)):
        errors.append("missing snapshot layout path: %s" % path)
    for path in sorted(set(actual) - set(expected)):
        errors.append("unexpected snapshot layout path: %s" % path)
    for path in sorted(set(actual) & set(expected)):
        if actual[path] != expected[path]:
            errors.append("snapshot kind mismatch for %s: got %s, expected %s" % (path, actual[path], expected[path]))
    checked = 0
    for section, entry in _manifest_entries(document):
        if entry["kind"] == "directory":
            continue
        path = snapshot / entry["path"]
        try:
            if entry["kind"] == "symlink":
                if not path.is_symlink():
                    errors.append("required symlink is not a link: %s" % entry["path"])
                    continue
                target, digest, size = _symlink_record(path)
                if target != entry["target"]:
                    errors.append("symlink target mismatch for %s: got %r, expected %r" % (entry["path"], target, entry["target"]))
            else:
                if not path.is_file() or path.is_symlink():
                    errors.append("required regular file is missing or is a link: %s" % entry["path"])
                    continue
                digest = hash_regular_file(path)
                size = path.stat().st_size
            if digest != entry["sha256"]:
                errors.append("SHA256 mismatch for %s (%s): got %s, expected %s" % (section, entry["path"], digest, entry["sha256"]))
            if size != entry["size"]:
                errors.append("size mismatch for %s: got %d, expected %d" % (entry["path"], size, entry["size"]))
            checked += 1
        except OSError as exc:
            errors.append("cannot verify %s: %s" % (entry["path"], exc))
    return errors, {"hashed_entries_checked": checked, "expected_layout_entries": len(expected), "actual_layout_entries": len(actual)}


def _verify_source_repository(source: Path, record: dict, allow_missing: bool) -> tuple[list[str], list[str], dict]:
    errors: list[str] = []
    warnings: list[str] = []
    checks = {"path": str(source), "available": False, "commit_tree_checked": False}
    if not source.is_dir():
        warnings.append("original source repository is unavailable; copied source was verified, but commit/tree could not be rechecked: %s" % source)
        return errors, warnings, checks
    checks["available"] = True
    if not _has_git_metadata(source):
        message = "source directory has no Git metadata; commit/tree were not rechecked: %s" % source
        if allow_missing:
            warnings.append(message)
            return errors, warnings, checks
        errors.append(message + "; pass --allow-missing-source if this is intentional")
        return errors, warnings, checks
    if record.get("git_metadata_present") is not True:
        warnings.append("source now has Git metadata, but the snapshot was explicitly created without commit/tree provenance")
        return errors, warnings, checks
    try:
        top = _git(source, ["rev-parse", "--show-toplevel"])
        commit = _git(source, ["rev-parse", "HEAD"])
        tree = _git(source, ["rev-parse", "HEAD^{tree}"])
    except SnapshotError as exc:
        errors.append(str(exc))
        return errors, warnings, checks
    if not top or Path(top).resolve() != source.resolve():
        errors.append("source is not the Git worktree top level recorded by the snapshot")
    if commit != record.get("commit"):
        errors.append("source commit mismatch: got %s, expected %s" % (commit, record.get("commit")))
    if tree != record.get("tree"):
        errors.append("source tree mismatch: got %s, expected %s" % (tree, record.get("tree")))
    checks.update(commit_tree_checked=True, actual_commit=commit, actual_tree=tree)
    return errors, warnings, checks


def verify_snapshot(
    snapshot: Path,
    allow_missing_source: bool = False,
    source: Path | None = None,
    *,
    check_source: bool = True,
) -> dict:
    snapshot = _absolute(snapshot)
    result = {
        "schema": VERIFY_SCHEMA,
        "snapshot": str(snapshot),
        "valid": False,
        "designation": None,
        "errors": [],
        "warnings": [],
        "checks": {},
    }
    try:
        document, manifest_bytes = _read_manifest(snapshot)
    except SnapshotError as exc:
        result["errors"].append(str(exc))
        return result
    result["manifest_sha256"] = hashlib.sha256(manifest_bytes).hexdigest()
    result["designation"] = document.get("designation", {}).get("status")
    schema_errors = validate_manifest(document)
    result["errors"].extend(schema_errors)
    if schema_errors:
        return result
    address_error = _content_address_error(snapshot, document, manifest_bytes)
    if address_error:
        result["errors"].append(address_error)
    result["checks"]["content_address_checked"] = address_error is None and re.fullmatch(
        r"(candidate|og)-" + re.escape(document["snapshot_id"]) + r"-[0-9a-f]{64}", snapshot.name
    ) is not None
    payload_errors, payload_checks = _verify_payload(snapshot, document)
    result["errors"].extend(payload_errors)
    result["checks"].update(payload_checks)
    if check_source:
        source_record = document["source"]
        source_path = _absolute(source) if source is not None else Path(source_record["repository_root"])
        source_errors, source_warnings, source_checks = _verify_source_repository(source_path, source_record, allow_missing_source)
        result["errors"].extend(source_errors)
        result["warnings"].extend(source_warnings)
        result["checks"]["source"] = source_checks
    else:
        result["checks"]["source"] = {"commit_tree_checked": False, "reason": "not requested for restore/forward-port payload verification"}
    if _is_temporary_path(snapshot.parent):
        result["warnings"].append("snapshot is under a temporary directory; retain an independent durable local backup")
    result["valid"] = not result["errors"]
    return result


def _require_valid_payload(snapshot: Path) -> tuple[dict, bytes, str]:
    document, manifest_bytes = _read_manifest(snapshot)
    result = verify_snapshot(snapshot, check_source=False)
    if not result["valid"]:
        raise SnapshotError("snapshot verification failed: " + "; ".join(result["errors"]))
    return document, manifest_bytes, result["manifest_sha256"]


def _copy_regular(source: Path, destination: Path, expected_sha256: str, mode: int) -> str:
    source_flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    source_fd = os.open(source, source_flags)
    try:
        if not stat.S_ISREG(os.fstat(source_fd).st_mode):
            raise SnapshotError("supplied source is no longer a regular file: %s" % source)
        destination_fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            method = "copy"
            try:
                fcntl.ioctl(destination_fd, FICLONE, source_fd)
                method = "reflink"
            except OSError:
                os.lseek(source_fd, 0, os.SEEK_SET)
                os.lseek(destination_fd, 0, os.SEEK_SET)
                os.ftruncate(destination_fd, 0)
                while True:
                    block = os.read(source_fd, COPY_BLOCK_SIZE)
                    if not block:
                        break
                    view = memoryview(block)
                    while view:
                        written = os.write(destination_fd, view)
                        if written == 0:
                            raise OSError("short write while copying %s" % source)
                        view = view[written:]
            os.fchmod(destination_fd, _private_mode(mode))
            os.fsync(destination_fd)
        finally:
            os.close(destination_fd)
        source_hash = _hash_fd(source_fd)
        if source_hash != expected_sha256:
            raise SnapshotError("original changed while copying: %s (expected SHA256 %s, got %s)" % (source, expected_sha256, source_hash))
        destination_hash = hash_regular_file(destination)
        if destination_hash != expected_sha256:
            raise SnapshotError("copied SHA256 mismatch for %s: got %s, expected %s" % (destination, destination_hash, expected_sha256))
        return method
    finally:
        os.close(source_fd)


def _materialize_entry(entry: PlannedEntry | dict, snapshot_root: Path, *, source: Path | None = None) -> str:
    record = entry.record if isinstance(entry, PlannedEntry) else entry
    source_path = entry.source if isinstance(entry, PlannedEntry) else source
    if source_path is None:
        raise SnapshotError("internal error: no source for %s" % record["path"])
    destination = snapshot_root / record["path"]
    destination.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    kind = record["kind"]
    if kind == "directory":
        destination.mkdir(mode=0o700, exist_ok=True)
        return "directory"
    if kind == "symlink":
        current_target = os.readlink(source_path)
        if current_target != record["target"]:
            raise SnapshotError("original symlink changed while copying: %s" % source_path)
        os.symlink(record["target"], destination)
        _, copied_hash, copied_size = _symlink_record(destination)
        if copied_hash != record["sha256"] or copied_size != record["size"]:
            raise SnapshotError("copied symlink hash mismatch: %s" % destination)
        return "symlink"
    return _copy_regular(source_path, destination, record["sha256"], record["mode"])


def _is_temporary_path(path: Path) -> bool:
    resolved = path.resolve()
    return any(_is_within(resolved, Path(item)) for item in ("/tmp", "/var/tmp"))


def _input_in_destination(path: Path, root: Path) -> bool:
    absolute = _absolute(path)
    return _is_within(absolute, root) or _is_within(absolute.resolve(), root)


def _new_snapshot_name(document: dict, payload: bytes) -> tuple[str, str]:
    prefix = "og" if document["designation"]["status"] == "user_confirmed_og" else "candidate"
    digest = hashlib.sha256(payload).hexdigest()
    return "%s-%s-%s" % (prefix, document["snapshot_id"], digest), digest


def create_snapshot(args) -> dict:
    source = _absolute(args.source).resolve()
    root = _absolute(args.root).resolve()
    if not source.is_dir():
        raise SnapshotError("source directory does not exist: %s" % source)
    if _input_in_destination(source, root):
        raise SnapshotError("source directory cannot be inside the snapshot destination root")
    scoped_paths = []
    for value in args.source_file:
        normalized = _normalized_relative(value)
        if normalized is None:
            raise SnapshotError("source scope must be a normalized relative file path: %r" % value)
        if normalized in scoped_paths:
            raise SnapshotError("duplicate source scope: %s" % normalized)
        scoped_paths.append(normalized)
    if not scoped_paths:
        raise SnapshotError("at least one --source-file is required")
    if args.seed is None or any(seed < 0 for seed in args.seed):
        raise SnapshotError("at least one non-negative --seed is required")
    confirmed = bool(args.user_confirmed)
    if confirmed:
        if not isinstance(args.confirmation, str) or OG_CONFIRMATION not in args.confirmation:
            raise SnapshotError("--user-confirmed requires --confirmation text containing %r" % OG_CONFIRMATION)
    elif args.confirmation is not None:
        raise SnapshotError("--confirmation is valid only together with --user-confirmed")

    source_record = _git_source_record(source, scoped_paths, args.allow_missing_source, args.source_repo)
    source_entries: list[PlannedEntry] = []
    for relative in scoped_paths:
        supplied = source / relative
        if _input_in_destination(supplied, root):
            raise SnapshotError("scoped source cannot be inside the snapshot destination root: %s" % supplied)
        _check_source_parents(source, relative)
        planned = _inspect_entry(supplied, "source/" + relative)
        if planned.record["kind"] == "directory":
            raise SnapshotError("--source-file names a file or symlink, not a directory: %s" % supplied)
        source_entries.append(planned)
    source_record["files"] = [entry.record for entry in source_entries]

    runtime_entries: dict[str, PlannedEntry] = {}
    for label in RUNTIME_LABELS:
        supplied = _absolute(getattr(args, label))
        if _input_in_destination(supplied, root):
            raise SnapshotError("runtime input cannot be inside the snapshot destination root: %s" % supplied)
        planned = _inspect_entry(supplied, "runtime/" + label)
        if planned.record["kind"] == "directory":
            raise SnapshotError("runtime input must be a file or symlink: %s" % supplied)
        runtime_entries[label] = planned

    bandit_entries: dict[str, PlannedEntry] = {}
    for side in ("before", "after"):
        supplied = _absolute(getattr(args, "bandit_" + side))
        if _input_in_destination(supplied, root):
            raise SnapshotError("bandit input cannot be inside the snapshot destination root: %s" % supplied)
        planned = _inspect_entry(supplied, "bandit/" + side)
        if planned.record["kind"] == "directory":
            raise SnapshotError("bandit input must be a file or symlink: %s" % supplied)
        bandit_entries[side] = planned

    evidence_entries: list[PlannedEntry] = []
    for index, supplied in enumerate(args.evidence or [], 1):
        supplied = _absolute(supplied)
        if _input_in_destination(supplied, root):
            raise SnapshotError("evidence input cannot be inside the snapshot destination root: %s" % supplied)
        evidence_entries.extend(_plan_evidence(supplied, index))
    all_entries = source_entries + list(runtime_entries.values()) + list(bandit_entries.values()) + evidence_entries
    seen_internal_paths = set()
    for entry in all_entries:
        path = entry.record["path"]
        if path in seen_internal_paths:
            raise SnapshotError("duplicate internal snapshot path: %s" % path)
        seen_internal_paths.add(path)

    timing = _parse_json_object(args.timing, "--timing")
    settings = _parse_json_object(args.settings, "--settings")
    build = _parse_json_object(args.build_provenance, "--build-provenance") if args.build_provenance is not None else None
    created = utc_now()
    compact = created.replace("-", "").replace(":", "")
    compact = re.sub(r"\..*Z$", "Z", compact)
    snapshot_id = "%s-%s" % (compact, uuid.uuid4().hex[:12])
    if not SNAPSHOT_ID_RE.fullmatch(snapshot_id):
        raise SnapshotError("could not construct immutable timestamp/id from clock: %s" % created)
    status = "user_confirmed_og" if confirmed else "candidate"
    limitations = list(args.limitation or [])
    limitations.append("A snapshot preserves supplied bytes and attestations; it does not itself prove runtime compatibility or acceptance.")
    if not evidence_entries:
        limitations.append("No evidence files were supplied with this snapshot.")
    limitations.append("Symlink targets are preserved as text; external targets are not implicitly copied.")
    limitations = list(dict.fromkeys(limitations))
    document = {
        "schema": MANIFEST_SCHEMA,
        "snapshot_id": snapshot_id,
        "created_utc": created,
        "local_only": True,
        "content_address": {
            "algorithm": "sha256",
            "input": "manifest.json bytes",
            "naming": "STATUS-SNAPSHOT_ID-FULL_MANIFEST_SHA256",
        },
        "designation": {
            "status": status,
            "user_confirmed": confirmed,
            "confirmation": args.confirmation if confirmed else None,
            "confirmed_utc": created if confirmed else None,
        },
        "source": source_record,
        "runtime": {label: runtime_entries[label].record for label in RUNTIME_LABELS},
        "bandit": {
            "before": bandit_entries["before"].record,
            "after": bandit_entries["after"].record,
            "changed": bandit_entries["before"].record["sha256"] != bandit_entries["after"].record["sha256"],
        },
        "provenance": {
            "build": build,
            "toolchain": args.toolchain,
            "wine": args.wine,
            "bwapi": args.bwapi,
            "shim": args.shim,
        },
        "protocol": {"name": args.protocol, "timing": timing},
        "evaluation": {"maps": args.maps, "settings": settings, "seeds": args.seed},
        "evidence": {
            "supplied_paths": [str(_absolute(path)) for path in (args.evidence or [])],
            "files": [entry.record for entry in evidence_entries],
        },
        "limitations": limitations,
        "notes": args.notes,
    }
    schema_errors = validate_manifest(document)
    if schema_errors:
        raise SnapshotError("internal manifest validation failed: " + "; ".join(schema_errors))
    payload = json_bytes(document)
    name, manifest_digest = _new_snapshot_name(document, payload)
    try:
        root.mkdir(parents=True, mode=0o700, exist_ok=True)
    except OSError as exc:
        raise SnapshotError("cannot create snapshot root %s: %s" % (root, exc)) from exc
    destination = root / name
    if _lexists(destination):
        raise SnapshotError("immutable snapshot already exists; refusing to modify or replace it: %s" % destination)
    try:
        destination.mkdir(mode=0o700)
    except FileExistsError as exc:
        raise SnapshotError("immutable snapshot already exists; refusing to modify or replace it: %s" % destination) from exc
    except OSError as exc:
        raise SnapshotError("cannot create new private snapshot %s: %s" % (destination, exc)) from exc

    # All validation and original hashes precede destination creation.  If an
    # I/O failure occurs now, the incomplete new directory is left private and
    # manifest-less for diagnosis; no old snapshot is altered or removed.
    methods = {"reflink": 0, "copy": 0, "symlink": 0, "directory": 0}
    for entry in all_entries:
        method = _materialize_entry(entry, destination)
        methods[method] += 1
    write_bytes_atomic_new(destination / "manifest.json", payload)
    _fsync_directory(destination)
    verification = verify_snapshot(destination, args.allow_missing_source, check_source=False)
    if not verification["valid"]:
        raise SnapshotError("new snapshot failed post-copy verification: " + "; ".join(verification["errors"]))
    warnings = []
    if _is_temporary_path(destination):
        warnings.append("destination is temporary; do not treat it as the only durable copy")
    return {
        "schema": MANIFEST_SCHEMA,
        "snapshot": str(destination),
        "manifest": str(destination / "manifest.json"),
        "manifest_sha256": manifest_digest,
        "designation": status,
        "source_files": len(source_entries),
        "runtime_files": len(runtime_entries),
        "evidence_entries": len(evidence_entries),
        "copy_methods": methods,
        "warnings": warnings,
        "proprietary_files_uploaded": False,
        "original_paths_modified": False,
    }


def _recovery_notes(document: dict, original_snapshot: Path, manifest_sha256: str) -> bytes:
    source = document["source"]
    designation = document["designation"]
    lines = [
        "# Pluto known-good recovery notes",
        "",
        "This directory was materialized as a NEW private recovery directory. No worktree was reset or overwritten.",
        "",
        "- Original snapshot: `%s`" % original_snapshot,
        "- Manifest SHA256: `%s`" % manifest_sha256,
        "- Designation: `%s`" % designation["status"],
        "- Source repository: `%s`" % source["repo"],
        "- Source branch: `%s`" % source["branch"],
        "- Source commit: `%s`" % source["commit"],
        "- Source tree: `%s`" % source["tree"],
        "",
        "Verify this restored payload with `pluto_snapshot.py verify --snapshot PATH --allow-missing-source` when the original Git metadata is intentionally unavailable.",
        "The source payload is under `source/`; runtime, bandit, and evidence payloads retain their manifest paths.",
        "Symlinks remain symlinks. Their external targets were not implicitly copied and may need separate local recovery.",
        "Proprietary binaries, weights, maps, logs, and other supplied evidence remain local. Do not upload them.",
        "No Git tag was created. This restore does not prove current Pluto is OG, compatible, or accepted.",
        "Run the relevant build, synthetic tests, and separate acceptance protocol before making any compatibility or performance claim.",
        "",
        "## Supplied notes",
        "",
        document["notes"] or "(none)",
        "",
        "## Recorded limitations",
        "",
    ]
    lines.extend("- " + item for item in document["limitations"])
    lines.extend(["", "## User confirmation", "", designation["confirmation"] or "(candidate; no user-confirmed OG attestation)", ""])
    return "\n".join(lines).encode("utf-8")


def restore_snapshot(snapshot: Path, destination: Path) -> dict:
    snapshot = _absolute(snapshot)
    destination_input = _absolute(destination)
    if _lexists(destination_input):
        raise SnapshotError("restore destination must be entirely new; refusing existing path: %s" % destination_input)
    if _is_within(destination_input, snapshot):
        raise SnapshotError("restore destination cannot be inside the immutable snapshot")
    document, manifest_bytes, manifest_digest = _require_valid_payload(snapshot)
    destination = destination_input.resolve()
    if _lexists(destination):
        raise SnapshotError("restore destination must be entirely new; refusing existing path: %s" % destination)
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.mkdir(mode=0o700)
    except FileExistsError as exc:
        raise SnapshotError("restore destination already exists: %s" % destination) from exc
    except OSError as exc:
        raise SnapshotError("cannot create restore destination %s: %s" % (destination, exc)) from exc
    entries = [entry for _, entry in _manifest_entries(document)]
    for entry in entries:
        _materialize_entry(entry, destination, source=snapshot / entry["path"])
    _copy_regular(snapshot / "manifest.json", destination / "manifest.json", manifest_digest, 0o600)
    write_bytes_atomic_new(destination / "RECOVERY.md", _recovery_notes(document, snapshot, manifest_digest))
    verification = verify_snapshot(destination, allow_missing_source=True, check_source=False)
    if not verification["valid"]:
        raise SnapshotError("restored payload failed verification: " + "; ".join(verification["errors"]))
    return {
        "snapshot": str(snapshot),
        "destination": str(destination),
        "manifest_sha256": manifest_digest,
        "designation": document["designation"]["status"],
        "recovery_notes": str(destination / "RECOVERY.md"),
        "verified": True,
        "worktree_reset": False,
    }


def _current_source_status(repo: Path, entry: dict) -> tuple[str, dict]:
    relative = entry["path"][len("source/"):]
    current = repo / relative
    record = {"path": relative, "expected_kind": entry["kind"], "expected_sha256": entry["sha256"]}
    # Do not follow a worktree symlinked parent to a path outside the repo.
    parent = repo
    for part in PurePosixPath(relative).parts[:-1]:
        parent = parent / part
        try:
            parent_st = parent.lstat()
        except FileNotFoundError:
            return "missing", {**record, "actual_kind": None}
        except OSError as exc:
            return "conflicting", {**record, "error": str(exc)}
        if stat.S_ISLNK(parent_st.st_mode) or not stat.S_ISDIR(parent_st.st_mode):
            return "conflicting", {
                **record,
                "actual_kind": "symlink-parent" if stat.S_ISLNK(parent_st.st_mode) else "non-directory-parent",
                "parent": parent.relative_to(repo).as_posix(),
            }
    try:
        st = current.lstat()
    except FileNotFoundError:
        return "missing", {**record, "actual_kind": None}
    except OSError as exc:
        return "conflicting", {**record, "error": str(exc)}
    if entry["kind"] == "symlink":
        if not stat.S_ISLNK(st.st_mode):
            return "conflicting", {**record, "actual_kind": "file" if stat.S_ISREG(st.st_mode) else "other"}
        target, digest, _ = _symlink_record(current)
        record.update(actual_kind="symlink", actual_sha256=digest, expected_target=entry["target"], actual_target=target)
        return ("unchanged" if digest == entry["sha256"] else "changed"), record
    if not stat.S_ISREG(st.st_mode):
        return "conflicting", {**record, "actual_kind": "symlink" if stat.S_ISLNK(st.st_mode) else "other"}
    try:
        digest = hash_regular_file(current)
    except OSError as exc:
        return "conflicting", {**record, "error": str(exc)}
    record.update(actual_kind="file", actual_sha256=digest)
    return ("unchanged" if digest == entry["sha256"] else "changed"), record


def forward_port(snapshot: Path, repo: Path, base_commit: str) -> dict:
    snapshot = _absolute(snapshot)
    repo = _absolute(repo).resolve()
    document, _, manifest_digest = _require_valid_payload(snapshot)
    if document["designation"]["status"] != "user_confirmed_og":
        raise SnapshotError("forward-port requires a user-confirmed OG snapshot; a candidate is not OG")
    if not repo.is_dir() or not _has_git_metadata(repo):
        raise SnapshotError("--repo must be an existing Git worktree with .git metadata: %s" % repo)
    top = _git(repo, ["rev-parse", "--show-toplevel"])
    if not top or Path(top).resolve() != repo:
        raise SnapshotError("--repo must be the Git worktree top level (Git reported %r)" % top)
    resolved_base = _git(repo, ["rev-parse", "--verify", "--end-of-options", base_commit + "^{commit}"])
    if not _validate_oid(resolved_base):
        raise SnapshotError("base commit did not resolve to a full commit ID: %s" % base_commit)
    groups = {"unchanged": [], "changed": [], "missing": [], "conflicting": []}
    for entry in document["source"]["files"]:
        status, record = _current_source_status(repo, entry)
        groups[status].append(record)
    scoped = [entry["path"][len("source/"):] for entry in document["source"]["files"]]
    dirty = _git(repo, ["status", "--porcelain=v1", "--untracked-files=normal", "--"] + scoped)
    base_diff = _git(repo, ["diff", "--name-status", resolved_base, "--"] + scoped)
    current_head = _git(repo, ["rev-parse", "HEAD"])
    tool = str(_absolute(Path(__file__)))
    new_worktree = "/path/to/NEW-WORKTREE"
    new_restore = "/path/to/NEW-RESTORE"
    commands = [
        "git -C %s worktree add %s %s" % (shlex.quote(str(repo)), shlex.quote(new_worktree), resolved_base),
        "python3 %s restore --snapshot %s --destination %s" % (shlex.quote(tool), shlex.quote(str(snapshot)), shlex.quote(new_restore)),
        "# Inspect the plan and restored source; only then, in the NEW worktree, a user may copy reviewed files:",
        "rsync -a -- %s/ %s/" % (shlex.quote(new_restore + "/source"), shlex.quote(new_worktree)),
        "git -C %s diff --check" % shlex.quote(new_worktree),
        "git -C %s diff -- %s" % (shlex.quote(new_worktree), " ".join(shlex.quote(path) for path in scoped)),
        "# Run the relevant build and tests in the NEW worktree; compatibility is not proven by this plan.",
    ]
    warnings = []
    if document["source"].get("commit") and document["source"]["commit"] != resolved_base:
        warnings.append("explicit base commit differs from the OG source commit; review the requested base deliberately")
    if current_head != resolved_base:
        warnings.append("current HEAD differs from the requested base; the worktree-add command intentionally starts a NEW worktree at the base")
    needs_review = any(groups[name] for name in ("changed", "missing", "conflicting"))
    return {
        "schema": FORWARD_PORT_SCHEMA,
        "snapshot": str(snapshot),
        "manifest_sha256": manifest_digest,
        "repo": str(repo),
        "base_commit": resolved_base,
        "current_head": current_head,
        "og_source_commit": document["source"].get("commit"),
        "status": groups,
        "needs_review": needs_review,
        "dirty_scoped_status": dirty.splitlines() if dirty else [],
        "base_scoped_diff": base_diff.splitlines() if base_diff else [],
        "warnings": warnings,
        "commands_for_new_worktree": commands,
        "repository_modified": False,
        "mutations_performed": False,
        "tests_run": False,
        "compatibility_proven": False,
        "note": "Planning and byte comparison only. No cherry-pick, merge, checkout, worktree creation, build, test, or source edit was performed.",
    }


def _path(value: str) -> Path:
    return Path(value).expanduser()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    snapshot = sub.add_parser("snapshot", help="create a NEW private content-addressed local snapshot")
    snapshot.add_argument("--root", type=_path, required=True, help="caller-provided durable local snapshot root")
    snapshot.add_argument("--source", type=_path, required=True, help="source worktree top level or explicitly non-Git source directory")
    snapshot.add_argument("--source-file", action="append", required=True, metavar="REL", help="scoped source file/symlink; repeat for every path")
    snapshot.add_argument("--source-repo", help="repository name/URL when supplied explicitly (otherwise origin or local path)")
    snapshot.add_argument("--allow-missing-source", action="store_true", help="allow source directories without .git commit/tree metadata")
    for label in RUNTIME_LABELS:
        snapshot.add_argument("--" + label.replace("_", "-"), type=_path, required=True, help="local %s runtime file or symlink" % label)
    snapshot.add_argument("--bandit-before", type=_path, required=True)
    snapshot.add_argument("--bandit-after", type=_path, required=True)
    snapshot.add_argument("--evidence", type=_path, action="append", default=[], help="evidence file/directory; repeatable; links are not followed")
    snapshot.add_argument("--protocol", required=True, help="protocol/version description, for example HXP4+HXRP")
    snapshot.add_argument("--timing", required=True, help="JSON object recording protocol/dispatch timing")
    snapshot.add_argument("--map", dest="maps", action="append", required=True, help="map name/path; repeatable")
    snapshot.add_argument("--settings", required=True, help="JSON object recording game/acceptance settings")
    snapshot.add_argument("--seed", type=int, action="append", required=True, help="non-negative game seed; repeatable")
    snapshot.add_argument("--build-provenance", help="optional JSON object with build command/inputs/provenance")
    snapshot.add_argument("--toolchain", help="optional compiler/toolchain version or description")
    snapshot.add_argument("--wine", help="optional Wine version/build provenance")
    snapshot.add_argument("--bwapi", help="optional BWAPI version/source provenance")
    snapshot.add_argument("--shim", help="optional bridge/shim version/source provenance")
    snapshot.add_argument("--limitation", action="append", default=[], help="explicit evidence/compatibility limitation; repeatable")
    snapshot.add_argument("--notes", default="", help="recovery notes retained in the manifest")
    snapshot.add_argument("--user-confirmed", action="store_true", help="designate this NEW snapshot user-confirmed OG; never changes an old candidate")
    snapshot.add_argument("--confirmation", help="required with --user-confirmed; must contain %r" % OG_CONFIRMATION)
    snapshot.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    snapshot.set_defaults(handler=_snapshot_command)

    verify = sub.add_parser("verify", help="validate schema, layout, hashes, and available source commit/tree")
    verify.add_argument("--snapshot", type=_path, required=True)
    verify.add_argument("--source", type=_path, help="optional source worktree override for commit/tree verification")
    verify.add_argument("--allow-missing-source", action="store_true", help="allow an existing source directory without Git metadata")
    verify.add_argument("--json", action="store_true")
    verify.set_defaults(handler=_verify_command)

    restore = sub.add_parser("restore", help="materialize a verified snapshot into a NEW private directory")
    restore.add_argument("--snapshot", type=_path, required=True)
    restore.add_argument("--destination", type=_path, required=True, help="entirely new path; even an existing empty directory is refused")
    restore.add_argument("--json", action="store_true")
    restore.set_defaults(handler=_restore_command)

    forward = sub.add_parser("forward-port", help="read-only OG comparison and commands for a later NEW worktree")
    forward.add_argument("--snapshot", type=_path, required=True)
    forward.add_argument("--repo", type=_path, required=True)
    forward.add_argument("--base-commit", required=True)
    forward.add_argument("--json", action="store_true")
    forward.set_defaults(handler=_forward_command)
    return parser


def _emit(result: dict, as_json: bool, human_lines: list[str]) -> None:
    if as_json:
        print(json.dumps(result, indent=2, sort_keys=True, ensure_ascii=True, allow_nan=False))
    else:
        print("\n".join(human_lines))


def _snapshot_command(args) -> int:
    result = create_snapshot(args)
    _emit(
        result,
        args.json,
        [
            "created %s snapshot: %s" % (result["designation"], result["snapshot"]),
            "manifest SHA256: %s" % result["manifest_sha256"],
            "Originals were not moved, edited, or deleted; proprietary files were not uploaded.",
        ] + ["warning: " + item for item in result["warnings"]],
    )
    return 0


def _verify_command(args) -> int:
    result = verify_snapshot(args.snapshot, args.allow_missing_source, args.source)
    lines = ["snapshot: %s" % result["snapshot"], "valid: %s" % str(result["valid"]).lower()]
    lines += ["error: " + item for item in result["errors"]]
    lines += ["warning: " + item for item in result["warnings"]]
    _emit(result, args.json, lines)
    return 0 if result["valid"] else 1


def _restore_command(args) -> int:
    result = restore_snapshot(args.snapshot, args.destination)
    _emit(
        result,
        args.json,
        [
            "restored and verified snapshot in NEW directory: %s" % result["destination"],
            "recovery notes: %s" % result["recovery_notes"],
            "No worktree was reset; no compatibility or OG claim was added.",
        ],
    )
    return 0


def _forward_command(args) -> int:
    result = forward_port(args.snapshot, args.repo, args.base_commit)
    status = result["status"]
    lines = [
        "forward-port plan (READ ONLY): %s" % result["snapshot"],
        "unchanged: %d; changed: %d; missing: %d; conflicting: %d" % tuple(len(status[name]) for name in ("unchanged", "changed", "missing", "conflicting")),
    ]
    for name in ("changed", "missing", "conflicting"):
        for record in status[name]:
            lines.append("%s: %s" % (name, record["path"]))
    lines += ["warning: " + item for item in result["warnings"]]
    lines.append("commands for the user to review and run separately in NEW paths:")
    lines += ["  " + command for command in result["commands_for_new_worktree"]]
    lines.append("compatibility proven: false (no build or tests were run; no repository mutation was performed)")
    _emit(result, args.json, lines)
    return 1 if result["needs_review"] else 0


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (SnapshotError, OSError) as exc:
        print("pluto-snapshot: error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
