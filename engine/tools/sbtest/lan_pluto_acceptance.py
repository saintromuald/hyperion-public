#!/usr/bin/env python3
"""Real two-human + one-Pluto standalone LAN acceptance.

The host uses the native Start Server / New Match / seat-level UI. A second
Hyperion joins the resulting room and readies itself. Only the host receives a
fresh test-owned inference runtime; the guest and replay never receive one.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time

import zero_acceptance as zero

ENGINE = Path(__file__).resolve().parents[2]
DEFAULT_GAME = ENGINE / "testrun/share/variants/zero-smoke/Game"
DEFAULT_MAP = DEFAULT_GAME / "Maps/BroodWar/(4)Boxed In.scm"
DEFAULT_RUNTIME = Path.home() / "Games/pluto-runtime"
CONTROL_KEY = "0123456789abcdef0123456789abcdef"


def lan_steps(seat):
    steps = {}
    for line in zero.read(seat / "hyperion_lan_steps.log").splitlines():
        match = re.match(r"step (\d+):(.*)$", line)
        if match:
            steps[int(match.group(1))] = match.group(2)
    return steps


def wait_for_steps(children, seats, target, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        progress = [max(lan_steps(seat), default=0) for seat in seats]
        if all(step >= target for step in progress):
            return
        for name, process in children.items():
            code = process.poll()
            if code is not None:
                raise RuntimeError(f"{name} exited early with code {code}")
        time.sleep(0.1)
    progress = [max(lan_steps(seat), default=0) for seat in seats]
    raise RuntimeError(f"timed out waiting for LAN step {target}; seats reached {progress}")


def wait_for_text(path, text, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text in zero.read(path):
            return
        time.sleep(0.1)
    raise RuntimeError(f"timed out waiting for {text!r} in {path}")


def room_password(port, key, timeout):
    # A second loopback console is how a test reads the generated join password.
    # It changes no match state: creation, seating and AI selection stay in the UI.
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                hello = json.dumps({
                    "t": "hello", "name": "Acceptance console", "machine": "test",
                    "os": "linux", "arch": "x86_64", "kind": "linux", "route": "install",
                    "version": "acceptance", "sim": 23, "fp": "", "mod": "",
                    "password": "", "key": key,
                }).encode()
                sock.sendall(struct.pack("<IB", len(hello) + 1, 0x20) + hello)
                while time.monotonic() < deadline:
                    header = sock.recv(4)
                    if len(header) != 4:
                        break
                    size = struct.unpack("<I", header)[0]
                    body = b""
                    while len(body) < size:
                        chunk = sock.recv(size - len(body))
                        if not chunk:
                            break
                        body += chunk
                    if len(body) != size or body[0] != 0x20:
                        break
                    message = json.loads(body[1:])
                    if message.get("passwordText"):
                        return message["passwordText"]
        except (ConnectionError, OSError, ValueError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    raise RuntimeError("could not read the host-created room's join password")


def cleanup_processes(work):
    # ServerStart and the room's replay copy are descendants, not direct Popen
    # children. Match only native processes whose command line names this test's
    # unique data directory; no other runtime or acceptance can be selected.
    marked = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            raw = (entry / "cmdline").read_bytes()
            text = raw.replace(b"\0", b" ")
            if str(work).encode() in text and b"Hyperion.x86_64" in text:
                marked.append(int(entry.name))
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    for pid in marked:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    time.sleep(0.5)
    for pid in marked:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            continue
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--map", dest="map_file", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--pluto-runtime", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=120)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    for path in (args.engine, args.game, args.map_file, args.pluto_runtime):
        if not path.exists():
            parser.error(f"missing {path}")
    if args.steps < 1 or args.timeout < 1:
        parser.error("--steps and --timeout must be positive")
    if args.out.exists():
        parser.error(f"acceptance directory already exists: {args.out}")

    work = args.out
    host = work / "seat0"
    guest = work / "seat1"
    map_dir = host / "maps/BroodWar"
    map_dir.mkdir(parents=True)
    guest.mkdir(parents=True)
    (host / "lanroom.json").write_text('{"copies":"Off"}')
    shutil.copy2(args.map_file, map_dir / args.map_file.name)
    shutil.copytree(args.pluto_runtime, work / "pluto-runtime", ignore=shutil.ignore_patterns(
        "*.log", "*.fifo", "frames.bin", "replies-raw.bin", "runtime-source.json"))

    game_port, web_port = zero.free_port(), zero.free_port()
    host_control, guest_control = zero.free_port(), zero.free_port()
    base = {
        **os.environ,
        "HYPERION_WINDOW": "background",
        "HYPERION_TEST_MOUSE": "1",
        "SDL_AUDIO_DRIVER": "dummy",
        "HYPERION_FRONT": "1",
        "HYPERION_CONTROL_KEY": CONTROL_KEY,
        "HYPERION_LAN_PORT": str(game_port),
        "HYPERION_LAN_WEB_PORT": str(web_port),
        "HYPERION_LAN_TRACE": "1",
        "HYPERION_LAN_KEY": "lan-pluto-acceptance-key",
        "HYPERION_GAME": str(args.game),
    }
    host_env = {
        **base,
        "HYPERION_FRONT_OPEN": "lanhost-match",
        "HYPERION_FRONT_MAP": f"BroodWar\\{args.map_file.name}",
        "HYPERION_PLUTO_FOLDER": str(work / "pluto-runtime"),
        "WINEPREFIX": str(work / "wine-prefix"),
    }
    guest_env = {
        **base,
        "HYPERION_PLUTO_FOLDER": str(work / "guest-no-runtime"),
        "WINEPREFIX": str(work / "guest-no-runtime-wine-prefix"),
    }
    children = {}
    try:
        children["host"] = subprocess.Popen([
            str(args.engine), "--data", host, f"--control={host_control}",
            "--install-folder", args.game,
        ], env=host_env, stdout=(work / "host.log").open("w"), stderr=subprocess.STDOUT)
        wait_for_text(host / "hyperion_front.log", "back on the set-up", 45)

        # Native seat controls at 640x480: row 3 -> Computer -> level -> Pluto.
        zero.ws_call(host_control, {"id": 1, "cmd": "click", "x": 82, "y": 161}, timeout=5)
        time.sleep(0.5)
        zero.ws_call(host_control, {"id": 2, "cmd": "click", "x": 52, "y": 180}, timeout=5)
        time.sleep(0.75)
        zero.ws_call(host_control, {"id": 3, "cmd": "click", "x": 300, "y": 161}, timeout=5)
        time.sleep(0.5)
        zero.ws_call(host_control, {"id": 4, "cmd": "click", "x": 280, "y": 181}, timeout=5)
        wait_for_text(host / "hyperion_front.log", "a computer level", 10)

        password = room_password(game_port, base["HYPERION_LAN_KEY"], 20)
        children["guest"] = subprocess.Popen([
            str(args.engine), "--data", guest, "--join", f"127.0.0.1:{game_port}",
            "--room-password", password, "--lan-name", "Bravo", "--lan-ready", f"--control={guest_control}",
            "--install-folder", args.game,
        ], env=guest_env, stdout=(work / "guest.log").open("w"), stderr=subprocess.STDOUT)
        wait_for_text(host / "hyperion_lan.log", "Bravo joined", 30)
        time.sleep(3)  # the guest's real periodic Ready message reaches the room
        answer = zero.ws_call(host_control, {"id": 5, "cmd": "snapshot", "name": "lobby.png"}, timeout=10)
        if not answer.get("ok"):
            raise RuntimeError(f"could not capture the configured LAN lobby: {answer}")
        zero.ws_call(host_control, {"id": 6, "cmd": "press", "key": "g"}, timeout=5)

        seats = [host, guest]
        wait_for_steps(children, seats, 1, args.timeout)
        for seat in seats:
            wait_for_text(seat / "hyperion_ai.log", "a game:", args.timeout)
        wait_for_steps(children, seats, args.steps, args.timeout)
        for seat in seats:
            wait_for_text(seat / "hyperion_pluto.log", "applied AI envelope", args.timeout)
        traces = [lan_steps(seat) for seat in seats]
        for step in range(1, args.steps + 1):
            expected = traces[0].get(step)
            if expected is None or traces[1].get(step) != expected:
                raise RuntimeError(
                    f"LAN seats diverged at step {step}: {expected!r} != {traces[1].get(step)!r}")

        zero.quit_seat([host_control, guest_control], 1)
        zero.quit_seat([host_control, guest_control], 0)
        zero.wait_for_game_end(seats, 2, args.timeout)
        live_counts, native_slot = zero.pluto_live_command_counts(seats, 0)
        recorded_counts = zero.validate_pluto_replays(seats, live_counts, native_slot, "PLuto")
        zero.validate_pluto_replay_playback(args.engine, work, seats, args)
        print(f"PASS: native UI launched a standalone LAN match with owner-0 Pluto on native slot {native_slot}; "
              f"{recorded_counts[0]} AI commands matched on both peers")
        print(f"PASS: both human clients read {args.steps} identical LAN steps and replayed without inference")
        print(f"acceptance directory: {work}")
        return 0
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        print(f"acceptance directory: {work}", file=sys.stderr)
        for name in ("host", "guest"):
            print(f"---- {name} ----\n" + "\n".join(
                zero.read(work / f"{name}.log").splitlines()[-20:]), file=sys.stderr)
        return 1
    finally:
        for process in reversed(list(children.values())):
            zero.stop(process)
        cleanup_processes(work)


if __name__ == "__main__":
    sys.exit(main())
