#!/usr/bin/env python3
"""Synthetic tests for pluto_acceptance; no game or inference is launched."""

import copy
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import pluto_acceptance as pa


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def make_reply(frame, commands=b"", win=0):
    return struct.pack("<4sII", b"HXRP", frame, len(commands)) + commands + struct.pack("<I", win)


def make_frame(frame):
    # Strict HXP3 transport fixture: two players, no units, one tile.
    header = struct.pack("<4s5I", b"HXP3", frame, 0, 1, 1, 2)
    players = struct.pack("<7I", 0, 50, 0, 8, 4, 32, 64) * 2
    return header + players + struct.pack("<I", 0x00010203)


def make_hxp4_frame(
    frame,
    *,
    order=75,
    build_queue=(8, 9, 10, 11, 12),
    remaining_research_time=120,
    tech_type=5,
    upgrade_type=61,
    player_tech=None,
):
    # One unit, one player, one tile: 32 + 40 + 78 + 28 + 119 + 4 = 301 bytes.
    header = struct.pack("<4s5I2I", b"HXP4", frame, 1, 1, 1, 1, 78, 119)
    unit = struct.pack("<9I4B", 7, 105, 0, 640, 864, 1500, 0, 0, order, 1, 1, 64, 7)
    extra_values = [0] * 26
    extra_values[7] = build_queue[2]
    extra_values[16] = 2
    extra = struct.pack("<5I2i4I5H10B", *extra_values)
    extra += struct.pack(
        "<6H2B", *build_queue, remaining_research_time, tech_type, upgrade_type
    )
    player = struct.pack("<7I", 2, 500, 100, 40, 22, 608, 928)
    player += player_tech if player_tech is not None else bytes(119)
    return header + unit + extra + player + struct.pack("<I", 0x00010203)


def write_map_manifest(root, paths):
    write_json(
        root / "pluto-acceptance-maps.json",
        {
            "schema": pa.MAP_MANIFEST_SCHEMA,
            "review": {
                "reviewer": "reviewed synthetic fixture",
                "reviewed_utc": "2026-01-01T00:00:00Z",
                "attestation": pa.MAP_REVIEW_ATTESTATION,
            },
            "maps": [
                {
                    "path": path.relative_to(root).as_posix(),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "players": 2,
                    "start_locations": 2,
                    "game_type": "melee",
                    "ordinary_start": True,
                }
                for path in paths
            ],
        },
    )


def valid_bandit_text(seq=7):
    return """# synthetic bandit
seq %d
format 2
# own opp arm wins losses
Z Z 9PoolSpeed 1 2 # 0x00020000
T T free 0 0 # 0x00000000
{"t":"start","id":1,"own_race":"Z"}
{"t":"end","id":1,"result":"win"}
end
""" % seq


class Hxp4AcceptanceTests(unittest.TestCase):
    def test_queue_research_and_player_tech(self):
        tech = bytearray(119)
        tech[0] = tech[24] = tech[43] = 1
        tech[44] = 2
        tech[44 + 46] = 3
        tech[44 + 60] = 2
        tech[105 + 5] = 0x08
        tech[111 + 7] = 0x10
        raw = make_hxp4_frame(0, player_tech=bytes(tech))
        self.assertEqual(len(raw), 301)
        records, evidence = pa.parse_hxp_capture(raw)
        self.assertTrue(evidence["valid"], evidence)
        self.assertEqual(len(records), 1)
        extra = records[0]["unit_extras"][0]
        self.assertEqual(extra["build_queue"], (8, 9, 10, 11, 12))
        self.assertEqual(extra["build_type"], 10)
        self.assertEqual(extra["remaining_research_time"], 120)
        self.assertEqual(extra["tech_type"], 5)
        self.assertEqual(extra["upgrade_type"], 61)
        owner = records[0]["player_tech"][0]
        self.assertEqual(owner["has_researched"][24], 1)
        self.assertEqual(owner["upgrade_level"][46], 3)
        self.assertEqual(owner["upgrade_level"][60], 2)
        self.assertEqual(owner["tech_in_progress"][5], 0x08)
        self.assertEqual(owner["upgrade_in_progress"][7], 0x10)

    def test_upgrade_and_nonresearch_defaults(self):
        raw = make_hxp4_frame(
            0,
            order=76,
            remaining_research_time=88,
            tech_type=44,
            upgrade_type=7,
        ) + make_hxp4_frame(
            1,
            order=5,
            remaining_research_time=0,
            tech_type=44,
            upgrade_type=61,
        )
        records, evidence = pa.parse_hxp_capture(raw)
        self.assertTrue(evidence["valid"], evidence)
        self.assertEqual(records[0]["unit_extras"][0]["upgrade_type"], 7)
        self.assertEqual(records[0]["unit_extras"][0]["remaining_research_time"], 88)
        self.assertEqual(records[1]["unit_extras"][0]["tech_type"], 44)
        self.assertEqual(records[1]["unit_extras"][0]["upgrade_type"], 61)
        self.assertEqual(records[1]["unit_extras"][0]["remaining_research_time"], 0)

    def test_invalid_size_queue_and_research_arm(self):
        raw = make_hxp4_frame(0)
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
                records, evidence = pa.parse_hxp_capture(bytes(mutated))
                self.assertEqual(records, [])
                self.assertFalse(evidence["valid"])

    def test_every_truncated_prefix_and_trailing_bytes(self):
        raw = make_hxp4_frame(0)
        for length in range(len(raw)):
            with self.subTest(length=length):
                records, evidence = pa.parse_hxp_capture(raw[:length])
                self.assertEqual(records, [])
                self.assertFalse(evidence["valid"])
        records, evidence = pa.parse_hxp_capture(raw + b"\x00")
        self.assertEqual(records, [])
        self.assertFalse(evidence["valid"])
        # HXP3 has no late-state tail and cannot be extended implicitly.
        records, evidence = pa.parse_hxp_capture(make_frame(0) + bytes(14))
        self.assertEqual(records, [])
        self.assertFalse(evidence["valid"])

    def test_mixed_version_stream(self):
        hxp2 = bytearray(make_frame(1))
        hxp2[0:4] = b"HXP2"
        players = struct.pack("<7I", 0, 50, 0, 8, 4, 32, 64) * 2
        hxpf = struct.pack("<4s5I", b"HXPF", 2, 0, 1, 1, 2) + players + b"\x03\x01"
        records, evidence = pa.parse_hxp_capture(
            make_hxp4_frame(0, order=5, remaining_research_time=0, tech_type=44)
            + bytes(hxp2)
            + hxpf
            + make_frame(3)
        )
        self.assertTrue(evidence["valid"], evidence)
        self.assertEqual(
            [record["protocol"] for record in records], ["HXP4", "HXP2", "HXPF", "HXP3"]
        )
        self.assertIsNotNone(records[0]["player_tech"])
        self.assertIsNone(records[1]["player_tech"])
        self.assertIsNone(records[2]["player_tech"])
        self.assertIsNone(records[3]["player_tech"])


