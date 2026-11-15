"""Hyperion Shield Zero/Hyperion Zero Private packaging policy tests; no StarCraft data is needed."""
import importlib.util
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package

PE_TESTS = Path(__file__).resolve().parent / "pluto-bridge" / "host" / "verify-pe-tests.py"
SPEC = importlib.util.spec_from_file_location("pluto_verify_pe_tests", PE_TESTS)
pe_tests = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = pe_tests
SPEC.loader.exec_module(pe_tests)


def args(edition="shield", game_files=None, channel="prerelease", **extra):
    values = dict(edition=edition, game_files=game_files, channel=channel)
    values.update(extra)
    return SimpleNamespace(**values)


def archive_names_and_reader(archive: Path):
    """Return regular-file names and a bounded reader, without loading a whole release in memory."""
    if archive.name.endswith(".zip"):
        z = zipfile.ZipFile(archive)
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        return z, names, z.read
    t = tarfile.open(archive, "r:gz")
    names = [i.name for i in t.getmembers() if i.isfile()]

    def read(name):
        return t.extractfile(name).read()

    return t, names, read


def inspect_shield_package(archive: Path) -> dict:
    """Inspect an actual Windows/Linux/Mac output, including its embedded bridge PE."""
    archive = Path(archive)
    closer, names, read = archive_names_and_reader(archive)
    try:
        normalized = [name.replace("\\", "/").lower() for name in names]
        if len(normalized) != len(set(normalized)):
            raise ValueError(f"{archive}: duplicate or case-colliding archive paths")
        official = [name for name in names
                    if name.replace("\\", "/").rsplit("/", 1)[-1].lower()
                    in package.OFFICIAL_PLUTO_FILENAMES]
        if official:
            raise ValueError(f"{archive}: official Pluto payload is forbidden: {official}")
        bridges = [name for name in names if name.endswith("/" + package.PLUTO_BRIDGE_ARCHIVE)
                   or name == package.PLUTO_BRIDGE_ARCHIVE]
        if len(bridges) != 1:
            raise ValueError(f"{archive}: expected exactly one {package.PLUTO_BRIDGE_ARCHIVE}")
        bridge_name = bridges[0]
        prefix = bridge_name[:-len(package.PLUTO_BRIDGE_ARCHIVE)]
        metadata_name = prefix + "release.json"
        if metadata_name not in names:
            raise ValueError(f"{archive}: missing {metadata_name}")
        package.pluto_pe_verifier().verify(read(bridge_name))
        metadata = json.loads(read(metadata_name))
        if archive.name.endswith("-windows-x64.zip"):
            platform = "windows"
        elif archive.name.endswith("-mac-arm64.zip"):
            platform = "mac-arm64"
        elif archive.name.endswith("-linux-x64.tar.gz"):
            platform = "linux-x64"
        elif archive.name.endswith("-linux-arm64.tar.gz"):
            platform = "linux-arm64"
        else:
            raise ValueError(f"{archive}: unknown Shield package platform")
        expected = {
            "product": "Hyperion Shield Zero",
            "visibility": "public",
            "platform": platform,
            "gameFilesBundled": False,
            "plutoSetupMode": "official-download",
            "plutoBridgeBundled": True,
            "plutoOfficialFilesBundled": False,
        }
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise ValueError(f"{archive}: release.json {key} is {metadata.get(key)!r}, expected {value!r}")
        if metadata.get("channel") not in ("prerelease", "release"):
            raise ValueError(f"{archive}: invalid release channel {metadata.get('channel')!r}")
        return metadata
    finally:
        closer.close()


def inspect_package_root(root: Path) -> int:
    archives = sorted(p for p in root.rglob("*") if p.is_file() and
                      (p.name.startswith("Hyperion-Shield-Zero-") and
                       (p.name.endswith(".zip") or p.name.endswith(".tar.gz"))))
    if not archives:
        raise ValueError(f"no Hyperion Shield Zero packages found under {root}")
    for archive in archives:
        metadata = inspect_shield_package(archive)
        print(f"PASS {archive}: validated bridge, official-download metadata, no official Pluto payloads "
              f"({metadata['platform']}, {metadata['channel']})")
    return 0


