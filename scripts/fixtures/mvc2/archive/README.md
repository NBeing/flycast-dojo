# David's PS2 combo archive, reconverted with the true grid origin

`tools/reconvert_p2m.py` re-runs David's own converter (`0915/flycast-rr/mvc2_data/converter`)
over his 444 pcsx2-rr movies with `P2M(origin=16)`: his auto-detected origin was 502 rows
late in 438/444 files and thousands of rows late in 6 analog-stick movies (docs/PS2-SIDE.md).
Rows are byte-identical to his; every frame number is now the PS2 frame. His state-derived
CLIP markers are shifted +502 (the PS2 states are on his OneDrive, not here; the shift is
exact arithmetic). Only the 24 `_OK` Dhalsim Revisited clips are committed; the rest is one
command away. `Combo_Dhalsim97`: window 4714..6201 (was 4212..5699), byte-identical content.
