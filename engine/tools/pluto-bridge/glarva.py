import struct, sys
sys.path.insert(0, '.')
import genframes
# genframes' frame + 3 larvae at the hatchery (type 35 = Zerg_Larva)
def frame(f):
    d = bytearray(genframes.frame(f))
    # rebuild the units section: header is 24 bytes; the 8 units follow, then players
    base = d
    nunits = 8
    # extend: insert 3 more units before the players/planes tail
    tail_start = 24 + nunits * 40
    head = bytes(base[:tail_start])
    tail = bytes(base[tail_start:])
    extra = b''
    for i in range(3):
        # id = 8+i (position-based), type 35 larva, player 0, at the hatchery
        extra += struct.pack('<IIIIIIIIIBBBB', 8 + i, 35, 0, 608 + i * 8, 928 + i * 8, 25, 0, 0, 11, 1, 1, 0, 0)
    # patch the unit count in the header (hdr[2])
    hdr = bytearray(head[:24])
    struct.pack_into('<I', hdr, 8, nunits + 3)
    return bytes(hdr) + head[24:] + extra + tail
for f in range(30):
    sys.stdout.buffer.write(frame(f))
