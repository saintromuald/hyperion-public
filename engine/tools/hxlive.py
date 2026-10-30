r"""hxlive.py -- Hyperion's live channel from the command line: what an editor such as
StarDraft 2, a script or an AI sends to a running Hyperion, one command a run. The protocol and
the cutscene format are in docs/live-channel-for-editors.md (docs\\ in a Hyperion package).

  python tools/hxlive.py status
  python tools/hxlive.py cutscene play tools/cutscenes/canvas-sample.json [--on-map]
  python tools/hxlive.py cutscene stop
  python tools/hxlive.py map push --tileset jungle --size 128x128 [--fill <tile>] [--scene s.json] [--wait] [--reveal]
  python tools/hxlive.py map push --from "<map.scm|.scx|.hxm|scenario.chk>" [--scene s.json] [--wait] [--reveal]
  python tools/hxlive.py map load "<map file>" [--ums] [--scene s.json] [--wait] [--reveal]
  python tools/hxlive.py map list [part of a name]
  python tools/hxlive.py map info "<map file>"
  python tools/hxlive.py send '{"cmd": "spawn", "unit": "terran_marine", "x": 1024, "y": 1024}'

The channel listens on 127.0.0.1 (port 27117 unless --port says otherwise) while Hyperion runs with
--control, Debug Tools is the active mod, or the canvas runs. Standard library only; the answer is
printed as JSON, and the exit code is 0 when it says ok. A map file is given by its path, or by a
name Hyperion looks for as the game does ("maps\(2)Volcanis.scm" in the StarCraft folder);
Hyperion reads an .scm, .scx or .hxm itself. --wait waits until the new map runs and prints the
status then; --reveal (which waits too) lifts the fog once it runs.
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import time


def channel_key(port):
    """The channel's key, which Hyperion writes each time the channel starts, in a file only this user
    reads; HYPERION_CONTROL_KEY instead when set, for a channel reached through a tunnel."""
    key = os.environ.get('HYPERION_CONTROL_KEY')
    if key:
        return key
    if os.name == 'nt':
        folder = os.environ.get('LOCALAPPDATA', '')
    else:
        folder = os.environ.get('XDG_DATA_HOME') or os.path.expanduser('~/.local/share')
    try:
        with open(os.path.join(folder, 'Hyperion', 'control-%d.key' % port)) as f:
            return f.read().strip()
    except OSError:
        return ''


def ws_call(port, message, timeout=30.0):
    """One message over a WebSocket (RFC 6455) to 127.0.0.1:port, and its answer."""
    sock = socket.create_connection(('127.0.0.1', port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(('GET /?t=%s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                  'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n'
                  % (channel_key(port), port, key)).encode())
    head = b''
    while b'\r\n\r\n' not in head:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError('the channel closed during the handshake')
        head += chunk
    if b' 101 ' not in head.split(b'\r\n', 1)[0]:
        raise ConnectionError(head.split(b'\r\n', 1)[0].decode(errors='replace'))
    rest = head.split(b'\r\n\r\n', 1)[1]

    data = json.dumps(message).encode()
    mask = os.urandom(4)
    frame = bytearray([0x81])
    if len(data) < 126:
        frame.append(0x80 | len(data))
    elif len(data) < 65536:
        frame.append(0x80 | 126)
        frame += struct.pack('>H', len(data))
    else:
        frame.append(0x80 | 127)
        frame += struct.pack('>Q', len(data))
    frame += mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    sock.sendall(frame)

    buf = bytearray(rest)

    def need(n):
        while len(buf) < n:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError('the channel closed before answering')
            buf.extend(chunk)

    text = bytearray()
    while True:
        need(2)
        fin, op, n = buf[0] & 0x80, buf[0] & 0x0F, buf[1] & 0x7F
        at = 2
        if n == 126:
            need(4)
            n = struct.unpack('>H', bytes(buf[2:4]))[0]
            at = 4
        elif n == 127:
            need(10)
            n = struct.unpack('>Q', bytes(buf[2:10]))[0]
            at = 10
        need(at + n)
        payload = bytes(buf[at:at + n])
        del buf[:at + n]
        if op == 0x8:
            raise ConnectionError('the channel closed before answering')
        if op in (0x1, 0x0):
            text += payload
            if fin:
                break
    sock.close()
    return json.loads(text.decode())


def read_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def chk_tiles(path):
    """ERA, DIM and MTXM of a scenario.chk, as an editor holding its map in memory would send them
    (a map file is sent by its path and Hyperion reads it)."""
    chk = open(path, 'rb').read()
    sections, i = {}, 0
    while i + 8 <= len(chk):
        tag, n = chk[i:i + 4].decode('latin-1'), struct.unpack_from('<i', chk, i + 4)[0]
        if n >= 0:
            sections[tag] = chk[i + 8:i + 8 + n]  # a later section of a name wins, as in the game
        i += 8 + max(n, 0)
    w, h = struct.unpack_from('<HH', sections['DIM '])
    era = struct.unpack_from('<H', sections['ERA '])[0] & 7
    mtxm = sections['MTXM'][:w * h * 2].ljust(w * h * 2, b'\0')
    return era, w, h, mtxm


TILESETS = ['badlands', 'platform', 'install', 'ashworld', 'jungle', 'desert', 'ice', 'twilight']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=27117)
    sub = ap.add_subparsers(dest='what', required=True)
    sub.add_parser('status')
    cs = sub.add_parser('cutscene')
    cs.add_argument('action', choices=['play', 'stop', 'status'])
    cs.add_argument('file', nargs='?', help='the cutscene, a JSON file of {steps: [...]}')
    cs.add_argument('--on-map', action='store_true', help='cutscene play: switch to the map the file names first')
    mp = sub.add_parser('map')
    mp.add_argument('action', choices=['push', 'load', 'list', 'info'])
    mp.add_argument('path', nargs='?', help='map load and info: the map file; map list: part of the names to list')
    mp.add_argument('--tileset', default=None)
    mp.add_argument('--size', default='64x64', help='map push: width x height in tiles')
    mp.add_argument('--from', dest='source', help='map push: take the tileset, size and tiles of this map')
    mp.add_argument('--fill', type=int, help='map push: one tile (MTXM value) for every cell')
    mp.add_argument('--ums', action='store_true', help='map load: as Use Map Settings')
    mp.add_argument('--ms', type=int, default=1000, help='how long the way out takes')
    mp.add_argument('--scene', help='a cutscene file to play when the map starts')
    mp.add_argument('--wait', action='store_true', help='wait until the new map runs')
    mp.add_argument('--reveal', action='store_true', help='wait, then lift the fog for every player')
    sd = sub.add_parser('send')
    sd.add_argument('message', help='a command as JSON, {"cmd": ...}')
    a = ap.parse_args()

    if a.what == 'status':
        msg = {'cmd': 'status'}
    elif a.what == 'send':
        msg = json.loads(a.message)
    elif a.what == 'cutscene':
        if a.action == 'play':
            if not a.file:
                ap.error('cutscene play needs a file')
            scene = read_json(a.file)
            m = scene.get('map') or {}
            if a.on_map and (m.get('push') or m.get('from')):
                msg = dict(m['push'], cmd='map_push') if m.get('push') else {'cmd': 'map_push', 'from': m['from']}
                msg.update(id=1, scene=scene, reveal=True)
                answer = ws_call(a.port, msg)
                if not answer.get('ok') and m.get('or'):
                    answer = ws_call(a.port, dict(m['or'], id=1, cmd='map_push', scene=scene, reveal=True))
                print(json.dumps(answer, indent=2))
                return 0 if answer.get('ok') else 1
            msg = dict(scene, cmd='cutscene_play')
        else:
            msg = {'cmd': 'cutscene_' + a.action}
    else:
        if a.action == 'list':
            msg = {'cmd': 'maps', 'filter': a.path or ''}
        elif a.action == 'info':
            if not a.path:
                ap.error('map info needs the map file')
            msg = {'cmd': 'map_info', 'path': map_path(a.path)}
        elif a.action == 'push':
            msg = {'cmd': 'map_push', 'ms': a.ms}
            if a.source and a.source.lower().endswith('.chk'):
                era, w, h, mtxm = chk_tiles(a.source)
                msg.update(tileset=TILESETS[era], width=w, height=h, tiles_b64=base64.b64encode(mtxm).decode())
            elif a.source:
                msg['from'] = map_path(a.source)
            else:
                w, h = (int(v) for v in a.size.lower().split('x'))
                msg.update(width=w, height=h)
            if a.tileset:
                msg['tileset'] = a.tileset
            if a.fill is not None:
                msg['fill'] = a.fill
        else:
            if not a.path:
                ap.error('map load needs the map file')
            msg = {'cmd': 'map_load', 'path': map_path(a.path), 'ums': a.ums, 'ms': a.ms}
        if a.scene and msg['cmd'] in ('map_push', 'map_load'):
            msg['scene'] = read_json(a.scene)
    msg.setdefault('id', 1)
    answer = ws_call(a.port, msg)
    print(json.dumps(answer, indent=2))
    if answer.get('ok') and a.what == 'map' and a.action in ('push', 'load') and (a.wait or a.reveal):
        status = wait_for_switch(a.port)
        print(json.dumps(status, indent=2))
        if status.get('map_swap_error') or status.get('state') != 'game':
            return 1
        if a.reveal:
            answer = ws_call(a.port, {'id': 2, 'cmd': 'reveal'})
    return 0 if answer.get('ok') else 1


def map_path(name):
    """A file that is here by its full path; anything else as given, for Hyperion to look for."""
    return os.path.abspath(name) if os.path.isfile(name) else name


def wait_for_switch(port, seconds=120):
    """Status every quarter second until a map switch has ended: the new game runs, or the switch
    failed (map_swap_error), or the time is up."""
    deadline = time.time() + seconds
    status = {}
    while time.time() < deadline:
        status = ws_call(port, {'id': 1, 'cmd': 'status'})
        if status.get('map_swap_error') or (status.get('map_swap') == 'none' and status.get('state') == 'game'
                                            and status.get('frame', 0) > 0):
            break
        time.sleep(0.25)
    return status


if __name__ == '__main__':
    sys.exit(main())