class SyntheticGame:
    def __init__(self, root):
        self.root = Path(root)
        self.map = self.root / "ASL" / "S1" / "(2)Synthetic.scx"
        self.map.parent.mkdir(parents=True, exist_ok=True)
        self.map.write_bytes(b"synthetic map")
        self.spec = pa.GameSpec(
            game=1,
            map_path=self.map,
            map_relative="S1/(2)Synthetic.scx",
            map_sha256=hashlib.sha256(b"synthetic map").hexdigest(),
            pluto_race="zerg",
            opponent_race="terran",
            requested_pluto_slot=0,
            seed=730000,
        )
        self.game_dir = self.root / "game-01"
        self.game_dir.mkdir()
        self.summary = {
            "ended": "last",
            "seconds": 321.5,
            "game_frames": 12000,
            "end_frame": 12001,
            "last_player": 1,
            "players": [
                {
                    "slot": 0,
                    "name": "Anonymous",
                    "race": 1,
                    "team": 1,
                    "level": "Cheater: insane",
                    "minerals": 10,
                    "gas": 20,
                    "minerals_gathered": 5000,
                    "gas_gathered": 1000,
                    "units": 0,
                    "units_made": 70,
                    "units_lost": 65,
                    "units_killed": 4,
                },
                {
                    "slot": 1,
                    "name": "PLuto",
                    "race": 0,
                    "team": 1,
                    "level": "Pluto",
                    "minerals": 100,
                    "gas": 200,
                    "minerals_gathered": 7000,
                    "gas_gathered": 2000,
                    "units": 12,
                    "units_made": 80,
                    "units_lost": 60,
                    "units_killed": 55,
                },
            ],
        }
        self.settings = {
            "map": str(self.map),
            "players": 2,
            "races": "z,t",
            "ai_levels": "p,i",
            "game_type": "melee",
            "seed": 730000,
            "frames": 30000,
        }
        self.pluto_log = """BWRL: onStart (player 0, opp race 1)
BWRL: inference server up (synthetic; v2.0 md09b_1331000, frames_per_step=6, max_units=768)
BWRL: own_race=0 opp_idx=1 opp="Bravo" -> BO 9PoolSpeed (mask=0x20000, 22 arms)
"""
        self.hyperion_log = (
            "Pluto: slot 1 plays Pluto\n"
            "Pluto: frame 0 and back\n"
            "Pluto: frame 0 has 2 cmd bytes\n"
            "Pluto: emitted 0 received 1 dispatched 3 received_bytes 2 accepted_bytes 2\n"
        )
        self.infer_log = "[pluto_infer] ready: model v2.0 md09b_1331000, frames_per_step 6\n"
        self.replies = (
            b"foreign bytes before the current run\n"
            + make_reply(0, b"\x1a\x00")
            + make_reply(1)
            + make_reply(2)
        )
        self.frames = b"".join(make_frame(frame) for frame in range(3))
        self.write()

    def write(self):
        write_json(self.game_dir / "summary.json", self.summary)
        write_json(self.game_dir / "settings.json", self.settings)
        (self.game_dir / "run.rep").write_bytes(b"replay")
        (self.game_dir / "replies-raw.bin").write_bytes(self.replies)
        (self.game_dir / "frames.bin").write_bytes(self.frames)
        (self.game_dir / "pluto.log").write_text(self.pluto_log, encoding="utf-8")
        (self.game_dir / "hyperion_pluto.log").write_text(self.hyperion_log, encoding="utf-8")
        (self.game_dir / "pluto_infer.log").write_text(self.infer_log, encoding="utf-8")
        (self.game_dir / "engine.log").write_text("synthetic engine log\n", encoding="utf-8")

    def artifacts(self):
        names = pa.REQUIRED_RUN_ARTIFACTS + pa.REQUIRED_GLOBAL_ARTIFACTS + ("engine.log",)
        return {name: {"required": True, "exists": True, "fresh": True, "sha256": "0" * 64} for name in names}

    def validate(self):
        self.write()
        return pa.validate_actual_game(self.spec, self.game_dir, self.artifacts())

    def classify(self, return_code=0):
        validation = self.validate()
        return pa.classify_outcome(
            self.spec,
            {"return_code": return_code, "timed_out": False, "launch_error": None},
            validation,
        )


class AcceptanceClassificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.game = SyntheticGame(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_win_uses_actual_pluto_label_not_requested_slot(self):
        validation = self.game.validate()
        self.assertTrue(validation["valid"], validation["errors"])
        self.assertEqual(validation["actual_pluto_slot"], 1)
        self.assertFalse(validation["requested_slot_matches_actual"])
        outcome, reasons, evidence = self.game.classify()
        self.assertEqual(outcome, "win", reasons)
        self.assertEqual(reasons, [])
        self.assertTrue(evidence["survivors"]["valid"])
        self.assertEqual(evidence["missing"], [])

    def test_loss_is_separate_and_corroborated(self):
        s = self.game.summary
        s["last_player"] = 0
        s["players"][0]["units"] = 20
        s["players"][0]["units_killed"] = 70
        s["players"][1]["units"] = 0
        s["players"][1]["units_lost"] = 80
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "loss", reasons)

    def test_frame_cap_with_both_alive_is_draw(self):
        s = self.game.summary
        s.update(ended="finished", last_player=-1, game_frames=29999, end_frame=30000)
        s["players"][0]["units"] = 30
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "draw", reasons)

    def test_early_finish_is_not_draw_or_win(self):
        self.game.summary.update(ended="finished", last_player=-1)
        self.game.summary["players"][0]["units"] = 30
        outcome, _, _ = self.game.classify()
        self.assertEqual(outcome, "incomplete")

    def test_false_win_rejections(self):
        mutations = {
            "wrong_end": lambda s: s.update(ended="finished"),
            "wrong_last_player": lambda s: s.update(last_player=0),
            "no_pluto_survivors": lambda s: s["players"][1].update(units=0),
            "opponent_still_alive": lambda s: s["players"][0].update(units=1),
            "no_pluto_kills": lambda s: s["players"][1].update(units_killed=0),
            "no_opponent_losses": lambda s: s["players"][0].update(units_lost=0),
            "no_production": lambda s: s["players"][1].update(units_made=0),
            "no_gathered_resources": lambda s: s["players"][1].update(minerals_gathered=0),
            "no_duration": lambda s: s.update(game_frames=0),
            "duration_beyond_cap": lambda s: s.update(game_frames=30001, end_frame=30001),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                summary = copy.deepcopy(self.game.summary)
                mutate(summary)
                self.game.summary = summary
                outcome, reasons, _ = self.game.classify()
                self.assertNotEqual(outcome, "win")
                self.assertIn(outcome, ("incomplete", "misconfigured"), reasons)

    def test_timeout_and_nonzero_exit_are_incomplete_even_with_win_summary(self):
        validation = self.game.validate()
        for execution in (
            {"return_code": None, "timed_out": True, "launch_error": None},
            {"return_code": -9, "timed_out": False, "launch_error": None},
            {"return_code": None, "timed_out": False, "launch_error": "synthetic launch error"},
        ):
            with self.subTest(execution=execution):
                outcome, _, _ = pa.classify_outcome(self.game.spec, execution, validation)
                self.assertEqual(outcome, "incomplete")

    def test_summary_race_mismatch_is_misconfigured(self):
        self.game.summary["players"][1]["race"] = 2
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "misconfigured")
        self.assertTrue(any("race" in reason for reason in reasons))

    def test_own_race_log_mismatch_is_rejected(self):
        self.game.pluto_log = self.game.pluto_log.replace("own_race=0", "own_race=1")
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertTrue(any("own_race" in error for error in validation["errors"]))

    def test_opponent_start_race_mismatch_is_rejected(self):
        self.game.pluto_log = self.game.pluto_log.replace("opp race 1", "opp race 2")
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertTrue(any("opponent race" in error for error in validation["errors"]))

    def test_pluto_label_and_level_are_both_required(self):
        self.game.summary["players"][0]["name"] = "PLuto"
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertTrue(any("PLuto-labelled" in error for error in validation["errors"]))

    def test_slot_alone_does_not_rescue_missing_pluto_label(self):
        self.game.summary["players"][1]["name"] = "NotPluto"
        self.game.summary["last_player"] = 1
        outcome, _, _ = self.game.classify()
        self.assertEqual(outcome, "misconfigured")

    def test_opponent_level_rejection(self):
        self.game.summary["players"][0]["level"] = "Elite"
        outcome, _, _ = self.game.classify()
        self.assertEqual(outcome, "misconfigured")

    def test_settings_rejections(self):
        mutations = {
            "frames": lambda s: s.update(frames=29999),
            "players": lambda s: s.update(players=3),
            "type": lambda s: s.update(game_type="ffa"),
            "races": lambda s: s.update(races="t,z"),
            "levels": lambda s: s.update(ai_levels="i,p"),
            "mutators": lambda s: s.update(mutators={"workers": 12}),
            "army": lambda s: s.update(army=100),
            "seed": lambda s: s.update(seed=1),
            "map": lambda s: s.update(map=str(self.game.map.with_name("(2)Other.scx"))),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                original = copy.deepcopy(self.game.settings)
                mutate(self.game.settings)
                validation = self.game.validate()
                self.assertFalse(validation["valid"])
                self.game.settings = original

    def test_missing_build_or_inference_evidence_is_rejected(self):
        self.game.pluto_log = "BWRL: own_race=-1\n"
        self.game.infer_log = ""
        self.game.hyperion_log = "Pluto: slot 0 plays Pluto\n"
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        joined = "\n".join(validation["errors"])
        self.assertIn("own_race", joined)
        self.assertIn("inference", joined)
        self.assertIn("PLuto-labelled slot", joined)

    def test_stale_required_artifact_is_rejected(self):
        artifacts = self.game.artifacts()
        artifacts["pluto.log"] = {"required": True, "exists": True, "fresh": False, "error": "stale"}
        self.game.write()
        validation = pa.validate_actual_game(self.game.spec, self.game.game_dir, artifacts)
        self.assertFalse(validation["valid"])
        self.assertTrue(any("stale" in error for error in validation["errors"]))

    def test_garbage_truncated_bad_commands_and_nonzero_win_have_no_valid_reply(self):
        bad_captures = {
            "garbage": b"not an HXRP reply",
            "magic_only": b"HXRP",
            "wrong_initial_frame": make_reply(1),
            "unknown_command": make_reply(0, b"\xff"),
            "truncated_win": make_reply(0)[:-1],
            "nonzero_win": make_reply(0, win=1),
        }
        for name, capture in bad_captures.items():
            with self.subTest(name=name):
                self.game.replies = capture
                validation = self.game.validate()
                self.assertFalse(validation["valid"])
                self.assertFalse(validation["reply_evidence"]["valid"])
                outcome, _, _ = self.game.classify()
                self.assertNotEqual(outcome, "win")

    def test_foreign_bytes_are_tolerated_but_candidates_remain_strict(self):
        capture = b"noise" + make_reply(0) + b"foreign" + make_reply(1) + make_reply(2)
        capture += make_reply(99, b"\xff")  # complete framing, invalid command: noise only
        records, evidence = pa.parse_hxrp_capture(capture)
        self.assertEqual([record["frame"] for record in records], [0, 1, 2])
        self.assertTrue(evidence["valid"])
        self.assertGreaterEqual(evidence["candidates_rejected"], 1)
        self.game.replies = capture
        self.assertTrue(self.game.validate()["valid"])

    def test_expected_frame_gap_and_ambiguous_current_runs_are_rejected(self):
        self.game.replies = make_reply(0) + make_reply(2)
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertFalse(validation["reply_evidence"]["coverage"]["dumped_frame_prefix_covered"])
        ambiguous = (make_reply(0) + make_reply(1)) * 2
        records, evidence = pa.parse_hxrp_capture(ambiguous)
        self.assertEqual(records, [])
        self.assertFalse(evidence["valid"])
        self.assertIn("ambiguous", evidence["selection_error"])

    def test_complete_strict_hxp_frames_and_reply_prefix_coverage_are_required(self):
        self.game.frames = self.game.frames[:-1]
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertTrue(any("frames.bin" in error for error in validation["errors"]))
        self.game.frames = b"".join(make_frame(frame) for frame in range(4))
        validation = self.game.validate()
        self.assertFalse(validation["valid"])
        self.assertTrue(any("does not cover" in error for error in validation["errors"]))

    def test_raw_commands_are_not_manufactured_into_application_evidence(self):
        stop = b"\x1a\x00"
        self.game.replies = make_reply(0, stop) + make_reply(1) + make_reply(2)
        self.game.hyperion_log = "Pluto: slot 1 plays Pluto\n"
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "incomplete")
        self.assertTrue(any("application" in reason for reason in reasons))
        validation = self.game.validate()
        self.assertFalse(validation["command_application"]["application_established"])
        self.game.hyperion_log += (
            "Pluto: frame 0 has 2 cmd bytes\n"
            "Pluto: emitted 0 received 1 dispatched 3 received_bytes 2 accepted_bytes 2\n"
        )
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "win", reasons)
        validation = self.game.validate()
        self.assertTrue(validation["command_application"]["application_established"])
        dispatch = validation["command_application"]["dispatch_log_records"][0]
        self.assertEqual((dispatch["emitted"], dispatch["received"], dispatch["dispatched"]), (0, 1, 3))

    def test_equal_emission_and_receipt_is_not_real_dispatch_evidence(self):
        stop = b"\x1a\x00"
        self.game.replies = make_reply(0, stop) + make_reply(1) + make_reply(2)
        self.game.hyperion_log = (
            "Pluto: slot 1 plays Pluto\n"
            "Pluto: frame 0 has 2 cmd bytes\n"
            "Pluto: emitted 0 received 0 dispatched 3 received_bytes 2 accepted_bytes 2\n"
        )
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "incomplete")
        self.assertTrue(any("application" in reason for reason in reasons))
        validation = self.game.validate()
        application = validation["command_application"]
        self.assertFalse(application["sufficient"])
        self.assertFalse(application["application_established"])
        self.assertEqual(len(application["dispatch_mismatches"]), 1)
        self.assertEqual(application["dispatch_mismatches"][0]["received"], 0)

    def test_lobby_only_opcodes_are_rejected_without_removing_gameplay_or_empty_batches(self):
        self.assertEqual(pa.parse_classic_commands(b""), [])
        empty_application = pa.command_application_evidence([], "", self.game.summary)
        self.assertTrue(empty_application["sufficient"])
        self.assertFalse(empty_application["application_established"])
        self.assertEqual(empty_application["status"], "no_nonempty_command_replies")
        for raw, name in ((b"\x1a\x00", "Stop"), (b"\x1f\x07\x00", "TrainUnit")):
            with self.subTest(gameplay_command=name):
                self.assertEqual(pa.parse_classic_commands(raw)[0]["name"], name)
        lobby_commands = {
            "StartGame": b"\x3c",
            "UpdateSlot": b"\x3e\x00\x00\x00\x00\x00",
            "RequestChangeRace": b"\x41\x00\x00",
            "ChangeSlot": b"\x44\x00\x00",
        }
        for name, command in lobby_commands.items():
            with self.subTest(lobby_command=name):
                self.game.replies = make_reply(0, command) + make_reply(1) + make_reply(2)
                validation = self.game.validate()
                self.assertFalse(validation["valid"])
                self.assertFalse(validation["reply_evidence"]["valid"])
                self.assertTrue(
                    any("lobby-only" in detail["error"] for detail in validation["reply_evidence"]["rejection_details"])
                )
                outcome, _, _ = self.game.classify()
                self.assertNotEqual(outcome, "win")

    def test_zero_command_production_is_not_a_natural_policy_win_loss_or_draw(self):
        original = copy.deepcopy(self.game.summary)

        def make_loss(summary):
            summary["last_player"] = 0
            summary["players"][0].update(units=20, units_killed=70)
            summary["players"][1].update(units=0, units_lost=80)

        def make_draw(summary):
            summary.update(ended="finished", last_player=-1, game_frames=29999, end_frame=30000)
            summary["players"][0]["units"] = 30

        scenarios = (("win", lambda summary: None), ("loss", make_loss), ("draw", make_draw))
        for name, mutate in scenarios:
            with self.subTest(outcome=name):
                self.game.summary = copy.deepcopy(original)
                mutate(self.game.summary)
                self.game.replies = b"foreign\n" + b"".join(make_reply(frame) for frame in range(3))
                self.game.hyperion_log = "Pluto: slot 1 plays Pluto\nPluto: frame 0 and back\n"
                outcome, reasons, _ = self.game.classify()
                self.assertEqual(outcome, "incomplete")
                self.assertTrue(any("natural-policy" in reason for reason in reasons))
                validation = self.game.validate()
                self.assertTrue(validation["valid"], validation["errors"])
                application = validation["command_application"]
                self.assertTrue(application["sufficient"])
                self.assertFalse(application["application_established"])
                self.assertEqual(application["raw_nonempty_command_frames"], 0)

    def test_partial_filtering_with_nonzero_accepted_bytes_is_positive_native_evidence(self):
        commands = b"\x1a\x00" * 2
        self.game.replies = make_reply(0, commands) + make_reply(1) + make_reply(2)
        self.game.hyperion_log = (
            "Pluto: slot 1 plays Pluto\n"
            "Pluto: frame 0 has 4 cmd bytes\n"
            "Pluto: emitted 0 received 1 dispatched 3 received_bytes 4 accepted_bytes 2\n"
        )
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "win", reasons)
        application = self.game.validate()["command_application"]
        self.assertTrue(application["sufficient"])
        self.assertTrue(application["application_established"])
        self.assertEqual(application["positive_native_dispatch_frames"], 1)
        self.assertEqual(application["fully_filtered_dispatch_frames"], 0)
        self.assertEqual(application["partially_filtered_dispatch_frames"], 1)

    def test_only_wholly_filtered_commands_are_sufficient_but_not_natural_policy_application(self):
        commands = b"\x1a\x00" * 2
        self.game.replies = make_reply(0, commands) + make_reply(1, commands) + make_reply(2)
        self.game.hyperion_log = (
            "Pluto: slot 1 plays Pluto\n"
            "Pluto: frame 0 has 4 cmd bytes\n"
            "Pluto: frame 1 has 4 cmd bytes\n"
            "Pluto: emitted 0 received 1 dispatched 3 received_bytes 4 accepted_bytes 0\n"
            "Pluto: emitted 1 received 2 dispatched 4 received_bytes 4 accepted_bytes 0\n"
        )
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "incomplete")
        self.assertTrue(any("natural-policy" in reason for reason in reasons))
        application = self.game.validate()["command_application"]
        self.assertTrue(application["sufficient"])
        self.assertEqual(application["receipt_mismatches"], [])
        self.assertEqual(application["dispatch_mismatches"], [])
        self.assertEqual(application["missing_dispatch"], [])
        self.assertEqual(application["fully_filtered_dispatch_frames"], 2)
        self.assertEqual(application["positive_native_dispatch_frames"], 0)
        self.assertEqual(application["status"], "filtering_corroborated_without_nonzero_application")
        self.assertFalse(application["application_established"])

    def test_explicit_received_and_accepted_byte_count_mismatches_are_rejected(self):
        commands = b"\x1a\x00" * 2
        scenarios = (("raw_mismatch", 3, 2), ("post_translation_overcount", 4, 5))
        for name, received_bytes, accepted_bytes in scenarios:
            with self.subTest(name=name):
                self.game.replies = make_reply(0, commands) + make_reply(1) + make_reply(2)
                self.game.hyperion_log = (
                    "Pluto: slot 1 plays Pluto\n"
                    "Pluto: frame 0 has 4 cmd bytes\n"
                    "Pluto: emitted 0 received 1 dispatched 3 received_bytes %d accepted_bytes %d\n"
                    % (received_bytes, accepted_bytes)
                )
                outcome, _, _ = self.game.classify()
                self.assertEqual(outcome, "incomplete")
                application = self.game.validate()["command_application"]
                self.assertFalse(application["sufficient"])
                self.assertFalse(application["application_established"])
                self.assertEqual(len(application["dispatch_mismatches"]), 1)

    def test_old_ambiguous_bytes_dispatch_log_is_rejected(self):
        self.game.replies = make_reply(0, b"\x1a\x00") + make_reply(1) + make_reply(2)
        self.game.hyperion_log = (
            "Pluto: slot 1 plays Pluto\n"
            "Pluto: frame 0 has 2 cmd bytes\n"
            "Pluto: emitted 0 received 1 dispatched 3 bytes 2\n"
        )
        outcome, reasons, _ = self.game.classify()
        self.assertEqual(outcome, "incomplete")
        self.assertTrue(any("legacy_dispatch_records" in reason for reason in reasons))
        application = self.game.validate()["command_application"]
        self.assertEqual(application["status"], "ambiguous_legacy_dispatch_evidence")
        self.assertEqual(len(application["legacy_dispatch_records"]), 1)
        self.assertEqual(application["dispatch_log_records"], [])
        self.assertFalse(application["application_established"])


class TargetAndBanditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_unmeasured_target_has_no_invented_number(self):
        path = self.root / "target.json"
        document = pa.create_target_file(path, None, True, None, ">=", None)
        self.assertIsNone(document["target"])
        self.assertEqual(document["baseline"]["status"], "unmeasured")
        guard = pa.BaselineGuard(path)
        self.assertEqual(guard.document, document)
        with self.assertRaises(pa.AcceptanceError):
            pa.create_target_file(self.root / "bad.json", None, True, "wins_total", ">=", 1)

    def test_measured_target_requires_and_preserves_evidence(self):
        evidence = self.root / "baseline.json"
        write_json(evidence, {"aggregate": {"valid_games": 10, "wins": 2, "losses": 8}})
        target = self.root / "target.json"
        document = pa.create_target_file(target, evidence, False, "wins_total", ">=", 3)
        self.assertEqual(document["baseline"]["metrics"]["wins_total"], 2)
        guard = pa.BaselineGuard(target)
        guard.check()
        target.chmod(0o644)
        target.write_text("tampered", encoding="utf-8")
        with self.assertRaises(pa.BaselineChanged):
            guard.check()

    def test_target_without_baseline_metric_is_rejected(self):
        evidence = self.root / "baseline.json"
        write_json(evidence, {"aggregate": {"valid_games": 10, "wins": 0, "losses": 0}})
        with self.assertRaises(pa.AcceptanceError):
            pa.create_target_file(self.root / "target.json", evidence, False, "win_rate_decided", ">", 0.5)

    def test_target_file_is_created_exclusively(self):
        target = self.root / "target.json"
        pa.create_target_file(target, None, True, None, ">=", None)
        with self.assertRaises(pa.AcceptanceError):
            pa.create_target_file(target, None, True, None, ">=", None)

    def test_clean_bandit_is_read_without_modification(self):
        path = self.root / "pluto_bandit_Bravo.txt"
        path.write_text(valid_bandit_text(), encoding="utf-8")
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        record = pa.read_bandit(path)
        self.assertEqual(record["seq"], 7)
        self.assertEqual(record["arms"], 2)
        self.assertEqual(record["records"], 2)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_unclean_bandit_is_rejected(self):
        bad_values = (
            "seq 1\nformat 2\nZ Z free 0 0\n",  # no end
            "seq 1\nformat 2\nZ Z free 0 0\nend\nend\n",
            "seq 1\nformat 2\nZ Z free 0 0\n{bad json}\nend\n",
            "seq 1\nformat 2\nnot an arm\nend\n",
        )
        for index, value in enumerate(bad_values):
            with self.subTest(index=index):
                path = self.root / ("bad-%d.txt" % index)
                path.write_text(value, encoding="utf-8")
                with self.assertRaises(pa.AcceptanceError):
                    pa.read_bandit(path)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.map_root = self.root / "ASL"
        for index in range(32):
            path = self.map_root / ("S%02d" % index) / ("(2)Map%02d.scx" % index)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("unique synthetic map %d" % index).encode())
        # Same content as map zero: not a distinct map for acceptance.
        duplicate = self.map_root / "S00" / "(2)Duplicate.scx"
        duplicate.write_bytes(b"unique synthetic map 0")
        excluded = self.map_root / "S00" / "(3)ThreePlayer.scx"
        excluded.write_bytes(b"three")
        observer = self.map_root / "S00" / "(2)Map00(Ob).scx"
        observer.write_bytes(b"observer")
        # Discovery trusts only this explicit reviewed settings/hash attestation,
        # never the synthetic bytes' suffix or "(2)" filename by themselves.
        reviewed = [self.map_root / ("S%02d" % index) / ("(2)Map%02d.scx" % index) for index in range(32)]
        write_map_manifest(self.map_root, reviewed + [duplicate])
        self.engine = self.root / "Hyperion.x86_64"
        self.engine.write_bytes(b"synthetic engine")
        self.data = self.root / "Data"
        (self.data / "benchmarks").mkdir(parents=True)
        self.pluto = self.data / "pluto"
        self.inference = self.pluto / "pluto" / "pluto_infer.exe"
        self.inference.parent.mkdir(parents=True)
        self.inference.write_bytes(b"synthetic official inference")
        self.official_dll = self.pluto / "pluto.dll"
        self.official_dll.write_bytes(b"synthetic official dll")
        (self.pluto / "pluto-bridge.exe").write_bytes(b"synthetic bridge")
        (self.pluto / "pluto" / "pluto_weights.bin").write_bytes(b"synthetic weights")
        self._weights_hash = pa.OFFICIAL_WEIGHTS_SHA256
        pa.OFFICIAL_WEIGHTS_SHA256 = hashlib.sha256((self.pluto / "pluto" / "pluto_weights.bin").read_bytes()).hexdigest()
        self.addCleanup(setattr, pa, "OFFICIAL_WEIGHTS_SHA256", self._weights_hash)
        self._dll_hash = pa.OFFICIAL_DLL_SHA256
        pa.OFFICIAL_DLL_SHA256 = hashlib.sha256(self.official_dll.read_bytes()).hexdigest()
        self.addCleanup(setattr, pa, "OFFICIAL_DLL_SHA256", self._dll_hash)
        self.bandit = self.pluto / "bwapi-data" / "write" / "pluto_bandit_Bravo.txt"
        self.bandit.parent.mkdir(parents=True)
        self.bandit.write_text(valid_bandit_text(), encoding="utf-8")
        self.target = self.root / "target.json"
        pa.create_target_file(self.target, None, True, None, ">=", None)
        self.config = pa.RunnerConfig(
            engine=self.engine,
            data_folder=self.data,
            pluto_folder=self.pluto,
            inference=self.inference,
            bandit=self.bandit,
            output=self.root / "fresh-series",
            baseline_target=self.target,
            map_root=self.map_root,
            map_manifest=self.map_root / "pluto-acceptance-maps.json",
            official_dll=self.official_dll,
        ).resolved()

    def tearDown(self):
        self.temporary.cleanup()

    def checked_preflight(self, config=None):
        inspector = mock.Mock()
        inspector.conflicts.return_value = []
        expected = hashlib.sha256(self.inference.read_bytes()).hexdigest()
        with mock.patch.object(pa, "OFFICIAL_INFERENCE_SHA256", expected):
            return pa.preflight(config or self.config, inspector)

    def test_schedule_is_exactly_30_balanced_natural_matchups(self):
        schedule = pa.build_schedule(self.config)
        self.assertEqual(len(schedule), 30)
        self.assertEqual(len({item.map_path for item in schedule}), 30)
        self.assertEqual(len({item.map_sha256 for item in schedule}), 30)
        for race in pa.RACES:
            own = [item for item in schedule if item.pluto_race == race]
            self.assertEqual(len(own), 10)
            self.assertEqual({item.opponent_race for item in own}, set(pa.RACES))
        matchups = {(item.pluto_race, item.opponent_race) for item in schedule}
        self.assertEqual(len(matchups), 9)
        for item in schedule:
            command = pa.command_for(item, self.config)
            self.assertEqual(command[command.index("--bench-frames") + 1], "30000")
            self.assertEqual(command[command.index("--players") + 1], "2")
            self.assertEqual(command[command.index("--ai-levels") + 1].count("p"), 1)
            self.assertEqual(command[command.index("--ai-levels") + 1].count("i"), 1)
            self.assertNotIn("--mutators", command)
            self.assertNotIn("--bench-army", command)

    def test_dry_run_writes_manifest_only_and_never_calls_popen(self):
        expected = hashlib.sha256(self.inference.read_bytes()).hexdigest()
        inspector = mock.Mock()
        inspector.conflicts.return_value = []
        with mock.patch.object(pa, "OFFICIAL_INFERENCE_SHA256", expected):
            checked = pa.preflight(self.config, inspector)
            with mock.patch.object(pa.subprocess, "Popen", side_effect=AssertionError("dry-run launched a process")):
                manifest_path = pa.dry_run(self.config, checked)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["mode"], "dry-run")
        self.assertEqual(len(manifest["games"]), 30)
        self.assertFalse(manifest["historical_results_used_as_games"])
        self.assertFalse((self.config.output / "results.json").exists())
        self.assertFalse((self.config.output / "report.md").exists())
        self.assertTrue((self.config.output / "bandit-before-series.txt").is_file())

    def test_existing_output_is_refused_not_resumed(self):
        self.config.output.mkdir()
        (self.config.output / "results.json").write_text("[]", encoding="utf-8")
        inspector = mock.Mock()
        inspector.conflicts.return_value = []
        expected = hashlib.sha256(self.inference.read_bytes()).hexdigest()
        with mock.patch.object(pa, "OFFICIAL_INFERENCE_SHA256", expected):
            with self.assertRaises(pa.AcceptanceError):
                pa.preflight(self.config, inspector)

    def test_official_hash_mismatch_is_fatal(self):
        with self.assertRaises(pa.AcceptanceError):
            pa.verify_binary(self.inference, "0" * 64, "official inference")
        record = pa.verify_binary(self.inference, hashlib.sha256(self.inference.read_bytes()).hexdigest(), "official inference")
        self.assertTrue(record["verified"])

    def test_plain_text_maps_without_a_reviewed_manifest_are_not_discoverable(self):
        root = self.root / "unreviewed"
        for index in range(30):
            path = root / ("S%02d" % index) / ("(2)Plain%02d.scx" % index)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("plain text is not map evidence", encoding="utf-8")
        with self.assertRaises(pa.AcceptanceError):
            pa.discover_maps(root)

    def test_reviewed_map_hash_and_settings_are_mandatory(self):
        document = pa.load_map_manifest(self.map_root, self.config.map_manifest)
        self.assertEqual(len(pa.discover_maps(self.map_root, self.config.map_manifest)), 30)
        tampered = copy.deepcopy(document)
        tampered.pop("_normalized_maps")
        tampered["maps"][0]["players"] = 3
        write_json(self.config.map_manifest, tampered)
        with self.assertRaises(pa.AcceptanceError):
            pa.discover_maps(self.map_root, self.config.map_manifest)
        write_map_manifest(
            self.map_root,
            [self.map_root / ("S%02d" % index) / ("(2)Map%02d.scx" % index) for index in range(32)]
            + [self.map_root / "S00" / "(2)Duplicate.scx"],
        )
        first_map = self.map_root / "S00" / "(2)Map00.scx"
        first_map.write_bytes(b"changed after review")
        with self.assertRaises(pa.AcceptanceError):
            pa.discover_maps(self.map_root, self.config.map_manifest)

    def test_every_critical_binary_and_scheduled_map_recheck_detects_changes(self):
        checked = self.checked_preflight()
        expected_labels = {
            "engine", "inference", "official_unmodified_dll", "deployed_dll", "bridge", "weights",
            "reviewed_map_manifest",
        }
        self.assertEqual(set(checked.integrity.files), expected_labels)
        for label, initial in checked.integrity.files.items():
            with self.subTest(input=label):
                path = Path(initial["path"])
                original = path.read_bytes()
                current = dict(initial)
                current.update(pa.file_fingerprint(path))
                guard = pa.SeriesIntegrity(
                    {label: current},
                    [],
                    {"file": pa.file_fingerprint(self.config.map_manifest)},
                )
                try:
                    path.write_bytes(b"series-critical mutation")
                    with self.assertRaises(pa.AcceptanceError):
                        guard.check()
                finally:
                    path.write_bytes(original)
        spec = checked.schedule[0]
        guard = pa.SeriesIntegrity({}, [spec], {"file": pa.file_fingerprint(self.config.map_manifest)})
        original = spec.map_path.read_bytes()
        try:
            spec.map_path.write_bytes(b"scheduled map mutation")
            with self.assertRaises(pa.AcceptanceError):
                guard.check()
        finally:
            spec.map_path.write_bytes(original)

    def test_integrity_failure_blocks_launch_and_final_results(self):
        checked = self.checked_preflight()
        self.config.output.mkdir()
        previous = pa.read_bandit(self.bandit)
        inspector = mock.Mock()
        inspector.conflicts.return_value = []
        with mock.patch.object(checked.integrity, "check", side_effect=pa.AcceptanceError("synthetic integrity change")):
            with mock.patch.object(pa, "execute_command") as execute:
                with self.assertRaises(pa.AcceptanceError):
                    pa.run_one(checked.schedule[0], self.config, previous, inspector, checked.integrity)
                execute.assert_not_called()
            with self.assertRaises(pa.AcceptanceError):
                pa._write_results(self.config, checked.target, {}, checked.integrity)
        self.assertFalse((self.config.output / "results.json").exists())

    def test_live_exit_is_nonzero_for_false_target_and_unevaluable_series(self):
        evidence = self.root / "exit-baseline.json"
        write_json(evidence, {"aggregate": {"valid_games": 30, "wins": 0, "losses": 30}})
        scenarios = (("false-target", 31, False, 1), ("not-evaluable", 0, True, 2))
        for name, target_value, include_incomplete, expected_exit in scenarios:
            with self.subTest(name=name):
                target = self.root / (name + ".json")
                pa.create_target_file(target, evidence, False, "wins_total", ">=", target_value)
                config = dataclasses.replace(
                    self.config,
                    baseline_target=target,
                    output=self.root / (name + "-output"),
                ).resolved()
                checked = self.checked_preflight(config)

                def fake_run_one(spec, _config, previous, _inspector, _integrity):
                    outcome = "incomplete" if include_incomplete and spec.game == 1 else "win"
                    accepted = outcome == "win"
                    metrics = {
                        "survivors": 5,
                        "units_made": 20,
                        "units_lost": 10,
                        "units_killed": 8,
                        "minerals": 50,
                        "gas": 25,
                        "minerals_gathered": 500,
                        "gas_gathered": 100,
                    }
                    return {
                        **spec.public(),
                        "outcome": outcome,
                        "reasons": [],
                        "include_in_performance_metrics": accepted,
                        "config_valid": True,
                        "game_frames": 100 if accepted else None,
                        "wall_time_seconds": 1.0,
                        "pluto": metrics if accepted else None,
                        "opponent": metrics if accepted else None,
                        "natural_build": {"build_order": "synthetic"},
                    }, previous, True

                inspector = mock.Mock()
                inspector.conflicts.return_value = []
                with (
                    mock.patch.object(pa, "ProcessInspector", return_value=inspector),
                    mock.patch.object(pa, "run_one", side_effect=fake_run_one) as run_one,
                    mock.patch.object(pa, "execute_command", side_effect=AssertionError("test launched a game")),
                    mock.patch.object(pa, "_write_results"),
                ):
                    exit_code = pa.live_run(config, checked)
                self.assertEqual(exit_code, expected_exit)
                self.assertEqual(run_one.call_count, 30)

    def test_artifact_collection_does_not_self_copy_engine_log(self):
        game_dir = self.root / "artifact-game"
        game_dir.mkdir()
        (game_dir / "engine.log").write_text("synthetic", encoding="utf-8")
        collection = pa.collect_artifacts(
            pa.build_schedule(self.config)[0],
            self.config,
            game_dir,
            set(),
            {},
            {"launched_epoch_ns": time.time_ns()},
        )
        self.assertTrue(collection["artifacts"]["engine.log"]["exists"])
        self.assertIn("sha256", collection["artifacts"]["engine.log"])

    def test_timeout_requests_immediate_owned_group_cleanup(self):
        game_dir = self.root / "timeout-game"
        game_dir.mkdir()
        process = mock.Mock(pid=43210)
        process.wait.side_effect = pa.subprocess.TimeoutExpired("synthetic", 1)
        process.poll.return_value = None
        popen = mock.Mock(return_value=process)
        with mock.patch.object(pa, "cleanup_process_group", return_value={"group_gone": True}) as cleanup:
            execution = pa.execute_command(["synthetic"], self.config, game_dir, 1, popen_factory=popen)
        self.assertTrue(execution["timed_out"])
        self.assertTrue(cleanup.call_args.kwargs["immediate"])
        self.assertTrue(popen.call_args.kwargs["start_new_session"])

    def test_natural_environment_clears_inference_and_test_overrides(self):
        with mock.patch.dict(
            pa.os.environ,
            {
                "BWRL_RESIGN_THRESHOLD": "0.1",
                "BWRL_FRAME_BUDGET_MS": "1",
                "HX_DEBUG": "1",
                "HX_TEST_MORPH": "1",
                "HX_TEST_RELEASE_DELAY_MS": "1000",
                "HX_TEST_SYNC_CLIENT": "1",
                "HX_TEST_ASYNC_CLIENT": "1",
            },
            clear=False,
        ):
            removals = pa.environment_removals()
            environment = pa.environment_for(self.config)
            record = pa.command_record(pa.build_schedule(self.config)[0], self.config)
        self.assertNotIn("BWRL_RESIGN_THRESHOLD", environment)
        self.assertNotIn("BWRL_FRAME_BUDGET_MS", environment)
        self.assertNotIn("HX_DEBUG", environment)
        self.assertNotIn("HX_TEST_MORPH", environment)
        self.assertNotIn("HX_TEST_RELEASE_DELAY_MS", environment)
        self.assertNotIn("HX_TEST_SYNC_CLIENT", environment)
        self.assertNotIn("HX_TEST_ASYNC_CLIENT", environment)
        self.assertIn("HX_TEST_RELEASE_DELAY_MS", removals)
        self.assertIn("BWRL_RESIGN_THRESHOLD", removals)
        self.assertEqual(record["environment_removals"], removals)
        self.assertEqual(environment["HYPERION_PLUTO_DUMP"], "1")

    def test_bandit_change_between_games_refuses_launch_not_reset(self):
        self.config.output.mkdir()
        schedule = pa.build_schedule(self.config)
        spec = schedule[0]
        integrity = pa.SeriesIntegrity({}, schedule, {"file": pa.file_fingerprint(self.config.map_manifest)})
        previous = pa.read_bandit(self.bandit)
        self.bandit.write_text(valid_bandit_text(seq=8), encoding="utf-8")
        inspector = mock.Mock()
        with mock.patch.object(pa, "execute_command") as execute:
            with self.assertRaises(pa.AcceptanceError):
                pa.run_one(spec, self.config, previous, inspector, integrity)
        execute.assert_not_called()
        self.assertEqual(pa.read_bandit(self.bandit)["seq"], 8)

    def test_malformed_post_game_bandit_is_preserved_and_stops_series(self):
        self.config.output.mkdir()
        schedule = pa.build_schedule(self.config)
        spec = schedule[0]
        integrity = pa.SeriesIntegrity({}, schedule, {"file": pa.file_fingerprint(self.config.map_manifest)})
        previous = pa.read_bandit(self.bandit)
        inspector = mock.Mock()
        inspector.conflicts.return_value = []

        def fake_execute(command, config, game_dir, timeout):
            (game_dir / "engine.log").write_text("synthetic crash\n", encoding="utf-8")
            self.bandit.write_text("truncated write", encoding="utf-8")
            return {
                "started_utc": "synthetic",
                "finished_utc": "synthetic",
                "launched_epoch_ns": time.time_ns(),
                "wall_time_seconds": 1.0,
                "return_code": -1,
                "timed_out": False,
                "interrupted": False,
                "launch_error": None,
                "cleanup": {"group_gone": True},
            }

        with mock.patch.object(pa, "execute_command", side_effect=fake_execute):
            record, after, safe = pa.run_one(spec, self.config, previous, inspector, integrity)
        self.assertFalse(safe)
        self.assertIsNone(after)
        self.assertEqual(record["outcome"], "incomplete")
        self.assertEqual((self.config.output / "game-01" / "bandit-after.txt").read_text(), "truncated write")
        self.assertIn("continuity_error", record["bandit"])


