#!/usr/bin/env python3
"""Static field-use audit for the pinned official Pluto v2.0 DLL.

No DLL code is loaded or executed. The audit checks the complete PE byte string
for both positive and negated little-endian address encodings, then anchors the
field claims in llvm-objdump's instruction text.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

EXPECTED_SHA256 = "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc"

ABSENT_NATIVE_INPUTS = {
    "UnitOrderingCount": 0x0066FF74,
    "UnitOrderingX": 0x0066FF78,
    "UnitOrderingY": 0x006769B8,
    "MiniTileFlags": 0x005993D0,
    "MapTileArray": 0x005993C4,
    "TileSetMap": 0x006D5EC8,
    "SAIPathing": 0x006D5BFC,
}
PRESENT_NATIVE_INPUTS = {
    "UnitNodeTable": 0x0059CCA8,
    "VisibleUnitFirst": 0x00628430,
    "ActiveTileArray": 0x006D1260,
    "BWGame": 0x0057F0F0,
}
REQUIRED_INSTRUCTIONS = {
    "queue_all_five_feature": (0x63FB1BE4, "[ebx + 0x98]"),
    "queue_entry_3_feature": (0x63FB1BED, "[ebx + 0x9e]"),
    "queue_entry_4_feature": (0x63FB1BF7, "[ebx + 0xa0]"),
    "queue_all_five_predicate": (0x63FB72E0, "[esi + 0x98]"),
    "queue_ring_predicate": (0x63FB7353, "[esi + 2*edx + 0x98]"),
    "research_order": (0x63FB1CA5, "0x4b"),
    "research_tech_type": (0x63FB1CAD, "[edx + 0xc8]"),
    "research_shared_timer": (0x63FB1CDD, "[eax + 0xc6]"),
    "upgrade_order": (0x63FB3D0F, "0x4c"),
    "upgrade_type": (0x63FB3D17, "[edx + 0xc9]"),
    "upgrade_level_sc": (0x63FB3D48, "0x58d2b0"),
    "upgrade_level_bw": (0x63FB3D73, "0x58f2fe"),
    "upgrade_progress_bitset": (0x63FBA4B1, "0x58f3e0"),
    "tech_progress_bitset": (0x63FBB0C8, "0x58f230"),
    "active_tile_plane": (0x63FBB516, "[0x6d1260]"),
    "active_tile_second_plane": (0x63FBB5BC, "[0x6d1260]"),
    "official_enemy_vtable": (0x63F81481, "[edx + 0x118]"),
    "official_chat_vtable": (0x63F8161D, "[ebx + 0xe0]"),
    "official_leave_vtable": (0x63F8163E, "[eax + 0x100]"),
    "official_draw_vtable": (0x63F816C0, "[eax + 0x148]"),
}
EXPECTED_BROODWAR_XREFS = {
    0x63F81476,
    0x63F815D4,
    0x63F81630,
    0x63F81680,
    0x63F816EC,
}


def occurrences(data: bytes, value: int) -> list[int]:
    result = []
    at = 0
    needle = struct.pack("<I", value & 0xFFFFFFFF)
    while True:
        at = data.find(needle, at)
        if at < 0:
            return result
        result.append(at)
        at += 1


def instruction_map(disasm: str) -> dict[int, str]:
    result = {}
    for line in disasm.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s", line)
        if match:
            result[int(match.group(1), 16)] = line.strip()
    return result


def audit(dll_path: Path, disasm_path: Path) -> dict:
    data = dll_path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"wrong official DLL SHA-256: {digest}")
    instructions = instruction_map(disasm_path.read_text())

    absent = {}
    for name, address in ABSENT_NATIVE_INPUTS.items():
        positive = occurrences(data, address)
        negative = occurrences(data, -address)
        absent[name] = {
            "address": f"0x{address:08x}",
            "positive_file_offsets": [hex(x) for x in positive],
            "negated_file_offsets": [hex(x) for x in negative],
        }
        if positive or negative:
            raise SystemExit(f"unexpected native reference to {name}: {positive=} {negative=}")

    present = {}
    for name, address in PRESENT_NATIVE_INPUTS.items():
        positive = occurrences(data, address)
        negative = occurrences(data, -address)
        present[name] = {
            "address": f"0x{address:08x}",
            "positive_file_offsets": [hex(x) for x in positive],
            "negated_file_offsets": [hex(x) for x in negative],
        }
        if not positive and not negative:
            raise SystemExit(f"expected native reference to {name} is missing")

    required = {}
    for name, (address, operand) in REQUIRED_INSTRUCTIONS.items():
        line = instructions.get(address)
        if line is None or operand not in line:
            raise SystemExit(
                f"missing instruction evidence {name} at {address:#x}: wanted {operand!r}, got {line!r}"
            )
        required[name] = {"address": f"0x{address:08x}", "instruction": line}

    broodwar_xrefs = sorted(
        address for address, line in instructions.items() if "0x63ff2034" in line
    )
    if set(broodwar_xrefs) != EXPECTED_BROODWAR_XREFS:
        raise SystemExit(
            f"unexpected Broodwar global xref set: {[hex(x) for x in broodwar_xrefs]}"
        )

    direct_native_branches = []
    for address, line in instructions.items():
        match = re.search(r"\b(?:call|jmp)\s+0x([0-9a-f]+)", line)
        if match and 0x400000 <= int(match.group(1), 16) < 0x800000:
            direct_native_branches.append(line)
    if direct_native_branches:
        raise SystemExit(f"unexpected direct branch into native SC range: {direct_native_branches}")

    return {
        "official_dll": str(dll_path),
        "official_dll_sha256": digest,
        "disassembly": str(disasm_path),
        "method": (
            "Whole-file little-endian scans for positive and negated native addresses; "
            "instruction-anchored queue/research/map evidence; exhaustive Broodwar-global "
            "xref and direct-native-branch checks. No code loaded or launched."
        ),
        "absent_native_inputs": absent,
        "present_native_inputs": present,
        "required_instruction_evidence": required,
        "broodwar_global_xrefs": [
            {"address": f"0x{x:08x}", "instruction": instructions[x]} for x in broodwar_xrefs
        ],
        "direct_native_branches": direct_native_branches,
        "conclusions": {
            "dynamic_unit_search": (
                "Not read by this official DLL. UnitOrderingCount/X/Y have no positive or "
                "negated references, and the only BWAPI Game calls are enemy race, chat, "
                "leaveGame, and debug drawing; no getUnitsInRectangle/getClosestUnit/getBestUnit "
                "path exists. Build no unit-search payload."
            ),
            "exact_minitile_walkability": (
                "Not read by this official DLL. Its terrain plane is built from four-byte "
                "ActiveTileArray entries. MiniTileFlags/MapTileArray/TileSetMap/SAIPathing "
                "have no positive or negated references. Keep HXP tile flags; do not add VF4 "
                "or a minitile plane for this policy."
            ),
            "five_entry_build_queues": (
                "Required. Official reads CUnit+0x98..+0xA0 and +0xA4 in feature and "
                "action-mask paths. HXP4 carries all five raw u16 entries."
            ),
            "research_upgrade_progress": (
                "Required. Official reads CUnit+0xC6/+0xC8/+0xC9 and owner "
                "currentUpgradeLevel/progress tables. HXP4 carries these fields and the "
                "native SC/BW player-tech records."
            ),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--disasm", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    report = audit(args.dll, args.disasm)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.write_text(encoded)
    print("PASS official static field audit")
    for key, value in report["conclusions"].items():
        print(f"  {key}: {value}")


if __name__ == "__main__":
    main()
