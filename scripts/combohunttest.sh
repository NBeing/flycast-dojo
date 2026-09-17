#!/usr/bin/env bash
# combohunttest - the combo FIXTURE lands on this build: the hunt finds David's candidate
# connecting from the base, and says so in the RECIPE's own numbers.
#
#   RUN:   scripts/combohunttest.sh              (needs a ROM, Xvfb, python3, the tour's base
#          clip with its slot-0 state, and the candidate macro under scripts/fixtures/mvc2)
#   PASS:  exit 0. Boots the base clip in a sandbox with dojo:ComboHunt=macro and
#          dojo:ComboHuntMacro=<the RECIPE's candidate>, waits for ONE
#          `COMBO HUNT RESULT:` line, and holds it against scripts/fixtures/mvc2/RECIPE.toml
#          - NOTHING is typed into this script; every expected number is a RECIPE pin:
#            H1 found=yes                      H2 candidate == [result].candidate
#            H3 peak  == [result].combo_peak   H4 after == [result].after_hash
#            H5 base  == [base].machine_hash   H6 phase == [phase].value
#          then `COMBOHUNTTEST RESULT: passed=N failed=N` and PASS/FAIL. The RESULT line is
#          printed verbatim, always.
#   EXIT:  0 pass · 1 a claim failed · 2 usage · 4 --sabotage: the arm FAILED TO FIRE ·
#          77 SKIP (no ROM / Xvfb / python3 / build / base clip / candidate; a stale binary;
#          or no RESULT line within $HUNT_WAIT_S, default 400 s - the hunt never spoke).
#   ARMS:  --sabotage window   (--self-test == the same; --list-sabotage lists it). `window`
#          runs the SAME macro over a pre-combo window (dojo:ComboHuntWindow=84-1500: the
#          file's first input is at 84, the marker-bracketed combo begins at 4212), so the
#          hunt must report found=no - H1 must redden, and H5 (the base hash, untouched by
#          the arm) must stay green. Judged by scripts/lib/arms.sh (applied / broke its
#          target / left its control green AND the control ran). `[MEASURED 2026-09-17]`
#          window 84-1500 gives peak=0 on all four phases, found=no. Inverted exits: 0 fired
#          · 4 did not · 2 INCONCLUSIVE · 1 broke its control.
#
# THE FAILURE THIS WOULD HAVE CAUGHT. A hunt that reports found=yes with a DIFFERENT peak
# or end hash than the RECIPE pins is the emulator having changed under the fixture - the
# "correct picture of the wrong thing" nbneo-rr warns about: David's PASS "verifies the
# infrastructure, not that hits connect"; this holds the hit count and the machine hash.
# And a base whose hash moved (H5) is caught BEFORE any candidate is judged against it.
#
# WHY A CLIP. The base is slot 0 of the tour's clip (David's V48 state, frame 9928, Sonson
# vs Marrow, in-match); the RECIPE pins the machine hash AFTER the load on this build, never
# the V48 file's digest (it cannot round-trip under V49). The candidate is David's
# PS2-converted Combo_Dhalsim97 - a CANDIDATE by provenance; it became a fixture only when
# the hunt observed it connecting here (peak 19).
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FIX="$ROOT/scripts/fixtures/mvc2"
RECIPE="${FIXTURES_RECIPE:-$FIX/RECIPE.toml}"
ARMS="$ROOT/scripts/lib/arms.sh"
KNOWN_ARMS="window"
WAIT_S="${HUNT_WAIT_S:-400}"

usage() { echo "usage: $0 [--sabotage <class> | --self-test | --list-sabotage]   (exit 2: usage)"; exit 2; }
ARM=""
while [ $# -gt 0 ]; do
	case "$1" in
		--sabotage) shift; [ $# -gt 0 ] || usage; ARM="$1" ;;
		--self-test) ARM=window ;;
		--list-sabotage) echo "window      H1 found=yes         - the same macro over the pre-combo window 84-1500; H1 must redden (found=no), H5 must stay green"; exit 0 ;;
		*) usage ;;
	esac
	shift
done
if [ -n "$ARM" ]; then case " $KNOWN_ARMS " in *" $ARM "*) ;; *) echo "combohunttest: unknown sabotage class '$ARM' (known: $KNOWN_ARMS)"; exit 2 ;; esac; fi

