from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from pluto_diff.cli import main
from pluto_diff.compare import CLASS_NAMES, compare_traces, human_report
from pluto_diff.trace import MANIFEST_NAME, load_trace
from tests.helpers import STOP, make_config, make_reply, make_trace


class DifferentialComparisonTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.counter = 0

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def path(self, label: str) -> Path:
        self.counter += 1
        return self.root / f"{self.counter}-{label}"

    def test_exact_equivalence_requires_complete_traces(self) -> None:
        left = make_trace(self.path("left"))
        right = make_trace(self.path("right"))
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "equivalent")
        self.assertIsNone(result["class"])
        self.assertEqual(result["details"]["frames_compared"], 5)

    def test_present_empty_inference_payloads_need_no_fake_hxrp(self) -> None:
        def empty_non_inference_frame(frame: int, data: dict) -> None:
            data["observation"] = b""
            data["model_response"] = b""
            data["bridge_reply"] = None

        left = make_trace(self.path("left"), mutate=empty_non_inference_frame)
        right = make_trace(self.path("right"), mutate=empty_non_inference_frame)
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "equivalent")
        frame = load_trace(left).frames[0]
        self.assertEqual(frame["observation"]["blob"]["size"], 0)
        self.assertEqual(frame["model_response"]["blob"]["size"], 0)
        self.assertIsNone(frame["bridge_reply"])

    def test_class_6_initial_configuration_mismatch(self) -> None:
        left = make_trace(self.path("left"))
        changed = make_config()
        changed["races"] = ["Zerg", "Protoss"]
        right = make_trace(self.path("right"), config=changed)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 6))
        self.assertEqual(result["class_name"], CLASS_NAMES[6])
        self.assertEqual(result["path"], "config.races[1]")
        self.assertEqual(result["left"], "Terran")
        self.assertEqual(result["right"], "Protoss")

    def test_pluto_dll_and_bridge_implementation_hashes_are_class_6_identity(self) -> None:
        for field in ("pluto_dll_hash", "bridge_hash"):
            with self.subTest(field=field):
                left = make_trace(self.path("left"))
                changed = make_config()
                changed[field] = "0" * 64
                right = make_trace(self.path("right"), config=changed)
                result = compare_traces(left, right)
                self.assertEqual((result["status"], result["class"]), ("diverged", 6))
                self.assertEqual(result["path"], f"config.{field}")
                self.assertNotEqual(
                    result["details"]["left_config_hash"],
                    result["details"]["right_config_hash"],
                )

    def test_bandit_history_hash_is_class_6_identity(self) -> None:
        left = make_trace(self.path("left"))
        changed = make_config()
        changed["bandit"]["history_hash"] = "0" * 64
        right = make_trace(self.path("right"), config=changed)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 6))
        self.assertEqual(result["path"], "config.bandit.history_hash")
        self.assertEqual(result["right"], "0" * 64)

    def test_bandit_arm_remains_class_6_identity(self) -> None:
        left = make_trace(self.path("left"))
        changed = make_config()
        changed["bandit"]["arm"] = 3
        right = make_trace(self.path("right"), config=changed)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 6))
        self.assertEqual(result["path"], "config.bandit.arm")
        self.assertEqual((result["left"], result["right"]), (2, 3))

    def test_class_1_hxp_state_input_only_difference(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 2:
                raw = bytearray(data["state_input"])
                raw[-1] ^= 0x80
                data["state_input"] = bytes(raw)

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 1))
        self.assertEqual(result["first_divergent_frame"], 2)
        self.assertTrue(result["path"].startswith("state_input.bytes["))
        self.assertIsInstance(result["left"], int)
        self.assertIsInstance(result["right"], int)
        self.assertEqual(len(result["details"]["left_sha256"]), 64)
        self.assertEqual(len(result["details"]["right_sha256"]), 64)

    def test_class_1_raw_observation_only_difference(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 2:
                raw = bytearray(data["observation"])
                raw[-1] ^= 0x80
                data["observation"] = bytes(raw)

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 1))
        self.assertEqual(result["first_divergent_frame"], 2)
        self.assertTrue(result["path"].startswith("observation.bytes["))
        self.assertIsInstance(result["left"], int)
        self.assertIsInstance(result["right"], int)
        self.assertEqual(len(result["details"]["left_sha256"]), 64)
        self.assertEqual(len(result["details"]["right_sha256"]), 64)

    def test_class_1_relevant_gamedata_input_and_exact_frame(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 4:
                data["game_data"]["players"][0]["minerals"] = 51

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual(result["class"], 1)
        self.assertEqual(result["first_divergent_frame"], 4)
        self.assertEqual(result["path"], "game_data.players[0].minerals")
        self.assertEqual((result["left"], result["right"]), (50, 51))

    def test_class_2_raw_model_response_only_difference(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 1:
                data["model_response"] = b"different-raw-response\x00\xff"

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 2))
        self.assertEqual(result["first_divergent_frame"], 1)
        self.assertTrue(result["path"].startswith("model_response.bytes["))
        self.assertIsInstance(result["left"], int)
        self.assertIsInstance(result["right"], int)
        self.assertEqual(len(result["details"]["left_sha256"]), 64)
        self.assertEqual(len(result["details"]["right_sha256"]), 64)

    def test_class_2_bridge_reply_only_difference(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 1:
                data["bridge_reply"] = make_reply(frame, STOP)

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 2))
        self.assertEqual(result["first_divergent_frame"], 1)
        self.assertTrue(result["path"].startswith("bridge_reply.bytes["))
        self.assertEqual(result["details"]["right_reply"]["commands"][0]["name"], "Stop")

    def test_class_2_emitted_classic_or_received_payload_only_difference(self) -> None:
        for field in ("emitted_commands", "received_commands"):
            with self.subTest(field=field):
                left = make_trace(self.path("left"))

                def mutate(frame: int, data: dict, field: str = field) -> None:
                    if frame == 2:
                        data[field] = STOP

                right = make_trace(self.path("right"), mutate=mutate)
                result = compare_traces(left, right)
                self.assertEqual(result["class"], 2)
                self.assertEqual(result["first_divergent_frame"], 2)
                self.assertTrue(result["path"].startswith(field))
                self.assertEqual(result["details"]["byte_offset"], 0)
                self.assertEqual(result["left"], {"missing": True})
                self.assertEqual(result["right"], 0x1A)
                self.assertEqual(len(result["details"]["left_sha256"]), 64)
                self.assertEqual(len(result["details"]["right_sha256"]), 64)

    def test_class_4_same_command_in_different_frame_records(self) -> None:
        for field, frame_key in (
            ("emitted_commands", "emitted_frame"),
            ("received_commands", "received_frame"),
            ("accepted_commands", "accepted_frame"),
        ):
            with self.subTest(field=field):
                def left_mutate(frame: int, data: dict, field: str = field) -> None:
                    if frame == 1:
                        data[field] = STOP

                def right_mutate(
                    frame: int,
                    data: dict,
                    field: str = field,
                    frame_key: str = frame_key,
                ) -> None:
                    if frame == 2:
                        data[field] = STOP
                        data[frame_key] += 1

                left = make_trace(self.path("left"), mutate=left_mutate)
                right = make_trace(self.path("right"), mutate=right_mutate)
                result = compare_traces(left, right)
                self.assertEqual(result["class"], 4)
                self.assertEqual(result["first_divergent_frame"], 1)
                self.assertEqual(result["path"], f"{field}.event_frame")
                self.assertEqual(result["details"]["left_record_frame"], 1)
                self.assertEqual(result["details"]["right_record_frame"], 2)

    def test_class_3_accepted_native_command_mismatch(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 3:
                data["accepted_commands"] = STOP

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 3))
        self.assertEqual(result["first_divergent_frame"], 3)
        self.assertIn("accepted native commands", result["summary"])

    def test_class_4_emitted_received_accepted_timing(self) -> None:
        for field in ("emitted_frame", "received_frame", "accepted_frame"):
            with self.subTest(field=field):
                left = make_trace(self.path("left"))

                def mutate(frame: int, data: dict, field: str = field) -> None:
                    if frame == 2:
                        data[field] += 1

                right = make_trace(self.path("right"), mutate=mutate)
                result = compare_traces(left, right)
                self.assertEqual(result["class"], 4)
                self.assertEqual(result["first_divergent_frame"], 2)
                self.assertEqual(result["right"] - result["left"], 1)

    def test_class_4_observation_of_change_timing(self) -> None:
        def left_mutate(frame: int, data: dict) -> None:
            if frame == 2:
                data["observation_of_change"] = [
                    {"change_frame": 1, "observed_frame": 2, "unit_id": 7, "field": "x"}
                ]

        def right_mutate(frame: int, data: dict) -> None:
            if frame == 3:
                data["observation_of_change"] = [
                    {"change_frame": 1, "observed_frame": 3, "unit_id": 7, "field": "x"}
                ]

        left = make_trace(self.path("left"), mutate=left_mutate)
        right = make_trace(self.path("right"), mutate=right_mutate)
        result = compare_traces(left, right)
        self.assertEqual(result["class"], 4)
        self.assertEqual(result["first_divergent_frame"], 2)
        self.assertTrue(result["path"].startswith("observation_of_change"))

    def test_class_4_unit_changes_at_t1_t3_t4(self) -> None:
        for left_change, right_change in ((1, 3), (1, 4), (3, 4)):
            with self.subTest(left=left_change, right=right_change):
                def transition(change: int):
                    def mutate(frame: int, data: dict) -> None:
                        if frame >= change:
                            data["unit_state"]["7"]["x"] = 110
                    return mutate

                left = make_trace(self.path("left"), mutate=transition(left_change))
                right = make_trace(self.path("right"), mutate=transition(right_change))
                result = compare_traces(left, right)
                self.assertEqual((result["status"], result["class"]), ("diverged", 4))
                self.assertEqual(result["first_divergent_frame"], min(left_change, right_change))
                self.assertEqual(result["details"]["left_change_frame"], left_change)
                self.assertEqual(result["details"]["right_change_frame"], right_change)
                self.assertEqual((result["left"], result["right"]), ((110, 100) if left_change < right_change else (100, 110)))

    def test_different_resulting_transition_values_are_class_5_not_latency(self) -> None:
        def left_mutate(frame: int, data: dict) -> None:
            if frame >= 1:
                data["unit_state"]["7"]["hp"] = 499

        def right_mutate(frame: int, data: dict) -> None:
            if frame >= 2:
                data["unit_state"]["7"]["hp"] = 498

        left = make_trace(self.path("left"), mutate=left_mutate)
        right = make_trace(self.path("right"), mutate=right_mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 5))
        self.assertEqual(result["first_divergent_frame"], 1)
        self.assertEqual(result["path"], "unit_state.7.hp")
        self.assertEqual((result["left"], result["right"]), (499, 500))

    def test_class_5_same_timing_numeric_simulation_mismatch(self) -> None:
        def left_mutate(frame: int, data: dict) -> None:
            if frame >= 2:
                data["unit_state"]["7"]["hp"] = 499

        def right_mutate(frame: int, data: dict) -> None:
            if frame >= 2:
                data["unit_state"]["7"]["hp"] = 498

        left = make_trace(self.path("left"), mutate=left_mutate)
        right = make_trace(self.path("right"), mutate=right_mutate)
        result = compare_traces(left, right)
        self.assertEqual((result["status"], result["class"]), ("diverged", 5))
        self.assertEqual(result["first_divergent_frame"], 2)
        self.assertEqual((result["left"], result["right"]), (499, 498))
        self.assertEqual(result["path"], "unit_state.7.hp")

    def test_class_5_single_sided_change_is_not_fabricated_timing(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 1:
                data["unit_state"]["7"]["cooldown"] = 9

        right = make_trace(self.path("right"), mutate=mutate)
        result = compare_traces(left, right)
        self.assertEqual(result["class"], 5)
        self.assertEqual(result["first_divergent_frame"], 1)

    def test_exact_numeric_comparison_has_no_epsilon(self) -> None:
        def left_mutate(frame: int, data: dict) -> None:
            if frame == 4:
                data["unit_state"]["7"]["hp"] = 1.0

        def right_mutate(frame: int, data: dict) -> None:
            if frame == 4:
                data["unit_state"]["7"]["hp"] = 1.0000000000000002

        left = make_trace(self.path("left"), mutate=left_mutate)
        right = make_trace(self.path("right"), mutate=right_mutate)
        result = compare_traces(left, right)
        self.assertEqual(result["class"], 5)
        self.assertEqual(result["first_divergent_frame"], 4)

    def test_missing_frame_is_incomplete_not_equality(self) -> None:
        left = make_trace(self.path("left"), frame_count=5)
        right = make_trace(self.path("right"), frame_count=3)
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["first_divergent_frame"], 3)
        self.assertIn("missing", result["summary"])

    def test_missing_stage_is_incomplete_not_skipped(self) -> None:
        left = make_trace(self.path("left"))
        right = make_trace(self.path("right"))
        lines = (right / MANIFEST_NAME).read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            record = json.loads(line)
            if record.get("record") == "frame" and record.get("frame") == 1:
                del record["model_response"]
                lines[index] = json.dumps(record, sort_keys=True, separators=(",", ":"))
        (right / MANIFEST_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["first_divergent_frame"], 1)
        self.assertEqual(result["path"], "model_response")

    def test_null_inference_payload_is_missing_not_present_empty(self) -> None:
        for field in ("observation", "model_response"):
            with self.subTest(field=field):
                left = make_trace(self.path("left"))
                right = make_trace(self.path("right"))
                lines = (right / MANIFEST_NAME).read_text(encoding="utf-8").splitlines()
                for index, line in enumerate(lines):
                    record = json.loads(line)
                    if record.get("record") == "frame" and record.get("frame") == 1:
                        record[field] = None
                        lines[index] = json.dumps(record, sort_keys=True, separators=(",", ":"))
                (right / MANIFEST_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
                result = compare_traces(left, right)
                self.assertEqual(result["status"], "incomplete")
                self.assertEqual(result["first_divergent_frame"], 1)
                self.assertEqual(result["path"], field)

    def test_one_sided_optional_bridge_reply_is_incomplete_coverage(self) -> None:
        def no_bridge(frame: int, data: dict) -> None:
            data["bridge_reply"] = None

        left = make_trace(self.path("left"))
        right = make_trace(self.path("right"), mutate=no_bridge)
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["path"], "bridge_reply")
        self.assertEqual(result["first_divergent_frame"], 0)

    def test_equal_partial_traces_remain_incomplete(self) -> None:
        left = make_trace(self.path("left"), status="partial")
        right = make_trace(self.path("right"), status="partial")
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")
        self.assertIsNone(result["first_divergent_frame"])

    def test_unfinished_trace_without_end_is_partial(self) -> None:
        left = make_trace(self.path("left"), finish=False)
        right = make_trace(self.path("right"), finish=False)
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")

    def test_compare_command_writes_json_and_numeric_human_report(self) -> None:
        left = make_trace(self.path("left"))

        def mutate(frame: int, data: dict) -> None:
            if frame == 2:
                data["game_data"]["players"][0]["minerals"] = 777

        right = make_trace(self.path("right"), mutate=mutate)
        json_path = self.root / "result.json"
        report_path = self.root / "report.txt"
        exit_code = main(
            ["compare", str(left), str(right), "--json", str(json_path), "--report", str(report_path)]
        )
        self.assertEqual(exit_code, 1)
        result = json.loads(json_path.read_text(encoding="utf-8"))
        self.assertEqual(result["first_divergent_frame"], 2)
        report = report_path.read_text(encoding="utf-8")
        self.assertIn("class: 1 (observation/state input)", report)
        self.assertIn("left:  50", report)
        self.assertIn("right: 777", report)
        self.assertEqual(human_report(result), report)

    def test_missing_trace_is_incomplete_result(self) -> None:
        present = make_trace(self.path("left"))
        result = compare_traces(present, self.root / "does-not-exist")
        self.assertEqual(result["status"], "incomplete")


if __name__ == "__main__":
    unittest.main()
