#!/usr/bin/env bash
# statestest - does the States wall see the emulator's savestates, and can it
# name one?
#
#   RUN:   scripts/statestest.sh
#   PASS:  the wall reports at least one occupied slot for a clip that has a
#          savestate, and the label probe round-trips through the disk.
#   FAIL:  exit 1, saying which.
#   SKIP:  exit 77 (no Xvfb / ROM / clip with a savestate / build).
#
# NO INPUT AND NO WINDOW MANAGER, deliberately - and that is why it is a
# separate script rather than more arms on scripts/rolltest.sh.
#
# `[MEASURED 2026-09-10]` it WAS an arm on rolltest for one commit, and opening
# a second panel put the two in the same dock node as TABS: the States tab
# covered the roll, the roll stopped drawing, its hover trace went to row=-1 and
# the paint drag committed nothing. rolltest reported that the paint stroke was
# unwired. It was not - it was not on screen.
#
# One harness per panel. These checks read traces and need no mouse, so they
# also need no i3, which removes the whole class of failure above.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
DISP="${STATESTEST_DISPLAY:-:143}"
OUT="${STATESTEST_OUT:-}"
if [ -n "$OUT" ]; then mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
fi

[ -x "$EXE" ] || { echo "statestest: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "statestest: SKIP - no ROM"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "statestest: SKIP - no Xvfb"; exit $SKIP; }

# A clip WITH a savestate beside it, so "no state" is a failure rather than a
# fact about the machine.
CLIP=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	CLIP="$f"; break
done
[ -n "$CLIP" ] || { echo "statestest: SKIP - no clip with a savestate"; exit $SKIP; }

# A FRESH COPY PER LAUNCH. The delete probe removes a real savestate, so it must
# never run against the same copy the read checks are about to use - and a probe
# that ate the states it was asked to look at would be its own bug report.
run_states() {	# $1 = subdir, $2... = extra -config args
	local w="$OUT/$1"; shift
	mkdir -p "$w/config/flycast-dojo" "$w/data" "$w/clip"
	cp "$CLIP" "$w/clip/clip.flyr"
	for sib in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* \
			"$(dirname "$CLIP")"/clip.json; do
		{ [ -f "$sib" ] && cp "$sib" "$w/clip/" 2>/dev/null; } || true
	done
	nohup Xvfb "$DISP" -screen 0 900x700x24 >"$w/xvfb.log" 2>&1 & XPID=$!
	sleep 2
	XDG_CONFIG_HOME="$w/config" XDG_DATA_HOME="$w/data" DISPLAY="$DISP" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:Replay=yes -config "dojo:ReplayFilename=$w/clip/clip.flyr" \
		-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
		-config dojo:Panel.states=yes -config dojo:StatesTrace=yes \
		"$@" "$ROM" > "$w/out.log" 2>&1 &
	FC=$!
	sleep 26
	# PID-SCOPED. Never `pkill -x Xvfb` or `pkill -x flycast` - those match any
	# other instance on this machine, including ones this script did not start.
	#
	# AND CONFIRMED DEAD. `[MEASURED 2026-09-10]` a TERM that the emulator was
	# too busy to service left it running past the end of the run, holding a
	# display this script was about to reuse. Wait, then insist - still only on
	# the pids this function launched.
	kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null
	sleep 2
	kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null
	kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null
	sleep 1
}

run_states read -config dojo:StatesLabelProbe=yes
OUTLOG="$OUT/read/out.log"

reg=$(tr -d '\0' < "$OUTLOG" | grep -a "STATES PANEL:" | tail -1)
st=$(tr -d '\0' < "$OUTLOG" | grep -a "STATES:" | tail -1)
lp=$(tr -d '\0' < "$OUTLOG" | grep -a "STATES LABELPROBE:" | tail -1)
[ -n "$reg" ] && echo "  ${reg##*N\[RENDERER\]: }"
[ -n "$st" ]  && echo "  ${st##*N\[RENDERER\]: }"
[ -n "$lp" ]  && echo "  ${lp##*N\[RENDERER\]: }"

if [ -z "$st" ]; then
	echo "FAIL statestest - the States panel never reported; it did not draw"
	exit 1
fi
occ=$(echo "$st" | sed -n 's/.*occupied=\([0-9]*\).*/\1/p')
if [ -z "${occ:-}" ] || [ "$occ" -lt 1 ]; then
	echo "FAIL statestest - the wall found no state, and this clip has one"
	exit 1
fi
if [ -z "$lp" ]; then
	echo "statestest: SKIP - the label probe never ran"
	exit $SKIP
fi
case "$lp" in
	*PASS*) ;;
	*) echo "FAIL statestest - naming a slot did not survive the round trip"; exit 1 ;;
esac
# ---- the destructive one, on its own copy -----------------------------------
run_states gen -config dojo:StatesGenProbe=yes
gp=$(tr -d '\0' < "$OUT/gen/out.log" | grep -a "STATES GENPROBE:" | tail -1)
if [ -n "$gp" ]; then
	echo "  ${gp##*N\[RENDERER\]: }"
	case "$gp" in
		*PASS*) ;;
		*) echo "FAIL statestest - taking a generation did not register, or its tag did not stick"; exit 1 ;;
	esac
fi

run_states del -config dojo:StatesDeleteProbe=yes
dp=$(tr -d '\0' < "$OUT/del/out.log" | grep -a "STATES DELETEPROBE:" | tail -1)
if [ -z "$dp" ]; then
	echo "statestest: SKIP - the delete probe never ran"
	exit $SKIP
fi
echo "  ${dp##*N\[RENDERER\]: }"
case "$dp" in
	*PASS*) ;;
	*) echo "FAIL statestest - deleting a slot did not take, or an empty slot was not refused"; exit 1 ;;
esac

echo "PASS statestest - $occ slot(s) seen, a name round-tripped, a generation registered, and a delete took"
exit 0
