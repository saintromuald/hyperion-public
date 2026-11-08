from __future__ import annotations

import struct
import unittest

from pluto_diff.commands import (
    LobbyOnlyCommandError,
    TruncatedCommandError,
    UnknownOpcodeError,
)
from pluto_diff.hxp import (
    HxpFormatError,
    HxpTruncatedError,
    parse_end,
    parse_frame,
    parse_frame_stream,
    parse_reply,
    parse_reply_stream,
)
from tests.helpers import (
    LOBBY_ONLY_COMMANDS,
    SELECT_9363,
    make_frame,
    make_reply,
    make_unit_frame,
)


class HxpFrameTests(unittest.TestCase):
    def test_hxp3_unit_extras_and_binary_planes(self) -> None:
        raw = make_unit_frame(42)
        frame = parse_frame(raw)
        self.assertEqual(frame.protocol, "HXP3")
        self.assertEqual(frame.frame, 42)
        self.assertEqual(frame.map_width, 2)
        self.assertEqual(frame.units[0]["id"], 7)
        self.assertEqual(frame.units[0]["direction"], 255)
        self.assertEqual(frame.units[0]["pad"], 0x80)
        self.assertEqual(frame.units[0]["generation"], 0x80)
        self.assertIsNotNone(frame.unit_extras)
        assert frame.unit_extras is not None
        self.assertEqual(frame.unit_extras[0]["status_flags"], 0x12345678)
        self.assertEqual(frame.unit_extras[0]["speed_x"], -123)
        self.assertEqual(frame.unit_extras[0]["speed_y"], 456)
        self.assertEqual(frame.unit_extras[0]["remaining_build_time"], 99)
        self.assertEqual(frame.unit_extras[0]["main_order_timer"], 10)
        self.assertEqual(frame.tile_flags, bytes(range(8)))
        self.assertEqual(frame.raw, raw)

    def test_hxp2_has_no_extras_and_hxpf_has_legacy_planes(self) -> None:
        legacy_raw = bytearray(make_unit_frame(1, protocol=b"HXPF"))
        legacy_raw[24 + 39] = 0  # final unit byte: legacy zero generation/pad
        legacy_with_zero = parse_frame(legacy_raw)
        self.assertEqual(legacy_with_zero.units[0]["generation"], 0)
        self.assertEqual(legacy_with_zero.units[0]["pad"], 0)

        hxp2 = parse_frame(make_unit_frame(2, protocol=b"HXP2"))
        self.assertEqual(hxp2.protocol, "HXP2")
        self.assertIsNone(hxp2.unit_extras)
        self.assertEqual(hxp2.tile_flags, bytes(range(8)))
        legacy = parse_frame(make_unit_frame(1, protocol=b"HXPF"))
        self.assertEqual(legacy.protocol, "HXPF")
        self.assertIsNone(legacy.tile_flags)
        self.assertEqual(legacy.seen, b"\x03")
        self.assertEqual(legacy.explored, b"\x00")

    def test_frame_stream_is_concatenated_not_resynchronised(self) -> None:
        raws = [make_frame(0), make_frame(1, protocol=b"HXP2"), make_frame(2, protocol=b"HXPF")]
        frames = parse_frame_stream(b"".join(raws))
        self.assertEqual([frame.frame for frame in frames], [0, 1, 2])
        self.assertEqual([frame.protocol for frame in frames], ["HXP3", "HXP2", "HXPF"])
        with self.assertRaises(HxpFormatError):
            parse_frame_stream(b"noise" + raws[0])
        with self.assertRaises(HxpFormatError):
            parse_frame(raws[0] + b"\x00")

    def test_every_truncated_frame_prefix_is_rejected(self) -> None:
        for protocol in (b"HXP3", b"HXP2", b"HXPF"):
            raw = make_unit_frame(9, protocol=protocol)
            for length in range(len(raw)):
                with self.subTest(protocol=protocol, length=length):
                    with self.assertRaises(HxpTruncatedError):
                        parse_frame(raw[:length])

    def test_invalid_magic_counts_and_map_dimensions_fail_before_payload(self) -> None:
        with self.assertRaises(HxpFormatError):
            parse_frame(b"NOPE" + bytes(100))
        too_many_units = struct.pack("<4s5I", b"HXP3", 0, 10000, 1, 1, 1)
        with self.assertRaises(HxpFormatError):
            parse_frame(too_many_units)
        too_many_players = struct.pack("<4s5I", b"HXP3", 0, 0, 1, 1, 13)
        with self.assertRaises(HxpFormatError):
            parse_frame(too_many_players)
        bad_map = struct.pack("<4s5I", b"HXP3", 0, 0, 0, 1, 1)
        with self.assertRaises(HxpFormatError):
            parse_frame(bad_map)


