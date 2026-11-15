#!/usr/bin/env python3
"""Host-only tests for the macOS acceptance harness; no Wine or official payload is used."""

import hashlib
from pathlib import Path
import signal
import struct
import tempfile
import unittest
import zipfile

import genframes
import macos_runtime_acceptance as acceptance
from pluto_diff.hxp import parse_frame_stream


class SyntheticFrameTests(unittest.TestCase):
    def test_generator_is_strict_public_hxpf(self):
        payload = genframes.stream(54)
        frames = parse_frame_stream(payload)
        self.assertEqual(len(frames), 54)
        self.assertEqual([frame.frame for frame in frames], list(range(54)))
        self.assertTrue(all(frame.protocol == "HXPF" for frame in frames))
        self.assertEqual(payload[:4], b"HXFP")
        self.assertEqual(payload, genframes.stream(54))
        with self.assertRaisesRegex(ValueError, "invalid HXP frame magic"):
            parse_frame_stream(b"HXPF" + payload[4:])


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.payloads = {
            "pluto.dll": b"synthetic DLL",
            "pluto/pluto_infer.exe": b"synthetic inference",
            "pluto/pluto_weights.bin": b"synthetic weights",
        }
        self.archive = self.root / "official.zip"
        with zipfile.ZipFile(self.archive, "w") as package:
            for name, data in self.payloads.items():
                package.writestr(name, data)
        self.spec = acceptance.ArchiveSpec(
            sha256=hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            size=self.archive.stat().st_size,
            payloads=tuple(
                acceptance.PayloadSpec(name, hashlib.sha256(data).hexdigest(), size=len(data))
                for name, data in self.payloads.items()
            ),
        )

    def tearDown(self):
        self.temporary.cleanup()

    def test_verified_extract_selects_exact_payloads(self):
        runtime = self.root / "runtime"
        evidence = acceptance.extract_official_runtime(self.archive, runtime, self.spec)
        self.assertEqual(set(evidence["payloads"]), set(self.payloads))
        for name, data in self.payloads.items():
            self.assertEqual((runtime / name).read_bytes(), data)
        self.assertTrue((runtime / "pluto" / "pluto_infer.exe").stat().st_mode & 0o111)

    def test_outer_hash_mismatch_extracts_nothing(self):
        runtime = self.root / "runtime"
        spec = acceptance.ArchiveSpec("0" * 64, self.spec.size, self.spec.payloads)
        with self.assertRaisesRegex(RuntimeError, "ZIP SHA-256"):
            acceptance.extract_official_runtime(self.archive, runtime, spec)
        self.assertFalse(runtime.exists())

    def test_inner_hash_mismatch_removes_partial_runtime(self):
        runtime = self.root / "runtime"
        payloads = list(self.spec.payloads)
        payloads[1] = acceptance.PayloadSpec(payloads[1].name, "0" * 64, size=payloads[1].size)
        spec = acceptance.ArchiveSpec(self.spec.sha256, self.spec.size, tuple(payloads))
        with self.assertRaisesRegex(RuntimeError, "pluto_infer.exe.*SHA-256"):
            acceptance.extract_official_runtime(self.archive, runtime, spec)
        self.assertFalse(runtime.exists())


class WinePreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def fake_wine(self, version="wine-11.21", dual_architecture=True, name="wine"):
        executable = self.root / name / "bin" / "wine"
        executable.parent.mkdir(parents=True)
        executable.write_text(f"#!/bin/sh\nprintf '%s\\n' '{version}'\n", encoding="utf-8")
        executable.chmod(0o755)
        if dual_architecture:
            for relative in ("i386-windows/ntdll.dll", "x86_64-windows/ntdll.dll", "x86_64-unix/wine"):
                path = self.root / name / "lib" / "wine" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"synthetic Wine runtime")
                path.chmod(0o755)
        return executable

    def test_regular_wine_11_unified_executable_passes(self):
        self.assertEqual(acceptance.probe_wine_11_unified(self.fake_wine(), {}), "wine-11.21")

    def test_true_directory_wrong_version_and_missing_architecture_fail(self):
        with self.assertRaisesRegex(RuntimeError, "wine-11"):
            acceptance.probe_wine_11_unified(Path("/bin/true"), {})
        directory = self.root / "directory"
        directory.mkdir()
        directory.chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, "regular executable"):
            acceptance.probe_wine_11_unified(directory, {})
        with self.assertRaisesRegex(RuntimeError, "wine-11"):
            acceptance.probe_wine_11_unified(self.fake_wine("wine-10.0"), {})
        with self.assertRaisesRegex(RuntimeError, "unified i386/x86-64"):
            acceptance.probe_wine_11_unified(self.fake_wine(dual_architecture=False, name="incomplete"), {})


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.runtime = Path(self.temporary.name)
        (self.runtime / "pluto.log").write_text(
            "BWRL: onStart (player 0, opp race 1)\nBWRL: inference server up\n", encoding="utf-8"
        )
        self.infer = (
            "[pluto_infer] ready: cpu backend int8mv, 6 threads, model v2.0 md09b_1331000 "
            "(step -1), frames_per_step 6, max_units 768, seed 0x255a83f6\n"
            "[pluto_infer] obs F6 flushed F6 (slip 0, infer 12.3 ms), next obs F12\n"
            "[pluto_infer] step 1 N=1 a=2 arg=0 pos=0 u=0 lp=-0.1 win=0.5\n"
            "[pluto_infer] shutdown after 1 steps; exiting\n"
        )
        (self.runtime / "pluto_infer.log").write_text(self.infer, encoding="utf-8")
        replies = []
        for frame in range(13):
            commands = b"\x1a\x00" if frame == 6 else b""
            replies.append(struct.pack("<4sII", b"HXRP", frame, len(commands)) + commands + struct.pack("<I", 0))
        self.stdout = b"".join(replies)
        self.stderr = b"[bridge] done (clean EOF)\n"

    def tearDown(self):
        self.temporary.cleanup()

    def validate(self, **changes):
        arguments = {
            "returncode": 0,
            "timed_out": False,
            "stdout": self.stdout,
            "stderr": self.stderr,
            "runtime": self.runtime,
            "frame_count": 12,
            "remaining_inference_processes": [],
        }
        arguments.update(changes)
        return acceptance.validate_evidence(**arguments)

    def test_real_handshake_steps_replies_and_clean_shutdown_pass(self):
        summary = self.validate()
        self.assertTrue(summary["valid"], summary["errors"])
        self.assertEqual(summary["reply_frames"], list(range(13)))
        self.assertEqual(summary["inference_observation_records"], 1)
        self.assertEqual(summary["inference_step_records"], 1)
        self.assertEqual(summary["inference_shutdown_steps"], 1)
        self.assertEqual(summary["strict_commands"], 1)

    def test_release_default_ready_and_shutdown_steps_are_sufficient(self):
        (self.runtime / "pluto_infer.log").write_text(
            "[pluto_infer] ready: cpu backend int8mv, 6 threads, model v2.0 md09b_1331000 "
            "(step -1), frames_per_step 6, max_units 768\n"
            "[pluto_infer] shutdown after 9 steps; exiting\n",
            encoding="utf-8",
        )
        summary = self.validate()
        self.assertTrue(summary["valid"], summary["errors"])
        self.assertEqual(summary["inference_observation_records"], 0)
        self.assertEqual(summary["inference_step_records"], 0)
        self.assertEqual(summary["inference_shutdown_steps"], 9)

    def test_any_nonzero_in_game_win_is_rejected(self):
        replies = []
        for frame in range(13):
            commands = b"\x1a\x00" if frame == 6 else b""
            win = 1 if frame == 6 else 0
            replies.append(struct.pack("<4sII", b"HXRP", frame, len(commands)) + commands + struct.pack("<I", win))
        summary = self.validate(stdout=b"".join(replies))
        self.assertFalse(summary["valid"])
        self.assertEqual(summary["nonzero_win_frames"], [6])
        self.assertTrue(any("nonzero win" in error for error in summary["errors"]))

    def test_missing_or_false_evidence_fails_closed(self):
        cases = {
            "ready": (self.infer.replace("[pluto_infer] ready:", "[pluto_infer] not-ready:"), self.stdout, self.stderr, 0, False, []),
            "step": (
                self.infer.replace("shutdown after 1 steps", "shutdown after 0 steps"),
                self.stdout,
                self.stderr,
                0,
                False,
                [],
            ),
            "clean": (self.infer, self.stdout, b"[bridge] done (failed)\n", 0, False, []),
            "commands": (
                self.infer,
                b"".join(
                    struct.pack("<4sII", b"HXRP", frame, 0) + struct.pack("<I", 0) for frame in range(13)
                ),
                self.stderr,
                0,
                False,
                [],
            ),
            "process": (self.infer, self.stdout, self.stderr, 0, False, ["123 pluto_infer.exe"]),
            "status": (self.infer, self.stdout, self.stderr, 1, False, []),
            "timeout": (self.infer, self.stdout, self.stderr, -9, True, []),
        }
        for name, (infer, stdout, stderr, returncode, timed_out, processes) in cases.items():
            with self.subTest(name=name):
                (self.runtime / "pluto_infer.log").write_text(infer, encoding="utf-8")
                summary = self.validate(
                    stdout=stdout,
                    stderr=stderr,
                    returncode=returncode,
                    timed_out=timed_out,
                    remaining_inference_processes=processes,
                )
                self.assertFalse(summary["valid"])
                self.assertTrue(summary["errors"])


