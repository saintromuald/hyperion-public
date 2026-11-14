#!/usr/bin/env python3
"""Two-client Hyperion Zero acceptance test, using only local development services.

The generated launch files contain session credentials. They stay in the local
acceptance directory and are never printed or uploaded.
"""
import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ENGINE = Path(__file__).resolve().parents[2]
HX = ENGINE / "src117/build/linux-x64/Hyperion.x86_64"
R2 = ENGINE / "tools/rallypoint2"
TARGET = ENGINE / "src117/build/linux-x64/rallypoint"
DEFAULT_GAME = ENGINE / "testrun/share/variants/zero-smoke/Game"
DEFAULT_MAPS = {
    1: DEFAULT_GAME / "Maps/(2)Volcanis.scm",
    2: DEFAULT_GAME / "Maps/ladder/Season 8/(4)Fighting Spirit.scx",
    3: DEFAULT_GAME / "Maps/ladder/(6)Acropolis.scm",
}
RESULT_CODES = [f"zero-acceptance-result-{1001 + i}" for i in range(6)]
RACES = ["z", "t", "p", "z", "t", "p"]
NAMES = ["Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot"]

sys.path.insert(0, str(ENGINE / "tools"))
from hxlive import ws_call  # noqa: E402


class NoticeHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 1024 * 1024:
                raise ValueError("invalid notice size")
            notice = json.loads(self.rfile.read(length))
            with self.server.notices_lock:
                self.server.notices.append(notice)
            self.send_response(204)
            self.end_headers()
        except (ValueError, json.JSONDecodeError):
            self.send_response(400)
            self.end_headers()

    def log_message(self, *_args):
        pass


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def read(path):
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def game_steps(seat):
    steps = {}
    for line in read(seat / "hyperion_sb_trace.log").splitlines():
        match = re.match(r"step (\d+):(.*)$", line)
        if match:
            steps[int(match.group(1))] = match.group(2)
    return steps


