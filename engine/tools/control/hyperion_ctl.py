"""Hyperion's live channel from the command line (src117/port/tools/port_control.cpp).

    python engine/tools/control/hyperion_ctl.py status
    python engine/tools/control/hyperion_ctl.py players      each slot in use: type, race, resources, units
    python engine/tools/control/hyperion_ctl.py reveal       the fog off, as black sheep wall (single player)
    python engine/tools/control/hyperion_ctl.py list
    python engine/tools/control/hyperion_ctl.py get gamma
    python engine/tools/control/hyperion_ctl.py set gamma 120
    python engine/tools/control/hyperion_ctl.py snapshot [name]     a PNG in the data folder's snapshots
    python engine/tools/control/hyperion_ctl.py pause | resume | step [frames]
    python engine/tools/control/hyperion_ctl.py hash [dump-path]    the state's hash a domain at a time
    python engine/tools/control/hyperion_ctl.py camera <x> <y>      the view's centre, in map pixels
    python engine/tools/control/hyperion_ctl.py canvas [tileset]    from the home screen
    python engine/tools/control/hyperion_ctl.py spawn <unit> <x> <y> [--player N] [--count N] [--attack X,Y]
    python engine/tools/control/hyperion_ctl.py select <x> <y> [--radius R] [--player N] [--sound]   up to 12 units
    python engine/tools/control/hyperion_ctl.py lights                   the 2D lights' table
    python engine/tools/control/hyperion_ctl.py light <image> [--color #rrggbb] [--radius R]
                                                  [--intensity I] [--flicker F] [--remove]
    python engine/tools/control/hyperion_ctl.py lights_save              the table to lights.json
    python engine/tools/control/hyperion_ctl.py dialogs                  the open dialogs and their places
    python engine/tools/control/hyperion_ctl.py send '{"cmd": "status"}'

The channel is open while Hyperion runs with --control[=<port>], while Debug Tools is the
active mod, and on the canvas; it listens on 127.0.0.1:27117 unless given another port
(--port here). A unit is its number in units.dat or one of the names in UNITS. Each
answer is printed as JSON; the exit code is 1 when the game said no.

The Client class is what the MCP server (hyperion_mcp.py) uses too.
"""
import argparse
import itertools
import json
import os
import sys

from websockets.sync.client import connect

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hxlive import channel_key  # noqa: E402  (the key's file, or HYPERION_CONTROL_KEY)

PORT = 27117

# Units by name, for the commands a person types; any other is given by its number.
UNITS = {
    "marine": 0, "ghost": 1, "vulture": 2, "goliath": 3, "tank": 5, "scv": 7, "wraith": 8,
    "vessel": 9, "dropship": 11, "battlecruiser": 12, "firebat": 32, "medic": 34,
    "valkyrie": 58, "zergling": 37, "hydralisk": 38, "ultralisk": 39, "drone": 41,
    "overlord": 42, "mutalisk": 43, "guardian": 44, "queen": 45, "defiler": 46,
    "scourge": 47, "devourer": 62, "lurker": 103, "corsair": 60, "dark_templar": 61,
    "dark_archon": 63, "probe": 64, "zealot": 65, "dragoon": 66, "high_templar": 67,
    "archon": 68, "shuttle": 69, "scout": 70, "arbiter": 71, "carrier": 72, "reaver": 83,
    "observer": 84,
}


def unit_id(name):
    """A unit's number from a number or a name in UNITS; any other name goes to the game as
    it is, which knows 1.17's units by their map names (terran_marine) and a mod's by its
    own."""
    if isinstance(name, int):
        return name
    text = str(name).strip().lower().replace(" ", "_")
    if text.isdigit():
        return int(text)
    if text in UNITS:
        return UNITS[text]
    if text:
        return text
    raise ValueError(f"no unit named {name}; give its number or one of {', '.join(sorted(UNITS))}")


class Client:
    """One connection to the channel. Each call sends a command and waits for its answer."""

    def __init__(self, port=PORT, timeout=5.0):
        self.ws = connect(f"ws://127.0.0.1:{port}/?t={channel_key(port)}", open_timeout=timeout, max_size=None)
        self.ids = itertools.count(1)

    def close(self):
        self.ws.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def call(self, cmd, timeout=10.0, **args):
        """The answer to cmd, a dict with "ok" and, when it is false, "error"."""
        request = {"id": next(self.ids), "cmd": cmd, **{k: v for k, v in args.items() if v is not None}}
        self.ws.send(json.dumps(request))
        while True:
            answer = json.loads(self.ws.recv(timeout=timeout))
            if answer.get("id") == request["id"]:
                return answer


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("command")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--player", type=int)
    ap.add_argument("--count", type=int)
    ap.add_argument("--attack", help="X,Y: attack-move the spawned units there")
    ap.add_argument("--color", help="light: #rrggbb")
    ap.add_argument("--radius", type=float, help="light: its reach in game pixels; select: how far from the point")
    ap.add_argument("--intensity", type=float, help="light: its strength")
    ap.add_argument("--flicker", type=float, help="light: how much it wavers, 0 to 1")
    ap.add_argument("--remove", action="store_true", help="light: take it out of the table")
    ap.add_argument("--sound", action="store_true", help="select: the selection's voice plays, as a click's does")
    a = ap.parse_args()
    c, rest = a.command, a.args

    try:
        client = Client(a.port)
    except OSError as e:
        sys.exit(f"cannot reach Hyperion on 127.0.0.1:{a.port} ({e}); is it running with --control, "
                 "Debug Tools active, or the canvas?")
    with client:
        if c == "send":
            request = json.loads(rest[0])
            answer = client.call(request.pop("cmd"), **request)
        elif c in ("status", "players", "reveal", "list", "pause", "resume"):
            answer = client.call(c)
        elif c == "get":
            answer = client.call("get", name=rest[0])
        elif c == "set":
            answer = client.call("set", name=rest[0], value=float(rest[1]))
        elif c == "snapshot":
            answer = client.call("snapshot", name=rest[0] if rest else None)
        elif c == "step":
            frames = int(rest[0]) if rest else 1
            answer = client.call("step", frames=frames, timeout=10.0 + frames / 10.0)
        elif c == "hash":
            answer = client.call("hash", dump=rest[0] if rest else None)
        elif c == "camera":
            answer = client.call("camera", x=int(rest[0]), y=int(rest[1]))
        elif c == "canvas":
            answer = client.call("canvas", tileset=rest[0] if rest else None)
        elif c in ("lights", "lights_save", "dialogs"):
            answer = client.call(c)
        elif c == "light":
            answer = client.call("light", image=int(rest[0]), color=a.color, radius=a.radius,
                                 intensity=a.intensity, flicker=a.flicker, remove=True if a.remove else None)
        elif c == "spawn":
            attack = [int(v) for v in a.attack.split(",")] if a.attack else None
            answer = client.call("spawn", unit=unit_id(rest[0]), x=int(rest[1]), y=int(rest[2]),
                                 player=a.player, count=a.count, attack=attack)
        elif c == "select":
            answer = client.call("select", x=int(rest[0]), y=int(rest[1]), player=a.player,
                                 radius=int(a.radius) if a.radius else None, sound=True if a.sound else None)
        else:
            sys.exit(f"no command {c}; see --help")
    print(json.dumps(answer, indent=2))
    sys.exit(0 if answer.get("ok") else 1)


if __name__ == "__main__":
    main()
