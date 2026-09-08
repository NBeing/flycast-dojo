#!/usr/bin/env bash
# Does docking a window beside the game actually SHRINK the game?
#
# This is the regression test for "the game is a node like any other". It drives
# a REAL drag with xdotool on a private Xvfb -- never the user's display, which
# would mean taking over their mouse -- and judges from the emulator's own
# TAS VIEWPORT trace rather than from pixels:
#
#   before   central 0,0 900x700     game 0,13 900x674
#   after    central 221,0 679x700   game 221,95 679x510
#
# PASS requires the central node to NARROW and the game rect to follow it. A
# game that ignored the dock layout -- the bug this replaced -- leaves both
# numbers unchanged and fails here.
#
# Exit: 0 pass, 1 fail, 77 skip (no Xvfb, no xdotool, no ROM, not built).
set -uo pipefail
SKIP=77
ROM="${ROM:-/home/nbee/dev/davids_fly/NoBGM_VMU.cdi}"
HERE="$(cd "$(dirname "$0")" && pwd)"
EXE="$HERE/../build-dojo7/flycast"
DISP=":${DOCKTEST_DISPLAY:-77}"
OUT="$(mktemp -d)"

# --self-test runs the SAME drag against the pre-2026-09-08 behaviour, where the
# picture was blitted full-window behind the UI. There the game must NOT follow
# the dock, and this script must therefore FAIL. A test that cannot fail is not
# evidence -- so the way to trust the PASS above is to watch this one go red.
MODE="run"
[ "${1:-}" = "--self-test" ] && MODE="selftest"
if [ "$MODE" = "selftest" ]; then
	PANEL_CFG="-config dojo:GamePanel=no -config dojo:DockGameViewport=no"
else
	PANEL_CFG="-config dojo:GamePanel=yes"
fi

command -v Xvfb    >/dev/null || { echo "docktest: SKIP - no Xvfb";    exit $SKIP; }
command -v xdotool >/dev/null || { echo "docktest: SKIP - no xdotool"; exit $SKIP; }
[ -f "$ROM" ] || { echo "docktest: SKIP - no ROM at $ROM"; exit $SKIP; }
[ -x "$EXE" ] || { echo "docktest: SKIP - not built"; exit $SKIP; }

mkdir -p "$OUT/config/flycast-dojo"
cat > "$OUT/config/flycast-dojo/flycast.lua" <<'LUAEOF'
-- One dockable window. Positioned for the first three paints ONLY: a per-frame
-- SetNextWindowPos drags a window straight back out of the dock you just
-- dropped it in, which reads as "docking is broken" and is not.
local paints = 0
local prev = flycast_callbacks and flycast_callbacks.overlay
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.overlay = function()
	if prev then prev() end
	paints = paints + 1
	if paints <= 3 then
		flycast.ui.SetNextWindowPos(420, 300)
		flycast.ui.SetNextWindowSize(220, 130)
	end
	if flycast.ui.Begin("Dock Me") then flycast.ui.Text("drag my title bar") end
	flycast.ui.End()
end
LUAEOF

FC=0
# KILL THE GROUP, NOT THE PID - the emulator is launched into its own process
# group, so killing the pid we were handed orphans it and the next run competes
# with it for the display. [MEASURED 2026-09-08] recordtest.sh had the same
# defect and left three emulators running across one debugging session.
cleanup() {
	[ "$FC" -ne 0 ] && { kill -- -"$FC" 2>/dev/null || kill "$FC" 2>/dev/null; }
	sleep 1; pkill -f "Xvfb $DISP" 2>/dev/null; return 0
}
trap cleanup EXIT

Xvfb "$DISP" -screen 0 1400x900x24 >/dev/null 2>&1 &
sleep 2
# A WINDOW MANAGER, because keyboard focus does not exist without one.
# [MEASURED 2026-09-08] with bare Xvfb, `xdotool key Escape` goes nowhere - SDL
# never gets SDL_WINDOW_INPUT_FOCUS, so the menu phase below silently tested
# nothing and PASSED with its fix deliberately removed. i3 in its own config-less
# session is enough; it is not the user's i3 and touches no real display.
if command -v i3 >/dev/null; then
	printf 'default_border none\nfor_window [class=".*"] floating enable\n' > "$OUT/i3.conf"
	DISPLAY="$DISP" i3 -c "$OUT/i3.conf" >/dev/null 2>&1 &
	sleep 2
	HAVE_WM=1
