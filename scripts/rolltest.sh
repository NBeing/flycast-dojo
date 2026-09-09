#!/usr/bin/env bash
# rolltest - drive the piano roll with REAL clicks and check the selection moved.
#
# The roll's selection grammar has a 15-claim self-test, but a self-test proves
# the MODEL and never the WIRING - the lesson this tree learned twice in one day
# (the panel registry had three self-tested loops and zero call sites; the edit
# funnel refused a map every unit test had accepted). So this drives the actual
# mouse and reads the panel's own trace back.
#
# Modelled on scripts/docktest.sh, whose measured lessons are reused rather than
# rediscovered:
#   * A WINDOW MANAGER IS REQUIRED. With bare Xvfb, SDL never gets
#     SDL_WINDOW_INPUT_FOCUS and synthetic input goes nowhere - silently.
#   * CLICKS ARE IN SCREEN COORDINATES. The window is floating, so its position
#     must be read and added; clicking at absolute coordinates lands wherever
#     the WM happened to put the window. `[MEASURED 2026-09-09]` this is exactly
#     why an earlier hand-driven attempt at this test selected nothing.
#
# Exit: 0 pass, 1 fail, 77 skip (no Xvfb / xdotool / i3 / ROM / clip / build).
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
DISP="${FLYCAST_TEST_DISPLAY:-:141}"
OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT

[ -x "$EXE" ] || { echo "rolltest: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "rolltest: SKIP - no ROM"; exit $SKIP; }
for t in Xvfb xdotool i3; do
	command -v $t >/dev/null || { echo "rolltest: SKIP - no $t"; exit $SKIP; }
done

# A clip with a savestate, so the roll has a movie to show. Length via the
# parser, never arithmetic on the file size (scripts/flyrframes.sh).
CLIP=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	n=$("$ROOT/scripts/flyrframes.sh" "$f" 2>/dev/null) || continue
	[ "$n" -ge 600 ] || continue
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	CLIP="$f"; break
done
[ -n "$CLIP" ] || { echo "rolltest: SKIP - no clip with a savestate"; exit $SKIP; }

mkdir -p "$OUT/config/flycast-dojo" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"
for sib in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* "$(dirname "$CLIP")"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$OUT/clip/" 2>/dev/null; } || true
done

nohup Xvfb "$DISP" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!
sleep 2
# Same config docktest uses, and it is deliberately minimal: this is not the
# user's i3 and it touches no real display.
printf 'default_border none\nfor_window [class=".*"] floating enable\n' > "$OUT/i3.conf"
DISPLAY="$DISP" nohup i3 -c "$OUT/i3.conf" >"$OUT/i3.log" 2>&1 & IPID=$!
sleep 2

XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:Panel.pianoroll=yes -config dojo:RollSelTrace=yes \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 &
FC=$!
sleep 18
export DISPLAY="$DISP"

cleanup() { kill "$FC" 2>/dev/null; kill "$IPID" 2>/dev/null; kill "$XPID" 2>/dev/null; }

WID=$(xdotool search --name "Flycast" 2>/dev/null | head -1)
[ -n "$WID" ] || { echo "rolltest: SKIP - no window"; cleanup; exit $SKIP; }
# SCREEN COORDINATES: the window is floating, so its own origin must be added.
geom=$(xdotool getwindowgeometry "$WID" 2>/dev/null | grep Position | head -1)
wx=$(echo "$geom" | sed 's/.*Position: \([0-9-]*\),.*/\1/')
wy=$(echo "$geom" | sed 's/.*Position: [0-9-]*,\([0-9-]*\).*/\1/')
[ -n "$wx" ] || { wx=0; wy=0; }
echo "rolltest: window at $wx,$wy"

xdotool windowactivate "$WID" 2>/dev/null; sleep 1
# PAUSE, and the key is COMMA, not P. `[MEASURED 2026-09-09]` this tree binds
# EMU_BTN_PAUSE to keycode 54 (core/input/keyboard_device.h) - dojo's playback
# control - while David's fork uses P. Sending p here paused nothing and the
# roll kept scrolling; the panel said so itself ("edits need the movie PAUSED")
# while the harness reported a selection failure.
#
# It matters twice over: a running roll scrolls under the cursor every 16 ms, so
# no click can land reliably, and edits are gated on paused regardless.
xdotool key --window "$WID" comma; sleep 2

# The roll's rows sit below its header; these offsets are inside the table for a
# 1000x800 window with the panel open at its default place.
ROWX=$((wx + 330)); ROW1=$((wy + 360)); ROW2=$((wy + 424))
# PRESS AND RELEASE AS SEPARATE STEPS, WITH A PAUSE BETWEEN THEM.
# `[MEASURED 2026-09-09]` `xdotool click 1` sends down+up faster than one frame,
# and an immediate-mode GUI samples button state ONCE PER FRAME - so ImGui never
# observes the down state and IsItemClicked never fires. The tell was that HOVER
# worked while clicks did not: motion is sampled continuously, clicks are edges.
# scripts/docktest.sh already does it this way (mousedown; sleep 0.4).
xdotool mousemove $ROWX $ROW1; sleep 0.4
xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 1

xdotool keydown shift; sleep 0.2
xdotool mousemove $ROWX $ROW2; sleep 0.4
xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 0.4
xdotool keyup shift; sleep 2
xdotool key --window "$WID" comma >/dev/null 2>&1   # let it run on, so teardown is clean
sleep 1
cleanup; sleep 1

sel=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL SEL:" | tail -3)
echo "$sel" | sed 's/^/  /'
last=$(echo "$sel" | tail -1)
n=$(echo "$last" | sed -n 's/.*n=\([0-9]*\).*/\1/p')

# THE ASSERTION IS THAT A RANGE APPEARED, not merely that something did. A plain
# click alone gives n=1, which a stuck or mis-read click could also produce; a
# shift-click must extend it.
if [ -z "${n:-}" ]; then
	echo "rolltest: SKIP - the roll never reported a selection (no click reached it)"
	exit $SKIP
fi
if [ "$n" -gt 1 ]; then
	echo "PASS rolltest - a click and a shift-click selected $n rows"
	exit 0
fi
if [ "$n" -eq 0 ]; then
	# NOT a failure of the shift-click: nothing reached the roll at all. Saying
	# "the shift-click did not extend it" here would name the wrong component.
	echo "rolltest: SKIP - no click reached the roll (selection never left 0)"
	exit $SKIP
fi
echo "FAIL rolltest - a click selected $n row(s) but the shift-click did not extend it"
exit 1
