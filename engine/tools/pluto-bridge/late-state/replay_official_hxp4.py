#!/usr/bin/env python3
"""Run and strictly validate a 54-frame HXP4 official-DLL replay."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
REPO = Path(os.environ.get("HYPERION_REPO", "/home/saintbellarmine/Github/hyperion-pluto-fidelity"))
sys.path.insert(0, str(REPO / "engine/tools/pluto-diff"))
from pluto_diff.hxp import parse_frame_stream, parse_reply_stream  # noqa: E402

payload = (ROOT / "official-long.bin").read_bytes()
frames = parse_frame_stream(payload)
if len(frames) != 54 or [frame.frame for frame in frames] != list(range(54)):
    raise RuntimeError("expected exactly HXP4 frames 0..53")
if any(frame.protocol != "HXP4" for frame in frames):
    raise RuntimeError("mixed/non-HXP4 long replay")

env = os.environ.copy()
env["WINEPREFIX"] = str(ROOT / "prefix")
env["WINEDEBUG"] = "-all"
env["HX_DEBUG"] = "1"
process = subprocess.Popen(
    ["wine", "pluto-bridge.exe"],
    cwd=ROOT,
    env=env,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    start_new_session=True,
)
try:
    stdout, stderr = process.communicate(payload, timeout=90)
    timed_out = False
except subprocess.TimeoutExpired:
    timed_out = True
    os.killpg(process.pid, signal.SIGKILL)
    stdout, stderr = process.communicate()
(ROOT / "hxp4-long.out").write_bytes(stdout)
(ROOT / "hxp4-long.err").write_bytes(stderr)
(ROOT / "hxp4-long.rc").write_text(("timeout\n" if timed_out else str(process.returncode) + "\n"))
if (ROOT / "pluto.log").exists():
    (ROOT / "hxp4-long.pluto.log").write_bytes((ROOT / "pluto.log").read_bytes())
replies = parse_reply_stream(stdout)
bridge_hash = hashlib.sha256((ROOT / "pluto-bridge.exe").read_bytes()).hexdigest()
summary = {
    "bridge_sha256": bridge_hash,
    "returncode": process.returncode,
    "timeout": timed_out,
    "input_sha256": hashlib.sha256(payload).hexdigest(),
    "output_sha256": hashlib.sha256(stdout).hexdigest(),
    "input_frames": len(frames),
    "input_protocols": sorted({frame.protocol for frame in frames}),
    "reply_frames": [reply.frame for reply in replies],
    "nonempty_replies": sum(bool(reply.command_bytes) for reply in replies),
    "command_bytes": sum(len(reply.command_bytes) for reply in replies),
    "strict_commands": sum(len(reply.commands) for reply in replies),
    "nonzero_win": sum(reply.win != 0 for reply in replies),
}
(ROOT / "hxp4-long.summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
print(json.dumps(summary, sort_keys=True))
if timed_out or process.returncode != 0:
    raise SystemExit(process.returncode or 124)
if summary["reply_frames"] != list(range(55)):
    raise RuntimeError("expected replies 0..53 and final flush 54")
if summary["nonempty_replies"] == 0 or summary["command_bytes"] == 0:
    raise RuntimeError("HXP4 replay produced no command evidence")
if summary["nonzero_win"]:
    raise RuntimeError("unexpected nonzero in-game win")
