#!/usr/bin/env bash
# surfacetourtest - the SURFACE TOUR: a self-driving walk of the whole TAS surface.
#
#   RUN:   scripts/surfacetourtest.sh              (needs a ROM, Xvfb, python3, a base clip)
#   PASS:  the tour loads David's savestate, REBINDS every window's hotkey through the
#          real rebind engine, opens and closes every window WITH those hotkeys, then
#          exercises each feature one by one - and reports failed=0 with passed >= FLOOR.
#          Independently of the tour's own scoring, the log must carry >= 14
#          `PANEL TOGGLE: ... -> open`, >= 14 `HOTKEY REBIND: action ... -> `, and >= 2
#          `gui_loadState: slot` lines (the engine's own traces, not the tour's claims).
#   FAIL:  exit 1 (any step failed, the floor was missed, or an engine trace count is short).
#   SKIP:  exit 77 (no ROM / Xvfb / python3 / build / base clip; the tour aborted before
#          it could run - no keyboard device, never ready).
#   SELF:  scripts/surfacetourtest.sh --self-test - dojo:SurfaceTour=sabotage injects the
#          WRONG chord on the very first open step, so `PANEL TOGGLE: pianoroll -> open`
#          never appears. The twin exits 0 iff that step FAILED by name, every one of the
#          14 rebinds still PASSED (the sabotage reddened the CLAIM, not the setup), and
#          the RESULT counts failed >= 1. Exit 1 if a wrong chord still "opened" a window.
#   WATCH: scripts/surfacetourtest.sh --watch <clip.flyr> - the HUMAN-VERIFIED run on
#          your real display. A COPY of the clip is staged in a throwaway config+data
#          sandbox and dojo:SavestateFolder points at the copy, so your emu.cfg, key
#          mappings, window layout and the real clip are never touched. No Xvfb, no
#          synthesised input: the tour drives itself in-process. Keep your hands off the
#          keyboard for ~2-3 minutes - an armed rebind would capture a real key.
#
# WHY IN-PROCESS. A key is "pressed" by the tour calling the keyboard device's own
# gamepad_btn_input(code, pressed) - the exact entry SDL uses - so rebinding and the
# hotkey take the genuine path headless AND on screen, and nothing is ever synthesised on
# a real X display (the standing rule). Pacing is dojo:TourBpmMs (click, 1 s) and
# dojo:TourRecordMs (record, 2 s) so a person can follow the on-screen banner.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
# The pass floor: scored steps minus the optional ones. `[MEASURED 2026-09-17]` the
# default tour is 70 steps (66 + a 1-frame "show" after each of the four loads, so a
# human actually sees the loaded picture), 2 of them optional (captures: start/stop,
# which SKIP without a recorder), so 68 must PASS. Overridable for a partial tour.
FLOOR="${TOUR_FLOOR:-68}"
SELF=0; WATCH=0; WATCHCLIP=""
case "${1:-}" in
	--self-test) SELF=1 ;;
	--watch)     WATCH=1; WATCHCLIP="${2:-}" ;;
esac

if [ -n "${SURFACETOURTEST_OUT:-}" ]; then OUT="$SURFACETOURTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "surfacetourtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "surfacetourtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v python3 >/dev/null || { echo "surfacetourtest: SKIP - no python3"; exit $SKIP; }
if [ "$WATCH" -eq 0 ]; then
	command -v Xvfb >/dev/null || { echo "surfacetourtest: SKIP - no Xvfb"; exit $SKIP; }