command -v python3 >/dev/null || { echo "combohunttest: SKIP - no python3"; exit $SKIP; }
command -v Xvfb    >/dev/null || { echo "combohunttest: SKIP - no Xvfb"; exit $SKIP; }
[ -x "$EXE" ] || { echo "combohunttest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "combohunttest: SKIP - no ROM ($ROM)"; exit $SKIP; }
[ -f "$RECIPE" ] || { echo "combohunttest: SKIP - no RECIPE ($RECIPE)"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "combohunttest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

get() { python3 -c "import tomllib,sys; d=tomllib.load(open(sys.argv[1],'rb')); print(d[sys.argv[2]][sys.argv[3]])" "$RECIPE" "$1" "$2"; }
MACRO="$FIX/$(get candidate file)"
[ -f "$MACRO" ] || { echo "combohunttest: SKIP - candidate absent ($MACRO)"; exit $SKIP; }
W_FOUND=yes; W_CAND="$(get result candidate)"; W_PEAK="$(get result combo_peak)"; W_AFTER="$(get result after_hash)"
W_BASE="$(get base machine_hash)"; W_PHASE="$(get phase value)"; W_FRAME="$(get base machine_frame)"
for v in "$W_CAND" "$W_PEAK" "$W_AFTER" "$W_BASE" "$W_PHASE"; do
	[ "$v" = unmeasured ] && { echo "combohunttest: SKIP - the RECIPE's result is still unmeasured (run the hunt, then pin it)"; exit $SKIP; }
done

# the base: the tour's clip (David's V48 slot-0 state), like surfacetourtest / ctltest.
CLIP="${FLYCAST_TEST_CLIP:-}"
if [ -z "$CLIP" ]; then
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		ls "$(dirname "$f")"/*.state >/dev/null 2>&1 && { CLIP="$f"; break; }
	done
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "combohunttest: SKIP - no base clip with a slot-0 state"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"; GAME="$(basename "$(dirname "$SRCDIR")")"

OUT="${COMBOHUNTTEST_OUT:-$(mktemp -d)}"; [ -n "${COMBOHUNTTEST_OUT:-}" ] || trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
CLIPDIR="$OUT/data/flycast-dojo/replays/$GAME/huntclip"
mkdir -p "$OUT/cfg/flycast-dojo" "$CLIPDIR"
cp "$CLIP" "$CLIPDIR/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do { [ -f "$sib" ] && cp "$sib" "$CLIPDIR/" 2>/dev/null; } || true; done
echo "combohunttest: base clip $CLIP"
echo "combohunttest: candidate $(basename "$MACRO")"

WINDOW=""
if [ "$ARM" = window ]; then
	WINDOW="-config dojo:ComboHuntWindow=84-1500"
	echo "SABOTAGE armed: window - the macro's PRE-COMBO window 84-1500 replaces the marker window"
	echo "SABOTAGE arm window: the run below is EXPECTED to be red"
fi

DN=$((175 + ($$ % 60))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "combohunttest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "combohunttest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

# a READ boot of the clip with AutoSeekState=0 (the base), the hunt armed on the macro
# class only (the RESULT then names THIS candidate, not the movie's own tail).
# shellcheck disable=SC2086
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIPDIR/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config "dojo:SavestateFolder=$CLIPDIR" \
	-config dojo:ComboHunt=macro -config "dojo:ComboHuntMacro=$MACRO" $WINDOW \
	-config window:width=1280 -config window:height=900 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
LOG="$OUT/out.log"
RESULT=""
for _ in $(seq 1 "$WAIT_S"); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$LOG" | grep -a "COMBO HUNT RESULT:" | tail -1 | sed 's/.*N\[[A-Z]*\]: //')"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
tr -d '\0' < "$LOG" | grep -a "COMBO HUNT: base\|COMBO HUNT: candidate\|COMBO HUNT RESULT" | sed 's/.*N\[[A-Z]*\]: /  /'
cleanup
if [ -z "$RESULT" ]; then
	echo "combohunttest: SKIP - no COMBO HUNT RESULT line within ${WAIT_S}s (the hunt never spoke; last lines:)"
	tr -d '\0' < "$LOG" | grep -a "COMBO HUNT\|gui_start_game\|SAVESTATE FOLDER" | tail -4 | sed 's/^/  /'
	exit $SKIP
fi
# the fields, from the RESULT line itself
kv() { sed -n "s/.*[ :]$1=\([^ ]*\).*/\1/p" <<<"$RESULT"; }
found=$(kv found); cand=$(kv candidate); peak=$(kv peak); after=$(kv after); base=$(kv base); phase=$(kv phase)

PASSED=0; FAILED=0; SEEN="$OUT/seen"; BROKEN="$OUT/broken"; : > "$SEEN"; : > "$BROKEN"
claim() {	# claim <id> <measured> <expected> <text>
	echo "$1" >> "$SEEN"
	if [ "$2" = "$3" ]; then PASSED=$((PASSED+1)); printf '  ok   %s  %s (%s)\n' "$1" "$4" "$2"
	else FAILED=$((FAILED+1)); echo "$1" >> "$BROKEN"; printf '  FAIL %s  %s: measured %s, RECIPE %s\n' "$1" "$4" "$2" "$3"; fi
}
claim H1 "$found" "$W_FOUND" "found"
claim H2 "$cand"  "$W_CAND"  "candidate"
claim H3 "$peak"  "$W_PEAK"  "peak == [result].combo_peak"
claim H4 "$after" "$W_AFTER" "after == [result].after_hash"
claim H5 "$base"  "$W_BASE"  "base == [base].machine_hash (slot 0 @ frame $W_FRAME)"
claim H6 "$phase" "$W_PHASE" "phase == [phase].value"
echo "COMBOHUNTTEST RESULT: passed=$PASSED failed=$FAILED"

if [ -n "$ARM" ]; then
	[ -x "$ARMS" ] || { echo "combohunttest: no judge at $ARMS"; exit 2; }
	"$ARMS" judge window "the pre-combo window 84-1500" H1 H5 "$FAILED" "$SEEN" "$BROKEN"; j=$?
	case "$j" in
		0) echo "PASS combohunttest --sabotage window - the arm fired as predicted (H1 reddened: $RESULT)"; exit 0 ;;
		2) echo "INCONCLUSIVE combohunttest --sabotage window (exit 2)"; exit 2 ;;
		*) if grep -aq "^H1$" "$BROKEN"; then echo "FAIL combohunttest --sabotage window - it fired but broke its control H5 (the base moved under the arm?)"; exit 1; fi
		   echo "FAIL combohunttest --sabotage window - the arm did NOT fire: the pre-combo window still found a hit (exit 4)"; exit 4 ;;
	esac
fi
if [ "$FAILED" -gt 0 ]; then echo "FAIL combohunttest - $FAILED claim(s) red against the RECIPE"; exit 1; fi
echo "PASS combohunttest - the fixture lands on this build: $RESULT"
exit 0
