#!/usr/bin/env python3
"""Official-DLL HXP4 receiver smoke; no natural game is launched."""

from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
PAYLOAD = (ROOT / "frames-two.bin").read_bytes()
ENV = os.environ.copy()
ENV["WINEPREFIX"] = str(ROOT / "prefix")
ENV["WINEDEBUG"] = "-all"
ENV["HX_DEBUG"] = "1"


def replies(blob: bytes) -> list[tuple[int, bytes, int]]:
    result = []
    cursor = 0
    while cursor < len(blob):
        if cursor + 16 > len(blob):
            raise AssertionError((cursor, len(blob)))
        magic, frame, count = struct.unpack_from("<III", blob, cursor)
        assert magic == 0x50525848, hex(magic)
        assert cursor + 16 + count <= len(blob)
        commands = blob[cursor + 12 : cursor + 12 + count]
        win = struct.unpack_from("<I", blob, cursor + 12 + count)[0]
        result.append((frame, commands, win))
        cursor += 16 + count
    return result


def run(name: str, payload: bytes, expected_replies: int, dump: Path | None = None):
    env = ENV.copy()
    if dump is not None:
        env["HX_TEST_LATE_STATE_DUMP"] = dump.name
    completed = subprocess.run(
        ["wine", "pluto-bridge.exe"],
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=ROOT,
        env=env,
        timeout=45,
    )
    (ROOT / f"{name}.out").write_bytes(completed.stdout)
    (ROOT / f"{name}.err").write_bytes(completed.stderr)
    (ROOT / f"{name}.rc").write_text(str(completed.returncode) + "\n")
    return completed.returncode, replies(completed.stdout), completed.stderr


dump = ROOT / "receiver-evidence.txt"
if dump.exists():
    dump.unlink()
rc, parsed, err = run("receiver-valid", PAYLOAD, 3, dump)
assert rc == 0, (rc, err.decode(errors="replace"))
assert [entry[0] for entry in parsed] == [0, 1, 2]
assert all(not commands and win == 0 for _, commands, win in parsed)
evidence = dump.read_text()
expected = [
    "frame=0 publication_before=0",
    "unit slot=1555 order=75 queue=8,9,10,11,12 queue_slot=2 research_time=120 tech=5 upgrade=61",
    "unitdata slot=1555 player=0 queue_count=5 queue=10,11,12,8,9 tech=5 upgrade=61 research_time=120 upgrade_time=0",
    "unit slot=1556 order=76 queue=228,228,228,228,228 queue_slot=0 research_time=88 tech=44 upgrade=7",
    "unitdata slot=1556 player=0 queue_count=0 queue=228,228,228,228,228 tech=44 upgrade=7 research_time=0 upgrade_time=88",
    "player p=0 researched0=1 researched24=1 researched43=1 level0=2 level46=3 level60=2 techbits=010000010008 upgradebits=0100000000400010",
    "playerdata p=0 has0=1 has24=1 has43=1 researching0=1 researching24=1 researching43=1 level0=2 level46=3 level60=2 upgrading0=1 upgrading46=1 upgrading60=1",
    "player p=1 researched0=0 researched24=0 researched43=0 level0=1 level46=0 level60=0 techbits=000000000000 upgradebits=0000000000000000",
    "playerdata p=1 has0=0 has24=0 has43=0 researching0=0 researching24=0 researching43=0 level0=0 level46=0 level60=0 upgrading0=0 upgrading46=0 upgrading60=0",
    "frame=1 publication_before=1",
]
for line in expected:
    assert line in evidence, line
print("PASS HXP4 official replay: replies 0/1/2 and native/BWAPI values proven before publication")

# Fewer than 12 declared players must zero-fill the remaining BWGame rows.
small_player = struct.pack("<7I", 0, 50, 0, 8, 4, 32, 64)
small = struct.pack("<4s7I", b"HXP4", 0, 0, 1, 1, 1, 78, 119) + small_player + bytes(119) + struct.pack("<I", 0x00010203)
rc, parsed, err = run("receiver-small-player", small, 2)
assert rc == 0, (rc, err.decode(errors="replace"))
assert [entry[0] for entry in parsed] == [0, 1]
assert all(not commands and win == 0 for _, commands, win in parsed)
print("PASS one declared player: absent BWGame rows are cleared without out-of-bounds reads")

N = struct.unpack_from("<I", PAYLOAD, 8)[0]
EXTRA0 = 32 + N * 40
FIRST = EXTRA0 + 78  # first own unit, index 1
SECOND = EXTRA0 + 2 * 78
cases = []
wrong_extra = bytearray(PAYLOAD)
struct.pack_into("<I", wrong_extra, 24, 64)
cases.append(("wrong-extra-size", wrong_extra, 0))
wrong_tech = bytearray(PAYLOAD)
struct.pack_into("<I", wrong_tech, 28, 0)
cases.append(("wrong-tech-size", wrong_tech, 0))
bad_slot = bytearray(PAYLOAD)
bad_slot[FIRST + 54] = 5
cases.append(("bad-queue-slot", bad_slot, 0))
bad_type = bytearray(PAYLOAD)
struct.pack_into("<H", bad_type, FIRST + 64, 229)
cases.append(("bad-queue-type", bad_type, 0))
mismatch = bytearray(PAYLOAD)
struct.pack_into("<I", mismatch, FIRST + 28, 11)
cases.append(("queue-mismatch", mismatch, 0))
bad_tech = bytearray(PAYLOAD)
bad_tech[FIRST + 76] = 45
cases.append(("bad-research-arm", bad_tech, 0))
bad_upgrade = bytearray(PAYLOAD)
bad_upgrade[SECOND + 77] = 62
cases.append(("bad-upgrade-arm", bad_upgrade, 0))
nonresearch = bytearray(PAYLOAD)
nonresearch[EXTRA0 + 76] = 5  # unit zero is not researching
cases.append(("nonresearch-union", nonresearch, 0))
cases.append(("truncated-player-tech", PAYLOAD[: -(4 + 1)], 1))
cases.append(("truncated-final-tile", PAYLOAD[:-1], 1))
cases.append(("trailing-byte", PAYLOAD + b"\x00", 2))
legacy = ROOT / "legacy-hxp3.bin"
if legacy.exists():
    cases.append(("legacy-hxp3-trailing", legacy.read_bytes() + bytes(14), 2))
for name, payload, expected_count in cases:
    rc, parsed, err = run(f"receiver-{name}", bytes(payload), expected_count)
    assert rc != 0, (name, rc)
    assert len(parsed) == expected_count, (name, len(parsed), expected_count)
    assert all(not commands and win == 0 for _, commands, win in parsed), name
    assert b"done (failed)" in err, name
    print(f"PASS {name}: rejected rc={rc}, replies={len(parsed)} (no failure flush)")