class OwnedProcessTests(unittest.TestCase):
    def test_unrelated_inference_process_is_not_a_survivor_or_cleanup_target(self):
        unrelated = acceptance.ProcessRecord(400, 400, "/other/runtime/pluto/pluto_infer.exe")
        killpg_calls = []
        kill_calls = []
        owner = acceptance.OwnedProcesses(
            100,
            Path("/tmp/owned-work"),
            lister=lambda: [unrelated],
            killpg=lambda pgid, sig: killpg_calls.append((pgid, sig)),
            kill=lambda pid, sig: kill_calls.append((pid, sig)),
        )
        self.assertEqual(owner.wait_for_survivors(timeout=0), [])
        self.assertEqual(owner.kill_and_wait(timeout=0), [])
        self.assertEqual(killpg_calls, [])
        self.assertEqual(kill_calls, [])

    def test_post_normal_exit_owned_survivor_is_rejected_and_cleaned_by_pid(self):
        owned = acceptance.ProcessRecord(
            200,
            200,
            r"Z:\tmp\owned-work\runtime\pluto\pluto_infer.exe --argmax",
        )
        unrelated = acceptance.ProcessRecord(400, 400, "/other/runtime/pluto/pluto_infer.exe")
        records = [owned, unrelated]
        killpg_calls = []
        kill_calls = []

        def killpg(pgid, sig):
            killpg_calls.append((pgid, sig))

        def kill(pid, sig):
            kill_calls.append((pid, sig))
            records.remove(next(record for record in records if record.pid == pid))

        owner = acceptance.OwnedProcesses(
            100, Path("/tmp/owned-work"), lister=lambda: list(records), killpg=killpg, kill=kill
        )
        remaining = owner.wait_for_survivors(timeout=0)
        self.assertEqual(remaining, [owned])
        self.assertEqual(acceptance.process_descriptions(remaining), acceptance.process_descriptions([owned]))
        summary = {"valid": False, "returncode": 0, "errors": ["owned survivor after normal exit"]}
        acceptance.cleanup_failed_evidence(owner, summary)
        self.assertEqual(summary["cleanup_remaining_processes"], [])
        self.assertEqual(killpg_calls, [])
        self.assertEqual(kill_calls, [(200, signal.SIGKILL)])
        self.assertEqual(records, [unrelated])

    def test_group_survivor_is_cleaned_through_its_process_group(self):
        owned = acceptance.ProcessRecord(101, 100, "/wine pluto-bridge.exe")
        records = [owned]
        kill_calls = []

        def killpg(pgid, sig):
            self.assertEqual((pgid, sig), (100, signal.SIGKILL))
            records.clear()

        owner = acceptance.OwnedProcesses(
            100,
            Path("/tmp/owned-work"),
            lister=lambda: list(records),
            killpg=killpg,
            kill=lambda pid, sig: kill_calls.append((pid, sig)),
        )
        self.assertEqual(owner.wait_for_survivors(timeout=0), [owned])
        self.assertEqual(owner.kill_and_wait(timeout=0), [])
        self.assertEqual(kill_calls, [])


if __name__ == "__main__":
    unittest.main()
