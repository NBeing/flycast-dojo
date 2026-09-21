#!/usr/bin/env python3
"""hand_strokes.py <macro.txt> <macroBase> <from> <to>  ->  stdout: "<label> <lo> <hi>" per stroke
The strokes a hand makes in the piano roll to author a macro segment. Directions are a GROUP on
the roll (a write replaces the group), so each run of an identical direction SET is one brush
stroke ("v>" = the down-right hold); buttons OR in, so each run of a button is one drag; a run of
one row is a tap. Absolute frames. `[2026-09-20]` feeds intent_hand.cpp (dojo:HandStrokes) -
David's ironman98 segment after his state 3 is 25 strokes."""
import os, sys
macro = open(sys.argv[1]).read().split('\n'); base = int(sys.argv[2]); lo = int(sys.argv[3]); hi = int(sys.argv[4])
L = {'W': '^', 'S': 'v', 'A': '<', 'D': '>', 'Z': 'LP', 'X': 'HP', 'V': 'LK', 'B': 'HK', 'C': 'A1', 'N': 'A2', 'M': 'ST'}
DIRS = ['^', 'v', '<', '>']
def cols(f):
    r = macro[f - base] if 0 <= f - base < len(macro) else ''
    return {L[c] for c in r.strip() if c in L}
strokes = []
def runs(key):   # key(f) -> a label or '' ; one stroke per run of the same non-empty label
    run = None
    for f in range(lo, hi + 1):
        k = key(f)
        if k and run and k == run[0] and f == run[2] + 1: run[2] = f
        else:
            if run: strokes.append((run[0], run[1], run[2]))
            run = [k, f, f] if k else None
    if run: strokes.append((run[0], run[1], run[2]))
runs(lambda f: ''.join(d for d in DIRS if d in cols(f)))
for label in L.values():
    if label not in DIRS: runs(lambda f, label=label: label if label in cols(f) else '')
strokes.sort(key=lambda s: (s[1], s[0]))
print(f'# {len(strokes)} strokes, rows {lo}..{hi}, from {os.path.basename(sys.argv[1])} (macroBase {base})')
for label, a, b in strokes: print(label, a, b)
