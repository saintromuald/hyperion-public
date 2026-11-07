#!/usr/bin/env python3
import struct, subprocess, time, os, select, sys
BIN = sys.argv[1] if len(sys.argv)>1 else 'real.bin'
data=open(BIN,'rb').read(); frames=[]; off=0
while off+12<=len(data):
    magic,frm,nu=struct.unpack_from('<III',data,off)
    if magic not in (0x50465848, 0x32505848, 0x33505848): break
    w,h,np=struct.unpack_from('<III',data,off+12)
    tile_bytes = w*h*4 if magic in (0x32505848, 0x33505848) else 2*((w*h+7)//8)
    extra_bytes = min(nu,9999)*64 if magic == 0x33505848 else 0
    need=24+min(nu,9999)*40+extra_bytes+min(np,12)*28+tile_bytes
    frames.append(data[off:off+need]); off+=need
# A benchmark dump can contain the short making game before playback. Use the
# last frame zero: that is the live benchmark's actual MatchStart.
starts = [i for i, raw in enumerate(frames) if struct.unpack_from('<I', raw, 4)[0] == 0]
if starts:
    frames = frames[starts[-1]:]
if len(sys.argv) > 2:
    frames = frames[:int(sys.argv[2])]
print('%d frames'%len(frames),flush=True)
env=dict(os.environ); env['HX_DEBUG']='1'
err=open('replay-late.err','w')
try:
    os.unlink('frames.fifo')
except FileNotFoundError:
    pass
os.mkfifo('frames.fifo')
fout=os.open('frames.fifo', os.O_RDWR)
p=subprocess.Popen(['wine','pluto-bridge.exe'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,cwd='.',env=env,bufsize=0)
buf=b''
for i,raw in enumerate(frames):
    # A benchmark capture can begin after its making/playback transition.
    # The bridge still needs a real frame zero for MatchStart and FIFO handoff.
    raw = raw[:4] + struct.pack('<I', i) + raw[8:]
    if p.poll() is not None:
        print('bridge exited before frame',i,flush=True); break
    if i < 1:
        p.stdin.write(raw); p.stdin.flush()
    else:
        os.write(fout, raw)
    deadline=time.time()+20; got=False
    while time.time()<deadline:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if r:
            chunk=os.read(p.stdout.fileno(),65536)
            if not chunk: break
            buf+=chunk
            if b'HXRP' in buf: got=True;break
        if p.poll() is not None: break
    print('frame',i,'reply',got,'process',p.poll(),flush=True)
    if p.poll() is not None: break
print('reply bytes',len(buf),flush=True)
time.sleep(1)
p.stdin.close(); os.close(fout)
if p.poll() is None: p.kill()
print('exit',p.wait(),flush=True)
