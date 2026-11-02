import struct, subprocess, time, select, sys
def frame(f):
    hdr = struct.pack('<III', 0x50465848, f, 8) + struct.pack('<III', 128, 128, 2)
    units = [(1000,45,0,608,928,1250,0,0,11,1,1),(1001,41,0,672,1064,40,0,0,11,1,1),
             (1002,41,0,640,864,40,0,0,11,1,1),(1003,42,0,512,1224,200,0,0,11,1,1),
             (2000,106,1,2976,2976,1500,0,0,11,1,1),(2001,7,1,2912,3112,60,0,0,11,1,1),
             (2002,7,1,3112,2912,60,0,0,11,1,1),(2003,0,1,3272,3272,40,0,0,11,1,1)]
    for u in units:
        hdr += struct.pack('<IIIIIIIIIBBBB', u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],0,0)
    for p in [(0,50,0,400,6,512,1024),(1,50,0,400,6,3072,3072)]:
        hdr += struct.pack('<IIIIIII', *p)
    return hdr

n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
p = subprocess.Popen(['wine','pluto-bridge.exe'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.DEVNULL, cwd='.')
buf = b''
shown = 0
t0 = time.time()
for f in range(n):
    p.stdin.write(frame(f))
    p.stdin.flush()
    # wait up to 3s for this frame's reply
    deadline = time.time() + 3
    while time.time() < deadline:
        r,_,_ = select.select([p.stdout], [], [], 0.1)
        if not r: continue
        chunk = p.stdout.read(4096)
        if not chunk: break
        buf += chunk
        # drain any complete replies
        while True:
            m = buf.find(b'HXRP')
            if m < 0: break
            if m + 16 > len(buf): break
            magic, frm, ncmd = struct.unpack_from('<III', buf, m)
            if m + 16 + ncmd > len(buf): break
            if ncmd:
                shown += 1
                if shown <= 10:
                    cmds = buf[m+12:m+12+ncmd]
                    print('f%d: %d cmd bytes %s' % (frm, ncmd, cmds[:20].hex(' ')))
            buf = buf[m+16+ncmd:]
        if buf.startswith(b'HXRP') or True:
            pass
    # next frame once we have at least drained; if the bridge died, stop
    if p.poll() is not None:
        print('bridge exited at frame', f)
        break
print('done in %.1fs; replies with commands: %d' % (time.time()-t0, shown))
p.kill()
