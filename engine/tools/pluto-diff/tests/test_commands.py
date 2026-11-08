from __future__ import annotations

import struct
import unittest

from pluto_diff.commands import (
    InvalidCommandError,
    LobbyOnlyCommandError,
    TruncatedCommandError,
    UnknownOpcodeError,
    command_layouts,
    parse_commands,
    parse_in_game_commands,
)
from tests.helpers import LOBBY_ONLY_COMMANDS


# Packed sizes from every BWAPI 4.4.0 OrderTypes.h class.  SelectRemove is the
# native third select-family opcode handled by BWAPI's CommandFilter.
FIXED_LENGTHS = {
    0x08: 1,
    0x0C: 8,
    0x0D: 3,
    0x0E: 5,
    0x10: 1,
    0x11: 1,
    0x12: 5,
    0x14: 10,
    0x15: 11,
    0x18: 1,
    0x19: 1,
    0x1A: 2,
    0x1B: 1,
    0x1C: 1,
    0x1E: 2,
    0x1F: 3,
    0x20: 3,
    0x21: 2,
    0x22: 2,
    0x23: 3,
    0x25: 2,
    0x26: 2,
    0x27: 1,
    0x28: 2,
    0x29: 3,
    0x2A: 1,
    0x2B: 2,
    0x2C: 2,
    0x2D: 2,
    0x2E: 1,
    0x2F: 5,
    0x30: 2,
    0x31: 1,
    0x32: 2,
    0x33: 1,
    0x34: 1,
    0x35: 3,
    0x36: 1,
    0x3C: 1,
    0x3E: 6,
    0x41: 3,
    0x44: 3,
    0x57: 2,
    0x58: 5,
    0x5A: 1,
}
SELECTS = {0x09: "Select", 0x0A: "SelectAdd", 0x0B: "SelectRemove"}


