#!/usr/bin/env python3
# bisect-frames.py — replay the captured real frames into the bridge with a
# filtered unit set, to find which units corrupt the fork's engine.
#   python3 bisect-frames.py <mode> [nFrames]
# modes: all | own | no-larva | no-resources | own-no-larva
import struct, subprocess, select, time, sys, os

MODE = sys.argv[1] if len(sys.argv) > 1 else 'all'
NFR = int(sys.argv[2]) if len(sys.argv) > 2 else 10
RES_TYPES = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}  # minerals, geysers etc.
LARVA = 35

d = open('frames.bin', 'rb').read()
frames = []
off = 0
while off + 24 <= len(d):
    magic, frm, n = struct.unpack_from('<III', d, off)
    if magic != 0x50465848:
        break
    w, h, np = struct.unpack_from('<III', d, off + 12)
    need = 24 + n * 40 + np * 28 + 2 * ((w * h + 7) // 8)
    if off + need > len(d):
        break
    frames.append(bytearray(d[off:off + need]))
    off += need

def keep(u):
    ty, pl = struct.unpack_from('<II', u, 4)
    if MODE == 'own':
        return pl == 0
    if MODE == 'no-larva':
        return ty != LARVA
    if MODE == 'no-resources':
        return ty not in RES_TYPES
    if MODE == 'own-no-larva':
        return pl == 0 and ty != LARVA
    return True

use = []
for fr in frames[:NFR]:
    magic, frm, n = struct.unpack_from('<III', fr, 0)
    w, h, np = struct.unpack_from('<III', fr, 12)
    kept = []
    for i in range(n):
        u = bytes(fr[24 + i * 40:24 + i * 40 + 40])
        if keep(u):
            kept.append(u)
    tail = bytes(fr[24 + n * 40:])
    out = bytearray()
    struct.pack_into('<I', fr, 4, len(use))
    struct.pack_into('<I', fr, 8, len(kept))
    out += fr[:24] + b''.join(kept) + tail
    use.append(bytes(out))

os.system('rm -f ~/.wine/drive_c/fault.log')
p = subprocess.Popen(['wine', 'pluto-bridge.exe'], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd='.')
buf = b''
for i, fr in enumerate(use):
    try:
        p.stdin.write(fr)
        p.stdin.flush()
    except BrokenPipeError:
        print('bridge died at frame %d' % i)
        break
    deadline = time.time() + 6
    got = False
    while time.time() < deadline:
        r, _, _ = select.select([p.stdout], [], [], 0.2)
        if not r:
            continue
        chunk = p.stdout.read(4096)
        if not chunk:
            break
        buf += chunk
        m = buf.find(b'HXRP')
        while m >= 0 and m + 16 <= len(buf):
            magic, frm, ncmd = struct.unpack_from('<III', buf, m)
            if m + 16 + ncmd > len(buf):
                break
            buf = buf[m + 16 + ncmd:]
            got = True
            m = buf.find(b'HXRP')
        if got:
            break
    if not got and p.poll() is not None:
        print('bridge exited at frame %d' % i)
        break
p.kill()
time.sleep(1)
try:
    fl = open(os.path.expanduser('~/.wine/drive_c/fault.log'), 'rb').read().decode(errors='replace')
    npairs = fl.count('exec fault #1')
    nlow = fl.count('low-addr')
    print('MODE=%s frames=%d exec-pairs=%d low-addr=%d' % (MODE, NFR, npairs, nlow))
except FileNotFoundError:
    print('MODE=%s frames=%d no faults' % (MODE, NFR))
