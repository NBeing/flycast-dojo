#!/usr/bin/env python3
"""flyr_shift.py <in.flyr> <out.flyr> <delta>  - every input row lands <delta> frames later (negative = earlier).
`[2026-09-20]` for the ironman98 latency experiment: does one frame of row-to-latch alignment turn 26 into 36."""
import struct, sys
src, dst, delta = sys.argv[1], sys.argv[2], int(sys.argv[3])
b = open(src, 'rb').read(); out = bytearray(); off = 0; n = 0
while off + 12 <= len(b):
    size, seq, cmd = struct.unpack_from('<III', b, off)
    msg = bytearray(b[off: off + 12 + size]); off += 12 + size
    if cmd == 6 and size >= 4:
        (rs,) = struct.unpack_from('<I', msg, 12)
        for p in range(16, len(msg) - rs + 1, rs):
            f = struct.unpack_from('<I', msg, p)[0]
            if f + delta >= 0:
                struct.pack_into('<I', msg, p, f + delta); n += 1
    out += msg
open(dst, 'wb').write(out); print('shifted', n, 'rows by', delta)
