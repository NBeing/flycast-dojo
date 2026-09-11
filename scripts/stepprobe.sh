#!/usr/bin/env bash
# stepprobe - how fast can a machine be advanced a frame at a time from the
# deferred point, and what does the time go on?
#
#   RUN:   scripts/stepprobe.sh [frames]          (default 30)
#   PASS:  prints one table row per configuration; the THREADED rows must
#          advance every frame, the SINGLE-THREADED row must advance none.
#   FAIL:  exit 1 if either of those two expectations is violated - both are
#          load-bearing claims in docs/STEP-GRANULARITY.md, and a silent change
#          to either would invalidate the document rather than this script.
#   SKIP:  exit 77 (no Xvfb / ROM / long clip / build).
#
# This is a MEASUREMENT harness, not a regression test: the milliseconds are
# expected to move between machines. What it asserts is the SHAPE - threaded
# advances, single-threaded does not, and batching gains nothing - because those
# three are what the document concludes from.
#
# `[MEASURED 2026-09-10]` first run: 94.11 ms mean per frame threaded, 0 of 30
# single-threaded, and one start with a 30-frame target advanced exactly ONE
# frame in ten seconds.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FRAMES="${1:-30}"
BASEDISP="${STEPPROBE_DISPLAY:-160}"

[ -x "$EXE" ] || { echo "stepprobe: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "stepprobe: SKIP - no ROM"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "stepprobe: SKIP - no Xvfb"; exit $SKIP; }

# A clip long enough that the movie is still playing when the probe fires. A
# short one leaves the emulator in GuiState::ReplayEnd and the probe would be
# measuring a machine with nothing to advance.
CLIP=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	n=$("$ROOT/scripts/flyrframes.sh" "$f" 2>/dev/null) || continue
	[ "$n" -ge 3600 ] || continue
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	CLIP="$f"; break
done
[ -n "$CLIP" ] || { echo "stepprobe: SKIP - no clip long enough with a savestate"; exit $SKIP; }

OUT="${STEPPROBE_OUT:-}"
if [ -n "$OUT" ]; then mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
fi

n=0
run() {	# $1 label, then extra -config args
	local label="$1"; shift
	local w="$OUT/$label"; mkdir -p "$w/config/flycast-dojo" "$w/data" "$w/clip"
	cp "$CLIP" "$w/clip/clip.flyr"
	n=$((n + 1))
	local disp=":$((BASEDISP + n))"
	nohup Xvfb "$disp" -screen 0 640x480x24 -nolisten tcp >"$w/xvfb.log" 2>&1 & local XP=$!
	sleep 2
	XDG_CONFIG_HOME="$w/config" XDG_DATA_HOME="$w/data" DISPLAY="$disp" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:Replay=yes -config "dojo:ReplayFilename=$w/clip/clip.flyr" \
		-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
		-config "dojo:StepProbe=$FRAMES" "$@" \
		"$ROM" > "$w/out.log" 2>&1 & local FP=$!
	# Long enough for the boot, the probe's own gate (frame 120), and its worst
	# case - the batch arm deliberately waits out a 10 s timeout.
	sleep 50
	# PID-SCOPED, and confirmed dead. Never `pkill -x Xvfb`.
	kill "$FP" 2>/dev/null; kill "$XP" 2>/dev/null
	sleep 2
	kill -0 "$FP" 2>/dev/null && kill -9 "$FP" 2>/dev/null
	kill -0 "$XP" 2>/dev/null && kill -9 "$XP" 2>/dev/null
	tr -d '\0' < "$w/out.log" | grep -a "STEP PROBE" | sed "s/.*N\[COMMON\]: /  /" \
		| sed "s/^/[$label] /"
	# Flattened to a file first: a `tr | grep -q` pipeline under pipefail
	# reports failure when the match succeeds early (see hotkeytest.sh), which
	# here would print "it never fired" about a probe that did.
	tr -d '\0' < "$w/out.log" > "$w/flat.log"
	grep -aq "STEP PROBE" "$w/flat.log" \
		|| echo "[$label]   (no probe line - it never fired)"
}

echo "stepprobe: $FRAMES frames, clip $(basename "$CLIP")"
run threaded  -config config:rend.ThreadedRendering=yes
run single    -config config:rend.ThreadedRendering=no
run batch     -config config:rend.ThreadedRendering=yes -config dojo:StepProbeBatch=yes
run noaudio   -config config:rend.ThreadedRendering=yes -config config:audio.backend=null

rc=0
# THE TWO SHAPE CLAIMS. Not the milliseconds - those are a fact about the
# machine - but the two outcomes docs/STEP-GRANULARITY.md reasons from.
# ONE grep each, for the pipefail reason above.
grep -aq "STEP PROBE:.*$FRAMES/$FRAMES frames advanced" "$OUT/threaded/out.log" 2>/dev/null \
	|| { echo "FAIL stepprobe - threaded did not advance every frame"; rc=1; }
grep -aq "STEP PROBE:.*0/$FRAMES frames advanced" "$OUT/single/out.log" 2>/dev/null \
	|| { echo "FAIL stepprobe - single-threaded advanced a frame; the document says it cannot"; rc=1; }
[ $rc -eq 0 ] && echo "PASS stepprobe - threaded advances every frame, single-threaded advances none"
exit $rc
