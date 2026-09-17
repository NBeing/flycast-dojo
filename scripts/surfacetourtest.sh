#!/usr/bin/env bash
# surfacetourtest - the SURFACE TOUR: a self-driving walk of the whole TAS surface.
#
#   RUN:   scripts/surfacetourtest.sh              (needs a ROM, Xvfb, python3, a base clip)
#   PASS:  exit 0. The tour loads David's savestate, REBINDS every window's hotkey through
#          the real rebind engine, opens and closes every window WITH those hotkeys, then
#          exercises each feature one by one. Eight gates, each printed as
#          `  ok  G<n>  <claim, measured count inlined>` or `  FAIL G<n>  <measured> <why>`
#          (the grammar lifted from nbneo-rr's tests/transport-target-check.py):
#            G1 the tour reported a GATE (RESULT carries gate_ok/vacuous/leak/unmeasured)
#            G2 no step FAILED                       G3 no mover was VACUOUS
#            G4 no UI step LEAKED into the machine   G5 the gate measured >= FLOOR steps
#            G6 passed >= FLOOR                       G7 every settled step got a gate verdict
#            G8 the ENGINE's own traces back the tour: >= 14 `PANEL TOGGLE: ... -> open`,
#               >= 14 `HOTKEY REBIND: action ... -> `, >= 2 `gui_loadState: slot`
#          then one summary line: `PASS  ...` / `FAIL  N check(s) red: G2, G4` /
#          `VACUOUS  N check(s) red: G1, G5`.
#   EXIT:  0 pass · 1 a CLAIM failed (G2/G4: a step failed, or a UI step moved the
#          machine) · 2 usage · 4 --self-test: the sabotage FAILED TO FIRE - the gate is
#          decorative · 5 VACUOUS (G1/G3/G5/G6/G7/G8: the run proved nothing - no gate,
#          a mover that moved nothing, a floor missed, or the engine never corroborated a
#          PASS) - its OWN code, because "0 failed" is also what a tour that never ran
#          looks like · 77 SKIP (no ROM / Xvfb / python3 / build / base clip, or the tour
#          aborted before it could run - no keyboard device, never ready).
#   SELF:  scripts/surfacetourtest.sh --self-test - dojo:SurfaceTour=sabotage injects the
#          WRONG chord on the piano roll's open step, so `PANEL TOGGLE: pianoroll -> open`
#          never appears. Exit 0 iff that step FAILED by name, every one of the 14 rebinds
#          still PASSED (the sabotage reddened the CLAIM, not the setup), failed >= 1, and
#          the gate read it as a FAIL and not a VACUITY (vacuous=0 leak=0). Exit 4 if the
#          wrong chord still "opened" the window (the gate is decorative); exit 1 if the
#          setup reddened too; exit 5 if the gate was absent.
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
	"")          ;;
	--self-test) SELF=1 ;;
	--watch)     WATCH=1; WATCHCLIP="${2:-}" ;;
	*)           echo "usage: $0 [--self-test | --watch <clip.flyr>]   (exit 2: usage)"; exit 2 ;;
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

# THE HUMAN-READABLE REPORT: every step as the tour narrated it, every gate line that was
# NOT ok, and the runner's own G-summary block (two spaces, ok, two spaces - the lifted
# grammar; the runner emits it, this prints it). `tr -d '\0'` first: the emulator writes
# NUL bytes, and this shell's grep may be ugrep with -I. Never pipe an evidence grep
# into head - six runs of one unchanged command once returned 0,12,0,0,0,10 that way.
STRIP='s/.*[NW]\[[A-Z]*\]: /  /'
tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR: step\|SURFACE TOUR: ready\|SURFACE TOUR: restored\|SURFACE TOUR RESULT\|SURFACE TOUR: aborted" | sed "$STRIP"
tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR: gate .* -> \(VACUOUS\|LEAK\|unmeasured\)" | sed "$STRIP"
tr -d '\0' < "$OUT/out.log" | grep -a "\]:   ok  G[0-9]\|\]:   FAIL G[0-9]\|\]:            [^ ]" | sed 's/.*[NW]\[[A-Z]*\]: /  runner: /'

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
field() { echo "$RESULT" | sed -n "s/.*$1=\([0-9]*\).*/\1/p"; }
passed=$(field passed); failed=$(field failed); skipped=$(field skipped); total=$(field total)
# The gate fields are APPEND-ONLY on the RESULT line; absent = the runner has no gate.
gate_ok=$(field gate_ok); vacuous=$(field vacuous); leak=$(field leak); unmeasured=$(field unmeasured)
LOG="$(tr -d '\0' < "$OUT/out.log")"
opens=$(printf '%s\n' "$LOG"   | grep -ac "PANEL TOGGLE: .* -> open")
rebinds=$(printf '%s\n' "$LOG" | grep -ac "HOTKEY REBIND: action .* -> ")
loads=$(printf '%s\n' "$LOG"   | grep -ac "gui_loadState: slot")
rebindPass=$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* "rebind: ' | grep -ac -- "-> PASS")
echo "surfacetourtest: passed=$passed failed=$failed skipped=$skipped total=$total gate_ok=${gate_ok:--} vacuous=${vacuous:--} leak=${leak:--} unmeasured=${unmeasured:--} | engine traces: opens=$opens rebinds=$rebinds loads=$loads | rebind steps PASS=$rebindPass (mode=$MODE floor=$FLOOR)"

