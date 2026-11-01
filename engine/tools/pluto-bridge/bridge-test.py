import struct, subprocess, time, select, sys
def frame(f):
    hdr = struct.pack('<III', 0x46505048, f, 8) + struct.pack('<III', 128, 128, 2)
    units = [(1000,45,0,608,928,1250,0,0,0,1,1),(1001,41,0,672,1064,40,0,0,0,1,1),
             (1002,41,0,640,864,40,0,0,0,1,1),(1003,42,0,512,1224,200,0,0,0,1,1),
             (2000,106,1,2976,2976,1500,0,0,0,1,1),(2001,7,1,2912,3112,60,0,0,0,1,1),
             (2002,7,1,3112,2912,60,0,0,0,1,1),(2003,0,1,3272,3272,40,0,0,0,1,1)]
    for u in units: hdr += struct.pack('<IIIIIIIIBBBB', u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],0)
    for p in [(0,50,0,400,6,512,1024),(1,50,0,400,6,3072,3072)]: hdr += struct.pack('<IIIIIII', *p)
    return hdr
log = open('btest.log','w', buffering=1)
p = subprocess.Popen(['wine','pluto-bridge.exe'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open("btest.err","w"), cwd='.')
t0 = time.time()
buf = b''
f = 0
while time.time() - t0 < 120 and f < 400:
    p.stdin.write(frame(f)); p.stdin.flush(); f += 1
    r,_,_ = select.select([p.stdout], [], [], 0.2)
    if r:
        chunk = p.stdout.read(4096)
        if not chunk: break
        buf += chunk
p.kill()
log.write('reply bytes: %d\n' % len(buf))
MAGIC = struct.pack('<I', 0x50524858)
# scan for the first magic; resync at each desync (Wine noise precedes it)
off = 0; got = 0
while got < 10:
    p = buf.find(MAGIC, off)
    if p < 0: break
    if p + 12 > len(buf): break
    m, f, n = struct.unpack_from('<III', buf, p)
    cmds = buf[p+12:p+12+n]
    off = p + 12 + n + 4
    if n:
        got += 1
        log.write('  f%d: %d cmd bytes %s\n' % (f, n, cmds[:28].hex(' ')))
    else:
        off = p + 16
if buf:
    off = 0; got = 0
    while off + 12 <= len(buf) and got < 8:
        m, f, n = struct.unpack_from('<III', buf, off)
        cmds = buf[off+12:off+12+n]
        off += 12 + n + 4
        if n:
            got += 1
            log.write('  f%d: %d cmd bytes %s\n' % (f, n, cmds[:24].hex(' ')))
log.close()
