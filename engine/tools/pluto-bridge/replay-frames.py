#!/usr/bin/env python3
# replay-frames.py — replay Data/pluto/frames.bin (dumped by the game with
# HYPERION_PLUTO_DUMP=1) into the bridge, frame by frame, as the client does:
# send one frame, wait for its reply, next. Prints the replies and the bridge's
# lifecycle, and stops when the bridge dies.
import struct, subprocess, sys, os, select

BIN = sys.argv[1] if len(sys.argv) > 1 else 'frames.bin'
data = open(BIN, 'rb').read()

frames = []
off = 0
while off + 12 <= len(data):
    magic, frm, nUnits = struct.unpack_from('<III', data, off)
    if magic not in (0x50465848, 0x32505848, 0x33505848):
        print('bad magic at %d: %08x' % (off, magic))
        break
    mapW, mapH, nPlayers = struct.unpack_from('<III', data, off + 12)
    nUnits = min(nUnits, 9999)
    nPlayers = min(nPlayers, 12)
    tile_bytes = mapW * mapH * 4 if magic in (0x32505848, 0x33505848) else 2 * ((mapW * mapH + 7) // 8)
    extra_bytes = nUnits * 64 if magic == 0x33505848 else 0
    need = 24 + nUnits * 40 + extra_bytes + nPlayers * 28 + tile_bytes
    if off + need > len(data):
        print('frame %d truncated' % frm)
        break
    frames.append(data[off:off + need])
    off += need
print('%d frames captured' % len(frames))

p = subprocess.Popen(['wine', 'pluto-bridge.exe'], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd='.')
buf = b''
replies = 0
for frm, raw in enumerate(frames):
    try:
        p.stdin.write(raw)
        p.stdin.flush()
    except BrokenPipeError:
        print('the bridge died at frame %d' % frm)
        break
    # wait for the reply (up to 8s)
    import time
    deadline = time.time() + 8
    got = False
    while time.time() < deadline:
        r, _, _ = select.select([p.stdout], [], [], 0.2)
        if not r:
            continue
        chunk = p.stdout.read(4096)
        if not chunk:
            break
        buf += chunk
        while True:
            m = buf.find(b'HXRP')
            if m < 0:
                break
            if m + 16 > len(buf):
                break
            magic, rfrm, n = struct.unpack_from('<III', buf, m)
            if m + 16 + n > len(buf):
                break
            cmds = buf[m+12:m+12+n]
            buf = buf[m+16+n:]
            got = True
            replies += 1
            if n:
                print('f%d reply: %d cmd bytes %s' % (rfrm, n, cmds[:20].hex(' ')))
        if got:
            break
    if not got:
        print('no reply for frame %d' % frm)
        if p.poll() is not None:
            print('bridge exited')
            break
print('replies:', replies)
p.kill()