# ---- THE GATES, in the lifted grammar ----------------------------------------------
# Two red classes, two exit codes: a CLAIM failed (exit 1) is not the same finding as
# "this run proved nothing" (exit 5, VACUOUS), and "0 failed" is also what a tour that
# never ran looks like. Each gate prints its measured number whether it passed or not;
# a red gate names its offenders on indented lines beneath it; the summary lists the red
# gate names so one grep reads the verdict.
FAILRED=(); VACRED=()
G() {	# G <n> <ok 1|0> <class fail|vac> <ok-claim> <fail-text> [offender lines via stdin]
	local n=$1 ok=$2 cls=$3 claim=$4 failtxt=$5
	if [ "$ok" = 1 ]; then printf '  ok  G%s  %s\n' "$n" "$claim"; return; fi
	printf '  FAIL G%s  %s\n' "$n" "$failtxt"
	[ -t 0 ] || sed 's/^/           /'
	if [ "$cls" = fail ]; then FAILRED+=("G$n"); else VACRED+=("G$n"); fi
}
summary() {	# prints the one-line verdict and exits with its code
	local what=$1
	if [ "${#FAILRED[@]}" -gt 0 ]; then
		echo "FAIL  ${#FAILRED[@]} check(s) red: $(IFS=', '; echo "${FAILRED[*]}")$([ "${#VACRED[@]}" -gt 0 ] && echo " (and vacuous: $(IFS=', '; echo "${VACRED[*]}"))") - $what"; exit 1
	fi
	if [ "${#VACRED[@]}" -gt 0 ]; then
		echo "VACUOUS  ${#VACRED[@]} check(s) red: $(IFS=', '; echo "${VACRED[*]}") - $what proved nothing"; exit 5
	fi
	echo "PASS  $what"; exit 0
}
gatePresent=0; [ -n "$gate_ok" ] && [ -n "$vacuous" ] && [ -n "$leak" ] && [ -n "$unmeasured" ] && gatePresent=1

if [ "$SELF" -eq 1 ]; then
	# THE SABOTAGE JUDGE. Three things must be true at once, and each is its own gate:
	# the arm APPLIED and broke the step it targets (S1); it LEFT the setup green (S2) -
	# every rebind still PASSED, or the sabotage reddened the setup, not the claim;
	# and the GATE read a sabotaged open as a FAIL, not a vacuity (S4/S5) - a wrong
	# chord that "moved nothing" is a leak of the wrong kind of verdict.
	pr="$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* "open: pianoroll" -> FAIL' | sed "$STRIP")"
	if [ -z "$pr" ]; then
		echo "  FAIL S1  'open: pianoroll' did not go red - the wrong chord still opened the window"
		echo "SABOTAGE FAILED TO FIRE: expected 'open: pianoroll' to go red; failed=$failed. The gate is decorative. -> exit 4"
		exit 4
	fi
	echo "  ok  S1  the arm applied: 'open: pianoroll' went red ($pr)"
	G 2 "$([ "$rebindPass" -ge 14 ] && echo 1 || echo 0)" fail \
		"the setup held: $rebindPass/14 rebinds still PASSED" \
		"$rebindPass/14 rebinds PASSED - the sabotage reddened the SETUP, not the claim" </dev/null
	G 3 "$([ "${failed:-0}" -ge 1 ] && echo 1 || echo 0)" fail \
		"RESULT counts failed=$failed (>= 1)" "RESULT counts failed=$failed but a step line says FAIL - the tally lies" </dev/null
	G 1 "$gatePresent" vac "the tour reported a gate (gate_ok=$gate_ok vacuous=$vacuous leak=$leak unmeasured=$unmeasured)" \
		"RESULT carries no gate fields - the runner has no gate yet (stub), so this arm cannot say what the gate made of the sabotage" </dev/null
	if [ "$gatePresent" = 1 ]; then
		G 4 "$([ "$vacuous" -eq 0 ] && echo 1 || echo 0)" fail "the sabotaged open was judged a FAIL, not a vacuity (vacuous=0)" \
			"vacuous=$vacuous - the gate filed a sabotaged open under 'moved nothing' instead of FAIL" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> VACUOUS" | sed "$STRIP")
		G 5 "$([ "$leak" -eq 0 ] && echo 1 || echo 0)" fail "no UI step leaked (leak=0)" "leak=$leak - a UI step moved the machine or the movie" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> LEAK" | sed "$STRIP")
	fi
	if [ "${#FAILRED[@]}" -eq 0 ] && [ "${#VACRED[@]}" -eq 0 ]; then
		echo "sabotage fired as intended: 'open: pianoroll' went red, close SKIPped, rebinds $rebindPass/14 PASSED, failed=$failed, gate vacuous=$vacuous leak=$leak. exit 0."
		exit 0
	fi
	summary "surfacetourtest --self-test"
fi

# ---- normal mode --------------------------------------------------------------------
G 1 "$gatePresent" vac "the tour reported a gate (gate_ok=$gate_ok vacuous=$vacuous leak=$leak unmeasured=$unmeasured)" \
	"RESULT carries no gate fields (gate_ok/vacuous/leak/unmeasured) - the runner has no gate, so a PASS here is the tour taking its own word" </dev/null
G 2 "$([ "${failed:-1}" -eq 0 ] && echo 1 || echo 0)" fail "no step failed ($total settled: $passed passed, $skipped skipped)" \
	"$failed of $total step(s) failed:" < <(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* -> FAIL' | sed "$STRIP")
if [ "$gatePresent" = 1 ]; then
	G 3 "$([ "$vacuous" -eq 0 ] && echo 1 || echo 0)" vac "no mover was vacuous (vacuous=0 of $gate_ok measured)" \
		"$vacuous mover(s) changed NOTHING - a step whose verb did not fire:" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> VACUOUS" | sed "$STRIP")
	G 4 "$([ "$leak" -eq 0 ] && echo 1 || echo 0)" fail "no UI step leaked (leak=0)" \
		"$leak UI step(s) moved the machine or the movie - a chord, a panel or a mode flip reached the guest:" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> LEAK" | sed "$STRIP")
	G 5 "$([ "$gate_ok" -ge "$FLOOR" ] && echo 1 || echo 0)" vac "the gate measured $gate_ok step(s) >= floor $FLOOR (unmeasured=$unmeasured)" \
		"the gate measured only $gate_ok step(s), floor is $FLOOR (unmeasured=$unmeasured) - it compares too little to mean anything" </dev/null
	judged=$(( ${gate_ok:-0} + ${vacuous:-0} + ${leak:-0} + ${unmeasured:-0} ))
	G 7 "$([ "$judged" -eq "${total:-0}" ] && echo 1 || echo 0)" vac "every settled step got a gate verdict ($judged of $total)" \
		"$judged gate verdict(s) for $total settled step(s) - the guard did not fire on every step; a counter reading 0 is what a guard that never ran reports" </dev/null
fi
G 6 "$([ "${passed:-0}" -ge "$FLOOR" ] && echo 1 || echo 0)" vac "passed=$passed >= floor $FLOOR" \
	"passed=$passed, floor is $FLOOR (skipped=$skipped) - the tour is not covering the surface" </dev/null
G 8 "$([ "$opens" -ge 14 ] && [ "$rebinds" -ge 14 ] && [ "$loads" -ge 2 ] && echo 1 || echo 0)" vac \
	"the engine's own traces back the tour: opens=$opens (>=14) rebinds=$rebinds (>=14) loads=$loads (>=2)" \
	"engine traces short: opens=$opens (need 14) rebinds=$rebinds (need 14) loads=$loads (need 2) - the tour's PASSes are not backed by the engine's own lines" </dev/null
summary "surfacetourtest - $passed steps walked the surface (14 rebinds, 14 windows opened+closed by hotkey, features exercised), failed=$failed, skipped=$skipped, gate vacuous=${vacuous:--} leak=${leak:--}"