else
	HAVE_WM=0
fi
XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	$PANEL_CFG -config dojo:ViewportTrace=yes -config dojo:UiIni=no \
	-config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config window:width=900 -config window:height=700 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 &
FC=$!
sleep 18

export DISPLAY="$DISP"
# The SDL window's own position, so the drag is in SCREEN coordinates. Without
# this the drag lands wherever the window manager happened to put the window.
geom=$(xdotool search --name "Flycast" getwindowgeometry 2>/dev/null | grep Position | head -1)
wx=$(echo "$geom" | sed -n 's/.*Position: \([0-9]*\),.*/\1/p')
wy=$(echo "$geom" | sed -n 's/.*Position: [0-9]*,\([0-9]*\).*/\1/p')
[ -n "$wx" ] || { echo "docktest: SKIP - no flycast window on $DISP"; exit $SKIP; }

# Grab the Dock Me title bar and walk it to the dockspace's left edge. The
# intermediate moves matter: ImGui needs motion events to recognise a drag, and
# a single jump to the target reads as a click.
xdotool mousemove $((wx + 470)) $((wy + 308)); sleep 0.4
xdotool mousedown 1; sleep 0.4
for x in 390 350 300 230 170 110 60 35 22; do
	xdotool mousemove $((wx + x)) $((wy + 350)); sleep 0.18
done
sleep 0.6
xdotool mouseup 1; sleep 2

before=$(grep -a "TAS VIEWPORT" "$OUT/out.log" | head -1)
after=$( grep -a "TAS VIEWPORT" "$OUT/out.log" | tail -1)
echo "  before: ${before#*RENDERER]: }"
echo "  after:  ${after#*RENDERER]: }"

cw() { echo "$1" | sed -n 's/.*central [0-9-]*,[0-9-]* \([0-9]*\)x.*/\1/p'; }
gw() { echo "$1" | sed -n 's/.*game [0-9-]*,[0-9-]* \([0-9]*\)x.*/\1/p'; }
bw=$(cw "$before"); aw=$(cw "$after")
bg=$(gw "$before"); ag=$(gw "$after")

if [ -z "${aw:-}" ] || [ -z "${ag:-}" ]; then echo "FAIL docktest - no viewport trace to judge"; exit 1; fi

if [ "$MODE" = "selftest" ]; then
	# The control. The dockspace still splits, so the central node narrows -- but
	# with the publish disabled the picture is derived from the WINDOW and does
	# not move, which is exactly the defect. If the game narrows here, the two
	# modes are not actually different and the PASS above proves nothing.
	if [ "$ag" -lt "$bg" ]; then
		echo "SELF-TEST FAIL - the game narrowed even with the panel OFF,"
		echo "  so this script would pass either way and is not evidence"
		exit 1
	fi
	echo "SELF-TEST OK - with the panel off the game stays $bg px wide while the"
	echo "  node narrows ($bw -> $aw), so a real regression would be caught"
	exit 0
fi
if [ "$aw" -ge "$bw" ]; then
	echo "FAIL docktest - the drop did not narrow the central node ($bw -> $aw)"; exit 1
fi
if [ "$ag" -ge "$bg" ]; then
	echo "FAIL docktest - the node narrowed but the GAME did not follow ($bg -> $ag)"
	echo "       that is the old bug: docked panels covering a full-window picture"; exit 1
fi
echo "  docked: the game narrowed $bg -> $ag px"