def wait_for_steps(processes, seats, target, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        progress = [max(game_steps(seat), default=0) for seat in seats]
        if all(step >= target for step in progress):
            return
        for name, process in processes.items():
            code = process.poll()
            if code is not None:
                raise RuntimeError(f"{name} exited early with code {code}")
        time.sleep(0.1)
    progress = [max(game_steps(seat), default=0) for seat in seats]
    raise RuntimeError(f"timed out waiting for step {target}; seats reached {progress}")


def quit_seat(ports, seat):
    # The live channel invokes End Mission's actual confirmation action, with
    # no synthetic result record and no dependence on window-manager focus.
    answer = ws_call(ports[seat], {"id": 1, "cmd": "quit"}, timeout=10)
    if not answer.get("ok"):
        raise RuntimeError(f"seat {seat} could not quit: {answer.get('error', 'unknown error')}")


def surrender_both(ports, seats, timeout):
    for seat in reversed(range(len(ports))):
        quit_seat(ports, seat)
        if len(ports) == 2 and seat == 1:
            deadline = time.monotonic() + timeout
            while "game menu: victory" not in read(seats[0] / "hyperion_front.log"):
                if time.monotonic() >= deadline:
                    raise RuntimeError("the remaining seat did not record the surrender victory")
                time.sleep(0.1)
            answer = ws_call(ports[0], {"id": 1, "cmd": "end", "victory": True}, timeout=10)
            if not answer.get("ok"):
                raise RuntimeError(f"the winner could not confirm the victory dialog: {answer}")
            break


def wait_for_notices(server, timeout, player_count, drop=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with server.notices_lock:
            results = [n for n in server.notices if n.get("event") == "result"]
            departures = [n for n in server.notices if n.get("event") == "departure"]
            slots = {n.get("slot") for n in results}
            departed = {n.get("slot") for n in departures}
            expected_results = {0} if drop else set(range(player_count))
            if slots == expected_results and departed == set(range(player_count)):
                return
        time.sleep(0.1)
    with server.notices_lock:
        kinds = [(n.get("event"), n.get("slot"), n.get("kind")) for n in server.notices]
    raise RuntimeError(f"timed out waiting for both results and clean departures; notices: {kinds}")


def wait_for_drop(server, slot, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with server.notices_lock:
            if any(n.get("event") == "departure" and n.get("slot") == slot and
                   n.get("kind") == "dropped" for n in server.notices):
                return
        time.sleep(0.1)
    raise RuntimeError(f"timed out waiting for slot {slot}'s dropped departure")


def validate_result_notices(server, game_id, team_size=1, random_race=False, decisive=False, drop=False):
    with server.notices_lock:
        notices = list(server.notices)
    player_count = team_size * 2
    reports = {}
    assigned_races = {}
    assigned_verdicts = {}
    for index, notice in enumerate(notices):
        if notice.get("event") != "result":
            continue
        slot = notice["slot"]
        if slot not in range(player_count) or slot in reports:
            raise RuntimeError(f"duplicate or invalid result slot {slot}")
        if notice.get("externalId") != game_id or notice.get("externalRef") != str(1001 + slot):
            raise RuntimeError(f"result slot {slot} has the wrong game or user correlation")
        raw = base64.b64decode(notice["payload"], validate=True)
        if not raw or len(raw) > 4096:
            raise RuntimeError(f"result slot {slot} has an invalid payload size")
        report = json.loads(raw)
        if set(report) != {"version", "userId", "resultCode", "time", "players", "netPlayers",
                           "localPlayerLoseType"}:
            raise RuntimeError(f"result slot {slot} has the wrong top-level fields")
        if report["version"] != 2 or report["userId"] != 1001 + slot:
            raise RuntimeError(f"result slot {slot} has the wrong version or reporter")
        if report["resultCode"] != RESULT_CODES[slot] or report["time"] <= 0:
            raise RuntimeError(f"result slot {slot} has invalid credentials or time")
        if len(report["players"]) != player_count:
            raise RuntimeError(f"result slot {slot} does not contain every player row")
        expected_users = [1001 + i for i in range(player_count)]
        for player, expected_user, expected_race in zip(report["players"], expected_users, RACES):
            bw_id = expected_user - 1001
            if player["userId"] != expected_user or player["bwPlayerId"] != bw_id:
                raise RuntimeError(f"result slot {slot} has a mismatched player identity")
            if player["stormId"] != bw_id:
                raise RuntimeError(f"result slot {slot} has a mismatched slot or assigned race: {player}")
            if random_race:
                if player["race"] not in ("z", "t", "p"):
                    raise RuntimeError(f"result slot {slot} retained an unresolved random race: {player}")
                if assigned_races.setdefault(bw_id, player["race"]) != player["race"]:
                    raise RuntimeError(f"result slot {slot} disagrees on the resolved race: {player}")
            elif player["race"] != expected_race:
                raise RuntimeError(f"result slot {slot} has a mismatched slot or assigned race: {player}")
            if player["victoryState"] not in (0, 1, 2, 3):
                raise RuntimeError(f"result slot {slot} has an invalid victory state")
            if decisive:
                expected_verdict = 2 if bw_id == player_count - 1 else 3
                if player["victoryState"] != expected_verdict:
                    raise RuntimeError(f"result slot {slot} has the wrong verdict: {player}")
                if assigned_verdicts.setdefault(bw_id, player["victoryState"]) != player["victoryState"]:
                    raise RuntimeError(f"result slot {slot} disagrees on the verdict: {player}")
            if len(player["alliances"]) != 8 or any(value not in (0, 1, 2) for value in player["alliances"]):
                raise RuntimeError(f"result slot {slot} has invalid alliance evidence")
        if len(report["netPlayers"]) != 8:
            raise RuntimeError(f"result slot {slot} does not contain all network rows")
        for storm_id, row in enumerate(report["netPlayers"]):
            if row != {"stormId": storm_id, "wasDropped": row["wasDropped"], "hasQuit": row["hasQuit"]}:
                raise RuntimeError(f"result slot {slot} has a malformed network row")
            if type(row["wasDropped"]) is not bool or type(row["hasQuit"]) is not bool:
                raise RuntimeError(f"result slot {slot} has non-boolean network evidence")
        if report["localPlayerLoseType"] not in (None, "targetedDisconnect", "massDisconnect"):
            raise RuntimeError(f"result slot {slot} has an invalid disconnect type")
        reports[slot] = (index, report)

    expected_slots = {0} if drop else set(range(player_count))
    if set(reports) != expected_slots:
        raise RuntimeError(f"results are missing for slots {sorted(expected_slots - set(reports))}")
    for slot, (result_index, report) in reports.items():
        departures = [(index, notice) for index, notice in enumerate(notices)
                      if notice.get("event") == "departure" and notice.get("slot") == slot]
        if len(departures) != 1:
            raise RuntimeError(f"result slot {slot} has {len(departures)} departures")
        departure_index, departure = departures[0]
        if departure_index <= result_index:
            raise RuntimeError(f"slot {slot} departed before its result arrived")
        if departure.get("kind") != "left" or departure.get("externalId") != game_id:
            raise RuntimeError(f"slot {slot} did not make a clean correlated departure")
        embedded = departure.get("result")
        if not embedded or json.loads(base64.b64decode(embedded["payload"], validate=True)) != report:
            raise RuntimeError(f"slot {slot}'s departure did not retain its exact result")


def validate_secret_free_logs(seats):
    for seat in seats:
        for path in seat.rglob("*"):
            if not path.is_file() or path.suffix not in (".log", ".txt", ".json"):
                continue
            text = read(path)
            for code in RESULT_CODES:
                if code in text:
                    raise RuntimeError(f"a result credential leaked into {path}")


def validate_drop_notice(server, game_id):
    # The survivor's ordinary result and clean departure are validated first.
    validate_result_notices(server, game_id, drop=True)
    with server.notices_lock:
        notices = list(server.notices)
    if any(n.get("event") == "result" and n.get("slot") == 1 for n in notices):
        raise RuntimeError("the hard-killed seat produced an impossible result")
    departures = [n for n in notices if n.get("event") == "departure" and n.get("slot") == 1]
    if len(departures) != 1:
        raise RuntimeError("the hard-killed seat did not produce exactly one departure")
    departure = departures[0]
    if departure.get("kind") != "dropped" or departure.get("externalId") != game_id:
        raise RuntimeError("the hard-killed seat was not reported as dropped")
    if departure.get("externalRef") != "1002" or "result" in departure:
        raise RuntimeError("the dropped departure has wrong identity or unexpected result evidence")


def manifest(setup, seat, game_id, seed, map_path, compatibility, rule, team_size, random_race=False, classic=False):
    user_ids = [1001 + i for i in range(team_size * 2)]
    selected = dict(compatibility)
    selected["rules"] = "classic" if classic else "hyperion"
    setup["hyperion"] = {
        "protocol": 1,
        "gameId": game_id,
        "name": "Hyperion Zero acceptance",
        "gameType": "oneVOne" if team_size == 1 else "topVBottom",
        "gameSubType": 0 if team_size == 1 else team_size,
        "seed": seed,
        "speed": 6,
        "disableAllianceChanges": True,
        "localUserId": user_ids[seat],
        "slots": [
            {
                "rpSlot": i,
                "stormId": i,
                "bwPlayerId": i,
                "userId": user_ids[i],
                "teamId": i // team_size,
                "name": NAMES[i],
                "race": "r" if random_race else RACES[i],
                "host": i == 0,
            }
            for i in range(team_size * 2)
        ],
        "map": {
            "format": map_path.suffix.lower().lstrip("."),
            "hash": hashlib.sha256(map_path.suffix.lower().lstrip(".").encode("ascii") + map_path.read_bytes()).hexdigest(),
            "path": str(map_path),
        },
        "compatibility": selected,
    }
    if rule == 2:
        setup["hyperion"]["mutators"] = {"antiMeta": 1}
    return setup


def validate_rule_logs(seats, rule):
    logs = [read(seat / "hyperion_mutators.log") for seat in seats]
    if rule == 0:
        if any("anti-meta:" in log for log in logs):
            raise RuntimeError("Classic rules enabled an anti-meta start")
        return None
    if rule == 1:
        if any("MutatorsForGame: on 0" not in log or "anti-meta:" in log for log in logs):
            raise RuntimeError("Rule 1 did not remain classic on both seats")
        return None
    rolls = []
    for log in logs:
        match = re.search(r"anti-meta: seed \S+ rolls (\d+) workers, (\d+) minerals, (\d+) gas", log)
        if not match or "MutatorsForGame: on 1" not in log:
            raise RuntimeError("Rule 2 did not enable Hyperion and the anti-meta start on both seats")
        rolls.append(tuple(int(value) for value in match.groups()))
    if any(roll != rolls[0] for roll in rolls):
        raise RuntimeError(f"the anti-meta rolls differ between seats: {rolls}")
    workers, minerals, gas = rolls[0]
    if workers not in range(4, 9) or minerals not in range(100, 301, 5) or gas not in range(0, 126, 5):
        raise RuntimeError(f"the anti-meta roll is outside its contract: {rolls[0]}")
    return rolls[0]


def wait_for_game_end(seats, expected, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if sum("lobby: the game ended:" in read(seat / "hyperion_lan.log") for seat in seats) >= expected:
            return
        time.sleep(0.1)
    raise RuntimeError("timed out waiting for native replay teardown")


def validate_replays(seats, rule, expected_roll):
    paths = [seat / "maps/replays/LastReplay.rep" for seat in seats]
    missing = [str(path) for path in paths if not path.exists()]
    if missing:
        raise RuntimeError(f"expected every seat to write a local replay; missing: {missing}")
    replay_mutators = []
    for path in paths:
        data = path.read_bytes()
        native_magic = data.startswith(b"hxRS") or (data.startswith(b"\x7f\xf9") and data[12:16] == b"hxRS")
        classic_magic = data.startswith(b"reRS") or (len(data) >= 16 and data[12:16] == b"reRS")
        if rule == 0 and not classic_magic:
            raise RuntimeError(f"{path} is not a classic reRS replay")
        if rule != 0 and not native_magic:
            raise RuntimeError(f"{path} does not have Hyperion hxRS replay magic")
        if rule == 0:
            if b"MUTS" in data or b"HX_OPT_SELECTION" in data or b"HX_OPT_MUTATORS" in data:
                raise RuntimeError("the Classic replay contains Hyperion QoL or mutator evidence")
            continue
        marker = data.rfind(b"HYPERION")
        if rule == 1:
            if marker >= 0:
                raise RuntimeError("Rule 1 unexpectedly produced a Hyperion-rules replay")
            continue
        if marker < 0:
            raise RuntimeError("Rule 2 did not produce a Hyperion replay")
        sections = {}
        offset = marker + 12
        while offset + 20 <= len(data):
            tag, _version, _flags, _count, _record_size, size = struct.unpack_from("<IHHIII", data, offset)
            name = struct.pack("<I", tag).decode("latin1")
            sections[name] = data[offset + 20:offset + 20 + size]
            offset += 20 + size
        if offset != len(data):
            raise RuntimeError("the Hyperion replay has a truncated section")
        rule_record = sections.get("RULE", b"")
        mutators = sections.get("MUTS", b"")
        if rule != 0 and not rule_record.startswith(b"hyperion\0"):
            raise RuntimeError(f"the Rule {rule} replay has the wrong RULE record")
        if len(rule_record) < 76:
            raise RuntimeError(f"the Rule {rule} replay has the wrong RULE record")
        options = struct.unpack_from("<I", rule_record, 72)[0]
        if rule == 0:
            if options & 0x40 or options & 0x100 or mutators:
                raise RuntimeError("the Classic replay enabled Hyperion selection QoL or mutators")
            continue
        if options & 0x40 == 0:  # HX_OPT_SELECTION
            raise RuntimeError(f"the Rule {rule} replay does not contain Hyperion selection QoL")
        if rule == 1:
            if options & 0x100 or mutators:  # HX_OPT_MUTATORS and its MUTS section
                raise RuntimeError("the Rule 1 replay unexpectedly contains mutators")
            continue
        if options & 0x100 == 0 or len(mutators) != 104:
            raise RuntimeError("the Rule 2 replay does not contain its exact MUTS record")
        values = struct.unpack("<16i", mutators)
        replay_mutators.append(values)
        workers, minerals, gas = values[5], values[2], values[3]
        if (workers, minerals, gas) != expected_roll:
            raise RuntimeError("the replay rerolled or changed the anti-meta start")
    if rule == 2 and (not replay_mutators or any(values != replay_mutators[0] for values in replay_mutators)):
        raise RuntimeError("the seats' Rule 2 replay mutators differ")


def validate_lifecycle_events(seats):
    for seat in range(len(seats)):
        events = []
        for line in read(seats[seat].parent / f"c{seat}.log").splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get("type") in ("start", "finished"):
                events.append(event["type"])
        if "start" not in events or "finished" not in events:
            raise RuntimeError(f"seat {seat} did not emit both native start and finished events: {events}")


def validate_classic_selection(hx, work, seats, args):
    port = free_port()
    data = work / "selection-playback"
    data.mkdir()
    process = subprocess.Popen([
        str(hx), "--data", data, "--map", args.map_file, "--rules", "classic",
        f"--control={port}", "--install-folder", args.game,
    ], env={**os.environ, "HYPERION_WINDOW": "hidden", "SDL_AUDIO_DRIVER": "dummy"},
       stdout=(work / "selection-playback.log").open("w"), stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + args.timeout
        selected_player = 0
        while True:
            if process.poll() is not None:
                raise RuntimeError("Classic selection game exited before the selection check")
            for player in range(2):
                try:
                    answer = ws_call(port, {
                        "id": 1, "cmd": "select", "x": 1536, "y": 1536,
                        "radius": 4096, "player": player,
                    }, timeout=2)
                    if answer.get("ok") and answer.get("selected", 0) > 0:
                        selected_player = player
                        break
                except (ConnectionError, OSError):
                    pass
            else:
                if time.monotonic() >= deadline:
                    raise RuntimeError("timed out opening the Classic selection game")
                time.sleep(0.1)
                continue
            break
        time.sleep(0.2)
        answer = ws_call(port, {
            "id": 2, "cmd": "select", "x": 1536, "y": 1536,
            "radius": 4096, "player": selected_player,
        }, timeout=10)
        if not answer.get("ok") or answer.get("selected", 0) < 1 or process.poll() is not None:
            raise RuntimeError(f"Classic selection-pool reuse failed: {answer}")
    finally:
        stop(process)


def run_negative_case(hx, work, setup_path, args):
    setup = json.loads(setup_path.read_text())
    kind = args.negative
    if kind == "rollback":
        setup["rollback"] = True
    elif kind == "roster":
        setup["hyperion"]["slots"][1]["userId"] += 1000
    elif kind == "profile":
        setup["hyperion"]["compatibility"]["simRevision"] += 1
    elif kind == "map-hash":
        setup["hyperion"]["map"]["hash"] = "0" * 64
    elif kind == "map-bytes":
        with open(setup["hyperion"]["map"]["path"], "ab") as altered:
            altered.write(b"altered after authentication")
    expected_reason = {
        "rollback": "rollback is not supported",
        "roster": "the authenticated roster does not match the manifest slots",
        "profile": "simulation revision does not match this Hyperion",
        "map-hash": "map hash does not match the downloaded map",
        "map-bytes": "map hash does not match the downloaded map",
    }[kind]
    negative_seat = work / "negative-seat"
    negative_seat.mkdir(exist_ok=True)
    negative_setup = work / "negative.json"
    negative_setup.write_text(json.dumps(setup))
    negative_setup.chmod(0o600)
    process = subprocess.Popen([
        str(hx), "--data", negative_seat, "--sb-setup", negative_setup,
        "--sb-role", "create", "--sb-map", setup["hyperion"]["map"]["path"],
        "--sb-name", "Negative", "--sb-speed", "6", "--sb-type", "oneVOne",
        "--install-folder", args.game,
    ], env={**os.environ, "HYPERION_WINDOW": "hidden", "SDL_AUDIO_DRIVER": "dummy"},
       stdout=(work / "negative.log").open("w"), stderr=subprocess.STDOUT)
    rejected = False
    try:
        deadline = time.monotonic() + max(20, args.timeout)
        while time.monotonic() < deadline:
            evidence = read(negative_seat / "hyperion_sb.log") + read(work / "negative.log")
            if "the launch manifest was rejected" in evidence:
                if expected_reason not in evidence:
                    raise RuntimeError(f"the {kind} manifest was rejected for an unrelated reason: {evidence}")
                rejected = True
                break
            if process.poll() is not None:
                break
            time.sleep(0.1)
    finally:
        stop(process)
    if not rejected:
        raise RuntimeError(f"the {kind} manifest was not rejected by the native parser")
    if game_steps(negative_seat):
        raise RuntimeError(f"the {kind} manifest reached gameplay instead of failing closed")
    print(f"PASS: the native parser rejected the {kind} manifest before gameplay")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--engine", type=Path,
                        help="exact Hyperion executable to test (defaults to the selected target's build output)")
    parser.add_argument("--map", dest="map_file", type=Path)
    parser.add_argument("--out", type=Path, help="keep the acceptance directory here")
    parser.add_argument("--target", choices=("linux-x64", "windows-x64"),
                        default="windows-x64" if os.name == "nt" else "linux-x64",
                        help="native engine and Rally Point binaries to test")
    parser.add_argument("--rallypoint-target", type=Path,
                        help="prebuilt Rally Point target directory to use instead of the default build output")
    parser.add_argument("--no-build", action="store_true",
                        help="use the selected prebuilt Rally Point binaries without invoking Cargo")
    parser.add_argument("--steps", type=int, default=600)
    parser.add_argument("--timeout", type=int, default=75)
    parser.add_argument("--result", action="store_true",
                        help="surrender both seats and validate raw results and clean departures")
    parser.add_argument("--drop", action="store_true",
                        help="hard-kill one seat and validate drop recovery and survivor evidence")
    parser.add_argument("--rule", type=int, choices=(1, 2), default=1,
                        help="canonical ruleset: Hyperion QoL or Hyperion anti-meta")
    parser.add_argument("--classic", action="store_true",
                        help="use ordinary Classic rules and reject Hyperion QoL/mutator leakage")
    parser.add_argument("--team-size", type=int, choices=(1, 2, 3), default=1,
                        help="players per team: 1v1, 2v2, or 3v3")
    parser.add_argument("--random-race", action="store_true",
                        help="request random races and validate their resolved raw-result evidence")
    parser.add_argument("--negative", choices=("rollback", "roster", "profile", "map-hash", "map-bytes"),
                        help="require the native parser to reject one invalid manifest before gameplay")
    args = parser.parse_args()
    if args.classic and args.rule == 2:
        parser.error("--classic and --rule 2 select different gameplay semantics")
    if args.result and args.drop:
        parser.error("--result and --drop are separate acceptance cases")
    if args.negative and (args.result or args.drop):
        parser.error("--negative is a separate parser acceptance case")
    if args.drop:
        if args.team_size != 1:
            parser.error("--drop currently supports only 1v1")
        args.result = True
    if args.map_file is None:
        args.map_file = DEFAULT_MAPS[args.team_size]
    if args.steps < 1 or args.timeout < 1:
        parser.error("--steps and --timeout must be positive")
    windows = args.target == "windows-x64"
    hx = args.engine or ENGINE / ("src117/build/x64/Hyperion.exe" if windows else "src117/build/linux-x64/Hyperion.x86_64")
    target = args.rallypoint_target or ENGINE / ("src117/build/x64/rallypoint" if windows else "src117/build/linux-x64/rallypoint")
    executable_suffix = ".exe" if windows else ""
    for path in [hx, args.game, args.map_file]:
        if not path.exists():
            parser.error(f"missing {path}")

    work = args.out or Path(tempfile.mkdtemp(prefix="zero-acceptance-", dir=ENGINE / "testrun/share"))
    work.mkdir(parents=True, exist_ok=True)
    children = {}
    notice_server = None
    notice_thread = None
    if args.result:
        notice_server = ThreadingHTTPServer(("127.0.0.1", 0), NoticeHandler)
        notice_server.notices = []
        notice_server.notices_lock = threading.Lock()
        notice_thread = threading.Thread(target=notice_server.serve_forever, daemon=True)
        notice_thread.start()
    os.environ["HYPERION_CONTROL_KEY"] = "0123456789abcdef0123456789abcdef"
    try:
        if not args.no_build:
            run(["cargo", "build", "-q", "--release", "-p", "rp-dev-setup", "-p", "rally-point-relay",
                 "-p", "rally-point-coordinator", "--manifest-path", str(R2 / "Cargo.toml"),
                 "--target-dir", str(target)], env={**os.environ, "CARGO_NET_OFFLINE": "true"})
        bin_dir = target / "release"
        for binary in ("rp_dev_setup", "rally-point-coordinator", "rally-point-relay"):
            if not (bin_dir / f"{binary}{executable_suffix}").exists():
                parser.error(f"missing prebuilt Rally Point binary: {bin_dir / f'{binary}{executable_suffix}'}")
        run([str(bin_dir / f"rp_dev_setup{executable_suffix}"), "gen-cert", "--cert", work / "cert.pem",
             "--key", work / "key.pem"])
        tenant = (work / "tenant.txt")
        run([str(bin_dir / "rp_dev_setup"), "gen-tenant"], stdout=tenant.open("w"))
        tenant_pkcs8 = tenant.read_text().splitlines()[0].strip()
        client_seed = os.urandom(32).hex()
        seed = random.SystemRandom().randrange(2**32)
        game_id = str(uuid.uuid4())
        coordinator_port, relay_port = free_port(), free_port()
        coordinator_url = f"http://127.0.0.1:{coordinator_port}"
        coordinator_args = [
            str(bin_dir / "rally-point-coordinator"), "--listen", f"127.0.0.1:{coordinator_port}",
            "--dev-tenant", "--tenant", "hyperion-dev", "--kid", "dev-key-1",
            "--allow-insecure-control", "--tenant-key", tenant_pkcs8,
            "--dev-tenant-client-key", client_seed,
        ]
        if notice_server is not None:
            host, port = notice_server.server_address
            coordinator_args += ["--dev-notify-url", f"http://{host}:{port}/notify"]
        children["coordinator"] = subprocess.Popen(
            coordinator_args, stdout=(work / "coordinator.log").open("w"), stderr=subprocess.STDOUT)
        children["relay"] = subprocess.Popen([
            str(bin_dir / "rally-point-relay"), "--listen", f"127.0.0.1:{relay_port}", "--relay-id", "1",
            "--coordinator-url", coordinator_url, "--cert", work / "cert.pem", "--key", work / "key.pem",
        ], stdout=(work / "relay.log").open("w"), stderr=subprocess.STDOUT)
        for attempt in range(30):
            result = subprocess.run([
                str(bin_dir / "rp_dev_setup"), "coord-create", "--coordinator", coordinator_url,
                "--tenant", "hyperion-dev", "--kid", "dev-key-1", "--client-seed", client_seed,
                "--slots", str(args.team_size * 2), "--name", "Hyperion Zero acceptance",
                "--formation-id", game_id,
                "--lobby-seed", str(seed), "--out-dir", work,
            ], stdout=(work / "mint.log").open("w"), stderr=subprocess.STDOUT)
            if result.returncode == 0:
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("the development session could not be created")

        identity = json.loads(subprocess.check_output([
            str(HX), "--sb-identity", "--install-folder", args.game,
        ], env={**os.environ, "HYPERION_WINDOW": "hidden", "SDL_AUDIO_DRIVER": "dummy"}, text=True))
        seats = []
        for seat in range(args.team_size * 2):
            seat_dir = work / f"seat{seat}"
            maps = seat_dir / "maps"
            maps.mkdir(parents=True)
            map_path = maps / args.map_file.name
            shutil.copyfile(args.map_file, map_path)
            setup_path = work / f"s{seat}.json"
            setup = json.loads(setup_path.read_text())
            setup = manifest(setup, seat, game_id, seed, map_path, identity, args.rule, args.team_size,
                             args.random_race, args.classic)
            setup_path.write_text(json.dumps(setup))
            setup_path.chmod(0o600)
            seats.append(seat_dir)

        if args.negative:
            run_negative_case(hx, work, work / "s0.json", args)
            return 0

        control_ports = tuple(free_port() for _ in range(args.team_size * 2))
        for seat, seat_dir in enumerate(seats):
            role = "create" if seat == 0 else "join"
            seat_args = [
                str(HX), "--data", seat_dir, "--sb-setup", work / f"s{seat}.json",
                "--sb-role", role, "--sb-map", seat_dir / "maps" / args.map_file.name,
                "--sb-name", NAMES[seat], "--sb-speed", "6",
                "--sb-type", "oneVOne" if args.team_size == 1 else "topVBottom",
                "--record-orders=24",
                "--install-folder", args.game,
            ]
            seat_env = {**os.environ, "HYPERION_WINDOW": "hidden", "SDL_AUDIO_DRIVER": "dummy",
                        "HYPERION_SB_EVENTS": "1"}
            seat_args.append(f"--control={control_ports[seat]}")
            if args.result:
                seat_env["HYPERION_SB_RESULT_CODE"] = RESULT_CODES[seat]
            children[f"seat{seat}"] = subprocess.Popen(
                seat_args, env=seat_env, stdout=(work / f"c{seat}.log").open("w"), stderr=subprocess.STDOUT)

        wait_for_steps(children, seats, 1, args.timeout)
        wait_for_steps(children, seats, args.steps, args.timeout)
        step_traces = [game_steps(seat) for seat in seats]
        for step in range(1, args.steps + 1):
            expected = step_traces[0].get(step)
            if expected is None:
                raise RuntimeError(f"seat 0 has no deterministic trace for step {step}")
            for seat, trace in enumerate(step_traces[1:], start=1):
                if trace.get(step) != expected:
                    raise RuntimeError(
                        f"seats 0 and {seat} diverged at step {step}: {expected!r} != {trace.get(step)!r}")
        logs = [read(seat / "hyperion_sb.log") for seat in seats]
        relay_log = read(work / "relay.log")
        if any("desync" in log.lower() or "left (reason" in log for log in logs) or "desync" in relay_log.lower():
            raise RuntimeError("a desync or unexpected departure was reported")
        if any("init data went" in log or "stat string went" in log for log in logs):
            raise RuntimeError("creator init/stat blobs were sent on the canonical lobby channel")
        expected_roll = validate_rule_logs(seats, 0 if args.classic else args.rule)
        if args.drop:
            children["seat1"].kill()
            children["seat1"].wait(timeout=10)
            wait_for_drop(notice_server, 1, args.timeout)
            surviving = {name: process for name, process in children.items() if name != "seat1"}
            wait_for_steps(surviving, seats[:1], args.steps + 10, args.timeout)
            quit_seat(control_ports, 0)
            wait_for_notices(notice_server, args.timeout, args.team_size * 2, drop=True)
            validate_drop_notice(notice_server, game_id)
            validate_secret_free_logs(seats)
            wait_for_game_end(seats[:1], 1, args.timeout)
            validate_lifecycle_events(seats[:1])
            validate_replays(seats[:1], 0 if args.classic else args.rule, expected_roll)
            if "desync" in read(work / "relay.log").lower():
                raise RuntimeError("a desync was reported during drop recovery")
            print(f"PASS: the dropped seat was evicted, the survivor passed {args.steps + 10} steps, "
                  "reported raw v2 evidence, and departed cleanly")
        elif args.result:
            surrender_both(control_ports, seats, args.timeout)
            wait_for_notices(notice_server, args.timeout, args.team_size * 2)
            validate_result_notices(notice_server, game_id, args.team_size, args.random_race,
                                    decisive=args.team_size == 1)
            validate_secret_free_logs(seats)
            wait_for_game_end(seats, args.team_size * 2, args.timeout)
            validate_lifecycle_events(seats)
            validate_replays(seats, 0 if args.classic else args.rule, expected_roll)
            print(f"PASS: all {len(seats)} Zero clients read {args.steps} identical steps, reported raw v2 results, "
                  "and made clean departures")
        else:
            surrender_both(control_ports, seats, args.timeout)
            wait_for_game_end(seats, len(seats), args.timeout)
            validate_secret_free_logs(seats)
            validate_lifecycle_events(seats)
            validate_replays(seats, 0 if args.classic else args.rule, expected_roll)
            print(f"PASS: all {len(seats)} Zero clients read {args.steps} identical steps, wrote valid replays, "
                  "and departed cleanly")
        if args.classic:
            validate_classic_selection(hx, work, seats, args)
            print("PASS: Classic selection allocated, removed, and reused its native pools")
        print(f"acceptance directory: {work}")
        return 0
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        print(f"acceptance directory: {work}", file=sys.stderr)
        for name in ("seat0", "seat1"):
            path = work / name / "hyperion_sb.log"
            if path.exists():
                print(f"---- {name} ----\n" + "\n".join(read(path).splitlines()[-20:]), file=sys.stderr)
        return 1
    finally:
        for process in reversed(list(children.values())):
            stop(process)
        if notice_server is not None:
            notice_server.shutdown()
            notice_server.server_close()
            notice_thread.join(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
