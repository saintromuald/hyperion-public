"""Pure-Python and engine-backed deterministic native tests.

Engine tests run only when HYPERION_NATIVE_ENGINE is explicitly set (normally by
run_tests.py --engine).  They never start Pluto or an inference process.
"""
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

import native_commands as cmd
import native_fixtures as fixtures
from native_runner import EngineSession


ENGINE = os.environ.get("HYPERION_NATIVE_ENGINE")
DATA = os.environ.get("HYPERION_NATIVE_DATA")
OUTPUT = os.environ.get("HYPERION_NATIVE_OUTPUT")


def units_of(record, unit_type=None, player=None, x=None, y=None, alive=True):
    result = []
    for row in record["units"]:
        if alive is not None and row["alive"] != alive:
            continue
        if unit_type is not None and row.get("type") != unit_type:
            continue
        if player is not None and row.get("player") != player:
            continue
        if x is not None and row.get("x") != x:
            continue
        if y is not None and row.get("y") != y:
            continue
        result.append(row)
    return result


def one(session, record, label, **criteria):
    rows = units_of(record, **criteria)
    session.expect(len(rows), 1, f"{label}: exactly one unit ({criteria})")
    return rows[0]


def by_id(session, record, table_id):
    rows = [row for row in record["units"] if row["id"] == table_id]
    session.expect(len(rows), 1, f"stable table id {table_id} remains in evidence")
    return rows[0]


def player(session, record, slot):
    rows = [row for row in record["players"] if row["slot"] == slot]
    session.expect(len(rows), 1, f"player slot {slot} is present")
    return rows[0]


def select(row):
    return cmd.select(row["unitId"])


def expect_fields(session, record, row, **expected):
    for name, value in expected.items():
        session.expect(row.get(name), value, f"unit {row['id']}.{name} at frame {record['frame']}")


def morph(session, larva, unit_type):
    session.turn(0, cmd.turn_bytes(select(larva), cmd.morph(unit_type)))
    return session.capture("morph-command")


def right_click(session, player, selected, target):
    packet = cmd.turn_bytes(cmd.select(*(row["unitId"] for row in selected)),
                            cmd.right_click(target["x"], target["y"], target["unitId"]))
    session.turn(player, packet)
    return session.capture("right-click-command")


def hostile(session):
    session.turn(0, cmd.set_allies(0))
    session.turn(1, cmd.set_allies(1))
    record = session.capture("hostile")
    session.expect(player(session, record, 0)["alliedMask"] & 2, 0, "player 1 hostile to player 0")
    session.expect(player(session, record, 1)["alliedMask"] & 1, 0, "player 0 hostile to player 1")
    return record


def allied(session):
    session.turn(0, cmd.set_allies(0, (1,)))
    session.turn(1, cmd.set_allies(1, (0,)))
    record = session.capture("allied")
    session.expect(player(session, record, 0)["alliedMask"] & 3, 3, "both players allied with player 0")
    session.expect(player(session, record, 1)["alliedMask"] & 3, 3, "both players allied with player 1")
    return record


class CommandPackingTests(unittest.TestCase):
    def test_select_layout(self):
        self.assertEqual(cmd.select(1, 0x234), b"\x09\x02\x01\x00\x34\x02")
        self.assertEqual(cmd.turn_bytes(cmd.select(1), cmd.kill_egg()), b"\x09\x01\x01\x00\x19")

    def test_right_click_and_action_layout(self):
        self.assertEqual(cmd.right_click(0x1234, 0x5678, 0x9ABC),
                         b"\x14\x34\x12\x78\x56\xbc\x9a\xe4\x00\x00")
        self.assertEqual(cmd.action(10, 20, cmd.ORDER["patrol"]),
                         b"\x15\x0a\x00\x14\x00\x00\x00\xe4\x00\x98\x00")

    def test_production_and_placebox_layouts(self):
        self.assertEqual(cmd.morph(cmd.UNIT["drone"]), b"\x23\x29\x00")
        self.assertEqual(cmd.build(cmd.UNIT["overlord"]), b"\x1f\x2a\x00")
        self.assertEqual(cmd.upgrade_building(cmd.UNIT["lair"]), b"\x35\x84\x00")
        self.assertEqual(cmd.placebox(cmd.ORDER["place_building"], cmd.UNIT["supply_depot"], 12, 13),
                         b"\x0c\x1e\x0c\x00\x0d\x00\x6d\x00")

    def test_alliance_layout(self):
        word = cmd.alliance_word(0, (1,))
        self.assertEqual(word, 0x0A)  # slot 0: 2, slot 1: 2 << 2
        self.assertEqual(cmd.set_allies(0, (1,)), struct.pack("<BI", 0x0E, word))
        self.assertEqual(cmd.alliance_word(1), 0x08)
        self.assertEqual(cmd.alliance_word(1, ()), 0x08)

    def test_invalid_fields_are_rejected(self):
        for call in (
            lambda: cmd.select(),
            lambda: cmd.select(*range(13)),
            lambda: cmd.select(-1),
            lambda: cmd.right_click(65536, 0),
            lambda: cmd.action(0, 0, 256),
            lambda: cmd.placebox(30, 109, -1, 0),
            lambda: cmd.set_allies(12),
            lambda: cmd.select(True),
        ):
            with self.subTest(call=call):
                with self.assertRaises(ValueError):
                    call()


