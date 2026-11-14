from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from pluto_diff.cli import main
from pluto_diff.worker_stalls import (
    DRONE,
    INCOMPLETE_WORKER_GRACE_FRAMES,
    SCV,
    WorkerStallError,
    analyze_worker_stalls,
)
from tests.helpers import LOBBY_ONLY_COMMANDS, make_reply


MORPH_DRONE = struct.pack("<BH", 0x23, 41)
TRAIN_SCV = struct.pack("<BH", 0x1F, 7)
TRAIN_PROBE = struct.pack("<BH", 0x1F, 64)


def select(unit_id: int) -> bytes:
    return struct.pack("<BBH", 0x09, 1, unit_id)


def unit(
    unit_id: int,
    unit_type: int,
    *,
    player: int = 0,
    order: int = 156,
    completed: int = 1,
    build_type: int = 228,
    remaining: int = 0,
    current_build_unit_id: int = 0xFFFFFFFF,
    generation: int = 0,
) -> dict[str, int]:
    return {
        "id": unit_id,
        "generation": generation,
        "type": unit_type,
        "player": player,
        "order": order,
        "completed": completed,
        "build_type": build_type,
        "remaining": remaining,
        "current_build_unit_id": current_build_unit_id,
    }


def frame(
    number: int,
    units: list[dict[str, int]],
    *,
    protocol: bytes = b"HXP3",
    race: int = 0,
    minerals: int = 50,
    gas: int = 0,
    supply_total: int = 18,
    supply_used: int = 8,
) -> bytes:
    unit_bytes = bytearray()
    extra_bytes = bytearray()
    for value in units:
        unit_bytes += struct.pack(
            "<9I4B",
            value["id"],
            value["type"],
            value["player"],
            100,
            200,
            500,
            0,
            0,
            value["order"],
            1,
            value["completed"],
            255,
            value["generation"],
        )
        if protocol in (b"HXP3", b"HXP4"):
            extra_bytes += struct.pack(
                "<5I2i4I5H10B",
                0,
                0,
                0,
                0xFFFFFFFF,
                value["current_build_unit_id"],
                0,
                0,
                value["build_type"],
                228,
                0,
                228,
                0,
                0,
                0,
                0,
                value["remaining"],
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            )
        if protocol == b"HXP4":
            extra_bytes += struct.pack(
                "<6H2B", value["build_type"], 228, 228, 228, 228, 0, 44, 61
            )
    header = struct.pack("<4s5I", protocol, number, len(units), 1, 1, 1)
    if protocol == b"HXP4":
        header += struct.pack("<II", 78, 119)
    player = struct.pack(
        "<7I", race, minerals, gas, supply_total, supply_used, 32, 64
    )
    if protocol == b"HXP4":
        player += bytes(119)
    planes = b"\x00\x00\x00\x00" if protocol in (b"HXP2", b"HXP3", b"HXP4") else b"\x01\x01"
    return header + unit_bytes + extra_bytes + player + planes


def zerg_state(
    number: int,
    *,
    minerals: int = 50,
    supply_total: int = 18,
    supply_used: int = 8,
    larva: dict[str, int] | None = None,
) -> bytes:
    return frame(
        number,
        [larva or unit(1, 35), unit(2, 41, order=85)],
        minerals=minerals,
        supply_total=supply_total,
        supply_used=supply_used,
    )


def replies_for(frames: int, commands: dict[int, bytes] | None = None) -> bytes:
    commands = commands or {}
    return b"".join(make_reply(number, commands.get(number, b"")) for number in range(frames))


def same_generation_zerg_completion_candidate(protocol: bytes) -> bytes:
    frames = []
    for number in range(241):
        if number < 4:
            changing = unit(1, 35)
        elif number < 11:
            changing = unit(
                1,
                36,
                completed=0,
                build_type=DRONE,
                remaining=300 - (number - 4),
            )
        else:
            changing = unit(1, DRONE, completed=1)
        frames.append(
            frame(
                number,
                [changing, unit(2, DRONE, order=85)],
                protocol=protocol,
            )
        )
    return b"".join(frames)