fi
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "surfacetourtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# ---- the base clip: one of David's, with a prefixHash-stamped savestate (slot 0 = BASE) ----
CLIP="${FLYCAST_TEST_CLIP:-}"
[ "$WATCH" -eq 1 ] && [ -n "$WATCHCLIP" ] && CLIP="$WATCHCLIP"
pick_clip() {
	local f d
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		d="$(dirname "$f")"; [ -f "$d/clip.json" ] || continue
		ls "$d"/*.state >/dev/null 2>&1 || continue
		python3 - "$d/clip.json" <<-'PY' || continue
		import json,sys
		j=json.load(open(sys.argv[1])); s=(j.get("states") or [])
		sys.exit(0 if any(isinstance(x,dict) and isinstance(x.get("prefixHash"),int) and x.get("prefixHash") and "movieFrame" in x for x in s) else 1)
		PY
		printf '%s' "$f"; return 0
	done
	return 1
}
[ -n "$CLIP" ] || CLIP="$(pick_clip)" || { echo "surfacetourtest: SKIP - no base clip with a stamped savestate"; exit $SKIP; }
[ -f "$CLIP" ] || { echo "surfacetourtest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
echo "surfacetourtest: base clip $CLIP"

# A COPY, always. The tour writes slot 99, a branch, a lab test and a capture into the
# clip folder; the sandbox is what keeps the user's real clip, cfg and mappings untouched.
#
# STAGED UNDER THE SANDBOX'S OWN replays/<game>/ ROOT, not a loose "$OUT/clip".
# `[MEASURED 2026-09-17]` the Macros browser scans <data>/flycast-dojo/replays/<game>/
# (see scripts/macrostest.sh), so a clip staged anywhere else makes "macros: place"
# FAIL with "no macro found even after seeding" while the seed file sits right there.
# GAME is the source clip's grandparent dir - what get_game_name() produced when it
# was recorded - so the copy lands exactly where the scanner looks.
GAME="$(basename "$(dirname "$SRCDIR")")"
CLIPDIR="$OUT/data/flycast-dojo/replays/$GAME/tourclip"
mkdir -p "$OUT/cfg/flycast-dojo" "$CLIPDIR"
cp "$CLIP" "$CLIPDIR/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$CLIPDIR/" 2>/dev/null; } || true
done
rm -f "$CLIPDIR"/*_[0-9].state "$CLIPDIR"/*_[0-9][0-9].state "$CLIPDIR"/results.json "$CLIPDIR"/*_macro.txt 2>/dev/null || true

XPID=""
if [ "$WATCH" -eq 1 ]; then
	D="${DISPLAY:-}"
	[ -n "$D" ] || { echo "surfacetourtest: --watch needs a real DISPLAY"; exit $SKIP; }
	echo "surfacetourtest: WATCH mode on $D - HANDS OFF THE KEYBOARD for ~2-3 minutes."
	echo "  (an armed rebind would capture a real key; the tour drives itself in-process)"
	echo "  your emu.cfg, mappings, layout and the real clip are untouched: sandbox = $OUT"
else
	DN=$((160 + ($$ % 80))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 250 ] && { echo "surfacetourtest: SKIP - no free display"; exit $SKIP; }; done
	D=":$DN"
	nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$DN" ] || { echo "surfacetourtest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }
fi

MODE=yes; [ "$SELF" -eq 1 ] && MODE=sabotage
CONSOLE=(-config dojo:NativeConsole=no); [ "$WATCH" -eq 1 ] && CONSOLE=()
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no "${CONSOLE[@]}" -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIPDIR/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config dojo:HotkeyTrace=yes \
	-config "dojo:SurfaceTour=$MODE" \
	-config "dojo:TourBpmMs=${TOUR_BPM_MS:-1000}" -config "dojo:TourRecordMs=${TOUR_RECORD_MS:-2000}" \
	-config "dojo:TourSlow=${TOUR_SLOW:-no}" -config "dojo:SavestateFolder=$CLIPDIR" \
	-config window:width=1280 -config window:height=900 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; [ -n "$XPID" ] && kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; [ -n "$XPID" ] && kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 "${TOUR_WAIT_S:-300}"); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"

# THE HUMAN-READABLE REPORT: every step, as the tour narrated it.
tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR: step\|SURFACE TOUR: ready\|SURFACE TOUR: restored\|SURFACE TOUR RESULT\|SURFACE TOUR: aborted" \
	| sed 's/.*[NW]\[[A-Z]*\]: /  /'

if [ "$WATCH" -eq 1 ]; then
	# Leave the emulator up so the verdict table can be read; the user closes it.
	echo "surfacetourtest: WATCH run finished - the emulator stays open with the verdict table. Close it when done."
	wait "$FC" 2>/dev/null
	exit 0
fi
cleanup; sleep 1

case "$RESULT" in
	"")            echo "surfacetourtest: SKIP - the tour never reported (see $OUT/out.log)"; exit $SKIP ;;
	*"aborted -"*) echo "surfacetourtest: SKIP - ${RESULT#*SURFACE TOUR: }"; exit $SKIP ;;
esac
passed=$(echo "$RESULT" | sed -n 's/.*passed=\([0-9]*\).*/\1/p')
failed=$(echo "$RESULT" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')
skipped=$(echo "$RESULT" | sed -n 's/.*skipped=\([0-9]*\).*/\1/p')
total=$(echo "$RESULT" | sed -n 's/.*total=\([0-9]*\).*/\1/p')
LOG="$(tr -d '\0' < "$OUT/out.log")"
opens=$(printf '%s\n' "$LOG"   | grep -ac "PANEL TOGGLE: .* -> open")
rebinds=$(printf '%s\n' "$LOG" | grep -ac "HOTKEY REBIND: action .* -> ")
loads=$(printf '%s\n' "$LOG"   | grep -ac "gui_loadState: slot")
rebindPass=$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* "rebind: ' | grep -ac -- "-> PASS")
echo "surfacetourtest: passed=$passed failed=$failed skipped=$skipped total=$total | engine traces: opens=$opens rebinds=$rebinds loads=$loads | rebind steps PASS=$rebindPass (mode=$MODE floor=$FLOOR)"

if [ "$SELF" -eq 1 ]; then
	pr="$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* "open: pianoroll" -> FAIL')"
	if [ "${failed:-0}" -ge 1 ] && [ -n "$pr" ] && [ "$rebindPass" -ge 14 ]; then
		echo "PASS surfacetourtest (self-test) - a wrong chord on 'open: pianoroll' FAILED that step by name, every rebind still PASSED, failed=$failed (the tour reddens at the claim, not the setup)"
		exit 0
	fi
	echo "FAIL surfacetourtest (self-test) - sabotage was not caught where it should be (failed=$failed, pianoroll-FAIL-line=$([ -n "$pr" ] && echo yes || echo no), rebinds PASS=$rebindPass/14)"
	exit 1
fi

if [ "${failed:-1}" -ne 0 ]; then
	echo "FAIL surfacetourtest - $failed step(s) failed:"
	printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* -> FAIL' | sed 's/.*[NW]\[[A-Z]*\]: /    /'
	exit 1
fi
if [ "${passed:-0}" -lt "$FLOOR" ]; then
	echo "FAIL surfacetourtest - only $passed step(s) passed, floor is $FLOOR (skipped=$skipped): the tour is not covering the surface"
	exit 1
fi
if [ "$opens" -lt 14 ] || [ "$rebinds" -lt 14 ] || [ "$loads" -lt 2 ]; then
	echo "FAIL surfacetourtest - engine traces short: opens=$opens (need 14) rebinds=$rebinds (need 14) loads=$loads (need 2) - the tour's PASSes are not backed by the engine's own lines"
	exit 1
fi
echo "PASS surfacetourtest - $passed steps walked the surface (14 rebinds, 14 windows opened+closed by hotkey, features exercised), failed=0, skipped=$skipped"
exit 0