class FixtureValidityTests(unittest.TestCase):
    def test_generation_is_deterministic(self):
        for name in fixtures.SCENARIOS:
            with self.subTest(name=name):
                self.assertEqual(fixtures.fixture_bytes(name), fixtures.fixture_bytes(name))

    def test_archives_are_self_contained_and_valid(self):
        for name in fixtures.SCENARIOS:
            with self.subTest(name=name):
                raw = fixtures.fixture_bytes(name)
                with zipfile.ZipFile(io.BytesIO(raw)) as package:
                    self.assertIn("map.json", package.namelist())
                    self.assertIn("layers/isom.bin", package.namelist())
                    self.assertEqual(len(package.read("layers/tiles.bin")), 256 * 256 * 2)
                    document = json.loads(package.read("map.json"))
                self.assertEqual(document["format"], "hyperion-map")
                self.assertEqual(document["version"], 1)
                self.assertEqual(document["size"], [256, 256])
                self.assertEqual(document["tileset"], "jungle")
                self.assertEqual(len(document["players"]), 2)
                self.assertNotEqual(document["players"][0]["race"], "user_select")
                self.assertNotEqual(document["players"][1]["race"], "user_select")
                starts = [row for row in document["units"] if row["type"] == "start_location"]
                self.assertEqual([(row["owner"], row["x"], row["y"]) for row in starts],
                                 [(0, 384, 240), (1, 2368, 304)])
                for row in document["units"]:
                    self.assertGreaterEqual(row["x"], 0)
                    self.assertGreaterEqual(row["y"], 0)
                    self.assertLess(row["x"], 256 * 32)
                    self.assertLess(row["y"], 256 * 32)
                    self.assertIs(type(row["owner"]), int if row["type"] != "mineral_field_1" else str)
                self.assertEqual(len(document["triggers"]), 1)
                self.assertNotIn("preserve_trigger", {a["type"] for a in document["triggers"][0]["actions"]})
                settings = document.get("settings", {})
                self.assertNotIn("weapons", settings, "scenario uses standard combat data")

    def test_requested_real_units_and_cloak_properties_are_present(self):
        mining = fixtures.fixture_document("one_worker_mining_and_returning")
        self.assertIn("mineral_field_1", {row["type"] for row in mining["units"]})
        mineral = next(row for row in mining["units"] if row["type"] == "mineral_field_1")
        self.assertEqual(mineral["resources"], 80)
        visibility = fixtures.fixture_document("visibility_vs_cloaked_detection")
        cloaked = [row for row in visibility["units"] if row["type"] == "protoss_dark_templar"]
        self.assertEqual(len(cloaked), 3)
        self.assertTrue(all(row["state"] == 1 and row["valid_state"] == 1 for row in cloaked))
        self.assertNotIn("shared_vision", visibility["forces"][0]["flags"])
        alliance = fixtures.fixture_document("team_and_alliance")
        self.assertIn("shared_vision", alliance["forces"][0]["flags"])
        for name in fixtures.SCENARIOS:
            self.assertGreater(len(fixtures.fixture_document(name)["units"]), 2, name)

    def test_scenario_coverage_and_independent_game_data(self):
        methods = {name[5:] for name in dir(NativeEngineTests) if name.startswith("test_")}
        self.assertEqual(methods, set(fixtures.SCENARIOS))
        self.assertEqual((cmd.ORDER["zerg_unit_morph"], cmd.ORDER["zerg_building_morph"],
                          cmd.ORDER["follow"], cmd.ORDER["return_minerals"], cmd.ORDER["patrol"]),
                         (42, 43, 49, 90, 152))
        for name in ("larva_to_drone", "egg_completion", "hatchery_to_lair",
                     "building_placement_and_completion", "supply_production_and_cap_handling"):
            self.assertNotIn("units", fixtures.fixture_document(name)["settings"],
                             f"{name} uses standard unit data, not a test-only timing override")


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("clang++"),
                     "pure native port regressions require Linux and clang++")