class WorkerStallTests(unittest.TestCase):
    def assert_identity_inconclusive(self, result: dict, protocol: bytes) -> None:
        request = result["worker_requests"][0]
        self.assertEqual(request["status"], "production_identity_inconclusive")
        self.assertEqual(request["identity_inconclusive_frame"], 11)
        self.assertFalse(request["completion_evidence"]["completed"])
        self.assertEqual(
            request["completion_evidence"]["identity_status"], "inconclusive"
        )
        self.assertIsNone(request["classification"])
        self.assertEqual(result["completed_worker_production_events"], [])
        self.assertEqual(result["worker_request_summary"]["completed"], 0)
        self.assertEqual(result["worker_request_summary"]["rejected_or_misapplied"], 0)
        self.assertEqual(result["worker_request_summary"]["pending_or_incomplete"], 1)
        ambiguous = result["identity_inconclusive_completion_events"]
        self.assertEqual(len(ambiguous), 1)
        self.assertEqual(ambiguous[0]["frame"], 11)
        self.assertEqual(ambiguous[0]["generation"], 0)
        self.assertEqual(ambiguous[0]["protocol"], protocol.decode("ascii"))
        self.assertFalse(ambiguous[0]["identity_established"])
        self.assertEqual(result["evidence_status"], "incomplete")
        self.assertEqual(len(result["stalls"]), 1)
        self.assertEqual(result["stalls"][0]["status"], "inconclusive")
        self.assertIsNone(result["stalls"][0]["classification"])
        self.assertEqual(len(result["stalls"][0]["identity_inconclusive_completion_events"]), 1)
        self.assertEqual(result["summary"]["actionable_stalls"], 0)
        self.assertEqual(result["summary"]["no_worker_request"], 0)

    def test_no_request_with_available_native_rules(self) -> None:
        result = analyze_worker_stalls(
            b"".join(zerg_state(number) for number in range(241)),
            replies_for(241),
            threshold=240,
        )
        self.assertEqual(len(result["stalls"]), 1)
        stall = result["stalls"][0]
        self.assertEqual(stall["classification"], "no_worker_request")
        self.assertEqual(stall["native_rule_subtype"], "no_state_transition")
        self.assertEqual(stall["frames_without_completed_worker_production"], 241)
        self.assertIsNone(stall["emitted_frame"])
        self.assertTrue(stall["reply_coverage"]["complete"])

    def test_supply_blocked_and_insufficient_resources_without_request(self) -> None:
        supply = analyze_worker_stalls(
            b"".join(
                zerg_state(number, supply_total=10, supply_used=10)
                for number in range(241)
            ),
            replies_for(241),
            threshold=240,
        )
        self.assertEqual(supply["stalls"][0]["classification"], "no_worker_request")
        self.assertEqual(supply["stalls"][0]["native_rule_subtype"], "supply_blocked")

        resources = analyze_worker_stalls(
            b"".join(zerg_state(number, minerals=0) for number in range(241)),
            replies_for(241),
            threshold=240,
        )
        self.assertEqual(resources["stalls"][0]["classification"], "no_worker_request")
        self.assertEqual(
            resources["stalls"][0]["native_rule_subtype"], "insufficient_resources"
        )

    def test_supply_blocked_request(self) -> None:
        result = analyze_worker_stalls(
            b"".join(
                zerg_state(number, supply_total=10, supply_used=10)
                for number in range(241)
            ),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["expected_apply_frame"], 4)
        self.assertEqual(request["classification"], "worker_request_rejected_or_misapplied")
        self.assertEqual(request["native_rule_subtype"], "supply_blocked")
        self.assertEqual(result["stalls"][0]["native_rule_subtype"], "supply_blocked")

    def test_insufficient_resources_rejects_request(self) -> None:
        result = analyze_worker_stalls(
            b"".join(zerg_state(number, minerals=0) for number in range(241)),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["command_bytes_hex"], MORPH_DRONE.hex())
        self.assertEqual(request["decoded_command"]["opcode"], 0x23)
        self.assertEqual(request["decoded_command"]["fields"]["unit_type"], 41)
        self.assertEqual(request["classification"], "worker_request_rejected_or_misapplied")
        self.assertEqual(request["native_rule_subtype"], "insufficient_resources")
        self.assertEqual(request["economy_at_emission"]["minerals"], 0)
        self.assertEqual(request["economy_at_apply"]["minerals"], 0)
        self.assertEqual(request["selected_larva_at_apply"][0]["type"], 35)
        self.assertEqual(result["stalls"][0]["classification"], "worker_request_rejected_or_misapplied")

    def test_rejected_or_misapplied_without_state_transition(self) -> None:
        result = analyze_worker_stalls(
            b"".join(zerg_state(number) for number in range(241)),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["classification"], "worker_request_rejected_or_misapplied")
        self.assertEqual(request["native_rule_subtype"], "no_state_transition")
        self.assertEqual(request["transition_observation_window"]["last_frame"], 8)
        self.assertFalse(request["completion_evidence"]["completed"])
        self.assertTrue(result["stalls"][0]["is_actionable_stall"])

    def test_pending_valid_egg_and_incomplete_drone_are_not_misapplied(self) -> None:
        frames = []
        for number in range(341):
            if number < 4:
                larva = unit(1, 35)
                minerals = 50
                supply_used = 8
            elif number < 304:
                larva = unit(
                    1,
                    36,
                    order=42,
                    completed=0,
                    build_type=41,
                    remaining=300 - (number - 4),
                )
                minerals = 0
                supply_used = 10
            else:
                # The normal lifecycle includes an incomplete drone after the
                # egg's 300 ticks; this must not trigger a simplistic deadline.
                larva = unit(1, 41, order=42, completed=0, build_type=41)
                minerals = 0
                supply_used = 10
            frames.append(
                zerg_state(
                    number,
                    minerals=minerals,
                    supply_used=supply_used,
                    larva=larva,
                )
            )
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(341, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["status"], "pending_valid_production")
        self.assertIsNone(request["classification"])
        self.assertEqual(request["production_start"]["frame"], 4)
        self.assertEqual(
            request["completion_window"]["incomplete_worker_first_frame"], 304
        )
        self.assertEqual(
            request["completion_window"]["valid_until_frame"],
            304 + INCOMPLETE_WORKER_GRACE_FRAMES,
        )
        self.assertGreater(request["completion_window"]["valid_until_frame"], 300)
        self.assertFalse(request["completion_evidence"]["completed"])
        self.assertEqual(result["summary"]["actionable_stalls"], 0)
        self.assertEqual(result["stalls"][0]["status"], "pending_valid_production")
        self.assertIsNone(result["stalls"][0]["classification"])

    def test_expired_timer_and_grace_with_paused_production_is_inconclusive(self) -> None:
        frames = []
        for number in range(241):
            larva = unit(1, 35) if number < 4 else unit(
                1,
                36,
                completed=0,
                build_type=41,
                remaining=1,  # A paused timer must not become rejection evidence.
            )
            frames.append(zerg_state(number, larva=larva))
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertTrue(request["completion_window"]["expired"])
        self.assertEqual(
            request["status"], "completion_window_expired_inconclusive"
        )
        self.assertIsNone(request["classification"])
        self.assertEqual(
            request["native_rule_subtype"], "pending_or_incomplete_evidence"
        )
        self.assertEqual(result["worker_request_summary"]["rejected_or_misapplied"], 0)
        self.assertEqual(result["worker_request_summary"]["pending_or_incomplete"], 1)
        self.assertEqual(result["stalls"][0]["status"], "inconclusive")
        self.assertIsNone(result["stalls"][0]["classification"])
        self.assertEqual(result["summary"]["actionable_stalls"], 0)

    def test_completed_drone_closes_interval_without_fault(self) -> None:
        frames = []
        for number in range(351):
            if number < 4:
                larva = unit(1, 35, generation=7)
            elif number < 304:
                larva = unit(
                    1,
                    36,
                    completed=0,
                    build_type=41,
                    remaining=300 - (number - 4),
                    generation=7,
                )
            else:
                larva = unit(
                    1,
                    41,
                    completed=int(number >= 350),
                    build_type=41,
                    generation=7,
                )
            frames.append(zerg_state(number, larva=larva))
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(351, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        self.assertEqual(result["worker_requests"][0]["status"], "completed")
        self.assertEqual(result["worker_requests"][0]["completion_evidence"]["frame"], 350)
        self.assertEqual(len(result["completed_worker_production_events"]), 1)
        self.assertEqual(result["completed_worker_production_events"][0]["generation"], 7)
        self.assertTrue(
            result["completed_worker_production_events"][0]["identity_established"]
        )
        self.assertEqual(result["identity_inconclusive_completion_events"], [])
        self.assertEqual(result["stalls"][0]["status"], "production_completed")
        self.assertFalse(result["stalls"][0]["is_actionable_stall"])

    def test_hxp4_nonzero_generation_establishes_completion_identity(self) -> None:
        frames = []
        for number in range(12):
            if number < 4:
                changing = unit(1, 35, generation=7)
            elif number < 11:
                changing = unit(
                    1,
                    36,
                    completed=0,
                    build_type=DRONE,
                    remaining=300 - (number - 4),
                    generation=7,
                )
            else:
                changing = unit(1, DRONE, completed=1, generation=7)
            frames.append(
                frame(
                    number,
                    [changing, unit(2, DRONE, order=85, generation=8)],
                    protocol=b"HXP4",
                )
            )
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(12, {0: select(1) + MORPH_DRONE}),
            threshold=1,
        )
        self.assertEqual(result["worker_requests"][0]["status"], "completed")
        events = result["completed_worker_production_events"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["protocol"], "HXP4")
        self.assertEqual(events[0]["generation"], 7)
        self.assertTrue(events[0]["identity_established"])

    def test_hxp4_zero_generation_remains_identity_inconclusive(self) -> None:
        result = analyze_worker_stalls(
            same_generation_zerg_completion_candidate(b"HXP4"),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        self.assert_identity_inconclusive(result, b"HXP4")

    def test_hxp2_and_hxpf_same_slot_replacement_is_identity_inconclusive(self) -> None:
        for protocol in (b"HXP2", b"HXPF"):
            with self.subTest(protocol=protocol):
                result = analyze_worker_stalls(
                    same_generation_zerg_completion_candidate(protocol),
                    replies_for(241, {0: select(1) + MORPH_DRONE}),
                    threshold=240,
                )
                self.assert_identity_inconclusive(result, protocol)

    def test_hxp3_zero_unchanged_generation_completion_is_identity_inconclusive(self) -> None:
        result = analyze_worker_stalls(
            same_generation_zerg_completion_candidate(b"HXP3"),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        self.assert_identity_inconclusive(result, b"HXP3")

    def test_interrupted_chain_is_uncertain_not_forced_native_fault(self) -> None:
        frames = []
        for number in range(241):
            if 4 <= number < 20:
                larva = unit(
                    1, 36, completed=0, build_type=41, remaining=300 - (number - 4)
                )
            else:
                larva = unit(1, 35)
            frames.append(zerg_state(number, larva=larva))
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(241, {0: select(1) + MORPH_DRONE}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["status"], "production_chain_disappeared")
        self.assertIsNone(request["classification"])
        self.assertEqual(
            request["native_rule_subtype"], "pending_or_incomplete_evidence"
        )
        self.assertEqual(result["summary"]["actionable_stalls"], 0)

    def test_immediate_slot_reuse_never_completes_the_old_production_chain(self) -> None:
        for target in (DRONE, SCV):
            with self.subTest(target=target):
                frames = []
                for number in range(32):
                    if target == DRONE:
                        if number < 4:
                            changing = unit(1, 35)
                        elif number < 11:
                            changing = unit(
                                1,
                                36,
                                completed=0,
                                build_type=DRONE,
                                remaining=300 - (number - 4),
                            )
                        else:
                            # Same slot and worker type on the very next frame,
                            # but a different native lifecycle generation.
                            changing = unit(1, DRONE, completed=1, generation=1)
                        units = [changing, unit(2, DRONE, order=85)]
                        command = select(1) + MORPH_DRONE
                        race = 0
                    else:
                        producer = unit(10, 106)
                        units = [producer]
                        if 4 <= number < 11:
                            units.append(
                                unit(
                                    11,
                                    SCV,
                                    completed=0,
                                    remaining=300 - (number - 4),
                                )
                            )
                        elif number >= 11:
                            units.append(unit(11, SCV, completed=1, generation=1))
                        command = select(10) + TRAIN_SCV
                        race = 1
                    frames.append(frame(number, units, race=race))
                result = analyze_worker_stalls(
                    b"".join(frames),
                    replies_for(32, {0: command}),
                    threshold=240,
                )
                request = result["worker_requests"][0]
                self.assertEqual(request["status"], "production_chain_disappeared")
                self.assertEqual(request["production_chain_end_frame"], 11)
                self.assertEqual(
                    request["production_start"]["produced_unit_generation"], 0
                )
                self.assertFalse(request["completion_evidence"]["completed"])
                self.assertIsNone(request["classification"])
                self.assertEqual(result["worker_request_summary"]["completed"], 0)
                self.assertEqual(
                    result["worker_request_summary"]["rejected_or_misapplied"], 0
                )
                replacements = [
                    event
                    for event in result["completed_worker_production_events"]
                    if event["unit_id"] == (1 if target == DRONE else 11)
                ]
                self.assertEqual(len(replacements), 1)
                self.assertEqual(replacements[0]["generation"], 1)
                self.assertTrue(replacements[0]["unit_existed_previous_frame"])
                self.assertFalse(
                    replacements[0]["same_identity_existed_previous_frame"]
                )

    def test_replaced_producer_cannot_donate_a_new_worker_to_the_old_request(self) -> None:
        frames = []
        for number in range(32):
            producer = unit(10, 106) if number < 4 else unit(
                10,
                106,
                generation=1,
                build_type=SCV,
                remaining=300,
                current_build_unit_id=11,
            )
            units = [producer]
            if number >= 4:
                units.append(unit(11, SCV, completed=int(number >= 11)))
            frames.append(frame(number, units, race=1))
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(32, {0: select(10) + TRAIN_SCV}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["status"], "producer_identity_changed")
        self.assertEqual(request["production_chain_end_frame"], 4)
        self.assertFalse(request["completion_evidence"]["completed"])
        self.assertIsNone(request["classification"])
        self.assertEqual(result["worker_request_summary"]["completed"], 0)
        self.assertEqual(result["worker_request_summary"]["rejected_or_misapplied"], 0)

    def test_pending_egg_does_not_hide_another_proven_rejected_request(self) -> None:
        frames = []
        for number in range(241):
            first = unit(1, 35) if number < 4 else unit(
                1,
                36,
                completed=0,
                build_type=41,
                remaining=300 - (number - 4),
            )
            frames.append(
                frame(
                    number,
                    [first, unit(2, 35), unit(3, 41)],
                    minerals=50 if number < 4 else 0,
                    supply_used=8 if number < 4 else 10,
                )
            )
        commands = select(1) + MORPH_DRONE + select(2) + MORPH_DRONE
        result = analyze_worker_stalls(
            b"".join(frames), replies_for(241, {0: commands}), threshold=240
        )
        self.assertEqual(result["worker_requests"][0]["status"], "pending_valid_production")
        self.assertEqual(
            result["worker_requests"][1]["native_rule_subtype"],
            "insufficient_resources",
        )
        self.assertEqual(
            result["stalls"][0]["classification"],
            "worker_request_rejected_or_misapplied",
        )
        self.assertEqual(result["stalls"][0]["native_rule_subtype"], "insufficient_resources")

    def test_duplicate_frame_zero_selects_live_stream_and_strict_reply_resync(self) -> None:
        scaffold_frames = b"".join(zerg_state(number, minerals=0) for number in range(6))
        live_frames = b"".join(zerg_state(number) for number in range(241))
        scaffold_replies = replies_for(6, {0: select(1) + MORPH_DRONE})
        # Both a valid wrong-frame HXRP and a command-invalid expected-frame
        # HXRP are foreign candidates.  Neither may replace the genuine reply 1.
        noisy_live_replies = make_reply(0) + b"foreign\x00bytes" + make_reply(999)
        noisy_live_replies += make_reply(1, b"\xff") + b"resync" + make_reply(1)
        noisy_live_replies += replies_for(241).split(make_reply(0), 1)[1].split(make_reply(1), 1)[1]
        result = analyze_worker_stalls(
            scaffold_frames + live_frames,
            scaffold_replies + noisy_live_replies,
            threshold=240,
        )
        self.assertEqual(result["stream_selection"]["frames"]["selected_start_record"], 6)
        self.assertEqual(result["coverage"]["state_frame_count"], 241)
        self.assertEqual(result["coverage"]["reply_frame_count"], 241)
        self.assertGreaterEqual(
            result["stream_selection"]["replies_raw"]["candidates_rejected"], 2
        )
        self.assertEqual(result["worker_request_summary"]["decoded_total_in_selected_replies"], 0)
        self.assertEqual(result["stalls"][0]["classification"], "no_worker_request")
        self.assertEqual(result["stalls"][0]["native_rule_subtype"], "no_state_transition")

    def test_scv_and_probe_train_opcodes_are_detected(self) -> None:
        for target, producer_type, race, command, kind in (
            (7, 106, 1, TRAIN_SCV, "terran_scv_train"),
            (64, 154, 2, TRAIN_PROBE, "protoss_probe_train"),
        ):
            with self.subTest(target=target):
                frames = b"".join(
                    frame(number, [unit(10, producer_type)], race=race)
                    for number in range(241)
                )
                result = analyze_worker_stalls(
                    frames,
                    replies_for(241, {0: select(10) + command}),
                    threshold=240,
                )
                request = result["worker_requests"][0]
                self.assertEqual(request["kind"], kind)
                self.assertEqual(request["decoded_command"]["opcode"], 0x1F)
                self.assertEqual(request["target_unit_type"], target)
                self.assertEqual(request["selected_hxp_unit_ids"], [10])
                self.assertEqual(request["native_rule_subtype"], "no_state_transition")

    def test_new_incomplete_trained_worker_is_pending_not_a_rejection(self) -> None:
        frames = []
        for number in range(241):
            units = [unit(10, 106)]
            if number >= 4:
                units.append(
                    unit(11, 7, completed=0, build_type=228, remaining=300 - (number - 4))
                )
            frames.append(frame(number, units, race=1))
        result = analyze_worker_stalls(
            b"".join(frames),
            replies_for(241, {0: select(10) + TRAIN_SCV}),
            threshold=240,
        )
        request = result["worker_requests"][0]
        self.assertEqual(request["status"], "pending_valid_production")
        self.assertEqual(request["production_start"]["frame"], 4)
        self.assertEqual(request["production_start"]["produced_unit_id"], 11)
        self.assertIsNone(request["classification"])
        self.assertEqual(result["summary"]["actionable_stalls"], 0)

    def test_missing_reply_coverage_does_not_prove_no_request(self) -> None:
        for raw in (b"", b"foreign" + make_reply(1) + make_reply(0, b"\xff")):
            with self.subTest(raw=raw):
                result = analyze_worker_stalls(
                    b"".join(zerg_state(number) for number in range(241)),
                    raw,
                    threshold=240,
                )
                self.assertIsNone(result["stalls"][0]["classification"])
                self.assertEqual(
                    result["stalls"][0]["native_rule_subtype"],
                    "pending_or_incomplete_evidence",
                )
                self.assertFalse(result["stalls"][0]["reply_coverage"]["complete"])

    def test_lobby_only_hxrp_is_not_accepted_as_worker_reply_evidence(self) -> None:
        frames = b"".join(zerg_state(number) for number in range(4))
        for opcode, commands in LOBBY_ONLY_COMMANDS.items():
            with self.subTest(opcode=opcode):
                result = analyze_worker_stalls(
                    frames,
                    make_reply(0) + make_reply(1, commands),
                    threshold=240,
                )
                self.assertEqual(result["coverage"]["reply_frame_count"], 1)
                self.assertEqual(result["coverage"]["reply_last_frame"], 0)
                diagnostics = result["stream_selection"]["replies_raw"]
                self.assertEqual(diagnostics["strict_replies_accepted"], 1)
                self.assertEqual(diagnostics["candidates_rejected"], 1)
                self.assertIn(
                    "lobby-only", diagnostics["rejection_details"][0]["error"]
                )
                self.assertEqual(
                    result["coverage"]["reply_missing_ranges_through_state_horizon"],
                    [[1, 3]],
                )

    def test_invalid_threshold_is_rejected(self) -> None:
        with self.assertRaises(WorkerStallError):
            analyze_worker_stalls(zerg_state(0), make_reply(0), threshold=0)

    def test_cli_writes_json_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            frames_path = root / "frames.bin"
            replies_path = root / "replies-raw.bin"
            json_path = root / "out" / "worker.json"
            report_path = root / "out" / "worker.txt"
            frames_path.write_bytes(b"".join(zerg_state(number) for number in range(241)))
            replies_path.write_bytes(replies_for(241))
            exit_code = main(
                [
                    "worker-stalls",
                    str(frames_path),
                    str(replies_path),
                    "--threshold",
                    "240",
                    "--json",
                    str(json_path),
                    "--report",
                    str(report_path),
                ]
            )
            self.assertEqual(exit_code, 0)
            result = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(result["schema"], "pluto-diff-worker-stalls/v1")
            self.assertIn("no_worker_request", report_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
