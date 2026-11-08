"""Canonical JSONL + content-addressed-blob Pluto frame traces."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
from typing import Any

from .commands import in_game_command_dicts
from .hxp import parse_frame, parse_reply


SCHEMA_VERSION = "pluto-diff/v2"
OBSERVATION_FORMAT = "PLSM/BWRL_SHM-request/v1"
MODEL_RESPONSE_FORMAT = "PLSM/BWRL_SHM-response/v1"
MANIFEST_NAME = "manifest.jsonl"
_HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
REQUIRED_CONFIG_FIELDS = (
    "engine_hash",
    "map",
    "races",
    "seed",
    "bandit",
    "inference_hash",
    "pluto_dll_hash",
    "bridge_hash",
    "opponent",
)
REQUIRED_FRAME_FIELDS = (
    "state_input",
    "observation",
    "game_data",
    "model_response",
    "emitted_commands",
    "received_commands",
    "accepted_commands",
    "unit_state",
    "observation_of_change",
)


class TraceError(ValueError):
    """Base class for trace failures."""


class TraceFormatError(TraceError):
    """Malformed, contradictory, or integrity-damaged trace data."""


class IncompleteTraceError(TraceError):
    """Data needed for a comparison was not captured or is missing on disk."""

    def __init__(
        self,
        message: str,
        *,
        frame: int | None = None,
        path: str | None = None,
    ) -> None:
        super().__init__(message)
        self.frame = frame
        self.path = path


def canonical_json(value: Any) -> str:
    """Canonical JSON used for manifest lines and configuration hashing."""

    return json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    )


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _validate_hash(value: Any, path: str) -> None:
    if not isinstance(value, str) or not _HASH_RE.fullmatch(value):
        raise TraceFormatError(f"{path} must be 64 lowercase hexadecimal SHA-256 digits")


def normalize_config(config: dict[str, Any]) -> dict[str, Any]:
    """Validate required run identity fields and remove any supplied outer hash."""

    if not isinstance(config, dict):
        raise TraceFormatError("config must be a JSON object")
    missing = [field for field in REQUIRED_CONFIG_FIELDS if field not in config]
    if missing:
        raise TraceFormatError(f"config is missing required fields: {', '.join(missing)}")
    answer = {key: value for key, value in config.items() if key != "config_hash"}
    _validate_hash(answer["engine_hash"], "config.engine_hash")
    _validate_hash(answer["inference_hash"], "config.inference_hash")
    _validate_hash(answer["pluto_dll_hash"], "config.pluto_dll_hash")
    _validate_hash(answer["bridge_hash"], "config.bridge_hash")
    map_config = answer["map"]
    if not isinstance(map_config, dict) or not isinstance(map_config.get("name"), str):
        raise TraceFormatError("config.map must contain a string name and sha256")
    _validate_hash(map_config.get("sha256"), "config.map.sha256")
    if not isinstance(answer["races"], list) or not answer["races"]:
        raise TraceFormatError("config.races must be a nonempty ordered JSON array")
    if not _is_int(answer["seed"]) or answer["seed"] < 0:
        raise TraceFormatError("config.seed must be a nonnegative integer")
    if not isinstance(answer["bandit"], dict):
        raise TraceFormatError("config.bandit must be a JSON object")
    _validate_hash(answer["bandit"].get("history_hash"), "config.bandit.history_hash")
    if not isinstance(answer["opponent"], (dict, str)):
        raise TraceFormatError("config.opponent must be a JSON object or string")
    try:
        canonical_json(answer)
    except (TypeError, ValueError) as exc:
        raise TraceFormatError(f"config is not finite canonical JSON: {exc}") from exc
    return answer


def config_hash(config: dict[str, Any]) -> str:
    normalized = normalize_config(config)
    return hashlib.sha256(canonical_json(normalized).encode("utf-8")).hexdigest()


def _validate_numeric_tree(value: Any, path: str) -> None:
    if isinstance(value, bool) or _is_int(value):
        return
    if isinstance(value, float):
        if not math.isfinite(value):
            raise TraceFormatError(f"{path} contains a non-finite float")
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            _validate_numeric_tree(item, f"{path}[{index}]")
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if not isinstance(key, str):
                raise TraceFormatError(f"{path} has a non-string JSON key")
            _validate_numeric_tree(item, f"{path}.{key}")
        return
    raise TraceFormatError(f"{path} must contain only numeric/bool arrays and objects")


def validate_unit_state(unit_state: Any) -> None:
    if not isinstance(unit_state, dict):
        raise TraceFormatError("unit_state must be an object keyed by stable decimal unit id")
    for unit_id, fields in unit_state.items():
        if not isinstance(unit_id, str) or not unit_id.isdigit() or str(int(unit_id)) != unit_id:
            raise TraceFormatError(f"invalid unit_state id {unit_id!r}")
        if not isinstance(fields, dict) or "exists" not in fields:
            raise TraceFormatError(f"unit_state.{unit_id} must be an object with numeric exists")
        if fields["exists"] not in (0, 1, False, True):
            raise TraceFormatError(f"unit_state.{unit_id}.exists must be 0 or 1")
        _validate_numeric_tree(fields, f"unit_state.{unit_id}")


def validate_change_events(events: Any, frame: int | None = None) -> None:
    if not isinstance(events, list):
        raise TraceFormatError("observation_of_change must be an array (empty means none observed)")
    for index, event in enumerate(events):
        path = f"observation_of_change[{index}]"
        if not isinstance(event, dict):
            raise TraceFormatError(f"{path} must be an object")
        change = event.get("change_frame")
        observed = event.get("observed_frame")
        if not _is_int(change) or change < 0 or not _is_int(observed) or observed < 0:
            raise TraceFormatError(f"{path} frame numbers must be nonnegative integers")
        if observed < change:
            raise TraceFormatError(f"{path} observes a change before it happened")
        if frame is not None and observed != frame:
            raise TraceFormatError(f"{path} must be recorded on its observed frame {observed}")
        if "unit_id" in event and (not _is_int(event["unit_id"]) or event["unit_id"] < 0):
            raise TraceFormatError(f"{path}.unit_id must be a nonnegative integer")
        if "field" in event and not isinstance(event["field"], str):
            raise TraceFormatError(f"{path}.field must be a string")


def _blob_path(root: Path, digest: str) -> Path:
    return root / "blobs" / digest[:2] / f"{digest}.bin"


def _blob_ref(root: Path, data: bytes) -> dict[str, Any]:
    digest = hashlib.sha256(data).hexdigest()
    path = _blob_path(root, digest)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        existing = path.read_bytes()
        if existing != data:
            raise TraceFormatError(f"content-addressed blob collision or corruption at {path}")
    else:
        # Exclusive creation prevents two capture writers from silently replacing
        # one another's payload.  fsync is left to the caller's capture durability
        # policy; manifest appends are flushed after every complete frame.
        try:
            with path.open("xb") as stream:
                stream.write(data)
        except FileExistsError:
            if path.read_bytes() != data:
                raise TraceFormatError(f"content-addressed blob corruption at {path}")
    return {
        "sha256": digest,
        "size": len(data),
        "path": path.relative_to(root).as_posix(),
    }


def _command_batch(root: Path, frame: int, data: bytes) -> dict[str, Any]:
    return {
        "frame": frame,
        "blob": _blob_ref(root, data),
        "commands": in_game_command_dicts(data),
    }


class TraceWriter:
    """Append-only canonical trace writer created by :meth:`create`."""

    def __init__(self, root: Path):
        self.root = root
        self._manifest = (root / MANIFEST_NAME).open("ab")
        self._next_frame = 0
        self._frame_count = 0
        self._unit_ids: set[str] | None = None
        self._unit_fields: dict[str, frozenset[str]] | None = None
        self._ended = False

    @classmethod
    def create(cls, root: str | os.PathLike[str], config: dict[str, Any]) -> "TraceWriter":
        path = Path(root)
        path.mkdir(parents=True, exist_ok=False)
        normalized = normalize_config(config)
        writer = cls(path)
        try:
            writer._write(
                {
                    "record": "header",
                    "schema": SCHEMA_VERSION,
                    "config": normalized,
                    "config_hash": config_hash(normalized),
                }
            )
        except Exception:
            writer.close()
            raise
        return writer

    @classmethod
    def open_append(cls, root: str | os.PathLike[str]) -> "TraceWriter":
        trace = load_trace(root)
        if trace.end is not None:
            raise TraceError("cannot append after a trace end record")
        if trace.issues:
            raise TraceError(f"cannot append to incomplete manifest: {'; '.join(trace.issues)}")
        expected = list(range(len(trace.frames)))
        if sorted(trace.frames) != expected:
            raise TraceError("cannot append to a trace with frame gaps")
        validate_trace(trace)
        writer = cls(Path(root))
        writer._next_frame = len(trace.frames)
        writer._frame_count = len(trace.frames)
        if trace.frames:
            validate_unit_state(trace.frames[0].get("unit_state"))
            writer._unit_ids = set(trace.frames[0]["unit_state"])
            writer._unit_fields = {
                unit_id: frozenset(fields)
                for unit_id, fields in trace.frames[0]["unit_state"].items()
            }
        return writer

    def _write(self, record: dict[str, Any]) -> None:
        if self._ended:
            raise TraceError("cannot append to a finished trace")
        line = canonical_json(record).encode("utf-8") + b"\n"
        self._manifest.write(line)
        self._manifest.flush()

    def append_frame(
        self,
        *,
        frame: int,
        state_input: bytes,
        observation: bytes,
        game_data: dict[str, Any],
        model_response: bytes,
        emitted_commands: bytes,
        emitted_frame: int,
        received_commands: bytes,
        received_frame: int,
        accepted_commands: bytes,
        accepted_frame: int,
        unit_state: dict[str, Any],
        observation_of_change: list[dict[str, Any]],
        bridge_reply: bytes | None = None,
    ) -> None:
        """Append one frame, keeping present empty inference payloads distinct from null."""

        if not _is_int(frame) or frame != self._next_frame:
            raise TraceFormatError(f"next canonical frame must be {self._next_frame}, got {frame}")
        for label, value in (
            ("emitted_frame", emitted_frame),
            ("received_frame", received_frame),
            ("accepted_frame", accepted_frame),
        ):
            if not _is_int(value) or value < 0:
                raise TraceFormatError(f"{label} must be a nonnegative integer")
        for label, value in (
            ("state_input", state_input),
            ("observation", observation),
            ("model_response", model_response),
            ("emitted_commands", emitted_commands),
            ("received_commands", received_commands),
            ("accepted_commands", accepted_commands),
        ):
            if not isinstance(value, bytes):
                raise TraceFormatError(f"{label} must be bytes (empty bytes are not missing)")
        if bridge_reply is not None and not isinstance(bridge_reply, bytes):
            raise TraceFormatError("bridge_reply must be bytes or null when not captured")
        parsed_state_input = parse_frame(state_input)
        if parsed_state_input.frame != frame:
            raise TraceFormatError(
                f"state_input internal frame {parsed_state_input.frame} does not match {frame}"
            )
        # Observation and model_response deliberately remain opaque.  Their
        # version labels identify the capture boundary; no PLSM action fields are
        # assumed or parsed here.  Empty bytes are valid on non-inference frames.
        parsed_bridge_reply = parse_reply(bridge_reply) if bridge_reply is not None else None
        if parsed_bridge_reply is not None and parsed_bridge_reply.frame != frame:
            raise TraceFormatError(
                f"bridge_reply internal frame {parsed_bridge_reply.frame} does not match {frame}"
            )
        _validate_numeric_tree(game_data, "game_data")
        validate_unit_state(unit_state)
        unit_ids = set(unit_state)
        unit_fields = {
            unit_id: frozenset(fields) for unit_id, fields in unit_state.items()
        }
        if self._unit_ids is None:
            self._unit_ids = unit_ids
            self._unit_fields = unit_fields
        elif unit_ids != self._unit_ids:
            raise TraceFormatError(
                "unit_state must retain one stable id universe on every frame; "
                "record nonexistent/dead units with exists=0 rather than omitting them"
            )
        elif unit_fields != self._unit_fields:
            raise TraceFormatError(
                "unit_state must retain the same numeric fields for every unit on every frame"
            )
        validate_change_events(observation_of_change, frame)
        record = {
            "record": "frame",
            "frame": frame,
            "state_input": {
                "blob": _blob_ref(self.root, state_input),
                "parsed": parsed_state_input.to_dict(),
            },
            "observation": {
                "format": OBSERVATION_FORMAT,
                "blob": _blob_ref(self.root, observation),
            },
            "game_data": game_data,
            "model_response": {
                "format": MODEL_RESPONSE_FORMAT,
                "blob": _blob_ref(self.root, model_response),
            },
            "emitted_commands": _command_batch(self.root, emitted_frame, emitted_commands),
            "received_commands": _command_batch(self.root, received_frame, received_commands),
            "accepted_commands": _command_batch(self.root, accepted_frame, accepted_commands),
            "bridge_reply": None,
            "unit_state": unit_state,
            "observation_of_change": observation_of_change,
        }
        if parsed_bridge_reply is not None:
            record["bridge_reply"] = {
                "blob": _blob_ref(self.root, bridge_reply),
                "parsed": parsed_bridge_reply.to_dict(),
            }
        self._write(record)
        self._next_frame += 1
        self._frame_count += 1

    def finish(self, *, status: str = "complete", message: str = "") -> None:
        if status not in ("complete", "partial"):
            raise TraceFormatError("trace end status must be complete or partial")
        if status == "complete" and self._frame_count == 0:
            raise TraceFormatError("a complete trace must contain frame 0")
        if not isinstance(message, str):
            raise TraceFormatError("trace end message must be a string")
        self._write(
            {
                "record": "end",
                "status": status,
                "message": message,
                "frame_count": self._frame_count,
                "last_frame": self._next_frame - 1,
            }
        )
        self._ended = True
        self._manifest.close()

    def close(self) -> None:
        if not self._manifest.closed:
            self._manifest.close()

    def __enter__(self) -> "TraceWriter":
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        if exc_type is None and not self._ended:
            self.finish(status="partial", message="writer closed without finish")
        else:
            self.close()


@dataclass
class Trace:
    root: Path
    header: dict[str, Any]
    frames: dict[int, dict[str, Any]]
    end: dict[str, Any] | None
    issues: list[str]

    @property
    def config(self) -> dict[str, Any]:
        return self.header["config"]

    @property
    def status(self) -> str:
        if (
            not self.frames
            or self.issues
            or self.end is None
            or self.end.get("status") != "complete"
        ):
            return "partial"
        return "complete"

    def read_blob(self, reference: Any) -> bytes:
        if not isinstance(reference, dict):
            raise IncompleteTraceError("missing binary blob reference")
        digest = reference.get("sha256")
        size = reference.get("size")
        _validate_hash(digest, "blob.sha256")
        if not _is_int(size) or size < 0:
            raise TraceFormatError("blob.size must be a nonnegative integer")
        expected_path = _blob_path(self.root, digest).relative_to(self.root).as_posix()
        if reference.get("path") != expected_path:
            raise TraceFormatError(
                f"blob path must be canonical {expected_path!r}, got {reference.get('path')!r}"
            )
        path = _blob_path(self.root, digest)
        try:
            data = path.read_bytes()
        except FileNotFoundError as exc:
            raise IncompleteTraceError(f"missing captured blob {expected_path}") from exc
        if len(data) != size:
            raise TraceFormatError(
                f"blob {expected_path} size {len(data)} does not match declared {size}"
            )
        actual = hashlib.sha256(data).hexdigest()
        if actual != digest:
            raise TraceFormatError(f"blob {expected_path} SHA-256 mismatch: actual {actual}")
        return data


def load_trace(root: str | os.PathLike[str]) -> Trace:
    """Load strict JSONL, preserving partial traces and detecting truncated lines."""

    path = Path(root)
    manifest = path / MANIFEST_NAME
    try:
        raw = manifest.read_bytes()
    except FileNotFoundError as exc:
        raise IncompleteTraceError(f"missing trace manifest {manifest}") from exc
    if not raw:
        raise IncompleteTraceError(f"empty trace manifest {manifest}")
    complete_lines = raw.splitlines(keepends=True)
    issues: list[str] = []
    if not raw.endswith(b"\n"):
        # A crash during append can leave valid JSON without the commit newline.
        # Exclude that entire final line rather than treating it as a committed frame.
        complete_lines = complete_lines[:-1]
        issues.append("manifest ends with an uncommitted/truncated final line")
    header: dict[str, Any] | None = None
    frames: dict[int, dict[str, Any]] = {}
    end: dict[str, Any] | None = None
    for line_number, line in enumerate(complete_lines, 1):
        try:
            text = line.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TraceFormatError(f"manifest line {line_number} is not UTF-8") from exc
        try:
            record = json.loads(
                text,
                parse_constant=lambda value: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON constant {value}")
                ),
            )
        except (json.JSONDecodeError, ValueError) as exc:
            raise TraceFormatError(f"invalid manifest JSON at line {line_number}: {exc}") from exc
        if not isinstance(record, dict):
            raise TraceFormatError(f"manifest line {line_number} is not an object")
        kind = record.get("record")
        if header is None:
            if kind != "header" or record.get("schema") != SCHEMA_VERSION:
                raise TraceFormatError("first manifest record must be a pluto-diff/v2 header")
            normalized = normalize_config(record.get("config"))
            expected_hash = config_hash(normalized)
            if record.get("config_hash") != expected_hash:
                raise TraceFormatError(
                    f"header config_hash {record.get('config_hash')!r} != computed {expected_hash}"
                )
            header = record
            continue
        if end is not None:
            raise TraceFormatError(f"record after end at manifest line {line_number}")
        if kind == "frame":
            frame = record.get("frame")
            if not _is_int(frame) or frame < 0:
                raise TraceFormatError(f"invalid frame number at manifest line {line_number}")
            if frame in frames:
                raise TraceFormatError(f"duplicate frame {frame} at manifest line {line_number}")
            frames[frame] = record
        elif kind == "end":
            if record.get("status") not in ("complete", "partial"):
                raise TraceFormatError(f"invalid end status at manifest line {line_number}")
            end = record
        else:
            raise TraceFormatError(f"unknown manifest record {kind!r} at line {line_number}")
    if header is None:
        raise IncompleteTraceError("trace has no committed header")
    if frames:
        expected = list(range(max(frames) + 1))
        if sorted(frames) != expected:
            issues.append("frame sequence has gaps")
        if min(frames) != 0:
            issues.append("frame sequence does not start at frame 0")
    if end is not None:
        if end.get("frame_count") != len(frames):
            issues.append("end frame_count does not match committed frame records")
        expected_last = max(frames) if frames else -1
        if end.get("last_frame") != expected_last:
            issues.append("end last_frame does not match committed frame records")
    return Trace(path, header, frames, end, issues)


def _frame_blob(trace: Trace, holder: dict[str, Any], frame: int, path: str) -> bytes:
    if "blob" not in holder or holder["blob"] is None:
        raise IncompleteTraceError(
            f"frame {frame} has no {path} blob reference",
            frame=frame,
            path=f"{path}.blob",
        )
    try:
        return trace.read_blob(holder["blob"])
    except IncompleteTraceError as exc:
        raise IncompleteTraceError(
            str(exc), frame=frame, path=f"{path}.blob"
        ) from exc


def _structured_holder(record: dict[str, Any], key: str, frame: int) -> dict[str, Any]:
    holder = record[key]
    if not isinstance(holder, dict):
        raise TraceFormatError(f"frame {frame} {key} must be a structured object")
    return holder


def validate_frame_record(
    trace: Trace,
    record: dict[str, Any],
    frame: int,
) -> int:
    """Validate one complete frame and its blobs, returning the blob count.

    Missing required stages/references are incomplete capture.  Wrong holder
    types, stale parsed caches, malformed binary payloads, and contradictory
    internal frame numbers are invalid trace data.  Optional ``bridge_reply``
    remains genuinely optional, but when captured it must be complete.
    """

    if not isinstance(record, dict) or record.get("record") != "frame":
        raise TraceFormatError(f"frame {frame} manifest value is not a frame record")
    if (
        not _is_int(frame)
        or frame < 0
        or not _is_int(record.get("frame"))
        or record.get("frame") != frame
    ):
        raise TraceFormatError(f"frame record key {frame} does not match its internal frame")
    missing = [
        key for key in REQUIRED_FRAME_FIELDS
        if key not in record or record[key] is None
    ]
    if missing:
        raise IncompleteTraceError(
            f"frame {frame} is missing required captured stages: {', '.join(missing)}",
            frame=frame,
            path=missing[0],
        )

    blobs = 0
    state_holder = _structured_holder(record, "state_input", frame)
    state_raw = _frame_blob(trace, state_holder, frame, "state_input")
    blobs += 1
    state_input = parse_frame(state_raw)
    if "parsed" not in state_holder:
        raise TraceFormatError(f"frame {frame} state_input.parsed cache is missing")
    if state_input.frame != frame:
        raise TraceFormatError(
            f"frame record {frame} contains state_input for internal frame {state_input.frame}"
        )
    if state_holder["parsed"] != state_input.to_dict():
        raise TraceFormatError(f"frame {frame} state_input.parsed does not match its binary blob")

    for key, expected_format in (
        ("observation", OBSERVATION_FORMAT),
        ("model_response", MODEL_RESPONSE_FORMAT),
    ):
        holder = _structured_holder(record, key, frame)
        actual_format = holder.get("format")
        if actual_format != expected_format:
            raise TraceFormatError(
                f"frame {frame} {key}.format must be {expected_format!r}, got {actual_format!r}"
            )
        _frame_blob(trace, holder, frame, key)
        blobs += 1

    _validate_numeric_tree(record["game_data"], "game_data")

    for key in ("emitted_commands", "received_commands", "accepted_commands"):
        holder = _structured_holder(record, key, frame)
        batch_frame = holder.get("frame")
        if not _is_int(batch_frame) or batch_frame < 0:
            raise TraceFormatError(f"frame {frame} {key}.frame is not a nonnegative integer")
        raw = _frame_blob(trace, holder, frame, key)
        blobs += 1
        decoded_commands = in_game_command_dicts(raw)
        if "commands" not in holder:
            raise TraceFormatError(f"frame {frame} {key}.commands cache is missing")
        if holder["commands"] != decoded_commands:
            raise TraceFormatError(
                f"frame {frame} {key}.commands does not match its binary blob"
            )

    bridge_holder = record.get("bridge_reply")
    if bridge_holder is not None:
        bridge_holder = _structured_holder(record, "bridge_reply", frame)
        bridge_raw = _frame_blob(trace, bridge_holder, frame, "bridge_reply")
        blobs += 1
        bridge_reply = parse_reply(bridge_raw)
        if "parsed" not in bridge_holder:
            raise TraceFormatError(f"frame {frame} bridge_reply.parsed cache is missing")
        if bridge_reply.frame != frame:
            raise TraceFormatError(
                f"frame record {frame} contains bridge_reply for internal frame {bridge_reply.frame}"
            )
        if bridge_holder["parsed"] != bridge_reply.to_dict():
            raise TraceFormatError(
                f"frame {frame} bridge_reply.parsed does not match its binary blob"
            )

    validate_unit_state(record["unit_state"])
    validate_change_events(record["observation_of_change"], frame)
    return blobs


def validate_stable_unit_coverage(
    record: dict[str, Any],
    frame: int,
    expected: dict[str, frozenset[str]] | None,
) -> dict[str, frozenset[str]]:
    coverage = {
        unit_id: frozenset(fields)
        for unit_id, fields in record["unit_state"].items()
    }
    if expected is not None and coverage != expected:
        raise IncompleteTraceError(
            f"frame {frame} unit_state id/field coverage differs from frame 0; "
            "use exists=0 and retain every numeric field",
            frame=frame,
            path="unit_state",
        )
    return coverage


def validate_trace(trace: Trace) -> int:
    """Validate every committed frame, blob, cache, and stable unit coverage."""

    if not trace.frames and trace.end is not None and trace.end.get("status") == "complete":
        raise IncompleteTraceError(
            "a complete trace must contain frame 0",
            frame=0,
            path="frames",
        )
    blobs = 0
    expected_coverage: dict[str, frozenset[str]] | None = None
    for frame, record in sorted(trace.frames.items()):
        blobs += validate_frame_record(trace, record, frame)
        expected_coverage = validate_stable_unit_coverage(
            record, frame, expected_coverage
        )
    return blobs
