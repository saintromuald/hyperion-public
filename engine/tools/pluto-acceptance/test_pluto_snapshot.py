#!/usr/bin/env python3
"""Synthetic tests for pluto_snapshot; no real assets, builds, or games are used."""

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock

import pluto_snapshot as ps


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "pluto.cpp").write_bytes(b"synthetic scoped source\n")
        (self.source / "pluto.h").write_bytes(b"synthetic header\n")
        self.runtime = self.root / "runtime-inputs"
        self.runtime.mkdir()
        self.runtime_paths = {}
        for label in ps.RUNTIME_LABELS:
            path = self.runtime / (label + ".bin")
            path.write_bytes(("synthetic %s bytes\n" % label).encode("utf-8"))
            self.runtime_paths[label] = path
        self.bandit_before = self.root / "bandit-before.txt"
        self.bandit_after = self.root / "bandit-after.txt"
        self.bandit_before.write_bytes(b"synthetic bandit before\n")
        self.bandit_after.write_bytes(b"synthetic bandit after\n")
        self.evidence = self.root / "evidence-input"
        (self.evidence / "nested").mkdir(parents=True)
        (self.evidence / "summary.json").write_text('{"synthetic": true}\n', encoding="utf-8")
        (self.evidence / "nested" / "run.log").write_text("synthetic evidence\n", encoding="utf-8")
        self.snapshots = self.root / "durable-snapshots"

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot_argv(
        self,
        *,
        source=None,
        source_files=("pluto.cpp", "pluto.h"),
        root=None,
        confirmed=False,
        confirmation=None,
        allow_missing=True,
        evidence=True,
        provenance=True,
    ):
        source = Path(source) if source is not None else self.source
        argv = [
            "snapshot",
            "--root",
            str(root or self.snapshots),
            "--source",
            str(source),
            "--protocol",
            "synthetic-HXP4+HXRP",
            "--timing",
            '{"receipt_frames":1,"dispatch_frames":3}',
            "--map",
            "(2)Synthetic.scx",
            "--settings",
            '{"players":2,"game_type":"melee","frames":30000}',
            "--seed",
            "730000",
            "--seed",
            "730001",
            "--bandit-before",
            str(self.bandit_before),
            "--bandit-after",
            str(self.bandit_after),
            "--toolchain",
            "synthetic-toolchain",
            "--wine",
            "synthetic-wine",
            "--bwapi",
            "synthetic-BWAPI",
            "--shim",
            "synthetic-shim",
            "--build-provenance",
            '{"command":"not executed by this test"}',
            "--limitation",
            "Synthetic fixture only; no runtime claim.",
            "--notes",
            "synthetic recovery notes",
            "--json",
        ]
        for relative in source_files:
            argv += ["--source-file", relative]
        for label in ps.RUNTIME_LABELS:
            argv += ["--" + label.replace("_", "-"), str(self.runtime_paths[label])]
        if evidence:
            argv += ["--evidence", str(self.evidence)]
        if allow_missing:
            argv += ["--allow-missing-source"]
        if confirmed:
            argv += ["--user-confirmed"]
        if confirmation is not None:
            argv += ["--confirmation", confirmation]
        if not provenance:
            for flag in ("--toolchain", "--wine", "--bwapi", "--shim", "--build-provenance"):
                index = argv.index(flag)
                del argv[index:index + 2]
        return argv

    def invoke(self, argv, expected=0):
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = ps.main(argv)
        self.assertEqual(code, expected, "stdout=%s\nstderr=%s" % (stdout.getvalue(), stderr.getvalue()))
        document = None
        if "--json" in argv and stdout.getvalue().strip():
            document = json.loads(stdout.getvalue())
        return code, document, stdout.getvalue(), stderr.getvalue()

    def make_snapshot(self, **kwargs):
        _, document, _, _ = self.invoke(self.snapshot_argv(**kwargs))
        return Path(document["snapshot"]), document

    def git(self, repo, *arguments):
        return subprocess.check_output(
            ["git", "-C", str(repo)] + list(arguments),
            text=True,
            encoding="utf-8",
            stderr=subprocess.DEVNULL,
        ).strip()

    def make_git_source(self):
        repo = self.root / "git-source"
        repo.mkdir()
        (repo / "pluto.cpp").write_bytes(b"committed synthetic source\n")
        (repo / "pluto.h").write_bytes(b"committed synthetic header\n")
        self.git(repo, "init", "-q")
        self.git(repo, "config", "user.name", "Synthetic Test")
        self.git(repo, "config", "user.email", "synthetic@example.invalid")
        self.git(repo, "add", "--", "pluto.cpp", "pluto.h")
        self.git(repo, "commit", "-q", "-m", "synthetic OG fixture")
        return repo, self.git(repo, "rev-parse", "HEAD")

    def test_roundtrip_snapshot_verify_restore_and_private_layout(self):
        snapshot, created = self.make_snapshot()
        self.assertEqual(created["designation"], "candidate")
        self.assertEqual(created["proprietary_files_uploaded"], False)
        self.assertEqual(created["original_paths_modified"], False)
        self.assertIn(created["manifest_sha256"], snapshot.name)
        self.assertTrue(snapshot.name.startswith("candidate-"))
        self.assertEqual(stat.S_IMODE(snapshot.stat().st_mode) & 0o077, 0)

        verified = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertTrue(verified["valid"], verified)
        self.assertEqual(verified["checks"]["hashed_entries_checked"], 12)
        self.assertTrue(verified["checks"]["content_address_checked"])
        self.assertTrue(any("temporary" in warning for warning in verified["warnings"]))

        destination = self.root / "restored"
        restored = ps.restore_snapshot(snapshot, destination)
        self.assertTrue(restored["verified"])
        self.assertEqual((destination / "source" / "pluto.cpp").read_bytes(), b"synthetic scoped source\n")
        self.assertEqual((destination / "runtime" / "weights").read_bytes(), self.runtime_paths["weights"].read_bytes())
        self.assertEqual((destination / "bandit" / "before").read_bytes(), self.bandit_before.read_bytes())
        self.assertEqual((destination / "evidence" / "001-evidence-input" / "nested" / "run.log").read_text(encoding="utf-8"), "synthetic evidence\n")
        self.assertIn("NEW private recovery directory", (destination / "RECOVERY.md").read_text(encoding="utf-8"))
        self.assertTrue(ps.verify_snapshot(destination, allow_missing_source=True)["valid"])
        manifest = ps.read_json(snapshot / "manifest.json")
        self.assertEqual(manifest["schema"], ps.MANIFEST_SCHEMA)
        self.assertEqual(manifest["bandit"]["changed"], True)
        self.assertEqual(manifest["provenance"]["wine"], "synthetic-wine")
        self.assertEqual(manifest["protocol"]["timing"]["dispatch_frames"], 3)
        self.assertEqual(manifest["evaluation"]["seeds"], [730000, 730001])
        self.assertNotEqual(manifest["bandit"]["before"]["sha256"], manifest["bandit"]["after"]["sha256"])

    def test_candidate_allows_unsupplied_optional_provenance_and_evidence(self):
        snapshot, created = self.make_snapshot(provenance=False, evidence=False)
        self.assertEqual(created["designation"], "candidate")
        self.assertEqual(created["evidence_entries"], 0)
        manifest = ps.read_json(snapshot / "manifest.json")
        self.assertEqual(
            manifest["provenance"],
            {"build": None, "toolchain": None, "wine": None, "bwapi": None, "shim": None},
        )
        self.assertEqual(manifest["evidence"]["files"], [])
        self.assertIn("No evidence files were supplied with this snapshot.", manifest["limitations"])
        self.assertTrue(ps.verify_snapshot(snapshot, allow_missing_source=True)["valid"])

    def test_tamper_detection_and_verify_cli_exit(self):
        snapshot, _ = self.make_snapshot()
        original = (snapshot / "runtime" / "engine").read_bytes()
        (snapshot / "runtime" / "engine").write_bytes(original + b"tampered")
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("SHA256 mismatch" in error and "runtime/engine" in error for error in result["errors"]))
        _, document, _, _ = self.invoke(
            ["verify", "--snapshot", str(snapshot), "--allow-missing-source", "--json"],
            expected=1,
        )
        self.assertFalse(document["valid"])

    def test_manifest_hash_change_is_detected_by_content_address(self):
        snapshot, _ = self.make_snapshot()
        path = snapshot / "manifest.json"
        document = ps.read_json(path)
        document["notes"] = "manifest tampering"
        path.write_bytes(ps.json_bytes(document))
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("content hash" in error for error in result["errors"]))

    def test_manifest_snapshot_id_and_designation_cannot_bypass_content_address(self):
        snapshot, _ = self.make_snapshot()
        manifest_path = snapshot / "manifest.json"
        document = ps.read_json(manifest_path)
        document["snapshot_id"] = "20990101T000000Z-000000000000"
        manifest_path.write_bytes(ps.json_bytes(document))
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("snapshot ID" in error for error in result["errors"]))

        document["designation"]["status"] = "user_confirmed_og"
        document["designation"]["user_confirmed"] = True
        manifest_path.write_bytes(ps.json_bytes(document))
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        with self.assertRaises(ps.SnapshotError):
            ps.forward_port(snapshot, snapshot, "0" * 40)

    def test_symlinks_roundtrip_as_links_and_target_tamper_is_detected(self):
        (self.source / "real.cpp").write_bytes(b"real synthetic bytes")
        os.symlink("real.cpp", self.source / "link.cpp")
        snapshot, _ = self.make_snapshot(source_files=("real.cpp", "link.cpp"))
        copied = snapshot / "source" / "link.cpp"
        self.assertTrue(copied.is_symlink())
        self.assertEqual(os.readlink(copied), "real.cpp")
        manifest = ps.read_json(snapshot / "manifest.json")
        link = next(item for item in manifest["source"]["files"] if item["path"].endswith("link.cpp"))
        self.assertEqual(link["target"], "real.cpp")
        self.assertEqual(link["hash_subject"], "symlink-target-text")
        self.assertEqual(link["sha256"], hashlib.sha256(b"real.cpp").hexdigest())
        destination = self.root / "link-restore"
        ps.restore_snapshot(snapshot, destination)
        self.assertTrue((destination / "source" / "link.cpp").is_symlink())
        self.assertEqual(os.readlink(destination / "source" / "link.cpp"), "real.cpp")
        copied.unlink()
        os.symlink("../outside", copied)
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("symlink target mismatch" in error for error in result["errors"]))

    def test_path_traversal_manifest_is_refused_before_restore(self):
        snapshot, _ = self.make_snapshot()
        manifest_path = snapshot / "manifest.json"
        document = ps.read_json(manifest_path)
        document["source"]["files"][0]["path"] = "../escape"
        manifest_path.write_bytes(ps.json_bytes(document))
        destination = self.root / "traversal-restore"
        with self.assertRaises(ps.SnapshotError):
            ps.restore_snapshot(snapshot, destination)
        self.assertFalse(destination.exists())
        self.assertFalse((self.root / "escape").exists())
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("unsafe" in error for error in result["errors"]))

    def test_symlink_parent_collision_is_refused(self):
        snapshot, _ = self.make_snapshot()
        manifest_path = snapshot / "manifest.json"
        document = ps.read_json(manifest_path)
        first, second = document["source"]["files"]
        first["path"] = "source/redirect"
        first["kind"] = "symlink"
        first["target"] = "../../outside"
        first["hash_subject"] = "symlink-target-text"
        first["sha256"] = hashlib.sha256(b"../../outside").hexdigest()
        first["size"] = len(b"../../outside")
        second["path"] = "source/redirect/escape"
        manifest_path.write_bytes(ps.json_bytes(document))
        with self.assertRaises(ps.SnapshotError):
            ps.restore_snapshot(snapshot, self.root / "collision-restore")
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("traverses a file/symlink" in error for error in result["errors"]))

    def test_restore_refuses_existing_even_empty_destination_without_changes(self):
        snapshot, _ = self.make_snapshot()
        destination = self.root / "existing"
        destination.mkdir()
        sentinel = destination / "keep.txt"
        sentinel.write_bytes(b"do not change")
        with self.assertRaises(ps.SnapshotError):
            ps.restore_snapshot(snapshot, destination)
        self.assertEqual(list(destination.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"do not change")

    def test_existing_snapshot_is_never_replaced(self):
        fixed_time = "2026-01-02T03:04:05Z"
        fixed_uuid = mock.Mock(hex="a" * 32)
        with mock.patch.object(ps, "utc_now", return_value=fixed_time), mock.patch.object(ps.uuid, "uuid4", return_value=fixed_uuid):
            snapshot, _ = self.make_snapshot()
            manifest_before = (snapshot / "manifest.json").read_bytes()
            self.invoke(self.snapshot_argv(), expected=2)
        self.assertEqual((snapshot / "manifest.json").read_bytes(), manifest_before)
        self.assertEqual((self.source / "pluto.cpp").read_bytes(), b"synthetic scoped source\n")

    def test_inputs_inside_destination_are_rejected(self):
        inside_source = self.snapshots / "source-inside"
        inside_source.mkdir(parents=True)
        (inside_source / "pluto.cpp").write_bytes(b"inside")
        with self.assertRaises(ps.SnapshotError):
            ps.create_snapshot(self.parse_snapshot(source=inside_source, source_files=("pluto.cpp",)))
        self.assertEqual(list(self.snapshots.iterdir()), [inside_source])

        outside_root = self.root / "other-snapshot-root"
        outside_root.mkdir()
        self.runtime_paths["engine"] = outside_root / "engine-inside"
        self.runtime_paths["engine"].write_bytes(b"inside runtime")
        with self.assertRaises(ps.SnapshotError):
            ps.create_snapshot(self.parse_snapshot(root=outside_root))
        self.assertEqual(list(outside_root.iterdir()), [self.runtime_paths["engine"]])

    def parse_snapshot(self, **kwargs):
        argv = self.snapshot_argv(**kwargs)
        return ps.build_parser().parse_args(argv)

    def test_og_confirmation_gate_and_immutable_candidate_distinction(self):
        self.invoke(
            self.snapshot_argv(confirmed=True, confirmation="not the required attestation"),
            expected=2,
        )
        self.invoke(
            self.snapshot_argv(confirmed=False, confirmation=ps.OG_CONFIRMATION),
            expected=2,
        )
        self.assertFalse(self.snapshots.exists())
        snapshot, created = self.make_snapshot(
            confirmed=True,
            confirmation="Synthetic reviewer: Pluto AI is properly working in this tiny fixture only.",
        )
        self.assertEqual(created["designation"], "user_confirmed_og")
        self.assertTrue(snapshot.name.startswith("og-"))
        manifest = ps.read_json(snapshot / "manifest.json")
        self.assertTrue(manifest["designation"]["user_confirmed"])
        self.assertIn(ps.OG_CONFIRMATION, manifest["designation"]["confirmation"])
        candidate, _ = self.make_snapshot(root=self.root / "candidate-root")
        self.assertEqual(ps.read_json(candidate / "manifest.json")["designation"]["status"], "candidate")

    def test_originals_are_not_moved_deleted_or_modified(self):
        inputs = [self.source / "pluto.cpp", self.source / "pluto.h", self.bandit_before, self.bandit_after]
        inputs += list(self.runtime_paths.values())
        inputs += [self.evidence / "summary.json", self.evidence / "nested" / "run.log"]
        before = {
            path: (path.read_bytes(), path.stat().st_mode, path.stat().st_mtime_ns)
            for path in inputs
        }
        snapshot, _ = self.make_snapshot()
        destination = self.root / "nonmutating-restore"
        ps.restore_snapshot(snapshot, destination)
        after = {
            path: (path.read_bytes(), path.stat().st_mode, path.stat().st_mtime_ns)
            for path in inputs
        }
        self.assertEqual(after, before)

    def test_forward_port_reports_dirty_changed_missing_and_conflicting_without_mutation(self):
        repo, base = self.make_git_source()
        snapshot, _ = self.make_snapshot(source=repo, allow_missing=False, confirmed=True, confirmation=ps.OG_CONFIRMATION)
        initial = ps.forward_port(snapshot, repo, base)
        self.assertFalse(initial["needs_review"])
        self.assertEqual(len(initial["status"]["unchanged"]), 2)
        self.assertFalse(initial["compatibility_proven"])
        self.assertFalse(initial["tests_run"])
        self.assertFalse(initial["repository_modified"])
        self.assertFalse((self.root / "NEW-WORKTREE").exists())

        tracked = repo / "pluto.cpp"
        tracked.write_bytes(b"dirty synthetic source\n")
        before_worktrees = self.git(repo, "worktree", "list", "--porcelain")
        changed = ps.forward_port(snapshot, repo, base)
        self.assertTrue(changed["needs_review"])
        self.assertEqual([item["path"] for item in changed["status"]["changed"]], ["pluto.cpp"])
        self.assertEqual(tracked.read_bytes(), b"dirty synthetic source\n")
        self.assertEqual(self.git(repo, "rev-parse", "HEAD"), base)
        self.assertEqual(self.git(repo, "worktree", "list", "--porcelain"), before_worktrees)
        self.assertTrue(changed["dirty_scoped_status"])

        tracked.unlink()
        missing = ps.forward_port(snapshot, repo, base)
        self.assertEqual([item["path"] for item in missing["status"]["missing"]], ["pluto.cpp"])
        os.symlink("pluto.h", tracked)
        conflicting = ps.forward_port(snapshot, repo, base)
        self.assertEqual([item["path"] for item in conflicting["status"]["conflicting"]], ["pluto.cpp"])
        self.assertEqual(os.readlink(tracked), "pluto.h")
        self.assertEqual(self.git(repo, "rev-parse", "HEAD"), base)

    def test_forward_port_does_not_follow_symlinked_source_parents(self):
        repo, _ = self.make_git_source()
        (repo / "nested").mkdir()
        (repo / "nested" / "pluto.cpp").write_bytes(b"nested committed source\n")
        self.git(repo, "add", "--", "nested/pluto.cpp")
        self.git(repo, "commit", "-q", "-m", "nested synthetic source")
        base = self.git(repo, "rev-parse", "HEAD")
        snapshot, _ = self.make_snapshot(
            source=repo,
            source_files=("nested/pluto.cpp",),
            allow_missing=False,
            confirmed=True,
            confirmation=ps.OG_CONFIRMATION,
        )
        (repo / "nested" / "pluto.cpp").unlink()
        (repo / "nested").rmdir()
        os.symlink(self.source, repo / "nested")
        result = ps.forward_port(snapshot, repo, base)
        self.assertEqual(result["status"]["conflicting"][0]["actual_kind"], "symlink-parent")
        self.assertEqual(os.readlink(repo / "nested"), str(self.source))
        self.assertEqual((self.source / "pluto.cpp").read_bytes(), b"synthetic scoped source\n")

    def test_forward_port_refuses_candidate(self):
        repo, base = self.make_git_source()
        snapshot, _ = self.make_snapshot(source=repo, allow_missing=False)
        with self.assertRaises(ps.SnapshotError):
            ps.forward_port(snapshot, repo, base)
        self.assertEqual((repo / "pluto.cpp").read_bytes(), b"committed synthetic source\n")

    def test_verify_rechecks_source_commit_and_tree_when_git_is_available(self):
        repo, base = self.make_git_source()
        snapshot, _ = self.make_snapshot(source=repo, allow_missing=False)
        self.assertTrue(ps.verify_snapshot(snapshot)["valid"])
        (repo / "pluto.cpp").write_bytes(b"new commit\n")
        self.git(repo, "add", "--", "pluto.cpp")
        self.git(repo, "commit", "-q", "-m", "advance synthetic source")
        self.assertNotEqual(self.git(repo, "rev-parse", "HEAD"), base)
        result = ps.verify_snapshot(snapshot)
        self.assertFalse(result["valid"])
        self.assertTrue(any("source commit mismatch" in error for error in result["errors"]))
        self.assertTrue(ps.verify_snapshot(snapshot, check_source=False)["valid"])

    def test_git_metadata_is_required_unless_explicitly_allowed(self):
        self.invoke(self.snapshot_argv(allow_missing=False), expected=2)
        snapshot, _ = self.make_snapshot(allow_missing=True)
        result = ps.verify_snapshot(snapshot)
        self.assertFalse(result["valid"])
        self.assertTrue(any("--allow-missing-source" in error for error in result["errors"]))
        self.assertTrue(ps.verify_snapshot(snapshot, allow_missing_source=True)["valid"])

    def test_unexpected_layout_entry_and_missing_required_hash_are_errors(self):
        snapshot, _ = self.make_snapshot()
        (snapshot / "runtime" / "extra").write_bytes(b"unexpected")
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("unexpected snapshot layout path" in error for error in result["errors"]))
        (snapshot / "runtime" / "extra").unlink()
        manifest_path = snapshot / "manifest.json"
        document = ps.read_json(manifest_path)
        del document["runtime"]["weights"]["sha256"]
        manifest_path.write_bytes(ps.json_bytes(document))
        result = ps.verify_snapshot(snapshot, allow_missing_source=True)
        self.assertFalse(result["valid"])
        self.assertTrue(any("runtime.weights needs lowercase SHA256" in error for error in result["errors"]))


if __name__ == "__main__":
    unittest.main()