class Hxp4FrameTests(unittest.TestCase):
    def test_late_state_queue_research_and_owner_tech(self) -> None:
        player_tech = bytearray(119)
        player_tech[0] = 1
        player_tech[24] = 1
        player_tech[43] = 1
        player_tech[44] = 2
        player_tech[44 + 46] = 3
        player_tech[44 + 60] = 2
        player_tech[105 + 5] = 0x08
        player_tech[111 + 7] = 0x10
        raw = make_unit_frame(
            43,
            protocol=b"HXP4",
            order=75,
            build_queue=(8, 9, 10, 11, 12),
            remaining_research_time=120,
            tech_type=5,
            upgrade_type=61,
            player_tech=bytes(player_tech),
        )
        self.assertEqual(len(raw), 305)
        self.assertEqual(struct.unpack_from("<II", raw, 24), (78, 119))
        frame = parse_frame(raw)
        self.assertEqual(frame.protocol, "HXP4")
        self.assertEqual(frame.units[0]["generation"], 0x80)
        assert frame.unit_extras is not None
        extra = frame.unit_extras[0]
        self.assertEqual(extra["build_queue"], (8, 9, 10, 11, 12))
        self.assertEqual(extra["build_type"], 10)
        self.assertEqual(extra["remaining_research_time"], 120)
        self.assertEqual(extra["tech_type"], 5)
        self.assertEqual(extra["upgrade_type"], 61)
        assert frame.player_tech is not None
        tech = frame.player_tech[0]
        self.assertEqual(tech["has_researched"][24], 1)
        self.assertEqual(tech["upgrade_level"][46], 3)
        self.assertEqual(tech["upgrade_level"][60], 2)
        self.assertEqual(tech["tech_in_progress"][5], 0x08)
        self.assertEqual(tech["upgrade_in_progress"][7], 0x10)
        self.assertEqual(frame.to_dict()["player_tech"][0]["upgrade_level"][60], 2)

    def test_upgrade_arm_and_nonresearch_defaults(self) -> None:
        upgraded = parse_frame(
            make_unit_frame(
                44,
                protocol=b"HXP4",
                order=76,
                build_queue=(8, 9, 10, 11, 12),
                remaining_research_time=88,
                tech_type=44,
                upgrade_type=7,
            )
        )
        assert upgraded.unit_extras is not None
        self.assertEqual(upgraded.unit_extras[0]["tech_type"], 44)
        self.assertEqual(upgraded.unit_extras[0]["upgrade_type"], 7)
        self.assertEqual(upgraded.unit_extras[0]["remaining_research_time"], 88)
        idle = parse_frame(make_unit_frame(45, protocol=b"HXP4"))
        assert idle.unit_extras is not None
        self.assertEqual(idle.unit_extras[0]["remaining_research_time"], 0)
        self.assertEqual(idle.unit_extras[0]["tech_type"], 44)
        self.assertEqual(idle.unit_extras[0]["upgrade_type"], 61)

    def test_invalid_size_queue_and_research_arm(self) -> None:
        raw = make_unit_frame(
            46,
            protocol=b"HXP4",
            order=75,
            build_queue=(8, 9, 10, 11, 12),
            remaining_research_time=120,
            tech_type=5,
            upgrade_type=61,
        )
        mutations = []
        wrong_size = bytearray(raw)
        struct.pack_into("<I", wrong_size, 24, 64)
        mutations.append(wrong_size)
        wrong_tech_size = bytearray(raw)
        struct.pack_into("<I", wrong_tech_size, 28, 0)
        mutations.append(wrong_tech_size)
        bad_slot = bytearray(raw)
        bad_slot[126] = 5
        mutations.append(bad_slot)
        bad_type = bytearray(raw)
        struct.pack_into("<H", bad_type, 136, 229)
        mutations.append(bad_type)
        mismatch = bytearray(raw)
        struct.pack_into("<I", mismatch, 100, 11)
        mutations.append(mismatch)
        bad_tech = bytearray(raw)
        bad_tech[148] = 45
        mutations.append(bad_tech)
        bad_upgrade = bytearray(raw)
        bad_upgrade[64] = 76
        bad_upgrade[148] = 44
        bad_upgrade[149] = 62
        mutations.append(bad_upgrade)
        nonresearch = bytearray(raw)
        nonresearch[64] = 5
        nonresearch[148] = 5
        mutations.append(nonresearch)
        for index, mutated in enumerate(mutations):
            with self.subTest(mutation=index):
                with self.assertRaises(HxpFormatError):
                    parse_frame(mutated)

    def test_every_truncated_prefix_and_trailing_bytes(self) -> None:
        raw = make_unit_frame(
            47,
            protocol=b"HXP4",
            order=75,
            build_queue=(8, 9, 10, 11, 12),
            remaining_research_time=120,
            tech_type=5,
            upgrade_type=61,
            player_tech=bytes(119),
        )
        for length in range(len(raw)):
            with self.subTest(length=length):
                with self.assertRaises(HxpTruncatedError):
                    parse_frame(raw[:length])
        with self.assertRaises(HxpFormatError):
            parse_frame(raw + b"\x00")
        # HXP3 never acquires HXP4 fields by appending a plausible 14-byte tail.
        hxp3 = make_unit_frame(48, protocol=b"HXP3")
        with self.assertRaises(HxpFormatError):
            parse_frame(hxp3 + bytes(14))

    def test_mixed_version_stream(self) -> None:
        frames = parse_frame_stream(
            make_unit_frame(0, protocol=b"HXP4")
            + make_unit_frame(1, protocol=b"HXP3")
            + make_unit_frame(2, protocol=b"HXP2")
            + make_unit_frame(3, protocol=b"HXPF")
        )
        self.assertEqual([frame.protocol for frame in frames], ["HXP4", "HXP3", "HXP2", "HXPF"])
        self.assertIsNotNone(frames[0].player_tech)
        self.assertIsNone(frames[1].player_tech)
        self.assertIsNone(frames[2].player_tech)
        self.assertIsNone(frames[3].player_tech)


