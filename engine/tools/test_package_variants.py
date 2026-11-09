"""Hyperion Shield/Zero packaging policy tests; no StarCraft data is needed."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package


def args(edition="shield", game_files=None, channel="prerelease"):
    return SimpleNamespace(edition=edition, game_files=game_files, channel=channel)


class PackageVariantTests(unittest.TestCase):
    def test_shield_metadata_is_public_and_carries_no_game_files(self):
        metadata = json.loads(package.edition_metadata(args(), "v1.2.3-beta", "linux-x64"))
        self.assertEqual(metadata["product"], "Hyperion Shield")
        self.assertEqual(metadata["visibility"], "public")
        self.assertEqual(metadata["channel"], "prerelease")
        self.assertFalse(metadata["gameFilesBundled"])
        self.assertEqual(package.game_file_entries(args()), [])

    def test_shield_rejects_a_game_files_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(SystemExit):
                package.validate_variant(args(game_files=Path(folder)))

    def test_zero_requires_a_remastered_install(self):
        with self.assertRaises(SystemExit):
            package.validate_variant(args("zero"))
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(SystemExit):
                package.validate_variant(args("zero", Path(folder)))

    def test_zero_metadata_launchers_and_archive_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            install = Path(folder)
            (install / "Data").mkdir()
            (install / "Data" / "data.000").write_bytes(b"remastered")
            (install / "Maps" ).mkdir()
            (install / "Maps" / "test.scm").write_bytes(b"map")
            zero = args("zero", install, "release")
            package.validate_variant(zero)

            metadata = json.loads(package.edition_metadata(zero, "v1.2.3-beta", "windows"))
            self.assertEqual(metadata["product"], "Hyperion Zero")
            self.assertEqual(metadata["visibility"], "private")
            self.assertEqual(metadata["channel"], "release")
            self.assertTrue(metadata["gameFilesBundled"])

            entries = package.game_file_entries(zero)
            self.assertEqual({name for name, _ in entries}, {"Game/Data/data.000", "Game/Maps/test.scm"})
            self.assertNotIn(str(install), {name for name, _ in entries})

            windows = package.zero_launcher(zero, "windows")
            self.assertEqual(windows[0], "Play Hyperion Zero.cmd")
            self.assertIn(b"--install-folder", windows[1])
            self.assertIn(b"%*", windows[1])
            linux = package.zero_launcher(zero, "linux-x64", "Hyperion.x86_64")
            self.assertEqual(linux[0], "play-hyperion-zero.sh")
            self.assertEqual(linux[2], 0o755)
            self.assertIn(b"Hyperion.x86_64", linux[1])
            self.assertIn(b'"$@"', linux[1])
            mac = package.zero_launcher(zero, "mac")
            self.assertEqual(mac[0], "Play Hyperion Zero.command")
            self.assertIn(b"Hyperion.app/Contents/MacOS/Hyperion", mac[1])


if __name__ == "__main__":
    unittest.main()