class ClassicCommandTests(unittest.TestCase):
    def test_inventory_contains_every_bwapi_layout(self) -> None:
        layouts = command_layouts()
        self.assertEqual(set(layouts), set(FIXED_LENGTHS) | set(SELECTS))
        for opcode, length in FIXED_LENGTHS.items():
            with self.subTest(opcode=opcode):
                self.assertEqual(layouts[opcode][1], length)
                raw = bytes([opcode]) + bytes(length - 1)
                commands = parse_commands(raw)
                self.assertEqual(len(commands), 1)
                self.assertEqual(commands[0].length, length)
                self.assertEqual(commands[0].raw, raw)
        for opcode, name in SELECTS.items():
            self.assertEqual(layouts[opcode], (name, None))

    def test_all_fixed_commands_reject_truncated_payload(self) -> None:
        for opcode, length in FIXED_LENGTHS.items():
            if length == 1:
                continue
            with self.subTest(opcode=opcode):
                raw = bytes([opcode]) + bytes(length - 2)
                with self.assertRaises(TruncatedCommandError) as caught:
                    parse_commands(raw)
                self.assertEqual(caught.exception.offset, 0)
                self.assertEqual(caught.exception.opcode, opcode)

    def test_select_family_maximum_and_decoding(self) -> None:
        for opcode, name in SELECTS.items():
            with self.subTest(opcode=opcode):
                ids = list(range(12))
                raw = bytes([opcode, 12]) + struct.pack("<12H", *ids)
                command = parse_commands(raw)[0]
                self.assertEqual(command.name, name)
                self.assertEqual(command.length, 26)
                self.assertEqual(command.fields, {"count": 12, "unit_ids": ids})
        self.assertEqual(parse_commands(b"\x09\x00")[0].fields["unit_ids"], [])

    def test_selection_count_over_twelve_is_invalid_not_variable_length(self) -> None:
        raw = b"\x09\x0d" + bytes(26)
        with self.assertRaises(InvalidCommandError):
            parse_commands(raw)
        with self.assertRaises(TruncatedCommandError):
            parse_commands(b"\x0a\x02\x01\x00")
        with self.assertRaises(TruncatedCommandError):
            parse_commands(b"\x0b")

    def test_signed_position_and_target_layouts(self) -> None:
        right = struct.pack("<BhhHHB", 0x14, -320, 640, 0xF123, 0xE4, 1)
        command = parse_commands(right)[0]
        self.assertEqual(
            command.fields,
            {"x": -320, "y": 640, "target_id": 0xF123, "target_type": 0xE4, "queued": 1},
        )
        attack = struct.pack("<BhhHHBB", 0x15, -1, -2, 0x8001, 0xE4, 0x89, 0)
        self.assertEqual(
            parse_commands(attack)[0].fields,
            {
                "x": -1,
                "y": -2,
                "target_id": 0x8001,
                "target_type": 0xE4,
                "order": 0x89,
                "queued": 0,
            },
        )

    def test_placebox_cancel_train_lift_and_lobby_layouts(self) -> None:
        placebox = struct.pack("<BBhhH", 0x0C, 0x24, -12, 34, 0xABCD)
        self.assertEqual(
            parse_commands(placebox)[0].fields,
            {"order": 0x24, "tile_x": -12, "tile_y": 34, "unit_type": 0xABCD},
        )
        cancel = struct.pack("<BbB", 0x20, -2, 0xFF)
        self.assertEqual(parse_commands(cancel)[0].fields, {"slot": -2, "unknown": 255})
        lift = struct.pack("<BI", 0x2F, 0xDEADBEEF)
        self.assertEqual(parse_commands(lift)[0].fields, {"unknown": 0xDEADBEEF})
        slot = struct.pack("<BBBBBB", 0x3E, 1, 2, 3, 4, 5)
        self.assertEqual(
            parse_commands(slot)[0].fields,
            {"slot": 1, "storm_player_id": 2, "owner": 3, "new_race": 4, "team": 5},
        )

    def test_in_game_profile_rejects_lobby_only_commands_without_changing_generic_grammar(self) -> None:
        for opcode, raw in LOBBY_ONLY_COMMANDS.items():
            with self.subTest(opcode=opcode):
                # The generic binary parser retains every genuine BWAPI layout.
                self.assertEqual(parse_commands(raw)[0].opcode, opcode)
                with self.assertRaises(LobbyOnlyCommandError) as caught:
                    parse_in_game_commands(b"\x27" + raw)
                self.assertEqual(caught.exception.offset, 1)
                self.assertEqual(caught.exception.opcode, opcode)
                self.assertIn("lobby-only", str(caught.exception))

    def test_in_game_profile_preserves_unknown_opcode_strictness(self) -> None:
        self.assertEqual(parse_in_game_commands(b"\x1a\x00")[0].name, "Stop")
        with self.assertRaises(UnknownOpcodeError) as caught:
            parse_in_game_commands(b"\x27\xff")
        self.assertEqual(caught.exception.offset, 1)
        self.assertEqual(caught.exception.opcode, 0xFF)

    def test_concatenation_offsets_and_empty_batch(self) -> None:
        raw = b"\x27" + struct.pack("<BH", 0x1F, 37) + b"\x09\x01\x03\x24"
        commands = parse_commands(raw)
        self.assertEqual([command.offset for command in commands], [0, 1, 4])
        self.assertEqual([command.length for command in commands], [1, 3, 4])
        self.assertEqual(parse_commands(b""), [])

    def test_unknown_opcode_rejects_whole_stream_without_prefix_success(self) -> None:
        for unknown in (0x00, 0x05, 0x16, 0x60, 0x89, 0xFF):
            with self.subTest(opcode=unknown):
                with self.assertRaises(UnknownOpcodeError) as caught:
                    parse_commands(b"\x27" + bytes([unknown]) + b"\x00" * 20)
                self.assertEqual(caught.exception.offset, 1)
                self.assertEqual(caught.exception.opcode, unknown)

    def test_bytes_like_input_is_binary_safe(self) -> None:
        raw = struct.pack("<BhhHHB", 0x14, -128, -256, 0xFFFF, 0xE4, 0x80)
        self.assertEqual(parse_commands(memoryview(raw))[0].raw, raw)
        self.assertEqual(parse_commands(bytearray(raw))[0].raw, raw)
        with self.assertRaises(TypeError):
            parse_commands("14")  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