class HxpReplyTests(unittest.TestCase):
    def test_reply_decodes_commands_and_win(self) -> None:
        raw = make_reply(17, SELECT_9363, win=1)
        reply = parse_reply(raw)
        self.assertEqual(reply.frame, 17)
        self.assertEqual(reply.command_bytes, SELECT_9363)
        self.assertEqual(reply.win, 1)
        self.assertEqual(reply.commands[0].fields["unit_ids"], [9363])
        self.assertEqual(reply.raw, raw)
        empty = parse_reply(make_reply(18))
        self.assertEqual(empty.command_bytes, b"")
        self.assertEqual(empty.commands, ())

    def test_reply_stream_and_trailing_bytes(self) -> None:
        stream = make_reply(0) + make_reply(1, SELECT_9363)
        replies = parse_reply_stream(stream)
        self.assertEqual([reply.frame for reply in replies], [0, 1])
        with self.assertRaises(HxpFormatError):
            parse_reply(stream)
        with self.assertRaises(HxpFormatError):
            parse_reply(b"garbage" + make_reply(0))

    def test_every_truncated_reply_prefix_is_rejected(self) -> None:
        raw = make_reply(4, SELECT_9363)
        for length in range(len(raw)):
            with self.subTest(length=length):
                with self.assertRaises(HxpTruncatedError):
                    parse_reply(raw[:length])

    def test_reply_rejects_unknown_or_truncated_native_commands(self) -> None:
        with self.assertRaises(UnknownOpcodeError):
            parse_reply(make_reply(0, b"\xff"))
        with self.assertRaises(TruncatedCommandError) as caught:
            parse_reply(make_reply(0, b"\x14\x00"))
        self.assertIn("truncated", str(caught.exception))
        oversized = struct.pack("<4sII", b"HXRP", 0, 0x10000)
        with self.assertRaises(HxpFormatError):
            parse_reply(oversized)

    def test_hxrp_rejects_lobby_only_commands_as_in_game_evidence(self) -> None:
        for opcode, commands in LOBBY_ONLY_COMMANDS.items():
            with self.subTest(opcode=opcode):
                with self.assertRaises(LobbyOnlyCommandError) as caught:
                    parse_reply(make_reply(4, commands))
                self.assertEqual(caught.exception.offset, 0)
                self.assertEqual(caught.exception.opcode, opcode)

    def test_hxpe_end_record(self) -> None:
        end = parse_end(struct.pack("<4sII", b"HXPE", 1, 0))
        self.assertEqual(end.winner, 1)
        with self.assertRaises(HxpFormatError):
            parse_end(struct.pack("<4sII", b"HXPE", 1, 1))
        with self.assertRaises(HxpTruncatedError):
            parse_end(b"HXPE")


if __name__ == "__main__":
    unittest.main()