class PortRegressionTests(unittest.TestCase):
    """Compile the real port TUs, but link only their focused pure helpers.

    Function/data sections plus --gc-sections discard bridge, UI, renderer and
    game-launch code. Small stubs observe net_process_game_turn directly. No
    Hyperion process or natural game is started by these tests.
    """

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.folder = Path(__file__).resolve().parent
        cls.engine = cls.folder.parents[1]
        cls.src = cls.engine / "src117"
        cls.third = cls.engine / "third_party"
        temp_parent = Path("/tmp/opencode")
        cls.temp = tempfile.TemporaryDirectory(
            prefix="pluto-port-regressions-",
            dir=temp_parent if temp_parent.is_dir() else None,
        )
        cls.build = Path(cls.temp.name)
        common = [
            "clang++", "-std=c++20", "-O0", "-w", "-Wno-c++11-narrowing",
            "-Wno-incompatible-ms-struct", "-fno-strict-aliasing", "-fwrapv",
            "-fno-delete-null-pointer-checks", "-ffp-contract=off", "-fsigned-char",
            "-mms-bitfields", "-fno-sized-deallocation", "-ffunction-sections",
            "-fdata-sections", "-DNDEBUG", "-DHIGHFIRST", "-include",
            str(cls.src / "h" / "compiler.h"), f"-I{cls.src / 'compat'}",
            f"-I{cls.src / 'h'}", "-DOC_WITH_HELD",
        ]
        def third_party(pattern):
            matches = sorted(cls.third.glob(pattern))
            if not matches:
                raise RuntimeError(f"pure native regression include is missing: {cls.third / pattern}")
            return matches[-1]

        control_includes = [
            f"-I{third_party('SDL3-*-src') / 'include'}",
            f"-I{third_party('CascLib-*') / 'src'}",
            f"-I{third_party('stb-*')}",
            f"-I{third_party('clay-*')}",
            "-DCASCLIB_NO_AUTO_LINK_LIBRARY",
        ]
        try:
            for source, extra in (("pluto_port_regression.cpp", []),
                                  ("control_turn_regression.cpp", control_includes)):
                output = cls.build / Path(source).stem
                command = common + extra + [str(cls.folder / source), "-Wl,--gc-sections", "-o", str(output)]
                built = subprocess.run(command, text=True, capture_output=True, timeout=180)
                if built.returncode:
                    raise RuntimeError(
                        f"pure native regression build failed: {' '.join(command)}\n"
                        f"stdout:\n{built.stdout}\nstderr:\n{built.stderr}"
                    )
        except Exception:
            cls.temp.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()
        super().tearDownClass()

    def run_regression(self, executable, scenario):
        command = [str(self.build / executable), scenario]
        result = subprocess.run(command, text=True, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0,
                         f"{' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")

    def test_pluto_no_target_sentinel(self):
        self.run_regression("pluto_port_regression", "no-target")

    def test_pluto_missing_target_drops_whole_command(self):
        self.run_regression("pluto_port_regression", "missing-target")

    def test_pluto_delayed_death_and_slot_reuse(self):
        self.run_regression("pluto_port_regression", "apply-reuse")

    def test_pluto_pending_reply_timing(self):
        self.run_regression("pluto_port_regression", "pending-timing")

    def test_pluto_repair_storm_mapping_participants(self):
        self.run_regression("pluto_port_regression", "repair-mapping")

    def test_pluto_seat_and_player_id_mismatch(self):
        self.run_regression("pluto_port_regression", "seat-mismatch")

    def test_turn_remaps_ff_owner_and_seat_mismatch(self):
        self.run_regression("control_turn_regression", "storm-owner")

    def test_turn_repair_storm_mapping_participants(self):
        self.run_regression("control_turn_regression", "repair-mapping")

    def test_turn_old_storm_mapping_follows_remaining_owner(self):
        self.run_regression("control_turn_regression", "old-owner-remap")

    def test_turn_rejects_malformed_and_unsafe_commands(self):
        self.run_regression("control_turn_regression", "malformed-turns")

    def test_turn_valid_command_after_sync_rejection(self):
        self.run_regression("control_turn_regression", "valid-after-reject")


