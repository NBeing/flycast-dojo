#!/usr/bin/env bash
# statesuitest - does the States panel's generations pane respond to a real
#                mouse click, and does it tell CLICK from CTRL+CLICK?
#
#   RUN:   scripts/statesuitest.sh
#   PASS:  a plain click on a generation's tag cell opens the TAGS editor, and a
#          Ctrl+click on the same cell opens the NOTES editor. Exit 0.
#   FAIL:  exit 1, naming the claim.
#   SKIP:  exit 77 (no Xvfb / xdotool / i3 / ROM / clip with a savestate / build).
#   SELF:  scripts/statesuitest.sh --self-test - drives the SAME clicks with the
#          modifier never pressed, and requires the Ctrl claim to fail.
#
# THE HOTKEY IS READ, NOT ASSUMED. The panel's binding is configurable, so this
# asks the emulator what it is (scripts/lib/hotkeys.sh) rather than carrying a
# second copy of that fact - CLAUDE.md rule 4. It opens the panel with
# `dojo:Panel.states=yes` all the same, and that is a LIMITATION worth stating:
#
#   `[MEASURED 2026-09-13]` driving the panel open with its real binding was
#   built and does not work in this harness. Clicks land and keys do not - not
#   even a HOTKEY trace - because a synthetic click is delivered by POSITION
#   while a key needs input FOCUS, and `xdotool search --name Flycast` returns
#   nothing here so windowactivate is a no-op. The clicks kept working anyway
#   because i3 places the window at 0,0 and the coordinates happen to land, so
#   the harness was half-connected and looked fine.
#
#   scripts/hotkeytest.sh DOES drive keys successfully and is where key-driven
#   paths are covered. Splitting them is not ideal and is not pretended to be.
#
# WHY THIS EXISTS. `[MEASURED 2026-09-13]` scripts/statestest.sh drives NO input
# at all - it reads traces - and says so deliberately. So the States panel, which
# is the surface a TAS artist actually clicks to name and annotate a state, had
# every one of its behaviours checked except the clicking.
#
# THE CLAIM WORTH TESTING IS THE MODIFIER. `[SOURCE]` states_panel.cpp draws the
# cell and then says "click to tag, Ctrl+click to annotate" - one cell, two
# destinations, chosen by `ImGui::GetIO().KeyCtrl`. A harness that forgets the
# modifier, or a build that stops reading it, produces the SAME observable for
# both gestures. That is why the plain-click claim alone would be worthless, and
# why --self-test releases the modifier rather than breaking the judge.
#
# ONE PANEL, and that is not tidiness. `[MEASURED 2026-09-10]` opening a second
# panel put two in the same dock node as TABS, the covered one stopped drawing,
# and its harness reported a feature unwired that was merely off screen.
set -uo pipefail

SKIP=77
SELFTEST=0
[ "${1:-}" = "--self-test" ] && SELFTEST=1

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
. "$ROOT/scripts/lib/hotkeys.sh"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
DISP="${STATESUI_DISPLAY:-:151}"
OUT="$(mktemp -d)"

XPID=0; IPID=0; FC=0
cleanup() {
	# PID-SCOPED, ALL THREE. `[SOURCE]` dc053dfeb - killing i3 or Xvfb by name on
	# a developer's own machine drops them to a login screen, and this script
	# runs on one.
	for p in $FC $IPID $XPID; do [ "$p" -ne 0 ] && kill "$p" 2>/dev/null; done
	sleep 1
	for p in $FC $IPID $XPID; do [ "$p" -ne 0 ] && kill -0 "$p" 2>/dev/null && kill -9 "$p" 2>/dev/null; done
	[ "${STATESUI_KEEP:-0}" = 1 ] || rm -rf "$OUT"
	return 0
}
trap cleanup EXIT

for t in Xvfb xdotool i3; do
	command -v "$t" >/dev/null || { echo "statesuitest: SKIP - no $t"; exit $SKIP; }