class PackageVariantTests(unittest.TestCase):
    def test_shield_metadata_is_public_and_carries_no_game_files(self):
        metadata = json.loads(package.edition_metadata(args(), "v1.2.3-beta", "linux-x64"))
        self.assertEqual(metadata["product"], "Hyperion Shield Zero")
        self.assertEqual(metadata["visibility"], "public")
        self.assertEqual(metadata["channel"], "prerelease")
        self.assertFalse(metadata["gameFilesBundled"])
        self.assertEqual(metadata["plutoSetupMode"], "official-download")
        self.assertTrue(metadata["plutoBridgeBundled"])
        self.assertFalse(metadata["plutoOfficialFilesBundled"])
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
            (install / "Maps").mkdir()
            (install / "Maps" / "test.scm").write_bytes(b"map")
            zero = args("zero", install, "release")
            package.validate_variant(zero)

            metadata = json.loads(package.edition_metadata(zero, "v1.2.3-beta", "windows"))
            self.assertEqual(metadata["product"], "Hyperion Zero Private")
            self.assertEqual(metadata["visibility"], "private")
            self.assertEqual(metadata["channel"], "release")
            self.assertTrue(metadata["gameFilesBundled"])
            self.assertEqual(metadata["plutoSetupMode"], "official-download")

            entries = package.game_file_entries(zero)
            self.assertEqual({name for name, _ in entries}, {"Game/Data/data.000", "Game/Maps/test.scm"})
            self.assertNotIn(str(install), {name for name, _ in entries})

            windows = package.zero_launcher(zero, "windows")
            self.assertEqual(windows[0], "Play Hyperion Zero Private.cmd")
            self.assertIn(b"--install-folder", windows[1])
            self.assertIn(b"%*", windows[1])
            linux = package.zero_launcher(zero, "linux-x64", "Hyperion.x86_64")
            self.assertEqual(linux[0], "play-hyperion-zero-private.sh")
            self.assertEqual(linux[2], 0o755)
            self.assertIn(b"Hyperion.x86_64", linux[1])
            self.assertIn(b'"$@"', linux[1])
            mac = package.zero_launcher(zero, "mac")
            self.assertEqual(mac[0], "Play Hyperion Zero Private.command")
            self.assertIn(b"Hyperion.app/Contents/MacOS/Hyperion", mac[1])

    def test_official_pluto_payloads_are_rejected_even_in_private_game_files(self):
        with tempfile.TemporaryDirectory() as folder:
            install = Path(folder)
            payload = install / "Data" / "pluto" / "pluto.dll"
            payload.parent.mkdir(parents=True)
            payload.write_bytes(b"not redistributable")
            with self.assertRaisesRegex(SystemExit, "official Pluto payload"):
                package.game_file_entries(args("zero", install))


