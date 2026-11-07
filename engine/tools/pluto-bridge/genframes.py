import struct, sys
def frame(f, w=128):
    hdr = struct.pack('<III', 0x50465848, f, 8) + struct.pack('<III', w, w, 2)
    units = [(1000,45,0,608,928,1250,0,0,11,1,1),(1001,41,0,672,1064,40,0,0,11,1,1),
             (1002,41,0,640,864,40,0,0,11,1,1),(1003,42,0,512,1224,200,0,0,11,1,1),
             (2000,106,1,2976,2976,1500,0,0,11,0,1),(2001,7,1,2912,3112,60,0,0,11,0,1),
             (2002,7,1,3112,2912,60,0,0,11,0,1),(2003,0,1,3272,3272,40,0,0,11,0,1)]
    for u in units:
        hdr += struct.pack('<IIIIIIIIIBBBB', u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],0,0)
    for p in [(0,50,0,400,6,512,1024),(1,50,0,400,6,3072,3072)]:
        hdr += struct.pack('<IIIIIII', *p)
    # the fog planes: 128x128 bits each, one bit a tile in row order; a
    # 24-tile circle around the bot's base (19,29) is seen and explored
    nbytes = (w * w + 7) // 8
    seen = bytearray(nbytes); explored = bytearray(nbytes)
    for y in range(w):
        for x in range(w):
            if (x-19)*(x-19) + (y-29)*(y-29) <= 24*24:
                t = y * 128 + x
                seen[t >> 3] |= 1 << (t & 7)
                explored[t >> 3] |= 1 << (t & 7)
    hdr += bytes(seen) + bytes(explored)
    return hdr
for f in range(30):
    sys.stdout.buffer.write(frame(f))