@unittest.skipUnless(ENGINE, "requires an explicit engine path (run_tests.py --engine)")
class NativeEngineTests(unittest.TestCase):
    maxDiff = None

    def run_scenario(self, body):
        self.assertTrue(OUTPUT, "run_tests.py supplies an output folder with --engine")
        name = self._testMethodName[5:] if self._testMethodName.startswith("test_") else self._testMethodName
        session = EngineSession(name, ENGINE, DATA, OUTPUT)
        with session:
            try:
                body(session)
            except Exception as error:
                session.evidence.unhandled(session, error)
                raise
            session.evidence.passed(session)

    def test_one_worker_mining_and_returning(self):
        def scenario(s):
            initial = s.evidence.last
            worker = one(s, initial, "worker", unit_type=cmd.UNIT["scv"], player=0)
            mineral = one(s, initial, "mineral", unit_type=cmd.UNIT["mineral_field_1"], player=11)
            depot = one(s, initial, "depot", unit_type=cmd.UNIT["command_center"], player=0)
            expect_fields(s, initial, depot, completed=True, player=0)
            s.expect(player(s, initial, 0)["minerals"], 1000, "initial minerals")
            s.expect(mineral["resourceAmount"], 80, "initial mineral amount")
            base = s.frame
            record = right_click(s, 0, [worker], mineral)
            worker_now = by_id(s, record, worker["id"])
            expect_fields(s, record, worker_now, order=cmd.ORDER["move_to_minerals"],
                          orderTargetId=mineral["id"], carriedResource=0, carriedAmount=0)
            record = s.step()
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["move_to_minerals"],
                          orderState=1, carriedResource=0)
            record = s.step_to(base + 26)
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["move_to_minerals"],
                          orderState=3, carriedResource=0)
            record = s.step_to(base + 35)
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["wait_for_minerals"])
            record = s.step()
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["mining_minerals"],
                          orderState=0)
            record = s.step()
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["mining_minerals"],
                          orderState=5, carriedResource=0, carriedAmount=0)
            s.expect(by_id(s, record, mineral["id"])["resourceAmount"], 80, "minerals not taken early")
            record = s.step_to(base + 118)
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["right_click_action"],
                          carriedResource=2, carriedAmount=8)
            s.expect(by_id(s, record, mineral["id"])["resourceAmount"], 72, "eight minerals removed")
            s.expect(player(s, record, 0)["minerals"], 1000, "minerals not banked before return")
            record = s.step()
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["return_minerals"],
                          orderTargetId=depot["id"], carriedResource=2, carriedAmount=8)
            record = s.step_to(base + 137)
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["move_to_minerals"],
                          orderState=0, carriedResource=0, carriedAmount=0)
            s.expect(player(s, record, 0)["minerals"], 1008, "exact banked mineral return")
        self.run_scenario(scenario)

    def test_repeated_worker_replacement_after_losses(self):
        def scenario(s):
            s.expect_true(s.frame in (2, 3), "the fixture starts with three associated larvae", s.frame)
            record = s.capture("three-larvae")
            larvae = sorted(units_of(record, unit_type=cmd.UNIT["larva"], player=0), key=lambda row: row["id"])
            s.expect(len(larvae), 3, "three larvae for three replacements")
            marines = sorted(units_of(record, unit_type=cmd.UNIT["marine"], player=1), key=lambda row: row["id"])
            s.expect(len(marines), 8, "small allied army")
            allied(s)
            dead = []
            for cycle in range(2):
                base = s.frame
                morph(s, larvae[cycle], cmd.UNIT["drone"])
                record = s.step_to(base + 334)
                drone = by_id(s, record, larvae[cycle]["id"])
                expect_fields(s, record, drone, alive=True, type=cmd.UNIT["drone"], completed=True)
                record = right_click(s, 1, marines, drone)
                for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=1):
                    expect_fields(s, record, marine, order=cmd.ORDER["follow"], orderTargetId=drone["id"])
                record = s.step_to(s.frame + 16)
                for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=1):
                    s.expect_true(marine["order"] in (cmd.ORDER["player_guard"], cmd.ORDER["guard_post"]),
                                  "allied follow has reached its terminal guard state", marine["order"])
                    expect_fields(s, record, marine, orderTargetId=-1)
                s.expect(by_id(s, record, drone["id"])["hp"], 40, "worker alive before hostile turn")
                hostile(s)
                record = right_click(s, 1, marines, by_id(s, record, drone["id"]))
                for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=1):
                    expect_fields(s, record, marine, order=cmd.ORDER["attack_unit"], orderTargetId=drone["id"])
                death_delay = 13 if cycle == 0 else 17
                record = s.step_to(s.frame + death_delay - 1)
                expect_fields(s, record, by_id(s, record, drone["id"]), hp=4, alive=True)
                record = s.step()
                expect_fields(s, record, by_id(s, record, drone["id"]), hp=0, alive=True)
                record = s.step()
                expect_fields(s, record, by_id(s, record, drone["id"]), alive=False)
                dead.append(drone["id"])
                allied(s)  # stop further automatic attacks before the next replacement
            base = s.frame
            morph(s, larvae[2], cmd.UNIT["drone"])
            record = s.step_to(base + 334)
            survivor = by_id(s, record, larvae[2]["id"])
            expect_fields(s, record, survivor, alive=True, type=cmd.UNIT["drone"], completed=True)
            for table_id in dead:
                expect_fields(s, record, by_id(s, record, table_id), alive=False)
            s.expect(player(s, record, 0)["minerals"], 850, "three standard drone costs, no refund for combat losses")
            s.expect(player(s, record, 0)["supply"], [2, 2, 2, 400], "only the surviving replacement consumes supply")
        self.run_scenario(scenario)

    def test_larva_to_drone(self):
        def scenario(s):
            initial = s.evidence.last
            larva = one(s, initial, "initial larva", unit_type=cmd.UNIT["larva"], player=0)
            base = s.frame
            record = morph(s, larva, cmd.UNIT["drone"])
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["larva"],
                          order=cmd.ORDER["zerg_unit_morph"], buildType=cmd.UNIT["drone"],
                          buildQueue=[cmd.UNIT["drone"], 228, 228, 228, 228], buildQueueSlot=0)
            record = s.step()
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["egg"],
                          previousType=cmd.UNIT["larva"], completed=False, remainingBuildTime=300,
                          orderState=1)
            record = s.step_to(base + 302)
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["egg"],
                          completed=False, remainingBuildTime=0, orderState=2)
            record = s.step_to(base + 319)
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["drone"],
                          previousType=cmd.UNIT["egg"], completed=False, remainingBuildTime=300)
            record = s.step_to(base + 334)
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["drone"],
                          previousType=cmd.UNIT["egg"], completed=True, remainingBuildTime=0,
                          buildType=228, buildQueue=[228, 228, 228, 228, 228])
            s.expect(player(s, record, 0)["minerals"], 950, "one standard drone cost")
        self.run_scenario(scenario)

    def test_larva_hatchery_association_and_spawning(self):
        def scenario(s):
            initial = s.evidence.last
            hatchery = one(s, initial, "hatchery", unit_type=cmd.UNIT["hatchery"], player=0)
            s.expect_true(s.frame < 360, "startup paused before second larva", s.frame)
            initial_larvae = units_of(initial, unit_type=cmd.UNIT["larva"], player=0)
            s.expect(len(initial_larvae), 1, "one larva at frame one")
            expect_fields(s, initial, initial_larvae[0], connectedId=hatchery["id"], completed=True)
            record = s.step_to(360)
            s.expect(len(units_of(record, unit_type=cmd.UNIT["larva"], player=0)), 1, "no second larva at frame 360")
            record = s.step_to(361)
            larvae = units_of(record, unit_type=cmd.UNIT["larva"], player=0)
            s.expect(len(larvae), 2, "second larva at frame 361")
            for larva in larvae:
                expect_fields(s, record, larva, connectedId=hatchery["id"], completed=True)
            record = s.step_to(714)
            s.expect(len(units_of(record, unit_type=cmd.UNIT["larva"], player=0)), 2, "no third larva at frame 714")
            record = s.step_to(715)
            larvae = units_of(record, unit_type=cmd.UNIT["larva"], player=0)
            s.expect(len(larvae), 3, "third larva at frame 715")
            for larva in larvae:
                expect_fields(s, record, larva, connectedId=hatchery["id"])
            record = s.step_to(1000)
            s.expect(len(units_of(record, unit_type=cmd.UNIT["larva"], player=0)), 3, "larva cap remains three")
        self.run_scenario(scenario)

    def test_egg_completion(self):
        def scenario(s):
            larva = one(s, s.evidence.last, "larva", unit_type=cmd.UNIT["larva"], player=0)
            base = s.frame
            morph(s, larva, cmd.UNIT["drone"])
            expected = [(1, 300, 1), (2, 299, 1), (3, 298, 1), (150, 151, 1),
                        (299, 2, 1), (300, 1, 1), (301, 0, 1), (302, 0, 2)]
            for delta, remaining, order_state in expected:
                record = s.step_to(base + delta)
                expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["egg"],
                              completed=False, remainingBuildTime=remaining, orderState=order_state)
            record = s.step_to(base + 319)
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["drone"],
                          completed=False, remainingBuildTime=300, previousType=cmd.UNIT["egg"])
            record = s.step_to(base + 334)
            expect_fields(s, record, by_id(s, record, larva["id"]), type=cmd.UNIT["drone"],
                          completed=True, remainingBuildTime=0, previousType=cmd.UNIT["egg"],
                          buildQueue=[228, 228, 228, 228, 228])
        self.run_scenario(scenario)

    def test_hatchery_to_lair(self):
        def scenario(s):
            hatchery = one(s, s.evidence.last, "hatchery", unit_type=cmd.UNIT["hatchery"], player=0)
            base = s.frame
            session_bytes = cmd.turn_bytes(select(hatchery), cmd.upgrade_building(cmd.UNIT["lair"]))
            s.turn(0, session_bytes)
            record = s.capture("lair-command")
            expect_fields(s, record, by_id(s, record, hatchery["id"]), type=cmd.UNIT["hatchery"],
                          order=cmd.ORDER["zerg_building_morph"], buildType=cmd.UNIT["lair"],
                          buildQueue=[cmd.UNIT["lair"], 228, 228, 228, 228])
            record = s.step()
            expect_fields(s, record, by_id(s, record, hatchery["id"]), type=cmd.UNIT["hatchery"],
                          order=cmd.ORDER["incomplete_morphing"], completed=False,
                          remainingBuildTime=1500, buildType=cmd.UNIT["lair"])
            s.expect(player(s, record, 0)["minerals"], 850, "standard lair mineral cost")
            s.expect(player(s, record, 0)["gas"], 400, "standard lair gas cost")
            record = s.step_to(base + 1501)
            expect_fields(s, record, by_id(s, record, hatchery["id"]), type=cmd.UNIT["hatchery"],
                          completed=False, remainingBuildTime=0, orderState=4)
            record = s.step()
            expect_fields(s, record, by_id(s, record, hatchery["id"]), orderState=5)
            record = s.step()
            expect_fields(s, record, by_id(s, record, hatchery["id"]), orderState=6)
            record = s.step_to(base + 1508)
            expect_fields(s, record, by_id(s, record, hatchery["id"]), type=cmd.UNIT["hatchery"],
                          completed=False, remainingBuildTime=0)
            record = s.step()
            expect_fields(s, record, by_id(s, record, hatchery["id"]), type=cmd.UNIT["lair"],
                          previousType=cmd.UNIT["hatchery"], completed=True, remainingBuildTime=0,
                          buildQueue=[228, 228, 228, 228, 228])
        self.run_scenario(scenario)

    def test_building_placement_and_completion(self):
        def scenario(s):
            worker = one(s, s.evidence.last, "SCV", unit_type=cmd.UNIT["scv"], player=0)
            base = s.frame
            packet = cmd.turn_bytes(select(worker), cmd.placebox(cmd.ORDER["place_building"],
                                                                 cmd.UNIT["supply_depot"], 12, 12))
            s.turn(0, packet)
            record = s.capture("place-building")
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["place_building"],
                          buildType=cmd.UNIT["supply_depot"], orderTarget=[432, 416])
            record = s.step()
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["place_building"])
            s.expect(len(units_of(record, unit_type=cmd.UNIT["supply_depot"], player=0)), 0,
                     "building not created before placement")
            record = s.step_to(base + 21)
            building = one(s, record, "new supply depot", unit_type=cmd.UNIT["supply_depot"], player=0)
            expect_fields(s, record, building, completed=False, remainingBuildTime=600,
                          connectedId=worker["id"], x=432, y=416)
            expect_fields(s, record, by_id(s, record, worker["id"]), order=cmd.ORDER["constructing_building"],
                          orderTargetId=building["id"])
            s.expect(player(s, record, 0)["minerals"], 900, "standard supply depot cost")
            record = s.step_to(base + 622)
            expect_fields(s, record, by_id(s, record, building["id"]), completed=False, remainingBuildTime=0)
            record = s.step_to(base + 624)
            expect_fields(s, record, by_id(s, record, building["id"]), completed=True,
                          remainingBuildTime=0, connectedId=-1)
            s.expect(player(s, record, 0)["supply"], [2, 16, 16, 400], "completed depot provides eight supply")
        self.run_scenario(scenario)

    def test_supply_production_and_cap_handling(self):
        def scenario(s):
            record = s.capture("three-larvae")
            larvae = sorted(units_of(record, unit_type=cmd.UNIT["larva"], player=0), key=lambda row: row["id"])
            s.expect(len(larvae), 3, "three larvae")
            s.expect(player(s, record, 0)["supply"], [0, 2, 2, 400], "hatchery-only supply")
            base = s.frame
            morph(s, larvae[0], cmd.UNIT["drone"])
            record = s.step()
            s.expect(player(s, record, 0)["supply"], [2, 2, 2, 400], "first egg reserves supply")
            s.expect(player(s, record, 0)["minerals"], 950, "first standard drone cost")
            record = s.step_to(base + 334)
            expect_fields(s, record, by_id(s, record, larvae[0]["id"]), type=cmd.UNIT["drone"], completed=True)
            morph(s, larvae[1], cmd.UNIT["drone"])
            record = s.step()
            expect_fields(s, record, by_id(s, record, larvae[1]["id"]), type=cmd.UNIT["larva"],
                          order=cmd.ORDER["larva"], buildType=228, buildQueue=[228, 228, 228, 228, 228])
            s.expect(player(s, record, 0)["supply"], [2, 2, 2, 400], "second drone rejected at cap")
            s.expect(player(s, record, 0)["minerals"], 950, "rejected drone costs nothing")
            base = s.frame
            morph(s, larvae[1], cmd.UNIT["overlord"])
            record = s.step()
            expect_fields(s, record, by_id(s, record, larvae[1]["id"]), type=cmd.UNIT["egg"],
                          previousType=cmd.UNIT["larva"], remainingBuildTime=600)
            s.expect(player(s, record, 0)["minerals"], 850, "standard overlord cost")
            record = s.step_to(base + 634)
            expect_fields(s, record, by_id(s, record, larvae[1]["id"]), type=cmd.UNIT["overlord"], completed=True)
            s.expect(player(s, record, 0)["supply"], [2, 18, 18, 400], "overlord adds eight supply")
            base = s.frame
            morph(s, larvae[2], cmd.UNIT["drone"])
            record = s.step()
            s.expect(player(s, record, 0)["supply"], [4, 18, 18, 400], "new cap admits another drone")
            s.expect(player(s, record, 0)["minerals"], 800, "second admitted standard drone cost")
            record = s.step_to(base + 334)
            expect_fields(s, record, by_id(s, record, larvae[2]["id"]), type=cmd.UNIT["drone"], completed=True)
            s.expect(len(units_of(record, unit_type=cmd.UNIT["drone"], player=0)), 2, "two completed drones")
        self.run_scenario(scenario)

    def test_small_army_attack_kills_target(self):
        def scenario(s):
            initial = s.evidence.last
            marines = sorted(units_of(initial, unit_type=cmd.UNIT["marine"], player=0), key=lambda row: row["id"])
            target = one(s, initial, "target SCV", unit_type=cmd.UNIT["scv"], player=1)
            s.expect(len(marines), 8, "eight marines")
            allied(s)
            base = s.frame
            record = right_click(s, 0, marines, target)
            for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=0):
                expect_fields(s, record, marine, order=cmd.ORDER["follow"], orderTargetId=target["id"])
            record = s.step_to(base + 20)
            for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=0):
                expect_fields(s, record, marine, order=cmd.ORDER["follow"], orderTargetId=target["id"])
            s.expect(by_id(s, record, target["id"])["hp"], 60, "target intact before attack")
            hostile(s)
            record = right_click(s, 0, marines, by_id(s, record, target["id"]))
            for marine in units_of(record, unit_type=cmd.UNIT["marine"], player=0):
                expect_fields(s, record, marine, order=cmd.ORDER["attack_unit"], orderTargetId=target["id"])
            record = s.step_to(s.frame + 3)
            expect_fields(s, record, by_id(s, record, target["id"]), hp=42, alive=True)
            record = s.step()
            expect_fields(s, record, by_id(s, record, target["id"]), hp=24, alive=True)
            record = s.step()
            expect_fields(s, record, by_id(s, record, target["id"]), hp=12, alive=True)
            record = s.step_to(s.frame + 11)
            expect_fields(s, record, by_id(s, record, target["id"]), hp=0, alive=True)
            record = s.step()
            expect_fields(s, record, by_id(s, record, target["id"]), alive=False)
        self.run_scenario(scenario)

    def test_attack_vs_follow_and_patrol(self):
        def scenario(s):
            marine = one(s, s.evidence.last, "marine", unit_type=cmd.UNIT["marine"], player=0)
            target = one(s, s.evidence.last, "target", unit_type=cmd.UNIT["scv"], player=1)
            allied(s)
            base = s.frame
            record = right_click(s, 0, [marine], target)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          orderTargetId=target["id"])
            record = s.step_to(base + 20)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          direction=64, currentDirection=64)
            session_bytes = cmd.turn_bytes(select(marine), cmd.action(768, 512, cmd.ORDER["patrol"]))
            s.turn(0, session_bytes)
            record = s.capture("patrol-command")
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["patrol"],
                          orderTargetId=-1, orderTarget=[768, 512])
            record = s.step()
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["patrol"])
            hostile(s)
            record = right_click(s, 0, [marine], by_id(s, record, target["id"]))
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["attack_unit"],
                          orderTargetId=target["id"])
            record = s.step_to(base + 24)
            expect_fields(s, record, by_id(s, record, target["id"]), hp=54, alive=True)
            cooldown = by_id(s, record, marine["id"])["groundCooldown"]
            s.expect(cooldown, 14, "marine cooldown after the patrol-interrupted shot")
            s.expect(s.frame, base + 24, "follow, patrol and attack use explicit frames")
        self.run_scenario(scenario)

    def test_weapon_cooldown_facing_velocity_and_order_targets(self):
        def scenario(s):
            marine = one(s, s.evidence.last, "marine", unit_type=cmd.UNIT["marine"], player=0)
            hatchery = one(s, s.evidence.last, "target hatchery", unit_type=cmd.UNIT["hatchery"], player=1)
            allied(s)
            base = s.frame
            packet = cmd.turn_bytes(select(marine), cmd.action(256, 240, cmd.ORDER["move"]))
            s.turn(0, packet)
            record = s.capture("move-west-command")
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["move"],
                          orderTargetId=-1, orderTarget=[256, 240], moveTargetId=-1)
            record = s.step_to(base + 5)
            moving = by_id(s, record, marine["id"])
            s.expect(moving["speedX"], -1024, "full westward speedX after acceleration")
            s.expect(moving["speedY"], -1, "classic horizontal movement's speedY rounding")
            s.expect(moving["direction"], 192, "west facing")
            record = s.step_to(base + 54)
            expect_fields(s, record, by_id(s, record, marine["id"]), x=256, y=240,
                          speedX=0, speedY=0, direction=192, currentDirection=192)
            record = right_click(s, 0, [marine], hatchery)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          orderTargetId=hatchery["id"], orderTarget=[384, 320])
            record = s.step_to(s.frame + 16)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["player_guard"],
                          direction=96, currentDirection=95)
            hostile(s)
            record = right_click(s, 0, [marine], by_id(s, record, hatchery["id"]))
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["attack_unit"],
                          orderTargetId=hatchery["id"], orderTarget=[384, 320])
            record = s.step_to(s.frame + 3)
            attacker = by_id(s, record, marine["id"])
            expect_fields(s, record, attacker, direction=96, currentDirection=95)
            cooldown = attacker["groundCooldown"]
            s.expect(cooldown, 12, "marine weapon cooldown on the first hit frame")
            s.expect(by_id(s, record, hatchery["id"])["hp"], 1245, "one six-damage marine shot minus one armor")
            record = s.step()
            expect_fields(s, record, by_id(s, record, marine["id"]), groundCooldown=cooldown - 1)
            s.expect(by_id(s, record, hatchery["id"])["hp"], 1245, "no shot while cooling down")
        self.run_scenario(scenario)

    def test_team_and_alliance(self):
        def scenario(s):
            marine = one(s, s.evidence.last, "own marine", unit_type=cmd.UNIT["marine"], player=0)
            other = one(s, s.evidence.last, "other marine", unit_type=cmd.UNIT["marine"], player=1)
            first = player(s, s.evidence.last, 0)
            second = player(s, s.evidence.last, 1)
            s.expect(first["team"], second["team"], "both map players share force zero")
            s.expect_true(first["team"] in (0, 1), "scenario force team numbering", first["team"])
            allied(s)
            record = right_click(s, 0, [marine], other)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          orderTargetId=other["id"])
            s.expect(by_id(s, record, other["id"])["visibleMask"] & 3, 3, "shared force vision")
            record = s.step_to(s.frame + 19)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          direction=64, currentDirection=64)
            hostile(s)
            record = right_click(s, 0, [marine], by_id(s, record, other["id"]))
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["attack_unit"],
                          orderTargetId=other["id"])
            record = s.step_to(s.frame + 3)
            expect_fields(s, record, by_id(s, record, other["id"]), hp=34)
            allied(s)
            record = right_click(s, 0, [marine], by_id(s, record, other["id"]))
            record = s.step_to(s.frame + 6)
            expect_fields(s, record, by_id(s, record, marine["id"]), order=cmd.ORDER["follow"],
                          orderTargetId=other["id"])
            record = s.step()
            expect_fields(s, record, by_id(s, record, other["id"]), hp=34)
        self.run_scenario(scenario)

    def test_visibility_vs_cloaked_detection(self):
        def scenario(s):
            initial = s.evidence.last
            expected = (
                (448, 3, 2, "visible but undetected cloaked target"),
                (960, 3, 3, "visible and detected cloaked target"),
                (1472, 2, 2, "cloaked target outside other player's vision"),
            )
            tracked = []
            for x, visible, detected, label in expected:
                dark = one(s, initial, label, unit_type=cmd.UNIT["dark_templar"], player=1, x=x)
                tracked.append((dark["id"], visible, detected, label))
            record = s.step_to(32)
            for table_id, visible, detected, label in tracked:
                dark = by_id(s, record, table_id)
                s.expect(dark["statusFlags"] & 0x100, 0x100, f"{label}: cloak status flag")
                s.expect(dark["visibleMask"] & 3, visible, f"{label}: visibility mask")
                s.expect(dark["detectedMask"] & 3, detected, f"{label}: detection mask")
            record = s.step_to(48)
            for table_id, visible, detected, label in tracked:
                dark = by_id(s, record, table_id)
                s.expect(dark["visibleMask"] & 3, visible, f"{label}: visibility remains stable")
                s.expect(dark["detectedMask"] & 3, detected, f"{label}: detection remains stable")
        self.run_scenario(scenario)


if __name__ == "__main__":
    unittest.main(verbosity=2)
