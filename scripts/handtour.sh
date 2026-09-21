#!/usr/bin/env bash
# handtour.sh - THE COMBO AUTHORED BY HAND (Surface Tour module `hand`, intent_hand.cpp).
#
#   WHAT:  boots David's ironman98 clip (RECIPE [david_ironman]) as the tour clip with a LITE
#          preamble (load BASE, show, rebind the roll's chord) and only the hand module: from his
#          state 3 the module blanks the recording's segment, proves a blank roll lands nothing
#          new, then authors the segment through the roll panel's own gestures - 26 drags,
#          brushes and taps generated from his macro (tools/hand_strokes.py, pinned in
#          scripts/fixtures/mvc2/combos/ironman98_hand.txt) - runs the game and reads the meter:
#          it must say the video's 94. Judged by surfacetourtest.sh's gates (G1..G9, floor 30).
#   USAGE: scripts/handtour.sh                    the run
#          scripts/handtour.sh --sabotage hand-thc  the arm: the A1+A2 press skipped, the run must redden
#          scripts/handtour.sh --watch             on the real display, the human watching the roll fill
#          scripts/handtour.sh --strokes           print the strokes and exit (0 = the pin is current)
#   EXIT:  surfacetourtest.sh's (0 pass · 1 claim · 2 usage · 4 arm failed to fire · 5 vacuous · 77 skip).
set -uo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
D="$ROOT/scripts/fixtures/mvc2/david/2026-09-20T19_54_03Z"
RECIPE="$ROOT/scripts/fixtures/mvc2/RECIPE.toml"
PIN="$ROOT/scripts/fixtures/mvc2/combos/ironman98_hand.txt"
SKIP=77
get() { python3 -c "import tomllib,sys; d=tomllib.load(open(sys.argv[1],'rb')); print(d[sys.argv[2]][sys.argv[3]])" "$RECIPE" "$1" "$2"; }
FLYR="$(ls "$D"/*.flyr 2>/dev/null | head -1)"
[ -n "$FLYR" ] && [ -f "$D/NoBGM_VMU_3.state" ] || { echo "handtour: SKIP - David's clip is not unpacked in $D (the zip)"; exit $SKIP; }
BASE="$(get david_ironman macro_base)"; PEAK="$(get david_ironman video_peak)"
SLOT=3; STOP=16560; FROM=16216
# the strokes: regenerated from the macro every run and compared to the pin - a stale pin is a finding
GEN="$(mktemp)"; python3 "$ROOT/tools/hand_strokes.py" "$D"/*_macro.txt "$BASE" "$FROM" "$STOP" > "$GEN"
if ! diff -q "$GEN" "$PIN" >/dev/null 2>&1; then echo "handtour: the pinned strokes differ from the macro's - regenerate $PIN"; diff "$GEN" "$PIN" | head; rm -f "$GEN"; exit 1; fi
rm -f "$GEN"
if [ "${1:-}" = "--strokes" ]; then cat "$PIN"; exit 0; fi
WATCH=()
if [ "${1:-}" = "--watch" ]; then WATCH=(--watch "$FLYR"); shift; fi
echo "handtour: BASE = slot $SLOT (frame $(python3 -c "import struct;print(struct.unpack('<I',open('$D/NoBGM_VMU_3.state.frame','rb').read(4))[0])")), $(grep -c '^[^#]' "$PIN") strokes, stop $STOP, want $PEAK"
FLYCAST_TEST_CLIP="$FLYR" TOUR_KEEP_SLOTS=yes TOUR_MODULES=hand TOUR_PREAMBLE=lite INTENT_SLOT=$SLOT \
HAND_STROKES="$PIN" HAND_STOP=$STOP HAND_PEAK="$PEAK" TOUR_FLOOR="${TOUR_FLOOR:-30}" TOUR_WAIT_S="${TOUR_WAIT_S:-900}" \
exec "$ROOT/scripts/surfacetourtest.sh" "${WATCH[@]}" "$@"
