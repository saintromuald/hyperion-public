"""First-divergence comparison and the six Phase 1 divergence classes."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Iterator

from .commands import CommandError, parse_in_game_commands
from .hxp import HxpError, parse_frame, parse_reply
from .trace import (
    IncompleteTraceError,
    MODEL_RESPONSE_FORMAT,
    OBSERVATION_FORMAT,
    Trace,
    TraceError,
    TraceFormatError,
    canonical_json,
    load_trace,
    validate_change_events,
    validate_frame_record,
    validate_stable_unit_coverage,
    validate_unit_state,
    _is_int,
    _validate_numeric_tree,
)


CLASS_NAMES = {
    1: "observation/state input",
    2: "model-response transport",
    3: "native command application",
    4: "action latency/timing",
    5: "native simulation",
    6: "opponent/configuration mismatch",
}
_MISSING = object()


class _Incomplete(Exception):
    def __init__(self, frame: int | None, path: str, message: str):
        self.frame = frame
        self.path = path
        self.message = message
        super().__init__(message)


def _trace_summary(trace: Trace) -> dict[str, Any]:
    return {
        "path": str(trace.root),
        "status": trace.status,
        "frames": len(trace.frames),
        "issues": list(trace.issues),
    }


def _result(
    status: str,
    *,
    divergence_class: int | None = None,
    frame: int | None = None,
    path: str | None = None,
    left: Any = None,
    right: Any = None,
    summary: str,
    details: dict[str, Any] | None = None,
    traces: tuple[Trace, Trace] | None = None,
) -> dict[str, Any]:
    answer: dict[str, Any] = {
        "schema": "pluto-diff-result/v2",
        "status": status,
        "class": divergence_class,
        "class_name": CLASS_NAMES.get(divergence_class),
        "first_divergent_frame": frame,
        "path": path,
        "left": left,
        "right": right,
        "summary": summary,
        "details": details or {},
    }
    if traces is not None:
        answer["traces"] = {
            "left": _trace_summary(traces[0]),
            "right": _trace_summary(traces[1]),
        }
    return answer


def _object_difference(left: Any, right: Any, path: str = "") -> tuple[str, Any, Any] | None:
    """Return the first deterministic exact JSON/numeric difference."""

    if isinstance(left, dict) and isinstance(right, dict):
        for key in sorted(set(left) | set(right)):
            child = f"{path}.{key}" if path else key
            if key not in left:
                return child, _MISSING, right[key]
            if key not in right:
                return child, left[key], _MISSING
            difference = _object_difference(left[key], right[key], child)
            if difference is not None:
                return difference
        return None
    if isinstance(left, list) and isinstance(right, list):
        for index in range(min(len(left), len(right))):
            difference = _object_difference(left[index], right[index], f"{path}[{index}]")
            if difference is not None:
                return difference
        if len(left) != len(right):
            return f"{path}.length", len(left), len(right)
        return None
    # Numeric comparison is exact (no tolerance).  JSON int 1 and float 1.0 are
    # the same numeric state; binary observation comparison remains byte-exact.
    if left != right or type(left) is not type(right) and not (
        isinstance(left, (int, float)) and not isinstance(left, bool)
        and isinstance(right, (int, float)) and not isinstance(right, bool)
    ):
        return path, left, right
    return None


def _external(value: Any) -> Any:
    if value is _MISSING:
        return {"missing": True}
    return value


def _different(
    traces: tuple[Trace, Trace],
    divergence_class: int,
    frame: int | None,
    path: str,
    left: Any,
    right: Any,
    summary: str,
    details: dict[str, Any] | None = None,
) -> dict[str, Any]:
    return _result(
        "diverged",
        divergence_class=divergence_class,
        frame=frame,
        path=path,
        left=_external(left),
        right=_external(right),
        summary=summary,
        details=details,
        traces=traces,
    )


def _required(record: dict[str, Any], key: str, frame: int) -> Any:
    if key not in record or record[key] is None:
        raise _Incomplete(frame, key, f"frame {frame} has no captured {key}")
    return record[key]


def _validate_frame_for_comparison(
    trace: Trace,
    record: dict[str, Any],
    frame: int,
    expected_coverage: dict[str, frozenset[str]] | None,
) -> dict[str, frozenset[str]]:
    try:
        validate_frame_record(trace, record, frame)
        return validate_stable_unit_coverage(record, frame, expected_coverage)
    except IncompleteTraceError as exc:
        raise _Incomplete(
            exc.frame if exc.frame is not None else frame,
            exc.path or "frame",
            str(exc),
        ) from exc


def _blob(trace: Trace, holder: Any, frame: int, path: str) -> bytes:
    if not isinstance(holder, dict) or "blob" not in holder:
        raise _Incomplete(frame, f"{path}.blob", f"frame {frame} has no {path} blob reference")
    try:
        return trace.read_blob(holder["blob"])
    except IncompleteTraceError as exc:
        raise _Incomplete(frame, f"{path}.blob", str(exc)) from exc


def _bytes_difference(left: bytes, right: bytes, path: str) -> tuple[str, Any, Any, dict[str, Any]] | None:
    import hashlib

    details = {
        "left_sha256": hashlib.sha256(left).hexdigest(),
        "right_sha256": hashlib.sha256(right).hexdigest(),
        "left_size": len(left),
        "right_size": len(right),
    }
    if left == right:
        return None
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            details["byte_offset"] = index
            return f"{path}.bytes[{index}]", a, b, details
    # The common prefix is equal but one payload has a first additional byte.
    # Report that numeric byte and explicitly mark the empty/shorter side missing.
    index = min(len(left), len(right))
    details["byte_offset"] = index
    return (
        f"{path}.bytes[{index}]",
        left[index] if index < len(left) else _MISSING,
        right[index] if index < len(right) else _MISSING,
        details,
    )


def _batch(batch: Any, trace: Trace, frame: int, path: str) -> tuple[int, bytes, list[Any]]:
    if not isinstance(batch, dict):
        raise _Incomplete(frame, path, f"frame {frame} has no structured {path}")
    batch_frame = batch.get("frame")
    if not _is_int(batch_frame) or batch_frame < 0:
        raise TraceFormatError(f"frame {frame} {path}.frame is not a nonnegative integer")
    raw = _blob(trace, batch, frame, path)
    commands = parse_in_game_commands(raw)
    if "commands" not in batch:
        raise TraceFormatError(f"frame {frame} {path}.commands cache is missing")
    if batch["commands"] != [command.to_dict() for command in commands]:
        raise TraceFormatError(f"frame {frame} {path}.commands does not match its binary blob")
    return batch_frame, raw, commands


def _bridge_reply(holder: Any, trace: Trace, frame: int) -> tuple[bytes, Any]:
    raw = _blob(trace, holder, frame, "bridge_reply")
    reply = parse_reply(raw)
    if reply.frame != frame:
        raise TraceFormatError(
            f"frame record {frame} contains bridge_reply for internal frame {reply.frame}"
        )
    if not isinstance(holder, dict) or "parsed" not in holder:
        raise TraceFormatError(f"frame {frame} bridge_reply.parsed cache is missing")
    if holder["parsed"] != reply.to_dict():
        raise TraceFormatError(f"frame {frame} bridge_reply.parsed does not match its binary blob")
    return raw, reply


def _state_input(holder: Any, trace: Trace, frame: int) -> tuple[bytes, Any]:
    raw = _blob(trace, holder, frame, "state_input")
    state_input = parse_frame(raw)
    if state_input.frame != frame:
        raise TraceFormatError(
            f"frame record {frame} contains state_input for internal frame {state_input.frame}"
        )
    if not isinstance(holder, dict) or "parsed" not in holder:
        raise TraceFormatError(f"frame {frame} state_input.parsed cache is missing")
    if holder["parsed"] != state_input.to_dict():
        raise TraceFormatError(f"frame {frame} state_input.parsed does not match its binary blob")
    return raw, state_input


def _opaque(holder: Any, trace: Trace, frame: int, path: str, expected_format: str) -> bytes:
    """Return an exact request/response payload without interpreting its layout."""

    if not isinstance(holder, dict):
        raise _Incomplete(frame, path, f"frame {frame} has no structured {path}")
    actual_format = holder.get("format")
    if actual_format != expected_format:
        raise TraceFormatError(
            f"frame {frame} {path}.format must be {expected_format!r}, got {actual_format!r}"
        )
    return _blob(trace, holder, frame, path)


def _batch_events(
    trace: Trace,
    name: str,
    end_frame: int,
) -> Iterator[tuple[int, int, bytes, list[Any]]]:
    """Yield command events lazily so later gaps cannot hide a decided mismatch."""

    for frame in range(end_frame + 1):
        record = trace.frames.get(frame)
        if record is None:
            return
        batch = _batch(_required(record, name, frame), trace, frame, name)
        if batch[1]:
            yield frame, batch[0], batch[1], batch[2]


def _batch_timing_difference(
    traces: tuple[Trace, Trace],
    name: str,
    frame: int,
    end_frame: int,
) -> dict[str, Any] | None:
    """Detect the same command event transported/applied at another frame."""

    left_events = _batch_events(traces[0], name, end_frame)
    right_events = _batch_events(traces[1], name, end_frame)
    for index, (left_event, right_event) in enumerate(zip(left_events, right_events)):
        left_record, left_frame, left_raw, left_commands = left_event
        right_record, right_frame, right_raw, right_commands = right_event
        if left_raw != right_raw:
            return None
        if left_record == right_record and left_frame == right_frame:
            continue
        if min(left_record, right_record) != frame:
            return None
        return {
            "event_index": index,
            "left_record_frame": left_record,
            "right_record_frame": right_record,
            "left_command_frame": left_frame,
            "right_command_frame": right_frame,
            "left_commands": [command.to_dict() for command in left_commands],
            "right_commands": [command.to_dict() for command in right_commands],
        }
    return None


def _state_events(
    trace: Trace,
    unit_id: str,
    field: str,
    end_frame: int,
) -> list[tuple[int, Any, Any]]:
    """Transitions on consecutive frames, stopping at the first unknown state."""

    events: list[tuple[int, Any, Any]] = []
    for frame in range(1, end_frame + 1):
        previous = trace.frames.get(frame - 1)
        current = trace.frames.get(frame)
        if previous is None or current is None:
            break
        previous_unit = previous.get("unit_state", {}).get(unit_id)
        current_unit = current.get("unit_state", {}).get(unit_id)
        if not isinstance(previous_unit, dict) or not isinstance(current_unit, dict):
            break
        if field not in previous_unit or field not in current_unit:
            break
        old = previous_unit[field]
        new = current_unit[field]
        if old != new:
            events.append((frame, old, new))
    return events


def _unit_timing_difference(
    left: Trace,
    right: Trace,
    frame: int,
    unit_id: str,
    field: str,
    end_frame: int,
) -> dict[str, Any] | None:
    """Return class-4 evidence only when both sides really change, at different frames."""

    left_events = _state_events(left, unit_id, field, end_frame)
    right_events = _state_events(right, unit_id, field, end_frame)
    for index, (left_event, right_event) in enumerate(zip(left_events, right_events)):
        left_frame, left_old, left_new = left_event
        right_frame, right_old, right_new = right_event
        if left_frame == right_frame:
            if _object_difference(left_new, right_new) is not None:
                # Same-time numeric divergence takes class 5 and would be found
                # at this earlier transition before any later timing difference.
                return None
            continue
        # Latency means the identical numeric transition happened at another
        # frame.  Different source or resulting values are simulation evidence,
        # even when each side happens to contain one transition.
        if (
            _object_difference(left_old, right_old) is not None
            or _object_difference(left_new, right_new) is not None
        ):
            return None
        if min(left_frame, right_frame) == frame:
            return {
                "unit_id": int(unit_id),
                "field": field,
                "transition_index": index,
                "left_change_frame": left_frame,
                "right_change_frame": right_frame,
                "left_previous": left_old,
                "right_previous": right_old,
                "left_new": left_new,
                "right_new": right_new,
            }
        # An earlier timing mismatch would already have been returned.
        if min(left_frame, right_frame) < frame:
            return None
    return None


def _compare_unit_states(
    traces: tuple[Trace, Trace],
    left_frame: dict[str, Any],
    right_frame: dict[str, Any],
    frame: int,
    end_frame: int,
) -> dict[str, Any] | None:
    left, right = traces
    left_state = _required(left_frame, "unit_state", frame)
    right_state = _required(right_frame, "unit_state", frame)
    validate_unit_state(left_state)
    validate_unit_state(right_state)
    if set(left_state) != set(right_state):
        left_only = sorted(set(left_state) - set(right_state))
        right_only = sorted(set(right_state) - set(left_state))
        raise _Incomplete(
            frame,
            "unit_state.unit_ids",
            "unit-state captures must use one stable id universe; "
            f"left-only={left_only}, right-only={right_only} (use exists=0, not omitted units)",
        )
    for unit_id in sorted(left_state, key=lambda value: int(value)):
        left_unit = left_state[unit_id]
        right_unit = right_state[unit_id]
        if set(left_unit) != set(right_unit):
            raise _Incomplete(
                frame,
                f"unit_state.{unit_id}",
                f"unit {unit_id} numeric fields differ in capture coverage, not just value",
            )
        fields = sorted(left_unit, key=lambda name: (name != "exists", name))
        for field in fields:
            left_value = left_unit[field]
            right_value = right_unit[field]
            if left_value == right_value:
                continue
            path = f"unit_state.{unit_id}.{field}"
            timing = _unit_timing_difference(left, right, frame, unit_id, field, end_frame)
            if timing is not None:
                return _different(
                    traces,
                    4,
                    frame,
                    path,
                    left_value,
                    right_value,
                    f"unit {unit_id} {field} changes at frame "
                    f"{timing['left_change_frame']} (left) vs {timing['right_change_frame']} (right)",
                    timing,
                )
            difference = _object_difference(left_value, right_value, path)
            actual_path = difference[0] if difference else path
            return _different(
                traces,
                5,
                frame,
                actual_path,
                left_value,
                right_value,
                f"native numeric unit state differs at frame {frame}: unit {unit_id} {field}",
                {"unit_id": int(unit_id), "field": field, "exact_comparison": True},
            )
    return None


def _compare_loaded(left: Trace, right: Trace) -> dict[str, Any]:
    traces = (left, right)
    config_difference = _object_difference(left.config, right.config, "config")
    if config_difference is not None:
        path, left_value, right_value = config_difference
        return _different(
            traces,
            6,
            None,
            path,
            left_value,
            right_value,
            "initial engine/map/race/seed/bandit/inference/Pluto-DLL/bridge/opponent configuration differs",
            {
                "left_config_hash": left.header["config_hash"],
                "right_config_hash": right.header["config_hash"],
            },
        )
    if not left.frames or not right.frames:
        raise _Incomplete(None, "frames", "both traces need frame 0 before simulation can be compared")

    highest = max(max(left.frames), max(right.frames))
    # Timing classification may look ahead through the contiguous common capture
    # window.  The comparison loop below still stops at its first missing frame.
    common_horizon = -1
    for frame in range(highest + 1):
        if frame not in left.frames or frame not in right.frames:
            break
        common_horizon = frame
    expected_coverages: list[dict[str, frozenset[str]] | None] = [None, None]
    for frame in range(highest + 1):
        if frame not in left.frames or frame not in right.frames:
            missing = []
            if frame not in left.frames:
                missing.append("left")
            if frame not in right.frames:
                missing.append("right")
            raise _Incomplete(
                frame,
                "frames",
                f"frame {frame} is missing on {', '.join(missing)}; missing data is not equality",
            )
        left_record = left.frames[frame]
        right_record = right.frames[frame]
        expected_coverages[0] = _validate_frame_for_comparison(
            left, left_record, frame, expected_coverages[0]
        )
        expected_coverages[1] = _validate_frame_for_comparison(
            right, right_record, frame, expected_coverages[1]
        )

        left_state_input = _state_input(
            _required(left_record, "state_input", frame), left, frame
        )
        right_state_input = _state_input(
            _required(right_record, "state_input", frame), right, frame
        )
        difference = _bytes_difference(left_state_input[0], right_state_input[0], "state_input")
        if difference is not None:
            path, a, b, details = difference
            return _different(
                traces, 1, frame, path, a, b,
                f"Hyperion-to-bridge state input first differs at frame {frame}", details,
            )

        left_observation = _opaque(
            _required(left_record, "observation", frame), left, frame,
            "observation", OBSERVATION_FORMAT,
        )
        right_observation = _opaque(
            _required(right_record, "observation", frame), right, frame,
            "observation", OBSERVATION_FORMAT,
        )
        difference = _bytes_difference(left_observation, right_observation, "observation")
        if difference is not None:
            path, a, b, details = difference
            return _different(
                traces, 1, frame, path, a, b,
                f"raw inference observation bytes first differ at frame {frame}", details,
            )

        left_game_data = _required(left_record, "game_data", frame)
        right_game_data = _required(right_record, "game_data", frame)
        _validate_numeric_tree(left_game_data, "game_data")
        _validate_numeric_tree(right_game_data, "game_data")
        difference = _object_difference(left_game_data, right_game_data, "game_data")
        if difference is not None:
            path, a, b = difference
            return _different(
                traces, 1, frame, path, a, b,
                f"relevant GameData input first differs at frame {frame}",
                {"exact_comparison": True},
            )

        left_response = _opaque(
            _required(left_record, "model_response", frame), left, frame,
            "model_response", MODEL_RESPONSE_FORMAT,
        )
        right_response = _opaque(
            _required(right_record, "model_response", frame), right, frame,
            "model_response", MODEL_RESPONSE_FORMAT,
        )
        difference = _bytes_difference(left_response, right_response, "model_response")
        if difference is not None:
            path, a, b, details = difference
            return _different(
                traces, 2, frame, path, a, b,
                f"raw inference response bytes first differ at frame {frame}", details,
            )

        left_bridge_holder = left_record.get("bridge_reply")
        right_bridge_holder = right_record.get("bridge_reply")
        if (left_bridge_holder is None) != (right_bridge_holder is None):
            raise _Incomplete(
                frame, "bridge_reply",
                "optional bridge_reply is captured on only one side; missing coverage is not equality",
            )
        if left_bridge_holder is not None and right_bridge_holder is not None:
            left_bridge = _bridge_reply(left_bridge_holder, left, frame)
            right_bridge = _bridge_reply(right_bridge_holder, right, frame)
            difference = _bytes_difference(left_bridge[0], right_bridge[0], "bridge_reply")
            if difference is not None:
                path, a, b, details = difference
                details["left_reply"] = left_bridge[1].to_dict()
                details["right_reply"] = right_bridge[1].to_dict()
                return _different(
                    traces, 2, frame, path, a, b,
                    f"later HXRP bridge reply first differs at frame {frame}", details,
                )

        left_batches = {}
        right_batches = {}
        for name in ("emitted_commands", "received_commands", "accepted_commands"):
            left_batches[name] = _batch(
                _required(left_record, name, frame), left, frame, name
            )
            right_batches[name] = _batch(
                _required(right_record, name, frame), right, frame, name
            )

        left_changes = _required(left_record, "observation_of_change", frame)
        right_changes = _required(right_record, "observation_of_change", frame)
        validate_change_events(left_changes, frame)
        validate_change_events(right_changes, frame)

        # Transport/apply/observation timing is class 4, before payload application
        # or native numeric state. Raw inference and optional HXRP bytes were compared.
        for name in ("emitted_commands", "received_commands", "accepted_commands"):
            left_frame_number = left_batches[name][0]
            right_frame_number = right_batches[name][0]
            if left_frame_number != right_frame_number:
                return _different(
                    traces, 4, frame, f"{name}.frame",
                    left_frame_number, right_frame_number,
                    f"{name} timing differs at compared frame {frame}",
                    {"compared_frame": frame},
                )
        difference = _object_difference(left_changes, right_changes, "observation_of_change")
        if difference is not None:
            path, a, b = difference
            return _different(
                traces, 4, frame, path, a, b,
                f"observation-of-change timing differs at frame {frame}",
            )

        for name, divergence_class in (("emitted_commands", 2), ("received_commands", 2)):
            difference = _bytes_difference(left_batches[name][1], right_batches[name][1], name)
            if difference is not None:
                timing = _batch_timing_difference(traces, name, frame, common_horizon)
                if timing is not None:
                    return _different(
                        traces, 4, frame, f"{name}.event_frame",
                        timing["left_record_frame"], timing["right_record_frame"],
                        f"the same {name} event occurs at different frames", timing,
                    )
                path, a, b, details = difference
                details["left_commands"] = [c.to_dict() for c in left_batches[name][2]]
                details["right_commands"] = [c.to_dict() for c in right_batches[name][2]]
                return _different(
                    traces, divergence_class, frame, path, a, b,
                    f"native command transport payload differs at frame {frame}", details,
                )

        difference = _bytes_difference(
            left_batches["accepted_commands"][1],
            right_batches["accepted_commands"][1],
            "accepted_commands",
        )
        if difference is not None:
            timing = _batch_timing_difference(traces, "accepted_commands", frame, common_horizon)
            if timing is not None:
                return _different(
                    traces, 4, frame, "accepted_commands.event_frame",
                    timing["left_record_frame"], timing["right_record_frame"],
                    "the same accepted command event is applied at different frames", timing,
                )
            path, a, b, details = difference
            details["left_commands"] = [c.to_dict() for c in left_batches["accepted_commands"][2]]
            details["right_commands"] = [c.to_dict() for c in right_batches["accepted_commands"][2]]
            return _different(
                traces, 3, frame, path, a, b,
                f"same model response produced different accepted native commands at frame {frame}",
                details,
            )

        state_result = _compare_unit_states(
            traces, left_record, right_record, frame, common_horizon
        )
        if state_result is not None:
            return state_result

    if left.status != "complete" or right.status != "complete":
        return _result(
            "incomplete",
            frame=None,
            path="trace.status",
            left=left.status,
            right=right.status,
            summary="all captured comparisons match, but at least one trace is partial",
            details={"left_issues": left.issues, "right_issues": right.issues},
            traces=traces,
        )
    return _result(
        "equivalent",
        frame=None,
        path=None,
        summary=f"all {highest + 1} per-frame records are exactly equivalent",
        details={"frames_compared": highest + 1, "exact_comparison": True},
        traces=traces,
    )


def compare_traces(
    left: str | os.PathLike[str] | Trace,
    right: str | os.PathLike[str] | Trace,
) -> dict[str, Any]:
    """Compare two trace directories and return a JSON-serialisable result."""

    traces: tuple[Trace, Trace] | None = None
    try:
        left_trace = left if isinstance(left, Trace) else load_trace(Path(left))
        right_trace = right if isinstance(right, Trace) else load_trace(Path(right))
        traces = (left_trace, right_trace)
        return _compare_loaded(left_trace, right_trace)
    except _Incomplete as exc:
        return _result(
            "incomplete",
            frame=exc.frame,
            path=exc.path,
            summary=exc.message,
            traces=traces,
        )
    except IncompleteTraceError as exc:
        return _result(
            "incomplete",
            frame=None,
            path="trace",
            summary=str(exc),
            traces=traces,
        )
    except (TraceError, CommandError, HxpError) as exc:
        return _result(
            "invalid",
            path=None,
            summary=f"invalid trace or binary payload: {exc}",
            traces=traces,
        )


def human_report(result: dict[str, Any]) -> str:
    """Render numeric left/right values without hiding them behind hashes."""

    lines = [
        f"status: {result.get('status')}",
        f"class: {result.get('class')} ({result.get('class_name') or 'n/a'})",
        f"first divergent frame: {result.get('first_divergent_frame')}",
        f"path: {result.get('path') or 'n/a'}",
        f"summary: {result.get('summary', '')}",
    ]
    if result.get("status") == "diverged":
        lines.extend(
            [
                "left:  " + json.dumps(result.get("left"), ensure_ascii=False, sort_keys=True),
                "right: " + json.dumps(result.get("right"), ensure_ascii=False, sort_keys=True),
            ]
        )
    details = result.get("details") or {}
    if details:
        lines.append("details: " + canonical_json(details))
    traces = result.get("traces") or {}
    if traces:
        for side in ("left", "right"):
            summary = traces.get(side, {})
            lines.append(
                f"{side} trace: status={summary.get('status')} "
                f"frames={summary.get('frames')} path={summary.get('path')}"
            )
    return "\n".join(lines) + "\n"
