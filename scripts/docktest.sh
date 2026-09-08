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
cleanup() { [ "$FC" -ne 0 ] && kill "$FC" 2>/dev/null; sleep 1; pkill -f "Xvfb $DISP" 2>/dev/null; return 0; }
trap cleanup EXIT

Xvfb "$DISP" -screen 0 1400x900x24 >/dev/null 2>&1 &
sleep 2
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
echo "PASS docktest - docking narrowed the game ($bg -> $ag px wide)"
