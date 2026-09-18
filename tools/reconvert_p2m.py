#!/usr/bin/env python3
"""
reconvert_p2m.py - David's 444 pcsx2-rr movies re-converted with the TRUE grid origin.

`[MEASURED 2026-09-18]` (docs/PS2-SIDE.md) his converter's parse_p2m._detect_origin walks
back from EOF while the analog columns are neutral and stops 502 rows late in 438/444
files (offset 6040 = 16 + 502*12); the real header is 16 bytes (u32 FrameMax, u32
rerecords, 8-byte timestamp) and (size-16) % 12 == 0 in 444/444. So every converted row
N was PS2 frame N+502, the "combo start (est)" fell in a stale tail past FrameMax, and
"122 truncated" was an artifact (2 real). Six analog-stick movies lost thousands of rows.

This wrapper calls HIS convert_file unchanged, with P2M(origin=16) forced, into our tree
(scripts/fixtures/mvc2/archive/), and SHIFTS the state-derived CLIP markers from his
existing headers by +502: the PS2 savestates that produced them are not on this machine
(they live on his OneDrive), so they cannot be re-scanned - the shift is exact arithmetic
(state frame = matched prefix length L from the old grid start = PS2 frame L+502 = row
L+502 of an origin-16 grid). Rows are byte-identical to his; only the numbering is true.

    python3 tools/reconvert_p2m.py [--only <substr>] [--dry]
"""
import sys, os, re, glob, argparse, shutil
CONV = "/home/nbee/dev/davids_fly/0915/flycast-rr/mvc2_data/converter"
ROOT = "/home/nbee/dev/davids_fly/0915/flycast-rr/mvc2_data"
OUT  = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts/fixtures/mvc2/archive")
sys.path.insert(0, CONV)
import parse_p2m as P
import p2m_to_flycast as C

SHIFT = 502
_orig_P2M = P.P2M
class P2M16(_orig_P2M):
    def __init__(self, path, origin=None):
        super().__init__(path, origin=16)
P.P2M = P2M16            # C.convert_file does `P.P2M(p2m_path)` - now origin 16

MARK = re.compile(r"^(#\s*(?:CLIP LIKELY (?:BEGINS|ENDS) HERE|\s{4}\S+\s+->)\s*:?\s*(?:frame )?)(\d+)(.*)$")

def shifted_markers(old_macro):
    """His header's state-derived lines with every frame number +SHIFT; None if no old file."""
    if not os.path.isfile(old_macro):
        return None
    out = []
    for l in open(old_macro, encoding="utf-8", errors="replace"):
        l = l.rstrip("\r\n")
        if not l.startswith("#"):
            break
        if "CLIP LIKELY" in l or re.match(r"^#\s{4}\S+\s+->\s+frame \d+", l):
            l = re.sub(r"frame (\d+)", lambda m: "frame %d" % (int(m.group(1)) + SHIFT), l)
            l = re.sub(r"\((\d+)\.\.(\d+)\)", lambda m: "(%d..%d)" % (int(m.group(1)) + SHIFT, int(m.group(2)) + SHIFT), l)
            out.append(l)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    config = C.MacroConfig(include_header=True)
    files = sorted(glob.glob(ROOT + "/*Replays/**/*.p2m", recursive=True))
    n = 0
    for src in files:
        if a.only and a.only not in src:
            continue
        rel = os.path.relpath(src, ROOT)
        stem = os.path.splitext(os.path.basename(src))[0]
        out_dir = os.path.join(OUT, os.path.dirname(rel))
        out = os.path.join(out_dir, stem + "_macro.txt")
        old = os.path.join(os.path.dirname(src), stem + "_macro.txt")
        if a.dry:
            print(rel); continue
        os.makedirs(out_dir, exist_ok=True)
        meta = C.convert_file(src, out, config)
        marks = shifted_markers(old)
        # replace the input-estimate marker block with the shifted state-derived one, and say so
        lines = open(out, encoding="utf-8", errors="replace").read().split("\n")
        hdr_end = next(i for i, l in enumerate(lines) if not l.startswith("#"))
        head = [l for l in lines[:hdr_end] if "CLIP LIKELY" not in l and not re.match(r"^#\s{4}\S+\s+->", l) and "clip markers" not in l and "savestates (name hints" not in l]
        note = ["# ---- clip markers (origin-16 reconversion, 2026-09-18: David's state-derived markers SHIFTED +%d;" % SHIFT,
                "#      the PS2 states are not on this machine, the shift is exact - see docs/PS2-SIDE.md) ----"]
        body = lines[hdr_end:]
        # drop his inline '# ===== CLIP LIKELY ...' body markers (they sat 502 rows early) and re-place them
        body = [l for l in body if "CLIP LIKELY" not in l]
        new_marks = marks or ["# (no state-derived markers in the old header - input estimate only)"]
        begins = ends = None
        for l in new_marks:
            m = re.search(r"BEGINS HERE: frame (\d+)", l); begins = int(m.group(1)) if m else begins
            m = re.search(r"ENDS HERE:\s+frame (\d+)", l); ends = int(m.group(1)) if m else ends
        rows = [l for l in body if l != ""]
        def place(idx, text):
            # rows are 1 line per frame after the header; comment lines don't consume a frame
            cnt = -1
            for i, l in enumerate(rows):
                if l.startswith("#"): continue
                cnt += 1
                if cnt == idx:
                    rows.insert(i, text); return
        if begins is not None: place(begins, "# %s CLIP LIKELY BEGINS HERE (frame %d) %s" % ("=" * 18, begins, "=" * 18))
        if ends is not None: place(ends, "# %s CLIP LIKELY ENDS HERE (frame %d) %s" % ("=" * 20, ends, "=" * 20))
        open(out, "w", newline="\n").write("\n".join(head + note + new_marks + rows) + "\n")
        n += 1
        print("%s  rows=%d  FrameMax=%d  begins=%s ends=%s" % (rel, meta.get("emit_count", -1), meta.get("frame_max", -1), begins, ends))
    print("reconverted", n)

if __name__ == "__main__":
    main()
