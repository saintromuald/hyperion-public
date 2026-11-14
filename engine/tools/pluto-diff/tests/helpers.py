from __future__ import annotations

import copy
import hashlib
from pathlib import Path
import struct
from typing import Any, Callable

from pluto_diff.trace import TraceWriter


EMPTY = b""
SELECT_9363 = struct.pack("<BBH", 0x09, 1, 9363)
STOP = b"\x1a\x00"
LOBBY_ONLY_COMMANDS = {
    0x3C: b"\x3c",
    0x3E: b"\x3e\x00\x00\x00\x00\x00",
    0x41: b"\x41\x00\x00",
    0x44: b"\x44\x00\x00",
}


def make_config(**changes: Any) -> dict[str, Any]:
    config: dict[str, Any] = {
        "engine_hash": hashlib.sha256(b"test-engine").hexdigest(),
        "map": {"name": "Differential (1x1).scm", "sha256": hashlib.sha256(b"map").hexdigest()},
        "races": ["Zerg", "Terran"],
        "seed": 0x12345678,
        "bandit": {"arm": 2, "history_hash": hashlib.sha256(b"bandit").hexdigest()},
        "inference_hash": hashlib.sha256(b"inference").hexdigest(),
        "pluto_dll_hash": hashlib.sha256(b"pluto-dll").hexdigest(),
        "bridge_hash": hashlib.sha256(b"pluto-bridge").hexdigest(),
        "opponent": {"name": "synthetic-opponent", "hash": hashlib.sha256(b"opponent").hexdigest()},
    }
    config.update(changes)
    return config


def make_frame(
    frame: int,
    *,
    protocol: bytes = b"HXP3",
    race: int = 0,
    minerals: int = 50,
    tile: int = 0x00010203,
) -> bytes:
    # One player, no units, one tile.  This is sufficient to exercise every
    # transport framing branch without any game or inference process.
    header = struct.pack("<4s5I", protocol, frame, 0, 1, 1, 1)
    player = struct.pack("<7I", race, minerals, 0, 8, 4, 32, 64)
    if protocol == b"HXP4":
        header += struct.pack("<II", 78, 119)
        player += bytes(119)
    if protocol in (b"HXP2", b"HXP3", b"HXP4"):
        tail = struct.pack("<I", tile)
    elif protocol == b"HXPF":
        tail = b"\x01\x01"
    else:
        raise AssertionError(protocol)
    return header + player + tail


def make_unit_frame(
    frame: int,
    *,
    protocol: bytes = b"HXP3",
    order: int = 5,
    build_queue: tuple[int, int, int, int, int] = (228, 228, 228, 228, 228),
    remaining_research_time: int = 0,
    tech_type: int = 44,
    upgrade_type: int = 61,
    player_tech: bytes | None = None,
) -> bytes:
    unit = struct.pack(
        "<9I4B",
        7,
        37,
        0,
        100,
        200,
        500,
        50,
        25,
        order,
        1,
        1,
        255,
        0x80,
    )
    extra = b""
    if protocol in (b"HXP3", b"HXP4"):
        extra = struct.pack(
            "<5I2i4I5H10B",
            0x12345678,
            1,
            2,
            3,
            4,
            -123,
            456,
            build_queue[2],
            37,
            10,
            7,
            320,
            640,
            960,
            1280,
            99,
            2,
            3,
            4,
            5,
            6,
            7,
            255,
            9,
            1,
            10,
        )
    if protocol == b"HXP4":
        extra += struct.pack(
            "<6H2B",
            *build_queue,
            remaining_research_time,
            tech_type,
            upgrade_type,
        )
    header = struct.pack("<4s5I", protocol, frame, 1, 2, 1, 1)
    if protocol == b"HXP4":
        header += struct.pack("<II", 78, 119)
    player = struct.pack("<7I", 0, 50, 0, 8, 4, 32, 64)
    if protocol == b"HXP4":
        player += player_tech if player_tech is not None else bytes(119)
    tiles = bytes(range(8)) if protocol in (b"HXP2", b"HXP3", b"HXP4") else b"\x03\x00"
    return header + unit + extra + player + tiles


def make_reply(frame: int, commands: bytes = EMPTY, *, win: int = 0) -> bytes:
    return struct.pack("<4sII", b"HXRP", frame, len(commands)) + commands + struct.pack("<I", win)


def make_observation(frame: int) -> bytes:
    # Opaque synthetic inference-request content, not an HXP frame and not a
    # claimed PLSM layout.  The harness compares these bytes without parsing.
    return b"synthetic-request\x00\xff" + struct.pack("<I", frame)


def make_model_response(frame: int) -> bytes:
    # Likewise opaque pre-DLL inference output, deliberately not an HXRP reply.
    return b"synthetic-response\x00\xfe" + struct.pack("<I", frame)


def base_unit_state() -> dict[str, Any]:
    return {
        "7": {
            "exists": 1,
            "type": 37,
            "player": 0,
            "x": 100,
            "y": 200,
            "hp": 500,
            "order": 5,
            "cooldown": 0,
        }
    }


def make_trace(
    root: Path,
    *,
    frame_count: int = 5,
    config: dict[str, Any] | None = None,
    mutate: Callable[[int, dict[str, Any]], None] | None = None,
    status: str = "complete",
    finish: bool = True,
) -> Path:
    writer = TraceWriter.create(root, copy.deepcopy(config or make_config()))
    for frame in range(frame_count):
        data: dict[str, Any] = {
            "frame": frame,
            "state_input": make_frame(frame),
            "observation": make_observation(frame),
            "game_data": {
                "frame": frame,
                "random_seed": 0x12345678,
                "players": [{"race": 0, "minerals": 50, "gas": 0}],
                "latency": 4,
            },
            "model_response": make_model_response(frame),
            "bridge_reply": make_reply(frame),
            "emitted_commands": EMPTY,
            "emitted_frame": frame,
            "received_commands": EMPTY,
            "received_frame": frame + 1,
            "accepted_commands": EMPTY,
            "accepted_frame": frame + 3,
            "unit_state": base_unit_state(),
            "observation_of_change": [],
        }
        if mutate is not None:
            mutate(frame, data)
        writer.append_frame(**data)
    if finish:
        writer.finish(status=status, message="synthetic test trace" if status == "complete" else "partial")
    else:
        writer.close()
    return root
