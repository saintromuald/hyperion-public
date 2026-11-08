"""Offline worker-production stall analysis for strict HXP/HXRP captures.

This module deliberately does not relax the transport parsers.  Frame input is
parsed as complete HXP2/HXP3/HXP4/HXPF records.  Raw reply input may contain foreign
bytes, but an HXRP candidate is accepted only after a complete strict parse and
an exact expected-frame match.

Worker completion is a numeric completed=0 -> completed=1 transition (or the
appearance of a completed worker after frame zero), never merely an egg or an
incomplete worker.  In-progress Zerg morphs use their observed remaining timer
plus a documented post-timer grace period; the incomplete-drone phase extends
that grace period instead of being treated as a failed 300-frame deadline.
Timer/grace expiry alone remains inconclusive.  HXP3/HXP4 chains retain the full
(slot, generation) identity so immediate slot reuse cannot complete an older
production lifecycle.  Legacy streams and generation-zero candidates cannot
prove lifecycle continuity and therefore cannot prove completion or rejection.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
import struct
from typing import Any, Iterable

from .commands import ClassicCommand, CommandError
from .hxp import (
    HxpError,
    HxFrame,
    HxReply,
    REPLY_MAGIC,
    parse_frame_stream,
    parse_reply,
)


EXPECTED_APPLY_LATENCY = 4
# This is observation slack after the expected application frame, not another
# claimed native latency.  It prevents an off-by-one capture boundary from
# becoming a rejection verdict.
TRANSITION_OBSERVATION_FRAMES = 4
# Diagnostic slack after an observed build timer expires.  This is deliberately
# not a native build-time constant.  Seeing an incomplete worker starts at least
# one fresh grace window, which covers the normal egg -> incomplete drone ->
# completed drone lifecycle.
INCOMPLETE_WORKER_GRACE_FRAMES = 120

DRONE = 41
SCV = 7
PROBE = 64
LARVA = 35
EGG = 36
WORKER_TYPES = {DRONE, SCV, PROBE}
WORKER_REQUESTS = {(0x23, DRONE), (0x1F, SCV), (0x1F, PROBE)}
WORKER_COST = {DRONE: (50, 0), SCV: (50, 0), PROBE: (50, 0)}
# HXP supply fields use Brood War's doubled supply units: a worker costs one
# displayed supply and therefore two numeric supply units.
WORKER_SUPPLY_COST = 2
PRODUCER_TYPES = {
    DRONE: {LARVA},
    SCV: {106},  # Command Center (lifted is the same unit type); addons do not train.
    PROBE: {154},  # Nexus.
}
REQUEST_KIND = {
    DRONE: "zerg_drone_morph",
    SCV: "terran_scv_train",
    PROBE: "protoss_probe_train",
}
_NO_ID = 0xFFFFFFFF
_REPLY_HEADER = struct.Struct("<4sII")
_MAX_REJECTION_DETAILS = 32


class WorkerStallError(ValueError):
    """Capture selection or analysis input is invalid or ambiguous."""


@dataclass(frozen=True)
class _Run:
    records: tuple[Any, ...]
    start_record: int
    start_byte: int | None = None
    end_byte: int | None = None


@dataclass
class _Request:
    emitted_frame: int
    command_index: int
    command: ClassicCommand
    reply: HxReply
    selected_ids: tuple[int, ...]
    target_type: int
    producer_ids: list[int] = field(default_factory=list)
    producer_generations: dict[int, int] = field(default_factory=dict)
    id_match: str = "unmatched"
    id_offset: int | None = None
    result: dict[str, Any] = field(default_factory=dict)
    # Internal evidence, not emitted as object references in JSON.
    start_frame: int | None = None
    chain_unit_id: int | None = None
    chain_generation: int | None = None
    completion_frame: int | None = None
    valid_until: int | None = None
    failed: bool = False
    pending: bool = True


def _select_longest_run(runs: list[_Run], what: str) -> _Run:
    if not runs:
        raise WorkerStallError(f"{what} contains no contiguous stream beginning at frame zero")
    longest = max(len(run.records) for run in runs)
    candidates = [run for run in runs if len(run.records) == longest]
    if len(candidates) != 1:
        starts = [run.start_record for run in candidates]
        raise WorkerStallError(
            f"ambiguous {what}: equally long frame-zero streams at record indices {starts}"
        )
    return candidates[0]


def _frame_runs(frames: list[HxFrame]) -> list[_Run]:
    if not frames or frames[0].frame != 0:
        raise WorkerStallError("frames input must begin with a frame-zero HXP record")
    starts = [index for index, frame in enumerate(frames) if frame.frame == 0]
    runs: list[_Run] = []
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(frames)
        records = frames[start:end]
        if any(frame.frame != offset for offset, frame in enumerate(records)):
            # This fragment is not a complete contiguous generation.  It can be
            # a damaged scaffold; a later valid live run remains selectable.
            continue
        runs.append(_Run(tuple(records), start))
    return runs


def select_live_frames(data: bytes) -> tuple[list[HxFrame], dict[str, Any]]:
    """Strictly parse frames and select the longest contiguous frame-zero run.

    A duplicate frame-zero scaffold is therefore ignored only when a strictly
    longer complete live generation exists.  Equal-length generations are an
    ambiguity, not something to resolve by timestamp or magic.
    """

    frames = parse_frame_stream(data)
    runs = _frame_runs(frames)
    selected = _select_longest_run(runs, "frames input")
    return list(selected.records), {
        "total_records": len(frames),
        "frame_zero_record_indices": [i for i, frame in enumerate(frames) if frame.frame == 0],
        "valid_contiguous_runs": [
            {
                "start_record": run.start_record,
                "record_count": len(run.records),
                "last_frame": run.records[-1].frame,
            }
            for run in runs
        ],
        "selected_start_record": selected.start_record,
        "selected_record_count": len(selected.records),
        "selected_first_frame": selected.records[0].frame,
        "selected_last_frame": selected.records[-1].frame,
        "discarded_scaffold_records": selected.start_record,
        "selection_rule": "unique longest strictly contiguous run beginning at frame zero",
    }


def _strict_reply_candidate(data: bytes, offset: int) -> tuple[HxReply | None, int, str | None, int | None]:
    """Return a fully parsed candidate, never a magic-only approximation."""

    if len(data) - offset < _REPLY_HEADER.size:
        return None, 0, "truncated HXRP header", None
    magic, frame, command_size = _REPLY_HEADER.unpack_from(data, offset)
    assert magic == REPLY_MAGIC
    size = _REPLY_HEADER.size + command_size + 4
    if offset + size > len(data):
        return None, size, "truncated HXRP payload or win field", frame
    try:
        reply = parse_reply(data[offset : offset + size])
    except (HxpError, CommandError) as exc:
        return None, size, str(exc), frame
    return reply, size, None, frame


def _reply_runs(data: bytes) -> tuple[list[_Run], dict[str, Any]]:
    """Scan foreign-byte-tolerant raw replies into expected-frame generations."""

    runs: list[_Run] = []
    current: list[HxReply] = []
    current_start = 0
    current_start_byte: int | None = None
    current_end_byte: int | None = None
    expected = 0
    offset = 0
    candidates = 0
    accepted = 0
    rejected = 0
    rejections: list[dict[str, Any]] = []

    def close_run() -> None:
        nonlocal current, current_start_byte, current_end_byte
        if current:
            runs.append(
                _Run(tuple(current), current_start, current_start_byte, current_end_byte)
            )
        current = []
        current_start_byte = None
        current_end_byte = None

    while True:
        found = data.find(REPLY_MAGIC, offset)
        if found < 0:
            break
        candidates += 1
        reply, size, error, candidate_frame = _strict_reply_candidate(data, found)
        if reply is None:
            rejected += 1
            if len(rejections) < _MAX_REJECTION_DETAILS:
                rejections.append(
                    {
                        "byte_offset": found,
                        "candidate_frame": candidate_frame,
                        "expected_frame": expected if current else 0,
                        "error": error,
                    }
                )
            offset = found + 1
            continue

        # A strictly valid zero starts a new possible generation.  Selecting the
        # longest generation later handles the duplicate frame-zero scaffold.
        if reply.frame == 0:
            close_run()
            current = [reply]
            current_start = accepted
            current_start_byte = found
            current_end_byte = found + size
            expected = 1
            accepted += 1
            offset = found + size
            continue

        if current and reply.frame == expected:
            current.append(reply)
            current_end_byte = found + size
            expected += 1
            accepted += 1
            offset = found + size
            continue

        rejected += 1
        if len(rejections) < _MAX_REJECTION_DETAILS:
            rejections.append(
                {
                    "byte_offset": found,
                    "candidate_frame": reply.frame,
                    "expected_frame": expected if current else 0,
                    "error": "valid HXRP record has a non-expected frame",
                }
            )
        offset = found + 1

    close_run()
    diagnostics = {
        "magic_candidates_seen": candidates,
        "strict_replies_accepted": accepted,
        "candidates_rejected": rejected,
        "rejection_details": rejections,
        "rejection_details_truncated": max(0, rejected - len(rejections)),
        "acceptance_rule": "complete strict HXRP parse and exact next expected frame; frame zero starts a candidate generation",
    }
    return runs, diagnostics


def select_live_replies(data: bytes) -> tuple[list[HxReply], dict[str, Any]]:
    """Parse raw replies cautiously, retaining only expected-frame records."""

    if not data:
        return [], {
            "total_records": 0,
            "selected_record_count": 0,
            "coverage_complete_from_zero": False,
            "magic_candidates_seen": 0,
            "strict_replies_accepted": 0,
            "candidates_rejected": 0,
            "rejection_details": [],
            "rejection_details_truncated": 0,
            "selection_rule": "unique longest strictly parsed expected-frame generation",
            "acceptance_rule": "complete strict HXRP parse and exact next expected frame; magic alone is never accepted",
        }
    runs, diagnostics = _reply_runs(data)
    if not runs:
        diagnostics.update(
            {
                "total_records": 0,
                "selected_record_count": 0,
                "coverage_complete_from_zero": False,
                "selection_rule": "unique longest strictly parsed expected-frame generation; no valid generation is incomplete coverage",
            }
        )
        return [], diagnostics
    selected = _select_longest_run(runs, "replies-raw input")
    replies = list(selected.records)
    diagnostics.update(
        {
            "total_records": sum(len(run.records) for run in runs),
            "valid_contiguous_runs": [
                {
                    "start_record": run.start_record,
                    "start_byte": run.start_byte,
                    "end_byte": run.end_byte,
                    "record_count": len(run.records),
                    "last_frame": run.records[-1].frame,
                }
                for run in runs
            ],
            "selected_start_record": selected.start_record,
            "selected_start_byte": selected.start_byte,
            "selected_end_byte": selected.end_byte,
            "selected_record_count": len(replies),
            "selected_first_frame": replies[0].frame,
            "selected_last_frame": replies[-1].frame,
            "coverage_complete_from_zero": True,
            "selection_rule": "unique longest strictly parsed expected-frame generation",
        }
    )
    return replies, diagnostics


def _economy(frame: HxFrame | None, player: int = 0) -> dict[str, Any] | None:
    if frame is None or player >= len(frame.players):
        return None
    values = frame.players[player]
    return {
        "race": values["race"],
        "minerals": values["minerals"],
        "gas": values["gas"],
        "supply_total": values["supply_total"],
        "supply_used": values["supply_used"],
        "supply_available_numeric": max(0, values["supply_total"] - values["supply_used"]),
        "worker_supply_cost_numeric": WORKER_SUPPLY_COST,
    }


def _snapshot_at(frame: HxFrame, index: int) -> dict[str, Any]:
    unit = frame.units[index]
    extra = frame.unit_extras[index] if frame.unit_extras is not None else None
    return {
        "unit_id": unit["id"],
        "slot": unit["id"],
        "generation": unit["generation"],
        "type": unit["type"],
        "player": unit["player"],
        "order": unit["order"],
        "completed": unit["completed"],
        "build_type": extra["build_type"] if extra else None,
        "order_unit_type": extra["order_unit_type"] if extra else None,
        "remaining_build_time": extra["remaining_build_time"] if extra else None,
        "current_build_unit_id": extra["current_build_unit_id"] if extra else None,
        "connected_id": extra["connected_id"] if extra else None,
        "order_state": extra["order_state"] if extra else None,
    }


def _unit_snapshot(
    frame: HxFrame | None,
    unit_id: int,
    generation: int | None = None,
) -> dict[str, Any] | None:
    """Resolve a slot, optionally restricted to one HXP3/HXP4 lifecycle."""

    if frame is None:
        return None
    for index, unit in enumerate(frame.units):
        if unit["id"] == unit_id and (
            generation is None or unit["generation"] == generation
        ):
            return _snapshot_at(frame, index)
    return None


def _completion_identity_established(frame: HxFrame, generation: int) -> bool:
    """Only nonzero HXP3/HXP4 generations prove a stable native lifecycle."""

    return frame.protocol in ("HXP3", "HXP4") and generation != 0


def _all_unit_snapshots(frame: HxFrame | None, player: int = 0) -> list[dict[str, Any]]:
    if frame is None:
        return []
    return [
        _snapshot_at(frame, index)
        for index, unit in enumerate(frame.units)
        if unit["player"] == player
    ]


def extract_worker_requests(replies: Iterable[HxReply]) -> list[_Request]:
    """Decode only the three specified native worker-production commands."""

    requests: list[_Request] = []
    for reply in replies:
        # Keep selection semantics local to a batch; an explicit Select is
        # required for attribution rather than assuming selection persists in a
        # capture with potentially missing reply coverage.
        selection: list[int] = []
        for command_index, command in enumerate(reply.commands):
            if command.name == "Select":
                selection = list(command.fields["unit_ids"])
            elif command.name == "SelectAdd":
                for unit_id in command.fields["unit_ids"]:
                    if unit_id not in selection:
                        selection.append(unit_id)
            elif command.name == "SelectRemove":
                removed = set(command.fields["unit_ids"])
                selection = [unit_id for unit_id in selection if unit_id not in removed]
            elif (command.opcode, command.fields.get("unit_type")) in WORKER_REQUESTS:
                requests.append(
                    _Request(
                        emitted_frame=reply.frame,
                        command_index=command_index,
                        command=command,
                        reply=reply,
                        selected_ids=tuple(selection),
                        target_type=command.fields["unit_type"],
                    )
                )
    return requests


def _compatible_producers(frame: HxFrame | None, target_type: int) -> set[int]:
    if frame is None:
        return set()
    return {
        unit["id"]
        for unit in frame.units
        if unit["player"] == 0 and unit["type"] in PRODUCER_TYPES[target_type]
    }


def _attribute_reply_requests(requests: list[_Request], frame: HxFrame | None) -> None:
    """Use direct IDs or a uniquely demonstrated constant capture-ID offset.

    Some historical captures use different unit-ID namespaces in classic
    commands and HXP.  An offset is used only when every selected producer in
    the same reply agrees on exactly one offset.  Otherwise attribution remains
    explicitly unmatched and cannot support a per-larva rejection claim.
    """

    if not requests:
        return
    direct_generations = (
        {unit["id"]: unit["generation"] for unit in frame.units}
        if frame is not None
        else {}
    )
    for request in requests:
        if request.selected_ids and all(
            unit_id in direct_generations for unit_id in request.selected_ids
        ):
            request.producer_ids = list(request.selected_ids)
            request.producer_generations = {
                unit_id: direct_generations[unit_id]
                for unit_id in request.selected_ids
            }
            request.id_match = "direct"
            request.id_offset = 0

    unresolved = [request for request in requests if request.id_match == "unmatched"]
    if not unresolved or frame is None:
        return
    flattened = [
        (request, unit_id)
        for request in unresolved
        for unit_id in request.selected_ids
    ]
    if not flattened or any(not request.selected_ids for request in unresolved):
        return
    first_request, first_id = flattened[0]
    possible_offsets = {
        first_id - producer_id
        for producer_id in _compatible_producers(frame, first_request.target_type)
    }
    valid_offsets: list[int] = []
    for id_offset in possible_offsets:
        used: set[int] = set()
        valid = True
        for request, selected_id in flattened:
            producer_id = selected_id - id_offset
            if producer_id not in _compatible_producers(frame, request.target_type):
                valid = False
                break
            used.add(producer_id)
        if valid and len(used) == len(flattened):
            valid_offsets.append(id_offset)
    if len(valid_offsets) != 1:
        return
    id_offset = valid_offsets[0]
    for request in unresolved:
        request.producer_ids = [unit_id - id_offset for unit_id in request.selected_ids]
        request.producer_generations = {
            unit_id: direct_generations[unit_id]
            for unit_id in request.producer_ids
            if unit_id in direct_generations
        }
        if len(request.producer_generations) != len(request.producer_ids):
            request.producer_ids = []
            request.producer_generations = {}
            continue
        request.id_match = "unique_constant_id_offset"
        request.id_offset = id_offset


def _completion_events(frames: list[HxFrame]) -> list[dict[str, Any]]:
    """Return completed=1 candidates tagged by lifecycle-identity confidence."""

    events: list[dict[str, Any]] = []
    previous = frames[0]
    for current in frames[1:]:
        previous_indices = {
            (unit["id"], unit["generation"]): index
            for index, unit in enumerate(previous.units)
        }
        for index, unit in enumerate(current.units):
            if unit["player"] != 0 or unit["type"] not in WORKER_TYPES or not unit["completed"]:
                continue
            key = (unit["id"], unit["generation"])
            previous_index = previous_indices.get(key)
            old = _snapshot_at(previous, previous_index) if previous_index is not None else None
            if old is not None and old["completed"] and old["type"] == unit["type"]:
                continue
            identity_established = _completion_identity_established(
                current, unit["generation"]
            )
            events.append(
                {
                    "frame": current.frame,
                    "unit_id": unit["id"],
                    "slot": unit["id"],
                    "generation": unit["generation"],
                    "protocol": current.protocol,
                    "identity_established": identity_established,
                    "identity_status": (
                        "established" if identity_established else "inconclusive"
                    ),
                    "unit_type": unit["type"],
                    "unit_existed_previous_frame": any(
                        previous_unit["id"] == unit["id"]
                        for previous_unit in previous.units
                    ),
                    "same_identity_existed_previous_frame": previous_index is not None,
                    "previous": old,
                    "current": _snapshot_at(current, index),
                    "evidence": (
                        "numeric worker state is completed=1 and the same nonzero HXP3/HXP4 (slot,generation) was not completed on the preceding frame"
                        if identity_established
                        else "completed=1 is visible, but legacy or generation-zero identity cannot prove this is a new completion of the same lifecycle"
                    ),
                }
            )
        previous = current
    return events


def _request_base(request: _Request, frames_by_number: dict[int, HxFrame]) -> dict[str, Any]:
    emitted = frames_by_number.get(request.emitted_frame)
    apply_frame = request.emitted_frame + EXPECTED_APPLY_LATENCY
    applied = frames_by_number.get(apply_frame)
    selected_emission = [
        snapshot
        for unit_id in request.producer_ids
        if (
            snapshot := _unit_snapshot(
                emitted, unit_id, request.producer_generations.get(unit_id)
            )
        )
        is not None
    ]
    selected_apply = [
        snapshot
        for unit_id in request.producer_ids
        if (
            snapshot := _unit_snapshot(
                applied, unit_id, request.producer_generations.get(unit_id)
            )
        )
        is not None
    ]
    return {
        "request_id": f"{request.emitted_frame}:{request.command.offset}:{request.command_index}",
        "kind": REQUEST_KIND[request.target_type],
        "emitted_frame": request.emitted_frame,
        "expected_apply_frame": apply_frame,
        "apply_latency_frames": EXPECTED_APPLY_LATENCY,
        "command_offset": request.command.offset,
        "command_index": request.command_index,
        "command_bytes_hex": request.command.raw.hex(),
        "reply_command_bytes_hex": request.reply.command_bytes.hex(),
        "decoded_command": request.command.to_dict(),
        "decoded_reply_commands": [command.to_dict() for command in request.reply.commands],
        "selected_command_unit_ids": list(request.selected_ids),
        "selected_hxp_unit_ids": list(request.producer_ids),
        "selected_hxp_unit_identities": [
            {"slot": unit_id, "generation": request.producer_generations[unit_id]}
            for unit_id in request.producer_ids
            if unit_id in request.producer_generations
        ],
        "unit_id_match": {
            "method": request.id_match,
            "constant_offset": request.id_offset,
        },
        "target_unit_type": request.target_type,
        "mineral_cost": WORKER_COST[request.target_type][0],
        "gas_cost": WORKER_COST[request.target_type][1],
        "supply_cost_numeric": WORKER_SUPPLY_COST,
        "economy_at_emission": _economy(emitted),
        "economy_at_apply": _economy(applied),
        "selected_units_at_emission": selected_emission,
        "selected_units_at_apply": selected_apply,
        "selected_larva_at_emission": selected_emission if request.target_type == DRONE else None,
        "selected_larva_at_apply": selected_apply if request.target_type == DRONE else None,
    }


def _egg_start(snapshot: dict[str, Any] | None, target_type: int) -> bool:
    if snapshot is None or snapshot["completed"]:
        return False
    if snapshot["type"] == EGG:
        return snapshot["build_type"] in (None, target_type)
    return snapshot["type"] == target_type


def _find_zerg_start(
    request: _Request,
    frames_by_number: dict[int, HxFrame],
    end_frame: int,
) -> tuple[int | None, int | None, list[tuple[int, int]]]:
    candidates: list[tuple[int, int]] = []
    starts: list[tuple[int, int]] = []
    emitted_frame = frames_by_number.get(request.emitted_frame)
    emitted_indices = (
        {
            (unit["id"], unit["generation"]): index
            for index, unit in enumerate(emitted_frame.units)
        }
        if emitted_frame is not None
        else {}
    )
    for frame_number in range(request.emitted_frame + EXPECTED_APPLY_LATENCY, end_frame + 1):
        frame = frames_by_number.get(frame_number)
        if frame is None:
            continue
        for index, unit in enumerate(frame.units):
            if unit["player"] != 0:
                continue
            snapshot = _snapshot_at(frame, index)
            if not _egg_start(snapshot, request.target_type):
                continue
            key = (unit["id"], unit["generation"])
            emitted_index = emitted_indices.get(key)
            emitted = (
                _snapshot_at(emitted_frame, emitted_index)
                if emitted_frame is not None and emitted_index is not None
                else None
            )
            if emitted is not None and emitted["type"] == EGG:
                continue  # Do not claim an egg that predated this request.
            candidates.append(key)
            if request.producer_generations.get(unit["id"]) == unit["generation"]:
                starts.append((frame_number, unit["id"]))
    if starts:
        return starts[0][0], starts[0][1], sorted(set(candidates))
    # A unique global chain can be attributed only when this is the sole request
    # for that target at that apply frame.  The caller adds that restriction.
    return None, None, sorted(set(candidates))


def _find_train_start(
    request: _Request,
    frames_by_number: dict[int, HxFrame],
    end_frame: int,
) -> tuple[int | None, int | None, int | None, list[tuple[int, int]]]:
    candidates: list[tuple[int, int]] = []
    for frame_number in range(request.emitted_frame + EXPECTED_APPLY_LATENCY, end_frame + 1):
        frame = frames_by_number.get(frame_number)
        if frame is None:
            continue
        for producer_id in request.producer_ids:
            producer = _unit_snapshot(
                frame, producer_id, request.producer_generations.get(producer_id)
            )
            if producer is None:
                continue
            produced_id = producer["current_build_unit_id"]
            if produced_id == _NO_ID:
                produced_id = None
            if producer["build_type"] == request.target_type and (
                producer["remaining_build_time"] is None
                or producer["remaining_build_time"] > 0
                or produced_id is not None
            ):
                return frame_number, producer_id, produced_id, candidates
        for unit in frame.units:
            if (
                unit["player"] == 0
                and unit["type"] == request.target_type
                and not unit["completed"]
            ):
                emitted = _unit_snapshot(
                    frames_by_number.get(request.emitted_frame),
                    unit["id"],
                    unit["generation"],
                )
                if emitted is not None and emitted["type"] == request.target_type:
                    continue  # Pre-existing incomplete training is not this request.
                candidates.append((unit["id"], unit["generation"]))
    return None, None, None, sorted(set(candidates))


def _native_rule(economy: dict[str, Any] | None, target_type: int) -> str | None:
    if economy is None:
        return None
    if economy["supply_used"] + WORKER_SUPPLY_COST > economy["supply_total"]:
        return "supply_blocked"
    mineral_cost, gas_cost = WORKER_COST[target_type]
    if economy["minerals"] < mineral_cost or economy["gas"] < gas_cost:
        return "insufficient_resources"
    return "no_state_transition"


def _completion_for_unit(
    events_by_unit: dict[tuple[int, int, int], dict[str, Any]],
    unit_id: int,
    generation: int,
    target_type: int,
    start: int,
) -> dict[str, Any] | None:
    event = events_by_unit.get((unit_id, generation, target_type))
    return event if event is not None and event["frame"] >= start else None


def _producer_identities_present(request: _Request, frame: HxFrame | None) -> bool:
    return len(request.producer_generations) == len(request.producer_ids) and all(
        _unit_snapshot(frame, unit_id, generation) is not None
        for unit_id, generation in request.producer_generations.items()
    )


def _evaluate_request(
    request: _Request,
    frames: list[HxFrame],
    requests_at_apply: dict[tuple[int, int], int],
    events_by_unit: dict[tuple[int, int, int], dict[str, Any]],
) -> None:
    frames_by_number = {frame.frame: frame for frame in frames}
    result = _request_base(request, frames_by_number)
    request.result = result
    horizon = frames[-1].frame
    apply_frame = request.emitted_frame + EXPECTED_APPLY_LATENCY
    transition_end = apply_frame + TRANSITION_OBSERVATION_FRAMES
    result["transition_observation_window"] = {
        "first_frame": apply_frame,
        "last_frame": transition_end,
        "post_apply_observation_frames": TRANSITION_OBSERVATION_FRAMES,
    }
    result["completion_evidence"] = {
        "completed": False,
        "frame": None,
        "unit_id": None,
        "slot": None,
        "generation": None,
        "details": "no completed worker transition attributed",
    }
    result["production_start"] = None
    result["completion_window"] = {
        "basis": "not established until a production transition is observed",
        "start_frame": None,
        "observed_remaining_build_time": None,
        "timer_end_frame": None,
        "post_timer_grace_frames": INCOMPLETE_WORKER_GRACE_FRAMES,
        "incomplete_worker_first_frame": None,
        "valid_until_frame": None,
        "expired": False,
    }

    if apply_frame > horizon:
        result.update(
            {
                "status": "awaiting_expected_apply",
                "classification": None,
                "native_rule_subtype": "pending_or_incomplete_evidence",
                "evidence_note": "state capture ends before the expected application frame",
            }
        )
        return

    unique_apply_request = requests_at_apply[(apply_frame, request.target_type)] == 1
    produced_id: int | None = None
    producer_id: int | None = None
    unattributed_candidates: list[tuple[int, int]] = []
    if request.target_type == DRONE:
        start, chain_id, unattributed_candidates = _find_zerg_start(
            request, frames_by_number, min(transition_end, horizon)
        )
        if start is None and unique_apply_request and len(unattributed_candidates) == 1:
            candidate_id, candidate_generation = unattributed_candidates[0]
            candidate_start = next(
                frame_number
                for frame_number in range(apply_frame, min(transition_end, horizon) + 1)
                if _egg_start(
                    _unit_snapshot(
                        frames_by_number.get(frame_number),
                        candidate_id,
                        candidate_generation,
                    ),
                    DRONE,
                )
            )
            if _producer_identities_present(
                request, frames_by_number.get(candidate_start)
            ):
                chain_id = candidate_id
                request.chain_generation = candidate_generation
                start = candidate_start
                result["unit_id_match"]["production_attribution"] = "unique_global_production_chain"
        request.start_frame = start
        request.chain_unit_id = chain_id
        if start is not None and chain_id is not None and request.chain_generation is None:
            started = _unit_snapshot(frames_by_number.get(start), chain_id)
            if started is not None:
                request.chain_generation = started["generation"]
    else:
        start, producer_id, produced_id, unattributed_candidates = _find_train_start(
            request, frames_by_number, min(transition_end, horizon)
        )
        if start is None and unique_apply_request and len(unattributed_candidates) == 1:
            candidate_id, candidate_generation = unattributed_candidates[0]
            candidate_start = next(
                frame_number
                for frame_number in range(apply_frame, min(transition_end, horizon) + 1)
                if (
                    (
                        snapshot := _unit_snapshot(
                            frames_by_number.get(frame_number),
                            candidate_id,
                            candidate_generation,
                        )
                    )
                    is not None
                    and snapshot["type"] == request.target_type
                    and not snapshot["completed"]
                )
            )
            if _producer_identities_present(
                request, frames_by_number.get(candidate_start)
            ):
                produced_id = candidate_id
                request.chain_generation = candidate_generation
                start = candidate_start
                producer_id = request.producer_ids[0] if len(request.producer_ids) == 1 else None
                result["unit_id_match"]["production_attribution"] = "unique_new_incomplete_worker"
        request.start_frame = start
        request.chain_unit_id = produced_id
        if start is not None and produced_id is not None and request.chain_generation is None:
            linked = _unit_snapshot(frames_by_number.get(start), produced_id)
            if linked is not None and linked["type"] == request.target_type:
                request.chain_generation = linked["generation"]
        if start is not None:
            result["production_start"] = {
                "frame": start,
                "producer_unit_id": producer_id,
                "producer_unit_generation": (
                    request.producer_generations.get(producer_id)
                    if producer_id is not None
                    else None
                ),
                "produced_unit_id": produced_id,
                "produced_unit_generation": request.chain_generation,
                "producer": (
                    _unit_snapshot(
                        frames_by_number.get(start),
                        producer_id,
                        request.producer_generations.get(producer_id),
                    )
                    if producer_id is not None
                    else None
                ),
                "worker": (
                    _unit_snapshot(
                        frames_by_number.get(start),
                        produced_id,
                        request.chain_generation,
                    )
                    if produced_id is not None
                    else None
                ),
            }

    if request.start_frame is None:
        producer_identity_end = next(
            (
                frame_number
                for frame_number in range(
                    apply_frame, min(transition_end, horizon) + 1
                )
                if request.producer_generations
                and not _producer_identities_present(
                    request, frames_by_number.get(frame_number)
                )
            ),
            None,
        )
        if producer_identity_end is not None:
            result.update(
                {
                    "status": "producer_identity_changed",
                    "classification": None,
                    "native_rule_subtype": "pending_or_incomplete_evidence",
                    "evidence_note": "the selected producer (slot,generation) disappeared before an attributable production start; replacement, cancellation, destruction, and misapplication cannot be distinguished",
                    "production_chain_end_frame": producer_identity_end,
                }
            )
            return
        if unattributed_candidates and not request.producer_ids:
            result.update(
                {
                    "status": "production_attribution_incomplete",
                    "classification": None,
                    "native_rule_subtype": "pending_or_incomplete_evidence",
                    "unattributed_production_unit_ids": [
                        unit_id for unit_id, _generation in unattributed_candidates
                    ],
                    "unattributed_production_unit_identities": [
                        {"slot": unit_id, "generation": generation}
                        for unit_id, generation in unattributed_candidates
                    ],
                    "evidence_note": "production changed, but the selected producer could not be matched uniquely",
                }
            )
            return
        if transition_end > horizon:
            result.update(
                {
                    "status": "awaiting_transition_observation",
                    "classification": None,
                    "native_rule_subtype": "pending_or_incomplete_evidence",
                    "evidence_note": "state capture ends before the no-transition observation window closes",
                }
            )
            return
        rule = _native_rule(_economy(frames_by_number.get(apply_frame)), request.target_type)
        if rule is None:
            result.update(
                {
                    "status": "apply_state_missing",
                    "classification": None,
                    "native_rule_subtype": "pending_or_incomplete_evidence",
                    "evidence_note": "expected-apply economy/state is unavailable",
                }
            )
            return
        request.failed = True
        request.pending = False
        result.update(
            {
                "status": "no_production_state_transition",
                "classification": "worker_request_rejected_or_misapplied",
                "native_rule_subtype": rule,
                "evidence_note": "no attributable or matching global worker-production transition occurred in the documented application observation window",
            }
        )
        return

    start = request.start_frame
    if request.target_type == DRONE:
        result["production_start"] = {
            "frame": start,
            "producer_unit_id": request.chain_unit_id,
            "producer_unit_generation": request.chain_generation,
            "produced_unit_id": request.chain_unit_id,
            "produced_unit_generation": request.chain_generation,
            "producer": _unit_snapshot(
                frames_by_number.get(start),
                request.chain_unit_id,
                request.chain_generation,
            ),
        }
    chain_id = request.chain_unit_id
    window = result["completion_window"]
    window.update(
        {
            "basis": "observed remaining_build_time plus post-timer grace; first incomplete worker extends grace",
            "start_frame": start,
            "observed_remaining_build_time": (
                result["production_start"]["producer"]["remaining_build_time"]
                if result["production_start"].get("producer")
                else None
            ),
        }
    )
    if window["observed_remaining_build_time"] is not None:
        timer_end = start + window["observed_remaining_build_time"]
        window["timer_end_frame"] = timer_end
        window["valid_until_frame"] = timer_end + INCOMPLETE_WORKER_GRACE_FRAMES
        request.valid_until = window["valid_until_frame"]

    previous_remaining = window["observed_remaining_build_time"]
    disappearance_frame: int | None = None
    worker_seen = request.target_type == DRONE
    last_frame = start
    for frame_number in range(start, horizon + 1):
        last_frame = frame_number
        # A linked produced unit may become visible after the producer's first
        # training state.  Resolve links only through the original producer
        # lifecycle, never through an immediate same-slot replacement.
        if request.target_type != DRONE and chain_id is None and producer_id is not None:
            producer = _unit_snapshot(
                frames_by_number.get(frame_number),
                producer_id,
                request.producer_generations.get(producer_id),
            )
            if producer is not None and producer["current_build_unit_id"] not in (None, _NO_ID):
                chain_id = producer["current_build_unit_id"]
                request.chain_unit_id = chain_id
                result["production_start"]["produced_unit_id"] = chain_id
        if chain_id is None:
            # Training state without a linked unit is still genuine progress.
            # It can only expire by the conservative observed timer window.
            if request.valid_until is not None and frame_number > request.valid_until:
                break
            continue
        snapshot = _unit_snapshot(
            frames_by_number.get(frame_number), chain_id, request.chain_generation
        )
        if snapshot is not None and request.chain_generation is None:
            compatible = (
                snapshot["type"] in (EGG, DRONE)
                if request.target_type == DRONE
                else snapshot["type"] == request.target_type
            )
            if compatible:
                # Bind the first compatible linked unit once.  Later snapshots
                # must retain both slot and generation.
                request.chain_generation = snapshot["generation"]
                result["production_start"]["produced_unit_generation"] = (
                    request.chain_generation
                )
        if snapshot is None:
            if request.target_type == DRONE or worker_seen:
                disappearance_frame = frame_number
                break
            # A producer link can precede the new worker's first visible unit
            # record.  It is not yet a disappearing-worker verdict.
            if request.valid_until is not None and frame_number > request.valid_until:
                break
            continue
        if request.target_type == DRONE and snapshot["type"] not in (EGG, DRONE):
            disappearance_frame = frame_number
            break
        if request.target_type != DRONE and snapshot["type"] != request.target_type:
            if worker_seen:
                disappearance_frame = frame_number
                break
            # Likewise, an unresolved placeholder before the worker appears is
            # incomplete evidence, not an instant cancellation claim.
            if request.valid_until is not None and frame_number > request.valid_until:
                break
            continue
        worker_seen = True
        if snapshot["type"] == request.target_type and snapshot["completed"]:
            if request.chain_generation is None:
                raise AssertionError("compatible linked worker must have a bound generation")
            current_frame = frames_by_number.get(frame_number)
            if current_frame is None or not _completion_identity_established(
                current_frame, request.chain_generation
            ):
                # HXP2/HXPF and generation-zero HXP3/HXP4 cannot distinguish a real
                # completion from a different lifecycle reusing the same slot.
                # Never turn that ambiguity into completion or rejection proof.
                request.pending = True
                request.failed = False
                result["completion_evidence"] = {
                    "completed": False,
                    "frame": frame_number,
                    "unit_id": chain_id,
                    "slot": chain_id,
                    "generation": request.chain_generation,
                    "identity_status": "inconclusive",
                    "current": snapshot,
                    "details": "completed=1 is visible, but the capture cannot establish that it belongs to the requested production lifecycle",
                }
                result.update(
                    {
                        "status": "production_identity_inconclusive",
                        "classification": None,
                        "native_rule_subtype": "pending_or_incomplete_evidence",
                        "evidence_note": "legacy or zero/unchanged-generation evidence cannot distinguish same-identity completion from same-slot replacement; no completion or fault is inferred",
                        "identity_inconclusive_frame": frame_number,
                    }
                )
                return
            completion = _completion_for_unit(
                events_by_unit,
                chain_id,
                request.chain_generation,
                request.target_type,
                apply_frame,
            )
            if completion is None:
                completion = {
                    "frame": frame_number,
                    "unit_id": chain_id,
                    "slot": chain_id,
                    "generation": request.chain_generation,
                    "protocol": current_frame.protocol,
                    "identity_established": True,
                    "identity_status": "established",
                    "unit_type": request.target_type,
                    "previous": None,
                    "current": snapshot,
                    "evidence": "linked worker is numerically completed=1",
                }
            request.completion_frame = completion["frame"]
            request.pending = False
            result["completion_evidence"] = {
                "completed": True,
                **completion,
                "attribution": "linked production (slot,generation) identity",
            }
            result.update(
                {
                    "status": "completed",
                    "classification": None,
                    "native_rule_subtype": None,
                    "evidence_note": "completed worker observed; this request is not rejected or misapplied",
                }
            )
            return
        remaining = snapshot["remaining_build_time"]
        if remaining is not None:
            if window["observed_remaining_build_time"] is None:
                window["observed_remaining_build_time"] = remaining
                window["timer_end_frame"] = frame_number + remaining
                window["valid_until_frame"] = (
                    frame_number + remaining + INCOMPLETE_WORKER_GRACE_FRAMES
                )
                request.valid_until = window["valid_until_frame"]
            elif previous_remaining is not None and remaining > previous_remaining:
                # A reset/increasing timer extends evidence instead of turning
                # paused or unusual native progress into a fixed-deadline fault.
                proposed = frame_number + remaining + INCOMPLETE_WORKER_GRACE_FRAMES
                if request.valid_until is None or proposed > request.valid_until:
                    request.valid_until = proposed
                    window["valid_until_frame"] = proposed
            previous_remaining = remaining
        if snapshot["type"] == request.target_type and not snapshot["completed"]:
            if window["incomplete_worker_first_frame"] is None:
                window["incomplete_worker_first_frame"] = frame_number
                proposed = frame_number + INCOMPLETE_WORKER_GRACE_FRAMES
                if request.valid_until is None or proposed > request.valid_until:
                    request.valid_until = proposed
                    window["valid_until_frame"] = proposed
        if request.valid_until is not None and frame_number > request.valid_until:
            break

    if disappearance_frame is not None:
        # Disappearance proves interruption, not whether native code rejected or
        # misapplied the request.  Cancellation and unit loss remain possible.
        request.pending = True
        result.update(
            {
                "status": "production_chain_disappeared",
                "classification": None,
                "native_rule_subtype": "pending_or_incomplete_evidence",
                "evidence_note": "production started but its linked unit disappeared or changed unexpectedly before completed=1; capture cannot distinguish cancellation, loss, or misapplication, so no fault label is forced",
                "production_chain_end_frame": disappearance_frame,
            }
        )
        return

    window["valid_until_frame"] = request.valid_until
    window["expired"] = request.valid_until is not None and last_frame > request.valid_until
    if window["expired"]:
        # Timer plus grace bounds how long production remains ordinarily valid;
        # it does not prove rejection.  Paused native timers, cancellation,
        # destruction, and misapplication remain distinguishable only by direct
        # evidence, so expiry alone must stay inconclusive.
        request.failed = False
        request.pending = True
        result.update(
            {
                "status": "completion_window_expired_inconclusive",
                "classification": None,
                "native_rule_subtype": "pending_or_incomplete_evidence",
                "evidence_note": "the observed-timer/incomplete-worker grace window expired without completion, but expiry alone does not prove cancellation, destruction, native rejection, or misapplication",
            }
        )
        return

    result.update(
        {
            "status": "pending_valid_production",
            "classification": None,
            "native_rule_subtype": "pending_or_incomplete_evidence",
            "evidence_note": "production started and its valid completion window has not passed; not a stalled/misapplied request",
        }
    )


def _missing_reply_ranges(replies: list[HxReply], start: int, end_exclusive: int) -> list[list[int]]:
    if end_exclusive <= start:
        return []
    last = replies[-1].frame if replies else -1
    missing_start = max(start, last + 1)
    if missing_start >= end_exclusive:
        return []
    return [[missing_start, end_exclusive - 1]]


def _public_request(request: _Request) -> dict[str, Any]:
    return request.result


def _stall(
    start: int,
    end_exclusive: int,
    threshold: int,
    frames: list[HxFrame],
    replies: list[HxReply],
    requests: list[_Request],
    completion: dict[str, Any] | None,
    identity_inconclusive_events: list[dict[str, Any]],
) -> dict[str, Any]:
    frames_by_number = {frame.frame: frame for frame in frames}
    last_observed = end_exclusive - 1
    ambiguous_here = [
        event for event in identity_inconclusive_events
        if start <= event["frame"] < end_exclusive
    ]
    relevant = [
        request
        for request in requests
        if request.emitted_frame < end_exclusive
        and (
            start <= request.emitted_frame
            or (request.start_frame is not None and request.start_frame < end_exclusive
                and (request.valid_until is None or request.valid_until >= start))
            or request.completion_frame == end_exclusive
        )
    ]
    missing = _missing_reply_ranges(replies, start, end_exclusive)
    coverage = {
        "complete": not missing,
        "missing_frame_ranges": missing,
        "required_first_frame": start,
        "required_last_frame": last_observed,
    }
    status = "inconclusive"
    classification: str | None = None
    subtype: str | None = "pending_or_incomplete_evidence"
    primary: _Request | None = None

    completed_here = [
        request for request in relevant
        if request.completion_frame is not None and request.completion_frame == end_exclusive
    ]
    active_pending = [
        request for request in relevant
        if request.result["status"] == "pending_valid_production"
        and request.start_frame is not None
        and (request.valid_until is None or request.valid_until >= last_observed)
    ]
    inconclusive = [
        request for request in relevant
        if request.result["status"] in (
            "completion_window_expired_inconclusive",
            "production_identity_inconclusive",
            "production_chain_disappeared",
            "producer_identity_changed",
            "production_attribution_incomplete",
            "apply_state_missing",
        )
    ]
    failed = sorted(
        [request for request in relevant if request.failed],
        key=lambda request: (request.emitted_frame, request.command.offset),
    )
    if failed:
        # One valid pending request must not hide a different request whose
        # native rejection is already proven.  Per-request evidence retains the
        # pending request's non-fault status as well.
        status = "stalled"
        primary = failed[0]
        classification = "worker_request_rejected_or_misapplied"
        subtype = primary.result["native_rule_subtype"]
    elif completed_here:
        status = "production_completed"
        classification = None
        subtype = None
        primary = completed_here[0]
    elif active_pending:
        status = "pending_valid_production"
        primary = active_pending[0]
    elif inconclusive:
        status = "inconclusive"
        primary = inconclusive[0]
    elif ambiguous_here:
        # An unprovable completion candidate is neither production evidence nor
        # evidence that a qualifying request was absent.
        status = "inconclusive"
    elif relevant:
        # Known requests still inside their application/evidence windows must
        # not be coerced into either fault label.
        status = "awaiting_request_evidence"
        primary = relevant[0]
    elif not missing:
        status = "stalled"
        classification = "no_worker_request"
        subtype = _native_rule(_economy(frames_by_number.get(last_observed)), DRONE)
        if subtype is None:
            status = "inconclusive"
            subtype = "pending_or_incomplete_evidence"
    # With no proven request and missing reply coverage, absence of decoded
    # commands is not evidence of absence.

    economy_at_classification = _economy(frames_by_number.get(last_observed))
    answer: dict[str, Any] = {
        "start_frame": start,
        "end_frame_exclusive": end_exclusive,
        "frames_without_completed_worker_production": end_exclusive - start,
        "threshold_frames": threshold,
        "status": status,
        "classification": classification,
        "native_rule_subtype": subtype,
        "is_actionable_stall": status == "stalled",
        "emitted_frame": primary.emitted_frame if primary else None,
        "expected_apply_frame": (
            primary.emitted_frame + EXPECTED_APPLY_LATENCY if primary else None
        ),
        "command_bytes_hex": primary.command.raw.hex() if primary else None,
        "decoded_command": primary.command.to_dict() if primary else None,
        "reply_command_bytes_hex": primary.reply.command_bytes.hex() if primary else None,
        "decoded_reply_commands": (
            [command.to_dict() for command in primary.reply.commands] if primary else []
        ),
        "economy_at_emission": primary.result["economy_at_emission"] if primary else None,
        "economy_at_apply": primary.result["economy_at_apply"] if primary else None,
        "economy_at_classification_frame": economy_at_classification,
        "selected_larva_at_emission": (
            primary.result["selected_larva_at_emission"] if primary else None
        ),
        "selected_larva_at_apply": primary.result["selected_larva_at_apply"] if primary else None,
        "completion_evidence": completion or (
            primary.result["completion_evidence"] if primary else {
                "completed": False,
                "frame": None,
                "details": "no completed worker production closes this interval",
            }
        ),
        "reply_coverage": coverage,
        "identity_inconclusive_completion_events": ambiguous_here,
        "worker_requests": [_public_request(request) for request in relevant],
        "worker_request_count": len(relevant),
        "unit_state_at_start": _all_unit_snapshots(frames_by_number.get(start)),
        "unit_state_at_classification_frame": _all_unit_snapshots(
            frames_by_number.get(last_observed)
        ),
        "evidence_note": (
            "valid production is pending or completed; no rejected/misapplied verdict is made"
            if status in ("pending_valid_production", "production_completed", "awaiting_request_evidence")
            else "no qualifying worker request is present in complete expected-frame reply coverage"
            if classification == "no_worker_request"
            else "evidence is incomplete; no two-label fault classification is forced"
            if classification is None
            else "at least one qualifying worker request has rejected/misapplied evidence"
        ),
    }
    return answer


def analyze_worker_stalls(
    frames_data: bytes,
    replies_raw_data: bytes,
    *,
    threshold: int = 240,
) -> dict[str, Any]:
    """Analyze worker production without changing any captured numeric state."""

    if isinstance(threshold, bool) or not isinstance(threshold, int) or threshold <= 0:
        raise WorkerStallError("threshold must be a positive integer number of frames")
    frames, frame_selection = select_live_frames(frames_data)
    replies, reply_selection = select_live_replies(replies_raw_data)
    frames_by_number = {frame.frame: frame for frame in frames}
    horizon = frames[-1].frame

    all_requests = extract_worker_requests(replies)
    by_reply: dict[int, list[_Request]] = {}
    for request in all_requests:
        by_reply.setdefault(request.emitted_frame, []).append(request)
    for emitted, group in by_reply.items():
        _attribute_reply_requests(group, frames_by_number.get(emitted))

    # Requests beyond numeric state coverage are counted but cannot be given a
    # production verdict.  This avoids enormous unsupported histories when a
    # reply file is much longer than its frames file.
    requests = [request for request in all_requests if request.emitted_frame <= horizon]
    completion_candidates = _completion_events(frames)
    events = [event for event in completion_candidates if event["identity_established"]]
    identity_inconclusive_events = [
        event for event in completion_candidates if not event["identity_established"]
    ]
    events_by_unit: dict[tuple[int, int, int], dict[str, Any]] = {}
    for event in events:
        # Slots can be reused.  Keep the first numeric completion for each full
        # (slot,generation,type) identity rather than merging lifecycles.
        events_by_unit.setdefault(
            (event["unit_id"], event["generation"], event["unit_type"]), event
        )
    apply_counts: dict[tuple[int, int], int] = {}
    for request in requests:
        key = (request.emitted_frame + EXPECTED_APPLY_LATENCY, request.target_type)
        apply_counts[key] = apply_counts.get(key, 0) + 1
    for request in requests:
        _evaluate_request(request, frames, apply_counts, events_by_unit)

    stalls: list[dict[str, Any]] = []
    start = frames[0].frame
    for completion in events:
        frame_number = completion["frame"]
        if frame_number - start >= threshold:
            stalls.append(
                _stall(
                    start,
                    frame_number,
                    threshold,
                    frames,
                    replies,
                    requests,
                    completion,
                    identity_inconclusive_events,
                )
            )
        start = frame_number + 1
    end_exclusive = horizon + 1
    if end_exclusive - start >= threshold:
        stalls.append(
            _stall(
                start,
                end_exclusive,
                threshold,
                frames,
                replies,
                requests,
                None,
                identity_inconclusive_events,
            )
        )

    beyond = [request for request in all_requests if request.emitted_frame > horizon]
    reply_missing_to_horizon = _missing_reply_ranges(replies, 0, end_exclusive)
    incomplete_reasons: list[str] = []
    if end_exclusive <= threshold:
        incomplete_reasons.append(
            f"only {end_exclusive} live state frames are available; no {threshold}-frame interval can be established"
        )
    if reply_missing_to_horizon:
        incomplete_reasons.append("expected-frame reply coverage does not span the live state stream")
    if beyond:
        incomplete_reasons.append(
            f"{len(beyond)} worker requests occur after the last numeric state frame and are counted but not classified"
        )
    if identity_inconclusive_events:
        incomplete_reasons.append(
            "one or more completed-worker candidates lack provable lifecycle identity"
        )
    if any(request.pending for request in requests):
        incomplete_reasons.append(
            "one or more worker requests remain pending or have inconclusive production evidence"
        )

    actionable = [stall for stall in stalls if stall["is_actionable_stall"]]
    result = {
        "schema": "pluto-diff-worker-stalls/v1",
        "status": "ok",
        "evidence_status": "incomplete" if incomplete_reasons else "complete",
        "threshold_frames": threshold,
        "player_slot": 0,
        "rules": {
            "worker_requests": [
                {"opcode": opcode, "unit_type": unit_type, "kind": REQUEST_KIND[unit_type]}
                for opcode, unit_type in sorted(WORKER_REQUESTS)
            ],
            "expected_apply_latency_frames": EXPECTED_APPLY_LATENCY,
            "transition_observation_frames_after_apply": TRANSITION_OBSERVATION_FRAMES,
            "incomplete_worker_grace_frames": INCOMPLETE_WORKER_GRACE_FRAMES,
            "worker_supply_cost_numeric": WORKER_SUPPLY_COST,
            "completion_rule": "completed=1 numeric worker transition on a provable nonzero HXP3/HXP4 (slot,generation); eggs and incomplete workers are pending, not completions",
            "identity_rule": "producer links, production chains, and completions retain (slot,generation); generation replacement terminates the old chain without completing it; HXP2/HXPF and generation-zero candidates are identity-inconclusive, never completion or rejection proof",
            "completion_window_rule": "observed remaining_build_time plus grace; first incomplete worker extends grace; increasing timers extend evidence; absent timers cannot force expiry; expiry alone is inconclusive, not rejection evidence",
            "uncertainty_rule": "classification is null with pending_or_incomplete_evidence when the capture cannot prove either fault label",
        },
        "stream_selection": {
            "frames": frame_selection,
            "replies_raw": reply_selection,
        },
        "coverage": {
            "state_first_frame": frames[0].frame,
            "state_last_frame": horizon,
            "state_frame_count": len(frames),
            "state_frames_contiguous": True,
            "reply_first_frame": replies[0].frame if replies else None,
            "reply_last_frame": replies[-1].frame if replies else None,
            "reply_frame_count": len(replies),
            "reply_missing_ranges_through_state_horizon": reply_missing_to_horizon,
            "worker_requests_after_state_horizon": len(beyond),
            "incomplete_reasons": incomplete_reasons,
        },
        "completed_worker_production_events": events,
        "identity_inconclusive_completion_events": identity_inconclusive_events,
        "worker_requests": [_public_request(request) for request in requests],
        "worker_request_summary": {
            "decoded_total_in_selected_replies": len(all_requests),
            "within_state_coverage": len(requests),
            "after_state_coverage": len(beyond),
            "completed": sum(request.completion_frame is not None for request in requests),
            "rejected_or_misapplied": sum(request.failed for request in requests),
            "pending_or_incomplete": sum(request.pending for request in requests),
        },
        "stalls": stalls,
        "summary": {
            "qualifying_intervals": len(stalls),
            "actionable_stalls": len(actionable),
            "no_worker_request": sum(
                stall["classification"] == "no_worker_request" for stall in stalls
            ),
            "worker_request_rejected_or_misapplied": sum(
                stall["classification"] == "worker_request_rejected_or_misapplied"
                for stall in stalls
            ),
            "pending_or_completed_intervals": sum(
                not stall["is_actionable_stall"] for stall in stalls
            ),
        },
    }
    return result


def analyze_worker_stall_files(
    frames_path: str | os.PathLike[str],
    replies_raw_path: str | os.PathLike[str],
    *,
    threshold: int = 240,
) -> dict[str, Any]:
    """Read two existing files; never starts or attaches to another process."""

    return analyze_worker_stalls(
        Path(frames_path).read_bytes(),
        Path(replies_raw_path).read_bytes(),
        threshold=threshold,
    )


def human_report(result: dict[str, Any]) -> str:
    """Render a compact report while retaining exact numeric evidence in JSON."""

    coverage = result["coverage"]
    streams = result["stream_selection"]
    lines = [
        f"status: {result['status']} ({result['evidence_status']} evidence)",
        f"threshold: {result['threshold_frames']} frames without completed worker production",
        (
            f"live state: frames {coverage['state_first_frame']}..{coverage['state_last_frame']} "
            f"({coverage['state_frame_count']} records), selected record "
            f"{streams['frames']['selected_start_record']} of {streams['frames']['total_records']}"
        ),
        (
            f"live replies: frames {coverage['reply_first_frame']}..{coverage['reply_last_frame']} "
            f"({coverage['reply_frame_count']} strict expected-frame records); "
            f"rejected magic candidates={streams['replies_raw'].get('candidates_rejected', 0)}"
        ),
        (
            "worker requests: "
            f"{result['worker_request_summary']['within_state_coverage']} within state coverage, "
            f"{result['worker_request_summary']['rejected_or_misapplied']} rejected/misapplied, "
            f"{result['worker_request_summary']['pending_or_incomplete']} pending/incomplete, "
            f"{result['worker_request_summary']['after_state_coverage']} beyond state coverage"
        ),
    ]
    for reason in coverage["incomplete_reasons"]:
        lines.append(f"coverage note: {reason}")
    for request in result["worker_requests"]:
        lines.append(
            (
                f"request {request['request_id']}: {request['kind']} emitted={request['emitted_frame']} "
                f"expected_apply={request['expected_apply_frame']} status={request['status']} "
                f"classification={request['classification']} subtype={request['native_rule_subtype']}"
            )
        )
        lines.append(
            f"  economy emission={request['economy_at_emission']} apply={request['economy_at_apply']}"
        )
        lines.append(
            f"  selected at apply={request['selected_units_at_apply']} completion={request['completion_evidence']}"
        )
    lines.append(f"qualifying intervals: {result['summary']['qualifying_intervals']}")
    for index, stall in enumerate(result["stalls"]):
        lines.append(
            (
                f"stall[{index}]: [{stall['start_frame']}, {stall['end_frame_exclusive']}) "
                f"frames={stall['frames_without_completed_worker_production']} status={stall['status']} "
                f"classification={stall['classification']} subtype={stall['native_rule_subtype']}"
            )
        )
        lines.append(
            f"  emitted={stall['emitted_frame']} expected_apply={stall['expected_apply_frame']} "
            f"economy_at_classification={stall['economy_at_classification_frame']}"
        )
        lines.append(f"  completion={stall['completion_evidence']}")
    if not result["stalls"]:
        lines.append("no threshold-length no-completion interval is established by the numeric state capture")
    return "\n".join(lines) + "\n"