# AND THE PICTURE IS ACTUALLY BEING DRAWN THERE.
#
# [MEASURED 2026-09-08] this check was missing and the omission was found by
# sabotage: breaking the game panel's registry id so NOTHING drew the picture
# left this harness fully green. It reads the TAS VIEWPORT trace, which is
# emitted by the dockspace host and computed by rend::gameViewport() - neither
# of which cares whether anyone drew. So "docking narrowed the game" was a claim
# about a RECTANGLE, not about the game.
#
# TAS PRESENT comes from the blit path, which runs every frame and reports which
# mode won. "panel" means the picture went through the panel; "blit full-window"
# means it did not.
present=$(grep -a "TAS PRESENT" "$OUT/out.log" | tail -1)
echo "  present:${present#*RENDERER]: }"
case "$present" in
	*"TAS PRESENT: panel"*) ;;
	*) echo "FAIL docktest - the dock node narrowed but the picture was not drawn"
	   echo "       as a panel, so the rectangle moved and nothing followed it"
	   exit 1 ;;
esac

# PHASE 2: THE PICTURE MUST NOT MOVE WHEN A MENU OPENS.
#
# Escape opens the in-game menu, which is a different GuiState with its own
# ImGui frame. If that state does not submit the dockspace host and the game
# panel, the renderer falls back to the full-window blit and the picture JUMPS
# out of its dock -- and back again when the menu closes. The trace only logs on
# CHANGE, so "the rect is the same after Escape" is the pass condition.
# FOCUS FIRST. Xvfb has no window manager, so nothing ever gives the SDL window
# keyboard focus and a bare `xdotool key` goes nowhere - which made the first
# version of this phase PASS with the fix deliberately removed. windowactivate
# plus an explicit --window target makes the key actually land. A phase that
# cannot fail is not a phase.
if [ "$HAVE_WM" = "0" ]; then
	echo "  menu:   SKIPPED - no window manager on $DISP, so a key press cannot"
	echo "          reach the emulator and this phase would pass without testing"
	echo "PASS docktest - docking narrowed the game ($bg -> $ag px wide)"
	echo "     (menu phase skipped, see above)"
	exit 0
fi
wid=$(xdotool search --name "Flycast" | head -1)
xdotool windowactivate --sync "$wid" 2>/dev/null
sleep 0.5
xdotool key --clearmodifiers Escape; sleep 2.5

# DID A MENU ACTUALLY OPEN? Without this the phase cannot tell "the picture
# stayed put" from "nothing happened", and those look identical in the trace.
#
# IT MUST BE A MENU STATE, not merely any state change. The first version
# accepted any TAS GUISTATE line and so accepted the BOOT transition
# (8 Loading -> 0 Closed), which every run has - so the gate passed on a run
# where Escape had done nothing at all. Commands(1) and Settings(2) are the two
# states Escape can produce.
if ! grep -a "TAS GUISTATE" "$OUT/out.log" | grep -qE '\-> (1|2)$'; then
	echo "  menu:   SKIPPED - Escape never changed the GUI state, so nothing was"
	echo "          tested; not reporting a pass for a phase that did not run"
	echo "PASS docktest - docking narrowed the game ($bg -> $ag px wide)"
	echo "     (menu phase skipped, see above)"
	exit 0
fi
echo "  state:  $(grep -a 'TAS GUISTATE' "$OUT/out.log" | tail -1 | sed 's/.*RENDERER\]: //')"
menu=$(grep -a "TAS VIEWPORT" "$OUT/out.log" | tail -1)
mg=$(gw "$menu")
# The present's own mode, which is logged from a path that runs in EVERY state -
# unlike TAS VIEWPORT, which a broken GuiState would simply never emit, leaving
# the rectangle unchanged and this check passing for the wrong reason.
mode=$(grep -a "TAS PRESENT" "$OUT/out.log" | tail -1)
echo "  menu:   ${menu#*RENDERER]: }"
echo "  present:${mode#*RENDERER]: }"
case "$mode" in
	*"blit full-window"*)
		echo "FAIL docktest - opening the menu dropped the picture back to a"
		echo "       full-window blit, so it jumps out of its dock and back"
		exit 1 ;;
	"") echo "FAIL docktest - no present-mode trace; the panel never engaged"; exit 1 ;;
esac
if [ "$mg" != "$ag" ]; then
	echo "FAIL docktest - the picture MOVED when the menu opened ($ag -> $mg px wide)"
	exit 1
fi

echo "PASS docktest - docking narrowed the game ($bg -> $ag px wide), and the"
echo "     menu left it where it was"
