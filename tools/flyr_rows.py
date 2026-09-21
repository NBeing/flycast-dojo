#!/usr/bin/env python3
"""flyr_rows.py <clip.flyr> <from> <to> [macro.txt macroBase]
Print the movie's P1/P2 kcode rows for frames [from,to] (last-write-wins across batches, like
Replay's parser) and, when a macro + base are given, the macro's row for the same frame beside it.
`[2026-09-20]` written for the ironman98 chase: the .flyr in a macro clip is a DUMMY (clip.json
replayDummy) and the question was whether it carries the macro's mash rows."""
import struct, sys
HEADER = 12
def rows(path):
    b = open(path, 'rb').read(); off = 0; out = {}
    while off + HEADER <= len(b):
        size, seq, cmd = struct.unpack_from('<III', b, off)
        body = b[off + HEADER: off + HEADER + size]; off += HEADER + size
        if cmd != 6 or len(body) < 4: continue   # MAPLE_BUFFER (6) batches only
        (rs,) = struct.unpack_from('<I', body, 0)
        for p in range(4, len(body) - rs + 1, rs):
            f = struct.unpack_from('<I', body, p)[0]
            out[f] = body[p + 4: p + rs]
    return out
if __name__ == '__main__':
    path, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    macro = base = None
    if len(sys.argv) > 5:
        macro = open(sys.argv[4]).read().split('\n'); base = int(sys.argv[5])
    r = rows(path)
    print('frames in movie:', len(r), 'min', min(r), 'max', max(r))
    for f in range(lo, hi + 1):
        rec = r.get(f)
        if rec is None: s = 'MISSING'
        else:
            k1 = struct.unpack_from('<I', rec, 0)[0] & 0xFFFFF; k2 = struct.unpack_from('<I', rec, 12)[0] & 0xFFFFF
            s = f'P1 {~k1 & 0xFFFFF:05x} P2 {~k2 & 0xFFFFF:05x}'   # kcode is active-low
        m = macro[f - base] if macro is not None and 0 <= f - base < len(macro) else ''
        print(f, s, '|', m)