done
[ -x "$EXE" ] || { echo "statesuitest: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "statesuitest: SKIP - no ROM"; exit $SKIP; }

CLIP=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	CLIP="$f"; break
done
[ -n "$CLIP" ] || { echo "statesuitest: SKIP - no clip with a savestate"; exit $SKIP; }

# EVERY EXIT PATH TEARS DOWN ITS TRIO.
#
# `[MEASURED 2026-09-13, by a peer session]` the early return below - taken when
# no STATES CELL line appears - skipped the kill at the bottom of gesture(), and
# the next gesture() then overwrote FC/IPID/XPID, so the EXIT trap only ever saw
# the SECOND trio. Ten flycast processes and one Xvfb ran at 100% CPU for twelve
# hours on a twelve-core machine; load average reached 73 and swap filled.
#
# THE RULE IS GENERAL: a return that does not pass through teardown() is a leak.
# Hoisted out of gesture() rather than nested in it, so it exists before the
# first call and cannot be half-defined on an early exit.
teardown() {
	for p in $FC $IPID $XPID; do [ "$p" -ne 0 ] && kill "$p" 2>/dev/null; done
	sleep 2
	for p in $FC $IPID $XPID; do [ "$p" -ne 0 ] && kill -0 "$p" 2>/dev/null && kill -9 "$p" 2>/dev/null; done
	FC=0; IPID=0; XPID=0
}

# ONE LAUNCH PER GESTURE, which is statestest.sh's shape and for a sharper
# reason here. `[MEASURED 2026-09-13]` doing both gestures in one process
# coupled them: the first click leaves the cell as an InputText, so the second
# lands in a text box rather than on a label and opens nothing. Dismissing it
# first needed a third gesture, and every candidate had its own problem -
# Escape is flycast's MENU toggle, a neutral click lands in the same text box,
# Return commits but moves focus. Each workaround added an assumption the test
# then depended on. A fresh process per gesture has none of them: the claim
# becomes "this gesture, on an untouched cell, opens that editor", which is
# exactly what the user does.
#
# $1 = ctrl|plain. Echoes the STATES EDIT line, or nothing.
gesture() {
	local mode="$1"
	local w="$OUT/$mode"
	mkdir -p "$w/config/flycast-dojo" "$w/data" "$w/clip"
	cp "$CLIP" "$w/clip/clip.flyr"
	for sib in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* "$(dirname "$CLIP")"/clip.json; do
		{ [ -f "$sib" ] && cp "$sib" "$w/clip/" 2>/dev/null; } || true
	done

	nohup Xvfb "$DISP" -screen 0 1600x1300x24 >"$w/xvfb.log" 2>&1 & XPID=$!
	sleep 2
	# DID OUR OWN Xvfb SURVIVE? If the display is already owned - by a leaked
	# server from an earlier run, which is exactly what this script used to
	# leave behind - Xvfb exits immediately and the emulator then attaches to a
	# display THIS RUN DID NOT CREATE and does not control. Everything after
	# would be measuring somebody else's screen. Same shape as hotkeytest
	# treating an empty window handle as a SKIP rather than continuing.
	if ! kill -0 "$XPID" 2>/dev/null; then
		echo "statesuitest: SKIP - Xvfb $DISP did not start (display already in use?)"
		sed -n '1,3p' "$w/xvfb.log" 2>/dev/null | sed 's/^/    /'
		teardown
		exit $SKIP
	fi
	DISPLAY="$DISP" nohup i3 -c "$OUT/i3.conf" >"$w/i3.log" 2>&1 & IPID=$!
	sleep 2
	XDG_CONFIG_HOME="$w/config" XDG_DATA_HOME="$w/data" DISPLAY="$DISP" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:Replay=yes -config "dojo:ReplayFilename=$w/clip/clip.flyr" \
		-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
		-config dojo:Panel.states=yes -config dojo:StatesTrace=yes \
		-config dojo:HotkeyTrace=yes \
		-config dojo:StatesGenProbe=yes \
		"$ROM" > "$w/out.log" 2>&1 &
	FC=$!
	sleep 22

	export DISPLAY="$DISP"
	local cell cx cy wid geom wx wy
	cell=$(tr -d '\0' < "$w/out.log" | grep -a "STATES CELL: gen" | tail -1)
	cx=$(echo "$cell" | sed -n 's/.*x=\([0-9-]*\).*/\1/p')
	cy=$(echo "$cell" | sed -n 's/.*y=\([0-9-]*\).*/\1/p')
	GENSN=$(tr -d '\0' < "$w/out.log" | grep -a "STATES GENPROBE" | tail -1 | sed 's/.*count [0-9]* -> \([0-9]*\).*/\1/')
	# RESULTS GO TO FILES, NOT STDOUT. `[MEASURED 2026-09-13]` the caller used
	# `X=$(gesture ...)`, which runs the function in a SUBSHELL - so everything
	# it set beyond its stdout was discarded and the caller read unbound
	# variables. Files cross that boundary; a command substitution does not.
	echo "$cell" > "$w.cell"
	# WHAT THE EMULATOR SAYS IS BOUND, read from its own output rather than
	# assumed here - see scripts/lib/hotkeys.sh for why a harness must not carry
	# its own copy of a configurable fact.
	hk_binding "$w/out.log" "States Window" > "$w.hkey"
	hk_keyboard_mappings "$w/out.log" | wc -l > "$w.nkbd"
	# DISTINCT DEVICES, not lines: the emulator re-logs the whole table whenever
	# mappings reload, so counting matches reported "2 devices, 4 unbound".
	{ tr -d '\0' < "$w/out.log" \
		| sed -n 's/.*HOTKEY BOUND: \[\([^]]*\)\] *States Window  *unbound.*/\1/p' \
		| sort -u | wc -l; } > "$w.unbound"
	echo "${GENSN:-0}" > "$w.gens"
	: > "$w.edit"
	if [ -z "$cx" ]; then teardown; return; fi

	wid=$(xdotool search --name "Flycast" 2>/dev/null | head -1)
	geom=$(xdotool getwindowgeometry "$wid" 2>/dev/null | grep Position | head -1)
	wx=$(echo "$geom" | sed 's/.*Position: \([0-9-]*\),.*/\1/'); [ -n "$wx" ] || wx=0
	wy=$(echo "$geom" | sed 's/.*Position: [0-9-]*,\([0-9-]*\).*/\1/'); [ -n "$wy" ] || wy=0
	xdotool windowactivate "$wid" 2>/dev/null; sleep 1
	xdotool mousemove $((wx + cx)) $((wy + cy)); sleep 0.5

	# DOWN, WAIT, UP - never `xdotool click`. `[SOURCE]` scripts/rolltest.sh,
	# 2026-09-09: a click sends press and release faster than one emulated frame,
	# and the host samples mouse STATE per frame, so the gesture falls between
	# two polls and is never seen. The mouse hovers correctly and nothing
	# happens, which reads as a dead feature. This harness lost a run to it
	# before reusing the lesson.
	[ "$mode" = ctrl ] && { xdotool keydown ctrl; sleep 0.3; }
	xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 1.5
	[ "$mode" = ctrl ] && { xdotool keyup ctrl; sleep 0.3; }

	tr -d '\0' < "$w/out.log" | grep -a "STATES EDIT:" | tail -1 > "$w.edit"

	teardown
}

printf 'font pango:monospace 8\n' > "$OUT/i3.conf"
fails=0
claim() {	# $1 name, $2 ok(0/1), $3 detail
	if [ "$2" -eq 1 ]; then echo "  PASS  $1  ${3:-}"
	else echo "  FAIL  $1  ${3:-}"; fails=$((fails+1)); fi
}

# THE DISCRIMINATING GESTURE FIRST. `--self-test` runs this same arm with the
# modifier never pressed and requires it to come back as tags, so a build that
# stopped reading KeyCtrl cannot pass both.
CTRLMODE=ctrl
[ "$SELFTEST" -eq 1 ] && CTRLMODE=plain
gesture "$CTRLMODE"
CTRL=$(cat "$OUT/$CTRLMODE.edit" 2>/dev/null || true)
CELLLINE=$(cat "$OUT/$CTRLMODE.cell" 2>/dev/null || true)
GENS=$(cat "$OUT/$CTRLMODE.gens" 2>/dev/null || echo 0)

# ---- what is this panel's hotkey, according to the emulator? --------------
HKEY=$(cat "$OUT/$CTRLMODE.hkey" 2>/dev/null || true)
NKBD=$(cat "$OUT/$CTRLMODE.nkbd" 2>/dev/null || echo 0)
NUNB=$(cat "$OUT/$CTRLMODE.unbound" 2>/dev/null || echo 0)
claim "the States window has a nameable hotkey" \
	"$([ -n "$HKEY" ] && echo 1 || echo 0)" \
	"${HKEY:-<unbound or not logged>} on 1 of ${NKBD} keyboard(s); ${NUNB} device(s) have it unbound"

claim "the generations pane has a row to click" "$([ "${GENS:-0}" -ge 1 ] && echo 1 || echo 0)" "count=${GENS:-0}"
claim "the panel published a clickable cell rect" "$([ -n "$CELLLINE" ] && echo 1 || echo 0)" "${CELLLINE:-<no STATES CELL line>}"

CTRLOK=0
echo "$CTRL" | grep -q "col=1" && CTRLOK=1
if [ "$SELFTEST" -eq 1 ]; then
	if [ "$CTRLOK" -eq 0 ]; then
		echo "  SELF  ok    with no modifier held the notes editor did NOT open: ${CTRL:-<nothing>}"
		echo "statesuitest: PASS --self-test - the Ctrl claim can fail"
		exit 0
	fi
	echo "  SELF  WRONG the notes editor opened with NO modifier held: $CTRL"
	echo "statesuitest: FAIL --self-test - the claim cannot distinguish the gesture"
	exit 1
fi
claim "Ctrl+click opens the notes editor" "$CTRLOK" "${CTRL:-<nothing>}"

gesture plain
PLAIN=$(cat "$OUT/plain.edit" 2>/dev/null || true)
PLAINOK=0
echo "$PLAIN" | grep -q "col=0" && PLAINOK=1
claim "a plain click opens the tags editor" "$PLAINOK" "${PLAIN:-<nothing>}"

[ "$fails" -eq 0 ] && { echo "statesuitest: PASS - the cell tells click from Ctrl+click"; exit 0; }
echo "statesuitest: FAIL - $fails claim(s)"
exit 1
