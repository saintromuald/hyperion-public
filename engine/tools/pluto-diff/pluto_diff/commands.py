"""Strict classic StarCraft command parsing for BWAPI 4.4.0 captures.

The opcode layouts are transcribed from BWAPI 4.4.0's packed
``BWAPI/Source/BW/OrderTypes.h``.  Opcode 0x0b (SelectRemove) is also accepted:
it is the third member of the native select family and is explicitly handled by
BWAPI's CommandFilter, with the same two-byte UnitTarget entries as 0x09/0x0a.

This is an AI/native command parser, not a permissive replay parser.  Replay,
chat, SC:R extended-id, and Hyperion wide-id opcodes are deliberately unknown.
Unknown bytes are never skipped and no attempt is made to resynchronise.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any, Iterable


IN_GAME_COMMAND_PROFILE = "hyperion-in-game/v1"


class CommandError(ValueError):
    """Base class for a malformed native command stream."""

    def __init__(self, message: str, *, offset: int, opcode: int | None = None):
        self.offset = offset
        self.opcode = opcode
        suffix = f" at byte {offset}"
        if opcode is not None:
            suffix += f" (opcode 0x{opcode:02x})"
        super().__init__(message + suffix)


class UnknownOpcodeError(CommandError):
    """An opcode outside the strict BWAPI/native AI command set."""


class TruncatedCommandError(CommandError):
    """A known command whose complete packed layout is not present."""


class InvalidCommandError(CommandError):
    """A command value that cannot fit its genuine BWAPI layout."""


class LobbyOnlyCommandError(InvalidCommandError):
    """A genuine BWAPI lobby command used in an in-game evidence batch."""


@dataclass(frozen=True)
class ClassicCommand:
    offset: int
    opcode: int
    name: str
    length: int
    fields: dict[str, Any]
    raw: bytes

    def to_dict(self) -> dict[str, Any]:
        # Do not include raw here: trace blobs own exact bytes.  Numeric decoded
        # fields make JSON/human differences useful without weakening comparison.
        return {
            "offset": self.offset,
            "opcode": self.opcode,
            "name": self.name,
            "length": self.length,
            "fields": self.fields,
        }


@dataclass(frozen=True)
class _Layout:
    name: str
    fmt: str
    fields: tuple[str, ...]
    aliases: tuple[str, ...] = ()

    @property
    def length(self) -> int:
        return struct.calcsize(self.fmt)


def _layout(
    name: str,
    payload_fmt: str = "",
    fields: Iterable[str] = (),
    aliases: Iterable[str] = (),
) -> _Layout:
    # All fields are packed and little-endian.  There is no native alignment.
    return _Layout(name, "<B" + payload_fmt, tuple(fields), tuple(aliases))


# Every class in BWAPI 4.4.0 OrderTypes.h is represented here.  Several C++
# class names share 0x0c; their one on-wire layout is described only once.
_LAYOUTS: dict[int, _Layout] = {
    0x08: _layout("RestartGame"),
    0x0C: _layout(
        "MakeBuilding",
        "BhhH",
        ("order", "tile_x", "tile_y", "unit_type"),
        ("PlaceCOP", "MakeAddon", "MakeNydusExit", "Land"),
    ),
    0x0D: _layout("SetVision", "H", ("vision",)),
    0x0E: _layout("SetAllies", "I", ("allies",)),
    0x10: _layout("PauseGame"),
    0x11: _layout("ResumeGame"),
    0x12: _layout("UseCheat", "I", ("flags",)),
    0x14: _layout(
        "RightClick",
        "hhHHB",
        ("x", "y", "target_id", "target_type", "queued"),
    ),
    0x15: _layout(
        "Attack",
        "hhHHBB",
        ("x", "y", "target_id", "target_type", "order", "queued"),
    ),
    0x18: _layout("CancelConstruction"),
    0x19: _layout("CancelUnitMorph"),
    0x1A: _layout("Stop", "B", ("queued",)),
    0x1B: _layout("CarrierStop"),
    0x1C: _layout("ReaverStop"),
    0x1E: _layout("ReturnCargo", "B", ("queued",)),
    0x1F: _layout("TrainUnit", "H", ("unit_type",)),
    0x20: _layout("CancelTrain", "bB", ("slot", "unknown")),
    0x21: _layout("Cloak", "B", ("unused",)),
    0x22: _layout("Decloak", "B", ("unused",)),
    0x23: _layout("UnitMorph", "H", ("unit_type",)),
    0x25: _layout("Unsiege", "B", ("unused",)),
    0x26: _layout("Siege", "B", ("unused",)),
    0x27: _layout("TrainFighter"),
    0x28: _layout("UnloadAll", "B", ("unused",)),
    0x29: _layout("UnloadUnit", "H", ("target_id",)),
    0x2A: _layout("MergeArchon"),
    0x2B: _layout("HoldPosition", "B", ("queued",)),
    0x2C: _layout("Burrow", "B", ("unused",)),
    0x2D: _layout("Unburrow", "B", ("unused",)),
    0x2E: _layout("CancelNuke"),
    # This is BWAPI's Lift layout (u32 unknown), not a guessed x/y point.
    0x2F: _layout("Lift", "I", ("unknown",)),
    0x30: _layout("Invent", "B", ("tech",)),
    0x31: _layout("CancelResearch"),
    0x32: _layout("Upgrade", "B", ("upgrade",)),
    0x33: _layout("CancelUpgrade"),
    0x34: _layout("CancelAddon"),
    0x35: _layout("BuildingMorph", "H", ("unit_type",)),
    0x36: _layout("UseStimPack"),
    0x3C: _layout("StartGame"),
    0x3E: _layout(
        "UpdateSlot",
        "BBBBB",
        ("slot", "storm_player_id", "owner", "new_race", "team"),
    ),
    0x41: _layout("RequestChangeRace", "BB", ("slot", "race")),
    0x44: _layout("ChangeSlot", "BB", ("slot", "slot_type")),
    0x57: _layout("LeaveGame", "B", ("type",)),
    0x58: _layout("MinimapPing", "hh", ("x", "y")),
    0x5A: _layout("MergeDarkArchon"),
}

# Hyperion's in-game dispatcher rejects/stops on these genuine BWAPI lobby
# commands.  They remain in the generic binary grammar above, but are not valid
# command evidence in HXP/HXRP captures or per-frame in-game TurnBuffers.
LOBBY_ONLY_OPCODES = frozenset({0x3C, 0x3E, 0x41, 0x44})

_SELECT_NAMES = {0x09: "Select", 0x0A: "SelectAdd", 0x0B: "SelectRemove"}
MAX_SELECTION_COUNT = 12  # BW::MAX_SELECTION_COUNT in BWAPI 4.4.0.


def command_layouts() -> dict[int, tuple[str, int | None]]:
    """Return a copy of the public opcode/length inventory.

    Variable select commands have ``None`` length.  This is intended for tests,
    diagnostics, and capture validators, not for permissive dispatch.
    """

    answer = {op: (layout.name, layout.length) for op, layout in _LAYOUTS.items()}
    answer.update({op: (name, None) for op, name in _SELECT_NAMES.items()})
    return dict(sorted(answer.items()))


def _unpack_fixed(layout: _Layout, data: bytes, offset: int) -> ClassicCommand:
    remaining = len(data) - offset
    if remaining < layout.length:
        raise TruncatedCommandError(
            f"truncated {layout.name}: need {layout.length} bytes, have {remaining}",
            offset=offset,
            opcode=data[offset],
        )
    values = struct.unpack_from(layout.fmt, data, offset)
    fields = dict(zip(layout.fields, values[1:]))
    return ClassicCommand(
        offset=offset,
        opcode=values[0],
        name=layout.name,
        length=layout.length,
        fields=fields,
        raw=data[offset : offset + layout.length],
    )


def _unpack_select(name: str, data: bytes, offset: int) -> ClassicCommand:
    if len(data) - offset < 2:
        raise TruncatedCommandError(
            f"truncated {name}: missing selection count",
            offset=offset,
            opcode=data[offset],
        )
    count = data[offset + 1]
    if count > MAX_SELECTION_COUNT:
        raise InvalidCommandError(
            f"{name} count {count} exceeds BW::MAX_SELECTION_COUNT={MAX_SELECTION_COUNT}",
            offset=offset,
            opcode=data[offset],
        )
    length = 2 + count * 2
    remaining = len(data) - offset
    if remaining < length:
        raise TruncatedCommandError(
            f"truncated {name}: count {count} needs {length} bytes, have {remaining}",
            offset=offset,
            opcode=data[offset],
        )
    ids = list(struct.unpack_from("<" + "H" * count, data, offset + 2)) if count else []
    return ClassicCommand(
        offset=offset,
        opcode=data[offset],
        name=name,
        length=length,
        fields={"count": count, "unit_ids": ids},
        raw=data[offset : offset + length],
    )


def _parse_command_stream(
    data: bytes | bytearray | memoryview,
    *,
    in_game: bool,
) -> list[ClassicCommand]:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("classic command input must be bytes-like")
    raw = bytes(data)
    commands: list[ClassicCommand] = []
    offset = 0
    while offset < len(raw):
        opcode = raw[offset]
        if in_game and opcode in LOBBY_ONLY_OPCODES:
            name = _LAYOUTS[opcode].name
            raise LobbyOnlyCommandError(
                f"lobby-only {name} is not dispatchable in the "
                f"{IN_GAME_COMMAND_PROFILE} command profile",
                offset=offset,
                opcode=opcode,
            )
        if opcode in _SELECT_NAMES:
            command = _unpack_select(_SELECT_NAMES[opcode], raw, offset)
        else:
            layout = _LAYOUTS.get(opcode)
            if layout is None:
                raise UnknownOpcodeError(
                    "unknown classic BWAPI command opcode", offset=offset, opcode=opcode
                )
            command = _unpack_fixed(layout, raw, offset)
        commands.append(command)
        offset += command.length
    return commands


def parse_commands(data: bytes | bytearray | memoryview) -> list[ClassicCommand]:
    """Parse the complete generic BWAPI binary grammar, including lobby layouts.

    Empty input is a valid empty batch.  Any unknown opcode, over-capacity
    selection, or truncated tail rejects the entire input; already parsed prefix
    commands are never returned as if the stream were valid.  This function does
    not claim that every genuine layout is dispatchable in a particular context.
    """

    return _parse_command_stream(data, in_game=False)


def parse_in_game_commands(
    data: bytes | bytearray | memoryview,
) -> list[ClassicCommand]:
    """Parse the Hyperion in-game profile, rejecting lobby-only commands."""

    return _parse_command_stream(data, in_game=True)


def command_dicts(data: bytes | bytearray | memoryview) -> list[dict[str, Any]]:
    """Return generic BWAPI command descriptions without a dispatch profile."""

    return [command.to_dict() for command in parse_commands(data)]


def in_game_command_dicts(
    data: bytes | bytearray | memoryview,
) -> list[dict[str, Any]]:
    """Return command descriptions validated by the Hyperion in-game profile."""

    return [command.to_dict() for command in parse_in_game_commands(data)]
