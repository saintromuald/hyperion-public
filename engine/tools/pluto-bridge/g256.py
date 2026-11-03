import struct, sys
sys.path.insert(0, '.')
import genframes
for f in range(30):
    sys.stdout.buffer.write(genframes.frame(f, 256))
