"""Strict, binary-safe HXP2/HXP3/HXP4/HXPF frame and HXRP reply parsers."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
from typing import Any

from .commands import ClassicCommand, parse_in_game_commands


class HxpError(ValueError):
    """Base error for malformed HXP transport bytes."""


class HxpTruncatedError(HxpError):
    """A record header or declared payload is incomplete."""


class HxpFormatError(HxpError):
    """A record has invalid magic, bounds, or trailing bytes."""


_HEADER = struct.Struct("<4s5I")
_HXP4_HEADER = struct.Struct("<4s7I")
_UNIT = struct.Struct("<9I4B")
_EXTRA = struct.Struct("<5I2i4I5H10B")
_LATE_EXTRA = struct.Struct("<6H2B")  # five queue entries, shared timer, tech, upgrade
_PLAYER = struct.Struct("<7I")
_PLAYER_TECH = struct.Struct("<44s61s6s8s")
_REPLY_HEADER = struct.Struct("<4sII")
_REPLY_TAIL = struct.Struct("<I")
_END = struct.Struct("<4sII")

FRAME_MAGICS = {b"HXP2", b"HXP3", b"HXP4", b"HXPF"}
REPLY_MAGIC = b"HXRP"
END_MAGIC = b"HXPE"
HXP4_EXTRA_BYTES = 78
HXP4_PLAYER_TECH_BYTES = 119
UNIT_TYPE_NONE = 228
TECH_TYPE_NONE = 44
UPGRADE_TYPE_NONE = 61
RESEARCH_TECH_ORDER = 75
UPGRADE_ORDER = 76
MAX_UNITS = 9999
MAX_PLAYERS = 12
MAX_MAP_DIM = 256
MAX_REPLY_COMMAND_BYTES = 0xFFFF

assert _HEADER.size == 24 and _HXP4_HEADER.size == 32
assert _EXTRA.size == 64 and _LATE_EXTRA.size == HXP4_EXTRA_BYTES - _EXTRA.size
assert _PLAYER_TECH.size == HXP4_PLAYER_TECH_BYTES

_UNIT_FIELDS = (
    "id",
    "type",
    "player",
    "x",
    "y",
    "hp",
    "shields",
    "energy",
    "order",
    "visible",
    "completed",
    "direction",
    "pad",
)
_EXTRA_FIELDS = (
    "status_flags",
    "order_target_id",
    "move_target_id",
    "connected_id",
    "current_build_unit_id",
    "speed_x",
    "speed_y",
    "build_type",
    "previous_type",
    "resource_count",
    "order_unit_type",
    "move_target_x",
    "move_target_y",
    "order_target_x",
    "order_target_y",
    "remaining_build_time",
    "build_queue_slot",
    "order_state",
    "ground_cooldown",
    "air_cooldown",
    "spell_cooldown",
    "movement_flags",
    "current_direction",
    "order_flags",
    "detected",
    "main_order_timer",
)
_PLAYER_FIELDS = (
    "race",
    "minerals",
    "gas",
    "supply_total",
    "supply_used",
    "start_x",
    "start_y",
)


def _numeric_dict(names: tuple[str, ...], values: tuple[Any, ...]) -> dict[str, Any]:
    return dict(zip(names, values))


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class HxFrame:
    protocol: str
    frame: int
    map_width: int
    map_height: int
    units: tuple[dict[str, int], ...]
    unit_extras: tuple[dict[str, Any], ...] | None
    players: tuple[dict[str, int], ...]
    player_tech: tuple[dict[str, tuple[int, ...]], ...] | None
    tile_flags: bytes | None
    seen: bytes | None
    explored: bytes | None
    raw: bytes

    def to_dict(self) -> dict[str, Any]:
        """Return metadata/numeric fields, hashing rather than flattening planes."""

        return {
            "protocol": self.protocol,
            "frame": self.frame,
            "unit_count": len(self.units),
            "map_width": self.map_width,
            "map_height": self.map_height,
            "player_count": len(self.players),
            "units": list(self.units),
            "unit_extras": list(self.unit_extras) if self.unit_extras is not None else None,
            "players": list(self.players),
            "player_tech": (
                [
                    {name: list(values) for name, values in tech.items()}
                    for tech in self.player_tech
                ]
                if self.player_tech is not None
                else None
            ),
            "tile_flags_sha256": _sha(self.tile_flags) if self.tile_flags is not None else None,
            "seen_sha256": _sha(self.seen) if self.seen is not None else None,
            "explored_sha256": _sha(self.explored) if self.explored is not None else None,
        }


@dataclass(frozen=True)
class HxReply:
    frame: int
    command_bytes: bytes
    win: int
    commands: tuple[ClassicCommand, ...]
    raw: bytes

    def to_dict(self) -> dict[str, Any]:
        return {
            "magic": "HXRP",
            "frame": self.frame,
            "command_byte_count": len(self.command_bytes),
            "win": self.win,
            "commands": [command.to_dict() for command in self.commands],
        }


@dataclass(frozen=True)
class HxEnd:
    winner: int
    reserved: int
    raw: bytes


def _take(data: bytes, offset: int, size: int, what: str) -> tuple[bytes, int]:
    end = offset + size
    if end > len(data):
        raise HxpTruncatedError(
            f"truncated {what}: need bytes {offset}..{end}, input has {len(data)}"
        )
    return data[offset:end], end


def _parse_frame_at(data: bytes, offset: int) -> tuple[HxFrame, int]:
    header_bytes, after_header = _take(data, offset, _HEADER.size, "HXP frame header")
    magic, frame, unit_count, map_width, map_height, player_count = _HEADER.unpack(header_bytes)
    if magic not in FRAME_MAGICS:
        raise HxpFormatError(f"invalid HXP frame magic {magic!r} at byte {offset}")
    if unit_count > MAX_UNITS:
        raise HxpFormatError(f"HXP unit count {unit_count} exceeds {MAX_UNITS} at byte {offset}")
    if player_count > MAX_PLAYERS:
        raise HxpFormatError(
            f"HXP player count {player_count} exceeds {MAX_PLAYERS} at byte {offset}"
        )
    if not (1 <= map_width <= MAX_MAP_DIM and 1 <= map_height <= MAX_MAP_DIM):
        raise HxpFormatError(
            f"invalid HXP map dimensions {map_width}x{map_height} at byte {offset}"
        )
    protocol = magic.decode("ascii")
    cursor = after_header
    if magic == b"HXP4":
        size_words, cursor = _take(data, cursor, 8, "HXP4 explicit section sizes")
        extra_size, player_tech_size = struct.unpack("<II", size_words)
        if extra_size != HXP4_EXTRA_BYTES or player_tech_size != HXP4_PLAYER_TECH_BYTES:
            raise HxpFormatError(
                f"HXP4 section sizes {extra_size}/{player_tech_size} do not equal "
                f"{HXP4_EXTRA_BYTES}/{HXP4_PLAYER_TECH_BYTES} at byte {offset}"
            )

    unit_bytes, cursor = _take(data, cursor, unit_count * _UNIT.size, "HXP units")

    def decode_unit(values: tuple[int, ...]) -> dict[str, int]:
        unit = _numeric_dict(_UNIT_FIELDS, values)
        # The final former pad byte is the native GetUnitID() >> 11 generation.
        # Keep "pad" as a compatibility alias for existing diff consumers.
        unit["generation"] = unit["pad"]
        return unit

    units = tuple(decode_unit(values) for values in _UNIT.iter_unpack(unit_bytes))
    extras: tuple[dict[str, Any], ...] | None = None
    if magic in (b"HXP3", b"HXP4"):
        record_size = _EXTRA.size if magic == b"HXP3" else HXP4_EXTRA_BYTES
        extra_bytes, cursor = _take(
            data, cursor, unit_count * record_size, f"{protocol} unit extras"
        )
        decoded_extras: list[dict[str, Any]] = []
        for index in range(unit_count):
            record = extra_bytes[index * record_size : (index + 1) * record_size]
            extra = _numeric_dict(_EXTRA_FIELDS, _EXTRA.unpack(record[: _EXTRA.size]))
            if magic == b"HXP4":
                late = _LATE_EXTRA.unpack(record[_EXTRA.size :])
                extra.update(
                    build_queue=tuple(late[:5]),
                    remaining_research_time=late[5],
                    tech_type=late[6],
                    upgrade_type=late[7],
                )
                slot = extra["build_queue_slot"]
                if slot >= 5:
                    raise HxpFormatError(f"HXP4 build queue slot {slot} at unit {index}")
                queue = extra["build_queue"]
                if any(unit_type > UNIT_TYPE_NONE for unit_type in queue):
                    raise HxpFormatError(f"HXP4 build queue type exceeds 228 at unit {index}")
                if extra["build_type"] != queue[slot]:
                    raise HxpFormatError(f"HXP4 active buildType/queue mismatch at unit {index}")
                order = units[index]["order"]
                tech = extra["tech_type"]
                upgrade = extra["upgrade_type"]
                remaining = extra["remaining_research_time"]
                if order == RESEARCH_TECH_ORDER:
                    if tech > TECH_TYPE_NONE or upgrade != UPGRADE_TYPE_NONE:
                        raise HxpFormatError(f"HXP4 invalid ResearchTech arm at unit {index}")
                elif order == UPGRADE_ORDER:
                    if tech != TECH_TYPE_NONE or upgrade > UPGRADE_TYPE_NONE:
                        raise HxpFormatError(f"HXP4 invalid Upgrade arm at unit {index}")
                elif remaining != 0 or tech != TECH_TYPE_NONE or upgrade != UPGRADE_TYPE_NONE:
                    raise HxpFormatError(f"HXP4 non-research union arm at unit {index}")
            decoded_extras.append(extra)
        extras = tuple(decoded_extras)

    player_bytes, cursor = _take(data, cursor, player_count * _PLAYER.size, "HXP players")
    players = tuple(
        _numeric_dict(_PLAYER_FIELDS, values)
        for values in _PLAYER.iter_unpack(player_bytes)
    )
    player_tech: tuple[dict[str, tuple[int, ...]], ...] | None = None
    if magic == b"HXP4":
        tech_bytes, cursor = _take(
            data,
            cursor,
            player_count * _PLAYER_TECH.size,
            "HXP4 player-tech records",
        )
        player_tech = tuple(
            {
                "has_researched": tuple(record[0]),
                "upgrade_level": tuple(record[1]),
                "tech_in_progress": tuple(record[2]),
                "upgrade_in_progress": tuple(record[3]),
            }
            for record in _PLAYER_TECH.iter_unpack(tech_bytes)
        )

    tile_count = map_width * map_height
    tile_flags: bytes | None = None
    seen: bytes | None = None
    explored: bytes | None = None
    if magic in (b"HXP2", b"HXP3", b"HXP4"):
        tile_flags, cursor = _take(data, cursor, tile_count * 4, f"{protocol} tile flags")
    else:
        plane_size = (tile_count + 7) // 8
        seen, cursor = _take(data, cursor, plane_size, "HXPF seen plane")
        explored, cursor = _take(data, cursor, plane_size, "HXPF explored plane")
    result = HxFrame(
        protocol=protocol,
        frame=frame,
        map_width=map_width,
        map_height=map_height,
        units=units,
        unit_extras=extras,
        players=players,
        player_tech=player_tech,
        tile_flags=tile_flags,
        seen=seen,
        explored=explored,
        raw=data[offset:cursor],
    )
    return result, cursor


def parse_frame(data: bytes | bytearray | memoryview) -> HxFrame:
    """Parse exactly one complete HXP2, HXP3, HXP4, or legacy HXPF frame."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("HXP frame input must be bytes-like")
    raw = bytes(data)
    frame, end = _parse_frame_at(raw, 0)
    if end != len(raw):
        raise HxpFormatError(f"trailing {len(raw) - end} bytes after one HXP frame")
    return frame