class HostLockTests(unittest.TestCase):
    def test_real_fcntl_contention_and_release_across_processes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "locks" / "acceptance.lock"
            path.parent.mkdir()
            child_code = (
                "import fcntl,sys; "
                "f=open(sys.argv[1],'a+b'); "
                "fcntl.flock(f.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB); "
                "print('locked',flush=True); "
                "sys.stdin.read(); "
                "fcntl.flock(f.fileno(),fcntl.LOCK_UN); f.close()"
            )
            with subprocess.Popen(
                [sys.executable, "-c", child_code, str(path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            ) as child:
                try:
                    self.assertEqual(child.stdout.readline().strip(), "locked")
                    with self.assertRaises(pa.AcceptanceError):
                        with pa.host_lock(path):
                            self.fail("a second process acquired a contended host lock")
                finally:
                    child.stdin.close()
                    child.wait(timeout=10)
                self.assertEqual(child.returncode, 0, child.stderr.read())
            with pa.host_lock(path):
                self.assertTrue(path.is_file())
            # The context manager also released its own lock for another holder.
            with pa.host_lock(path):
                pass


class AggregateTests(unittest.TestCase):
    def record(self, game, race, outcome):
        accepted = outcome in ("win", "loss", "draw")
        return {
            "game": game,
            "pluto_race": race,
            "opponent_race": "terran" if race != "terran" else "zerg",
            "map_relative": "map-%d" % game,
            "outcome": outcome,
            "reasons": [],
            "include_in_performance_metrics": accepted,
            "config_valid": outcome != "misconfigured",
            "game_frames": 100 if accepted else None,
            "wall_time_seconds": 2.0,
            "pluto": {
                "survivors": 5,
                "units_made": 20,
                "units_lost": 10,
                "units_killed": 8,
                "minerals": 50,
                "gas": 25,
                "minerals_gathered": 500,
                "gas_gathered": 100,
            } if accepted else None,
            "opponent": {
                "survivors": 0,
                "units_made": 20,
                "units_lost": 15,
                "units_killed": 8,
                "minerals": 20,
                "gas": 10,
                "minerals_gathered": 400,
                "gas_gathered": 80,
            } if accepted else None,
            "natural_build": {"build_order": "9PoolSpeed"} if outcome != "misconfigured" else None,
        }

    def test_aggregate_keeps_incomplete_and_misconfigured_out_of_wins_and_metrics(self):
        games = [
            self.record(1, "zerg", "win"),
            self.record(2, "terran", "loss"),
            self.record(3, "protoss", "draw"),
            self.record(4, "zerg", "incomplete"),
            self.record(5, "zerg", "misconfigured"),
            self.record(6, "zerg", "not_run"),
        ]
        aggregate = pa.aggregate_results(games, "aborted")
        self.assertEqual(aggregate["wins"], 1)
        self.assertEqual(aggregate["losses"], 1)
        self.assertEqual(aggregate["draws"], 1)
        self.assertEqual(aggregate["incomplete"], 1)
        self.assertEqual(aggregate["misconfigured"], 1)
        self.assertEqual(aggregate["not_run"], 1)
        self.assertEqual(aggregate["valid_games"], 3)
        self.assertEqual(aggregate["metrics"]["pluto_units_killed"], 24)
        self.assertEqual(aggregate["wins_by_race"], {"zerg": 1, "terran": 0, "protoss": 0})
        self.assertEqual(aggregate["win_rate_decided"], 0.5)

    def test_unset_target_is_not_a_pass_or_fail_claim(self):
        target = {"baseline": {"status": "unmeasured"}, "target": None}
        aggregate = pa.aggregate_results([], "completed")
        evaluation = pa.evaluate_target(target, aggregate)
        self.assertEqual(evaluation["status"], "not_set")
        self.assertIsNone(evaluation["meets_target"])

    def test_process_exit_distinguishes_observation_target_failure_and_unevaluable_series(self):
        games = [self.record(index + 1, pa.RACES[index % 3], "win") for index in range(30)]
        aggregate = pa.aggregate_results(games, "completed")
        target_doc = {
            "baseline": {"status": "measured"},
            "target": {"metric": "wins_total", "comparison": ">=", "value": 31},
        }
        failed = pa.evaluate_target(target_doc, aggregate)
        self.assertTrue(failed["observation_complete"])
        self.assertFalse(failed["meets_target"])
        self.assertEqual(pa.acceptance_exit_code(aggregate, failed), 1)
        target_doc["target"]["value"] = 30
        passed = pa.evaluate_target(target_doc, aggregate)
        self.assertEqual(pa.acceptance_exit_code(aggregate, passed), 0)
        self.assertTrue(passed["acceptance_passed"])

        observational = pa.evaluate_target({"baseline": {"status": "unmeasured"}, "target": None}, aggregate)
        self.assertEqual(pa.acceptance_exit_code(aggregate, observational), 0)
        self.assertTrue(observational["observation_complete"])
        self.assertIsNone(observational["acceptance_passed"])

        games[4] = self.record(5, games[4]["pluto_race"], "incomplete")
        incomplete = pa.aggregate_results(games, "completed")
        not_evaluable = pa.evaluate_target(target_doc, incomplete)
        self.assertEqual(not_evaluable["status"], "not_evaluable")
        self.assertEqual(pa.acceptance_exit_code(incomplete, not_evaluable), 2)
        self.assertEqual(pa.acceptance_exit_code(incomplete, observational), 2)
        aborted = pa.aggregate_results(games, "aborted")
        self.assertEqual(pa.acceptance_exit_code(aborted, pa.evaluate_target(target_doc, aborted)), 2)

    def test_report_renders_separate_outcomes_and_no_numeric_target_claim(self):
        games = [
            self.record(1, "zerg", "win"),
            self.record(2, "terran", "loss"),
            self.record(3, "protoss", "draw"),
            self.record(4, "zerg", "incomplete"),
            self.record(5, "zerg", "misconfigured"),
            self.record(6, "zerg", "not_run"),
        ]
        aggregate = pa.aggregate_results(games, "aborted")
        target = {"baseline": {"status": "unmeasured"}, "target": None}
        results = {
            "series_id": "synthetic",
            "status": "aborted",
            "finished_utc": "synthetic-time",
            "aggregate": aggregate,
            "target_evaluation": pa.evaluate_target(target, aggregate),
            "games": games,
        }
        report = pa.render_report(results)
        self.assertIn("Zerg Pluto", report)
        self.assertIn("not_set", report)
        self.assertIn("incomplete", report)
        self.assertIn("Kills", report)
        self.assertIn("Natural build-order records", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
