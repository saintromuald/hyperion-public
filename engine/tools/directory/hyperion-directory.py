#!/usr/bin/env python3
"""hyperion-directory -- the game list for Hyperion's rooms over the internet.

One small server (Python 3 standard library only). Rooms announce themselves to
it over UDP; the game's own Multiplayer screen asks it where the games are, the
same way it asks a room on a LAN, and this page lists them for a browser.

  * Rooms (Hyperion --room) announce: lanroom.json `announce: "host:port"` sends
    an HXLR datagram every ten seconds (with `announceHost` the address players
    join at, and `announceSecret` the shared secret). An entry that is not
    announced again within --expire seconds drops off the list.
  * The game finds the rooms: a joiner sets HYPERION_LAN_HOSTS to this host's
    address (the query goes to UDP 6111, like a room's). Each answer carries the
    room's own summary plus "host", the address to actually join at -- so a room
    behind NAT can be listed while the directory answers for it. Rooms the game
    also hears on its LAN appear once: the list is keyed by host and port.
  * A browser sees the list on the page this server serves (--http-port 7118).

Run it somewhere everyone can reach: a VPS, or any machine on the group's
tailnet. The netcode itself (rally-point coordinator + relay) is separate and
lives with the group's server; this process only lists games.

Usage:
    hyperion-directory.py [--udp-port 6111] [--http-port 7118]
                          [--expire 30] [--secret <shared>] [--bind 0.0.0.0]
"""
import argparse
import json
import selectors
import socket
import struct
import threading
import time
import html
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

QUERY = b"HXLQ"   # the game's query (four bytes), answered HXLA + the room's advert frame
ANSWER = b"HXLA"
ANNOUNCE = b"HXLR"  # a room's registration: the advert JSON, NUL-terminated
PROTOCOL = 2        # port_lanproto.h kProtocol
ADVERT_ROOM = 2     # port_lanproto.h Advert::ADVERT_ROOM


