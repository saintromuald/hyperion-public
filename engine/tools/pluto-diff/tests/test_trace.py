from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from typing import Callable

from pluto_diff.cli import main
from pluto_diff.commands import LobbyOnlyCommandError
from pluto_diff.compare import compare_traces
from pluto_diff.trace import (
    MANIFEST_NAME,
    SCHEMA_VERSION,
    IncompleteTraceError,
    TraceFormatError,
    TraceWriter,
    config_hash,
    load_trace,
    validate_trace,
)
from tests.helpers import (
    LOBBY_ONLY_COMMANDS,
    base_unit_state,
    make_config,
    make_frame,
    make_model_response,
    make_observation,
    make_reply,
    make_trace,
)


class TraceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def mutate_frame(
        self, root: Path, frame: int, mutate: Callable[[dict, list[dict]], None]
    ) -> None:
        lines = (root / MANIFEST_NAME).read_text(encoding="utf-8").splitlines()
        records = [json.loads(line) for line in lines]
        for record in records:
            if record.get("record") == "frame" and record.get("frame") == frame:
                mutate(record, records)
                break
        else:
            raise AssertionError(f"frame {frame} not found")
        (root / MANIFEST_NAME).write_text(
            "\n".join(
                json.dumps(record, sort_keys=True, separators=(",", ":"))
                for record in records
            )
            + "\n",
            encoding="utf-8",
        )

    def test_canonical_config_and_content_addressed_blobs(self) -> None:
        root = make_trace(self.root / "trace", frame_count=2)
        trace = load_trace(root)
        self.assertEqual(trace.status, "complete")
        self.assertEqual(trace.header["schema"], SCHEMA_VERSION)
        self.assertEqual(trace.header["config_hash"], config_hash(trace.config))
        self.assertEqual(trace.config["bandit"]["arm"], 2)
        frame = trace.frames[0]
        self.assertEqual(frame["state_input"]["parsed"]["protocol"], "HXP3")
        self.assertEqual(frame["observation"]["format"], "PLSM/BWRL_SHM-request/v1")
        self.assertEqual(frame["model_response"]["format"], "PLSM/BWRL_SHM-response/v1")
        self.assertNotIn("parsed", frame["observation"])
        self.assertNotIn("parsed", frame["model_response"])
        self.assertEqual(frame["bridge_reply"]["parsed"]["magic"], "HXRP")
        empty_refs = [frame[name]["blob"] for name in ("emitted_commands", "received_commands", "accepted_commands")]
        self.assertEqual({ref["sha256"] for ref in empty_refs}, {hashlib.sha256(b"").hexdigest()})
        for ref in empty_refs:
            self.assertEqual(trace.read_blob(ref), b"")
        lines = (root / MANIFEST_NAME).read_bytes().splitlines()
        self.assertTrue(all(line for line in lines))
        self.assertEqual(json.loads(lines[-1])["record"], "end")

    def test_config_requires_explicit_pluto_dll_and_bridge_sha256(self) -> None:
        for field in ("pluto_dll_hash", "bridge_hash"):
            with self.subTest(field=field, case="missing"):
                config = make_config()
                del config[field]
                with self.assertRaisesRegex(TraceFormatError, field):
                    config_hash(config)
            with self.subTest(field=field, case="invalid"):
                config = make_config()
                config[field] = "not-a-sha256"
                with self.assertRaisesRegex(TraceFormatError, field):
                    config_hash(config)

    def test_bandit_requires_meaningful_history_hash(self) -> None:
        invalid_bandits = (
            {},
            {"arm": 2},
            {"arm": 2, "history_hash": None},
            {"arm": 2, "history_hash": "not-a-sha256"},
            {"arm": 2, "history_hash": "A" * 64},
            {"arm": 2, "history_hash": 2},
        )
        for bandit in invalid_bandits:
            with self.subTest(bandit=bandit):
                config = make_config()
                config["bandit"] = bandit
                with self.assertRaisesRegex(TraceFormatError, "bandit.history_hash"):
                    config_hash(config)
        config = make_config()
        config["bandit"] = []
        with self.assertRaisesRegex(TraceFormatError, "bandit must be a JSON object"):
            config_hash(config)

    def test_writer_rejects_frame_gap_and_unstable_unit_universe(self) -> None:
        writer = TraceWriter.create(self.root / "gap", make_config())
        with self.assertRaises(TraceFormatError):
            writer.append_frame(
                frame=1,
                state_input=make_frame(1),
                observation=make_observation(1),
                game_data={},
                model_response=make_model_response(1),
                emitted_commands=b"",
                emitted_frame=1,
                received_commands=b"",
                received_frame=2,
                accepted_commands=b"",
                accepted_frame=5,
                unit_state=base_unit_state(),
                observation_of_change=[],
            )
        writer.close()

        writer = TraceWriter.create(self.root / "units", make_config())
        common = dict(
            game_data={},
            emitted_commands=b"",
            received_commands=b"",
            accepted_commands=b"",
            observation_of_change=[],
        )
        writer.append_frame(
            frame=0,
            state_input=make_frame(0),
            observation=make_observation(0),
            model_response=make_model_response(0),
            emitted_frame=0,
            received_frame=1,
            accepted_frame=4,
            unit_state=base_unit_state(),
            **common,
        )
        with self.assertRaises(TraceFormatError):
            writer.append_frame(
                frame=1,
                state_input=make_frame(1),
                observation=make_observation(1),
                model_response=make_model_response(1),
                emitted_frame=1,
                received_frame=2,
                accepted_frame=5,
                unit_state={},
                **common,
            )
        missing_field = base_unit_state()
        del missing_field["7"]["x"]
        with self.assertRaisesRegex(TraceFormatError, "same numeric fields"):
            writer.append_frame(
                frame=1,
                state_input=make_frame(1),
                observation=make_observation(1),
                model_response=make_model_response(1),
                emitted_frame=1,
                received_frame=2,
                accepted_frame=5,
                unit_state=missing_field,
                **common,
            )
        writer.close()

    def test_writer_rejects_lobby_only_in_game_command_evidence(self) -> None:
        for field in ("emitted_commands", "received_commands", "accepted_commands"):
            for opcode, commands in LOBBY_ONLY_COMMANDS.items():
                with self.subTest(field=field, opcode=opcode):
                    writer = TraceWriter.create(
                        self.root / f"writer-{field}-{opcode:02x}", make_config()
                    )
                    values = dict(
                        frame=0,
                        state_input=make_frame(0),
                        observation=make_observation(0),
                        game_data={},
                        model_response=make_model_response(0),
                        emitted_commands=b"",
                        emitted_frame=0,
                        received_commands=b"",
                        received_frame=1,
                        accepted_commands=b"",
                        accepted_frame=4,
                        unit_state=base_unit_state(),
                        observation_of_change=[],
                    )
                    values[field] = commands
                    with self.assertRaises(LobbyOnlyCommandError):
                        writer.append_frame(**values)
                    writer.close()

        for opcode, commands in LOBBY_ONLY_COMMANDS.items():
            with self.subTest(field="bridge_reply", opcode=opcode):
                writer = TraceWriter.create(
                    self.root / f"writer-bridge-{opcode:02x}", make_config()
                )
                with self.assertRaises(LobbyOnlyCommandError):
                    writer.append_frame(
                        frame=0,
                        state_input=make_frame(0),
                        observation=make_observation(0),
                        game_data={},
                        model_response=make_model_response(0),
                        emitted_commands=b"",
                        emitted_frame=0,
                        received_commands=b"",
                        received_frame=1,
                        accepted_commands=b"",
                        accepted_frame=4,
                        unit_state=base_unit_state(),
                        observation_of_change=[],
                        bridge_reply=make_reply(0, commands),
                    )
                writer.close()

    def test_uncommitted_final_jsonl_line_is_partial_not_committed(self) -> None:
        root = make_trace(self.root / "trace", frame_count=2, finish=False)
        with (root / MANIFEST_NAME).open("ab") as stream:
            stream.write(b'{"record":"frame","frame":2')
        trace = load_trace(root)
        self.assertEqual(trace.status, "partial")
        self.assertEqual(len(trace.frames), 2)
        self.assertIn("truncated", trace.issues[0])

    def test_header_plus_complete_end_without_frame_zero_is_incomplete(self) -> None:
        root = self.root / "empty-complete"
        writer = TraceWriter.create(root, make_config())
        writer.close()
        with (root / MANIFEST_NAME).open("ab") as stream:
            stream.write(
                json.dumps(
                    {
                        "record": "end",
                        "status": "complete",
                        "message": "invalid empty completion",
                        "frame_count": 0,
                        "last_frame": -1,
                    },
                    sort_keys=True,
                    separators=(",", ":"),
                ).encode("utf-8")
                + b"\n"
            )
        trace = load_trace(root)
        self.assertEqual(trace.frames, {})
        self.assertEqual(trace.status, "partial")
        with self.assertRaisesRegex(IncompleteTraceError, "frame 0"):
            validate_trace(trace)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(["validate", str(root)]), 2)
        result = compare_traces(root, root)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["path"], "frames")

    def test_committed_malformed_jsonl_is_invalid(self) -> None:
        root = make_trace(self.root / "trace", finish=False)
        with (root / MANIFEST_NAME).open("ab") as stream:
            stream.write(b"not-json\n")
        with self.assertRaises(TraceFormatError):
            load_trace(root)

    def test_blob_hash_damage_is_invalid_not_a_divergence_class(self) -> None:
        left = make_trace(self.root / "left")
        right = make_trace(self.root / "right")
        reference = load_trace(left).frames[0]["observation"]["blob"]
        path = left / reference["path"]
        damaged = bytearray(path.read_bytes())
        damaged[-1] ^= 1
        path.write_bytes(damaged)
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "invalid")
        self.assertIn("SHA-256", result["summary"])

    def test_missing_blob_is_incomplete(self) -> None:
        left = make_trace(self.root / "left")
        right = make_trace(self.root / "right")
        reference = load_trace(left).frames[0]["observation"]["blob"]
        (left / reference["path"]).unlink()
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["first_divergent_frame"], 0)

    def test_unknown_opcode_in_canonically_referenced_blob_is_invalid(self) -> None:
        left = make_trace(self.root / "left")
        right = make_trace(self.root / "right")
        lines = (left / MANIFEST_NAME).read_text(encoding="utf-8").splitlines()
        raw = b"\xff"
        digest = hashlib.sha256(raw).hexdigest()
        relative = f"blobs/{digest[:2]}/{digest}.bin"
        blob = left / relative
        blob.parent.mkdir(parents=True, exist_ok=True)
        blob.write_bytes(raw)
        for index, line in enumerate(lines):
            record = json.loads(line)
            if record.get("record") == "frame" and record.get("frame") == 1:
                record["accepted_commands"]["blob"] = {"sha256": digest, "size": 1, "path": relative}
                del record["accepted_commands"]["commands"]
                lines[index] = json.dumps(record, sort_keys=True, separators=(",", ":"))
        (left / MANIFEST_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = compare_traces(left, right)
        self.assertEqual(result["status"], "invalid")
        self.assertIn("unknown", result["summary"])

    def test_validate_missing_model_response_fails_as_incomplete(self) -> None:
        root = make_trace(self.root / "missing-response")

        def remove_response(record: dict, _records: list[dict]) -> None:
            del record["model_response"]

        self.mutate_frame(root, 0, remove_response)
        with contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(main(["validate", str(root)]), 2)
        self.assertIn("model_response", stderr.getvalue())
        compared = compare_traces(root, root)
        self.assertEqual(compared["status"], "incomplete")
        self.assertEqual(compared["first_divergent_frame"], 0)
        self.assertEqual(compared["path"], "model_response")

    def test_validate_and_compare_reject_malformed_frame_structure(self) -> None:
        mutations = {
            "opaque-holder": lambda record: record.__setitem__("model_response", []),
            "state-cache": lambda record: record["state_input"].pop("parsed"),
            "command-cache": lambda record: record["accepted_commands"].pop("commands"),
            "change-events": lambda record: record.__setitem__("observation_of_change", {}),
            "unit-exists": lambda record: record["unit_state"]["7"].pop("exists"),
        }
        for label, mutation in mutations.items():
            with self.subTest(label=label):
                root = make_trace(self.root / label)
                self.mutate_frame(
                    root, 0, lambda record, _records, change=mutation: change(record)
                )
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(main(["validate", str(root)]), 3)
                result = compare_traces(root, root)
                self.assertEqual(result["status"], "invalid")

    def test_validate_and_compare_reject_lobby_only_command_blobs(self) -> None:
        for opcode, raw in LOBBY_ONLY_COMMANDS.items():
            with self.subTest(opcode=opcode):
                root = make_trace(self.root / f"lobby-blob-{opcode:02x}")
                digest = hashlib.sha256(raw).hexdigest()
                relative = f"blobs/{digest[:2]}/{digest}.bin"
                blob = root / relative
                blob.parent.mkdir(parents=True, exist_ok=True)
                blob.write_bytes(raw)

                def replace_accepted(record: dict, _records: list[dict]) -> None:
                    record["accepted_commands"]["blob"] = {
                        "sha256": digest,
                        "size": len(raw),
                        "path": relative,
                    }

                self.mutate_frame(root, 0, replace_accepted)
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(main(["validate", str(root)]), 3)
                result = compare_traces(root, root)
                self.assertEqual(result["status"], "invalid")
                self.assertIn("lobby-only", result["summary"])

    def test_unstable_unit_field_coverage_is_incomplete_even_on_both_sides(self) -> None:
        root = make_trace(self.root / "unstable-fields")

        def remove_x(record: dict, _records: list[dict]) -> None:
            del record["unit_state"]["7"]["x"]

        self.mutate_frame(root, 1, remove_x)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(["validate", str(root)]), 2)
        result = compare_traces(root, root)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["first_divergent_frame"], 1)
        self.assertEqual(result["path"], "unit_state")

    def test_hxrp_internal_frame_must_equal_its_frame_record(self) -> None:
        writer = TraceWriter.create(self.root / "wrong-writer-reply", make_config())
        with self.assertRaisesRegex(TraceFormatError, "internal frame 1.*frame 0|internal frame 1 does not match 0"):
            writer.append_frame(
                frame=0,
                state_input=make_frame(0),
                observation=make_observation(0),
                game_data={},
                model_response=make_model_response(0),
                emitted_commands=b"",
                emitted_frame=0,
                received_commands=b"",
                received_frame=1,
                accepted_commands=b"",
                accepted_frame=4,
                unit_state=base_unit_state(),
                observation_of_change=[],
                bridge_reply=make_reply(1),
            )
        writer.close()

        root = make_trace(self.root / "wrong-reader-reply")

        def replace_with_frame_one_reply(record: dict, records: list[dict]) -> None:
            other = next(
                value
                for value in records
                if value.get("record") == "frame" and value.get("frame") == 1
            )
            record["bridge_reply"] = other["bridge_reply"]

        self.mutate_frame(root, 0, replace_with_frame_one_reply)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(["validate", str(root)]), 3)
        result = compare_traces(root, root)
        self.assertEqual(result["status"], "invalid")
        self.assertIn("internal frame 1", result["summary"])

    def test_cli_capture_workflow(self) -> None:
        trace = self.root / "cli-trace"
        config = self.root / "config.json"
        config.write_text(json.dumps(make_config()), encoding="utf-8")
        state_input = self.root / "frame.hxp3"
        state_input.write_bytes(make_frame(0))
        observation = self.root / "inference-request.bin"
        observation.write_bytes(make_observation(0))
        response = self.root / "inference-response.bin"
        response.write_bytes(make_model_response(0))
        bridge_reply = self.root / "reply.hxrp"
        bridge_reply.write_bytes(make_reply(0))
        empty = self.root / "empty.bin"
        empty.write_bytes(b"")
        spec = self.root / "frame.json"
        spec.write_text(
            json.dumps(
                {
                    "frame": 0,
                    "game_data": {"frame": 0, "minerals": 50},
                    "emitted_frame": 0,
                    "received_frame": 1,
                    "accepted_frame": 4,
                    "unit_state": base_unit_state(),
                    "observation_of_change": [],
                }
            ),
            encoding="utf-8",
        )
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(["init", str(trace), "--config", str(config)]), 0)
            self.assertEqual(
                main(
                    [
                        "add",
                        str(trace),
                        "--spec",
                        str(spec),
                        "--state-input",
                        str(state_input),
                        "--observation",
                        str(observation),
                        "--model-response",
                        str(response),
                        "--bridge-reply",
                        str(bridge_reply),
                        "--emitted",
                        str(empty),
                        "--received",
                        str(empty),
                        "--accepted",
                        str(empty),
                    ]
                ),
                0,
            )
            self.assertEqual(main(["finish", str(trace)]), 0)
            self.assertEqual(main(["validate", str(trace)]), 0)
        loaded = load_trace(trace)
        self.assertEqual(loaded.status, "complete")
        self.assertEqual(loaded.frames[0]["game_data"]["minerals"], 50)


if __name__ == "__main__":
    unittest.main()