class PackageOutputTests(unittest.TestCase):
    """Run every real packager against synthetic non-game inputs and inspect its output."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.engine = self.root / "engine"
        self.out = self.root / "out"
        self.empty = self.root / "empty"
        self.empty.mkdir()
        self.built = self.engine / "src117" / "build" / "x64" / "Hyperion.exe"
        self.sdl = self.root / "SDL3.dll"
        self.built.parent.mkdir(parents=True)
        self.built.write_bytes(b"synthetic Windows program")
        self.sdl.write_bytes(b"synthetic SDL")
        for platform, program, library in (
                ("linux-x64", "Hyperion.x86_64", "libSDL3.so.0"),
                ("mac-arm64", "Hyperion.arm64", "libSDL3.0.dylib")):
            build = self.engine / "src117" / "build" / platform
            build.mkdir(parents=True)
            (build / program).write_bytes(("synthetic " + platform).encode())
            (build / library).write_bytes(b"synthetic SDL")
        self.bridge = self.root / "pluto-bridge.exe"
        self.bridge.write_bytes(pe_tests.valid_fixture())
        self.bwapi = self.root / "bwapi"
        self.bwapi.mkdir()
        (self.bwapi / "COPYING").write_text("synthetic license fixture\n")
        (self.bwapi / "SOURCE.txt").write_text("synthetic source fixture\n")
        self.guide = self.root / "guide.html"
        self.guide.write_text("synthetic guide\n")
        self.patchers = [
            mock.patch.object(package, "ENGINE", self.engine),
            mock.patch.object(package, "BUILT", self.built),
            mock.patch.object(package, "SDL3", self.sdl),
            mock.patch.object(package, "PLUTO_BRIDGE", self.bridge),
            mock.patch.object(package, "PLUTO_BWAPI", self.bwapi),
            mock.patch.object(package, "MODS", self.empty),
            mock.patch.object(package, "NOTES", self.empty),
            mock.patch.object(package, "WEB", self.empty),
            mock.patch.object(package, "HUDS", self.empty),
            mock.patch.object(package, "MAPS", self.empty),
            mock.patch.object(package, "DOCS", []),
            mock.patch.object(package, "TOOLS", []),
            mock.patch.object(package, "CPP_CLIENT", []),
            mock.patch.object(package, "WEB_PROGRAM", []),
            mock.patch.object(package, "GUIDE", self.guide),
            mock.patch.object(package, "font_licenses", lambda: "synthetic font licenses\n"),
            mock.patch.object(package, "keep_symbols", lambda *a, **kw: None),
        ]
        for patcher in self.patchers:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.temporary.cleanup()

    def only_archive(self, suffix):
        archives = [p for p in self.out.rglob("*") if p.is_file() and p.name.endswith(suffix)]
        self.assertEqual(len(archives), 1)
        return archives[0]

    def test_windows_output_and_install_have_the_same_validated_bridge(self):
        self.assertEqual(package.package_windows(args(out=self.out), "v1.2.3-beta"), 0)
        archive = self.only_archive("-windows-x64.zip")
        metadata = inspect_shield_package(archive)
        self.assertEqual(metadata["platform"], "windows")
        self.assertEqual(inspect_package_root(self.out), 0)

        installed = self.root / "installed"
        installed.mkdir()
        self.assertEqual(package.install([installed], "v1.2.3-beta", args()), 0)
        with zipfile.ZipFile(archive) as z:
            packaged_bridge = z.read(package.PLUTO_BRIDGE_ARCHIVE)
        installed_bridge = installed / package.PLUTO_BRIDGE_ARCHIVE
        self.assertEqual(installed_bridge.read_bytes(), packaged_bridge)
        package.pluto_pe_verifier().verify(installed_bridge.read_bytes())
        installed_metadata = json.loads((installed / "release.json").read_text(encoding="utf-8"))
        self.assertEqual(installed_metadata["plutoSetupMode"], "official-download")

    def test_linux_output_contains_valid_bridge_and_no_official_payload(self):
        self.assertEqual(package.package_linux(args(out=self.out, linux="x64"), "v1.2.3-beta"), 0)
        archive = self.only_archive("-linux-x64.tar.gz")
        self.assertEqual(inspect_shield_package(archive)["platform"], "linux-x64")
        self.assertEqual(inspect_package_root(self.out), 0)

    def test_mac_output_contains_valid_bridge_and_no_official_payload(self):
        def fake_tool(command, **kwargs):
            if command[0] == "sips":
                Path(command[command.index("--out") + 1]).write_bytes(b"png")
            elif command[0] == "iconutil":
                Path(command[command.index("-o") + 1]).write_bytes(b"icns")
            elif command[0] == "ditto":
                stage, archive = Path(command[-2]), Path(command[-1])
                with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
                    for path in sorted(stage.rglob("*")):
                        if path.is_file():
                            z.write(path, stage.name + "/" + path.relative_to(stage).as_posix())
            return subprocess.CompletedProcess(command, 0)

        with mock.patch.object(package.sys, "platform", "darwin"), \
                mock.patch.object(package.subprocess, "run", side_effect=fake_tool):
            self.assertEqual(package.package_mac(args(out=self.out), "v1.2.3-beta"), 0)
        archive = self.only_archive("-mac-arm64.zip")
        self.assertEqual(inspect_shield_package(archive)["platform"], "mac-arm64")
        self.assertEqual(inspect_package_root(self.out), 0)

    def test_archive_inspector_rejects_an_official_payload(self):
        package.package_windows(args(out=self.out), "v1.2.3-beta")
        archive = self.only_archive("-windows-x64.zip")
        with zipfile.ZipFile(archive, "a") as z:
            z.writestr("Data/pluto/pluto.dll", b"forbidden")
        with self.assertRaisesRegex(ValueError, "official Pluto payload"):
            inspect_shield_package(archive)

    def test_all_package_builders_reject_missing_or_invalid_bridge(self):
        install_gate = self.root / "install-gate"
        install_gate.mkdir()
        builders = (
            lambda: package.package_windows(args(out=self.out), "v1.2.3-beta"),
            lambda: package.package_linux(args(out=self.out, linux="x64"), "v1.2.3-beta"),
            lambda: package.package_mac(args(out=self.out), "v1.2.3-beta"),
            lambda: package.install([install_gate], "v1.2.3-beta", args()),
        )
        self.bridge.unlink()
        with mock.patch.object(package.sys, "platform", "darwin"):
            for build in builders:
                with self.subTest(build=build, state="missing"), self.assertRaises(SystemExit):
                    build()
        self.bridge.write_bytes(b"not a PE")
        with mock.patch.object(package.sys, "platform", "darwin"):
            for build in builders:
                with self.subTest(build=build, state="invalid"), self.assertRaises(SystemExit):
                    build()


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--package-root":
        try:
            sys.exit(inspect_package_root(Path(sys.argv[2])))
        except (OSError, ValueError, KeyError, json.JSONDecodeError,
                package.pluto_pe_verifier().PeVerificationError) as error:
            print(f"package gate failed: {error}", file=sys.stderr)
            sys.exit(1)
    unittest.main()