class Directory:
    def __init__(self, secret, expire):
        self.secret = secret
        self.expire = expire
        self.rooms = {}          # key -> {"json": ..., "seen": monotonic}
        self.last_query = {}     # source ip -> last query time (rate limit)

    def register(self, src_ip, payload):
        if len(payload) < 5 or payload[:4] != ANNOUNCE:
            return
        try:
            info = json.loads(payload[4:].rstrip(b"\x00").decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return
        if self.secret and info.pop("secret", None) != self.secret:
            return
        if not isinstance(info, dict):
            return
        host = str(info.get("host") or "").strip() or src_ip
        port = info.get("port")
        if not isinstance(port, int) or not (1 <= port <= 65535):
            port = 6112
        key = "%s:%d" % (host, port)
        # the address joiners use, and whether its games play online (netcode)
        info["host"] = host
        info["netcode"] = bool(info.get("netcode"))
        self.rooms[key] = {"json": info, "seen": time.monotonic()}
        # one host may list a handful of rooms; beyond that, the oldest go
        mine = [k for k in self.rooms if self.rooms[k]["json"].get("host") == host]
        while len(mine) > 8:
            oldest = min(mine, key=lambda k: self.rooms[k]["seen"])
            self.rooms.pop(oldest, None)
            mine.remove(oldest)
        if len(self.rooms) > 1000:
            for k in sorted(self.rooms, key=lambda k: self.rooms[k]["seen"])[: len(self.rooms) - 1000]:
                self.rooms.pop(k, None)

    def prune(self):
        now = time.monotonic()
        for k in [k for k, v in self.rooms.items() if now - v["seen"] > self.expire]:
            self.rooms.pop(k, None)

    def answers(self, src_ip):
        """The bytes to send one querier: one datagram per live room."""
        self.prune()
        now = time.monotonic()
        if now - self.last_query.get(src_ip, 0.0) < 0.5:  # an HXLQ flood: answer once a beat
            return []
        self.last_query[src_ip] = now
        out = []
        for key in sorted(self.rooms):
            info = self.rooms[key]["json"]
            blob = json.dumps(info, separators=(",", ":")).encode("utf-8")
            out.append(ANSWER + struct.pack("<H", PROTOCOL) + bytes([ADVERT_ROOM]) + blob + b"\x00")
        return out


def serve_udp(dirn, sock):
    while True:
        try:
            data, addr = sock.recvfrom(65535)
        except OSError:
            return
        if data[:4] == QUERY:
            for reply in dirn.answers(addr[0]):
                try:
                    sock.sendto(reply, addr)
                except OSError:
                    pass
        elif data[:4] == ANNOUNCE:
            dirn.register(addr[0], data)


class Page(BaseHTTPRequestHandler):
    def log_message(self, *a):  # quiet
        pass

    def do_GET(self):
        if self.path == "/rooms.json":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            body = json.dumps([self.server.dirn.rooms[k]["json"] for k in sorted(self.server.dirn.rooms)])
            self.wfile.write(body.encode("utf-8"))
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(page(self.server.dirn.rooms).encode("utf-8"))


def page(rooms):
    rows = []
    for key in sorted(rooms):
        r = rooms[key]["json"]
        badge = ("<span class='n'>NETCODE</span>" if r.get("netcode")
                 else "<span class='l'>LAN</span>")
        lock = " <span class='pw'>password</span>" if r.get("password") else ""
        rows.append(
            "<tr><td>%s %s</td><td>%s</td><td>%s</td><td>%d</td><td>%d</td></tr>"
            % (html.escape(str(r.get("name", "A room"))), badge,
               html.escape(str(r.get("machine", ""))),
               html.escape("%s:%s" % (r.get("host", "?"), r.get("port", 6112))) + lock,
               r.get("people", 0), r.get("matches", 0)))
    body = ("<h1>Hyperion games</h1>"
            "<p>Rooms whose hosts announced them in the last while. In the game: "
            "Multiplayer &#8594; <b>Join a Room</b> (set <code>HYPERION_LAN_HOSTS</code> to "
            "this machine), then click a room. <span class='n'>NETCODE</span> rooms play their "
            "matches online through the relay netcode; <span class='l'>LAN</span> rooms play "
            "on the host's own network.</p>"
            "<table><tr><th>Room</th><th>Machine</th><th>Join at</th><th>In</th><th>Playing</th></tr>"
            + "".join(rows) + "</table>")
    return ("<!doctype html><html><head><meta charset='utf-8'>"
            "<title>Hyperion games</title><style>"
            "body{font-family:system-ui,sans-serif;margin:2em auto;max-width:56em;color:#ddd;background:#14161a}"
            "table{border-collapse:collapse;width:100%}td,th{border:1px solid #333;padding:.5em .8em;text-align:left}"
            "th{color:#9ab}code{background:#222;padding:.1em .3em}"
            ".n{background:#2f9e4429;color:#5fd27f;border:1px solid #2f9e44;border-radius:3px;padding:0 .4em;font-size:.85em}"
            ".l{background:#868e9629;color:#adb5bd;border:1px solid #555;border-radius:3px;padding:0 .4em;font-size:.85em}"
            ".pw{color:#e0a13b;font-size:.85em}"
            "</style></head><body>" + body + "</body></html>")


def main():
    ap = argparse.ArgumentParser(description="Hyperion's game list (rooms announce, games join)")
    ap.add_argument("--udp-port", type=int, default=6111, help="the game query port (default 6111)")
    ap.add_argument("--http-port", type=int, default=7118, help="the list's page (default 7118)")
    ap.add_argument("--expire", type=int, default=30, help="seconds without an announce before a room drops")
    ap.add_argument("--secret", default="", help="the shared announce secret (lanroom.json announceSecret)")
    ap.add_argument("--bind", default="0.0.0.0")
    a = ap.parse_args()
    dirn = Directory(a.secret, a.expire)

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind((a.bind, a.udp_port))

    httpd = ThreadingHTTPServer((a.bind, a.http_port), Page)
    httpd.dirn = dirn

    print("hyperion-directory: UDP %s:%d (the game's query port), the page on http://%s:%d"
          % (a.bind, a.udp_port, a.bind, a.http_port))
    if not a.secret:
        print("  no --secret: any room may announce (fine on a private network; set one on the internet)")
    threads = [
        threading.Thread(target=serve_udp, args=(dirn, udp), daemon=True),
        threading.Thread(target=httpd.serve_forever, daemon=True),
    ]
    for t in threads:
        t.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        httpd.shutdown()


if __name__ == "__main__":
    main()