def parse_frame_stream(data: bytes | bytearray | memoryview) -> list[HxFrame]:
    """Parse a concatenation of complete frames with no skipped/resynced bytes."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("HXP stream input must be bytes-like")
    raw = bytes(data)
    frames: list[HxFrame] = []
    offset = 0
    while offset < len(raw):
        frame, offset = _parse_frame_at(raw, offset)
        frames.append(frame)
    return frames


def _parse_reply_at(data: bytes, offset: int) -> tuple[HxReply, int]:
    header_bytes, cursor = _take(data, offset, _REPLY_HEADER.size, "HXRP reply header")
    magic, frame, command_size = _REPLY_HEADER.unpack(header_bytes)
    if magic != REPLY_MAGIC:
        raise HxpFormatError(f"invalid HXRP reply magic {magic!r} at byte {offset}")
    if command_size > MAX_REPLY_COMMAND_BYTES:
        raise HxpFormatError(
            f"HXRP command size {command_size} exceeds {MAX_REPLY_COMMAND_BYTES} at byte {offset}"
        )
    command_bytes, cursor = _take(data, cursor, command_size, "HXRP classic commands")
    tail, cursor = _take(data, cursor, _REPLY_TAIL.size, "HXRP win field")
    win = _REPLY_TAIL.unpack(tail)[0]
    # Parse the whole command payload before accepting the reply.  CommandError
    # deliberately propagates with its exact command-stream offset.
    commands = tuple(parse_in_game_commands(command_bytes))
    return (
        HxReply(
            frame=frame,
            command_bytes=command_bytes,
            win=win,
            commands=commands,
            raw=data[offset:cursor],
        ),
        cursor,
    )


def parse_reply(data: bytes | bytearray | memoryview) -> HxReply:
    """Parse exactly one complete HXRP reply and its native command payload."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("HXRP reply input must be bytes-like")
    raw = bytes(data)
    reply, end = _parse_reply_at(raw, 0)
    if end != len(raw):
        raise HxpFormatError(f"trailing {len(raw) - end} bytes after one HXRP reply")
    return reply


def parse_reply_stream(data: bytes | bytearray | memoryview) -> list[HxReply]:
    """Parse concatenated replies without treating noise as a resync marker."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("HXRP stream input must be bytes-like")
    raw = bytes(data)
    replies: list[HxReply] = []
    offset = 0
    while offset < len(raw):
        reply, offset = _parse_reply_at(raw, offset)
        replies.append(reply)
    return replies


def parse_end(data: bytes | bytearray | memoryview) -> HxEnd:
    """Parse the 12-byte HXPE match-end record, rejecting nonzero reservation."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("HXPE input must be bytes-like")
    raw = bytes(data)
    if len(raw) != _END.size:
        raise HxpTruncatedError(f"HXPE must be exactly 12 bytes, got {len(raw)}")
    magic, winner, reserved = _END.unpack(raw)
    if magic != END_MAGIC:
        raise HxpFormatError(f"invalid HXPE magic {magic!r}")
    if reserved != 0:
        raise HxpFormatError(f"HXPE reserved field must be zero, got {reserved}")
    return HxEnd(winner=winner, reserved=reserved, raw=raw)
