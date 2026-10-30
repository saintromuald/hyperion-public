r"""lab_stream.py -- the AI lab's stream from outside Hyperion (docs/modules/lab.md): a
client of the live channel that subscribes to the lab's topics and prints what comes.

  python tools/lab_stream.py [--port 27117] [--topics lab.workers,lab.games,lab.samples]
                             [--seconds 0] [--raw]

The lab is Hyperion with the AI lab mod active (its live channel on, port 27117 unless --control
names another). Topics:

  lab.workers  once a second: the farm as lab_status has it (running, pace, each worker's game,
               frame, speed and units)
  lab.games    each game's start (its map, seed, levels and seats) and end (its line as the
               project's games.jsonl has it)
  lab.samples  each worker's units, at most 10 times a second: the players' banks and supply,
               and the units as base64, 10 bytes each, little-endian: id (4), x and y in map
               pixels (2 each), player (1), kind (1: building 1, air 2, worker 4, resource 8)

Each message is {"topic": ..., "data": ...}. Standard library only; the channel speaks
WebSocket (RFC 6455) on 127.0.0.1. Run with --seconds to stop after so long, --raw to print each
message's JSON as it came. Stream is the client, for a script of your own.
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import time
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hxlive import channel_key  # noqa: E402


def kind(bits):
    """A unit's kind by its most telling bit."""
    return ("resource" if bits & 8 else "building" if bits & 1 else "worker" if bits & 4 else
            "air" if bits & 2 else "ground")


class Stream:
    """One WebSocket to the live channel: send a command, read messages as they come."""

    def __init__(self, port, timeout=10.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(("GET /?t=%s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                           % (channel_key(port), port, key)).encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("the channel closed during the handshake")
            head += chunk
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(head.split(b"\r\n", 1)[0].decode(errors="replace"))
        self.buf = bytearray(head.split(b"\r\n\r\n", 1)[1])

    def send(self, message):
        data = json.dumps(message).encode()
        mask = os.urandom(4)
        frame = bytearray([0x81])
        if len(data) < 126:
            frame.append(0x80 | len(data))
        elif len(data) < 65536:
            frame.append(0x80 | 126)
            frame += struct.pack(">H", len(data))
        else:
            frame.append(0x80 | 127)
            frame += struct.pack(">Q", len(data))
        frame += mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(frame)

    def _need(self, n, until):
        while len(self.buf) < n:
            left = until - time.time()
            if left <= 0:
                return False
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(1 << 20)
            except socket.timeout:
                return False
            if not chunk:
                raise ConnectionError("the channel closed")
            self.buf.extend(chunk)
        return True

    def read(self, timeout=1.0):
        """The next message as JSON, or None if none comes in timeout seconds."""
        until = time.time() + timeout
        text = bytearray()
        while True:
            if not self._need(2, until):
                return None
            fin, op, n = self.buf[0] & 0x80, self.buf[0] & 0x0F, self.buf[1] & 0x7F
            at = 2
            if n == 126:
                if not self._need(4, until):
                    return None
                n, at = struct.unpack(">H", bytes(self.buf[2:4]))[0], 4
            elif n == 127:
                if not self._need(10, until):
                    return None
                n, at = struct.unpack(">Q", bytes(self.buf[2:10]))[0], 10
            if not self._need(at + n, until + 30):  # a frame begun comes whole
                return None
            payload = bytes(self.buf[at:at + n])
            del self.buf[:at + n]
            if op == 0x8:
                raise ConnectionError("the channel closed")
            if op in (0x1, 0x0):
                text += payload
                if fin:
                    return json.loads(text.decode())

    def close(self):
        self.sock.close()


def units(data):
    """lab.samples' units: (id, x, y, player, kind) each."""
    raw = base64.b64decode(data["units"])
    return [struct.unpack_from("<IHHBB", raw, i) for i in range(0, len(raw), 10)]


def line(m):
    t, d = m.get("topic"), m.get("data", {})
    if t == "lab.workers":
        ws = d.get("workers", [])
        return "workers: %s, pace %s%s, %d playing: %s" % (
            "running" if d.get("running") else "stopped", "x%d" % d["pace"] if d.get("pace") else "max",
            ", paused" if d.get("paused") else "", len(ws),
            ", ".join("game %d at %d" % (w["game"], w["frame"]) for w in ws[:6]) + (" ..." if len(ws) > 6 else ""))
    if t == "lab.games" and d.get("event") == "start":
        return "game %d starts on %s, seed %d, levels %s" % (d["game"], d["map"], d["seed"], d["levels"] or "classic")
    if t == "lab.games":
        g = d.get("game", {})
        return "game %d ended: %s at frame %s, winners %s" % (g.get("game"), g.get("ended"), g.get("end_frame"),
                                                             g.get("winners"))
    if t == "lab.samples":
        u = units(d)
        kinds = Counter(kind(k) for _, _, _, _, k in u)
        return "game %d frame %d: %d units (%s); %s" % (
            d["game"], d["frame"], len(u), ", ".join("%d %s" % (n, k) for k, n in sorted(kinds.items())),
            ", ".join("slot %d %d minerals %d/%d supply" % (s["slot"], s["minerals"], s["supply"], s["supply_most"])
                      for s in d["seats"]))
    return json.dumps(m)[:200]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=27117)
    ap.add_argument("--topics", default="lab.workers,lab.games,lab.samples")
    ap.add_argument("--seconds", type=float, default=0, help="stop after so long (0: until interrupted)")
    ap.add_argument("--raw", action="store_true", help="each message's JSON as it came")
    a = ap.parse_args()
    s = Stream(a.port)
    s.send({"id": 1, "cmd": "subscribe", "topics": [t for t in a.topics.split(",") if t]})
    end = time.time() + a.seconds if a.seconds else None
    counts = Counter()
    try:
        while end is None or time.time() < end:
            m = s.read(timeout=1.0)
            if m is None:
                continue
            if "topic" not in m:
                if not m.get("ok", True):
                    print("refused:", m.get("error"), file=sys.stderr)
                    return 1
                continue
            counts[m["topic"]] += 1
            print(json.dumps(m) if a.raw else line(m), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
    print("messages: " + (", ".join("%s %d" % kv for kv in sorted(counts.items())) or "none"), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
