#!/usr/bin/env bash
# rolltest - drive the piano roll with REAL clicks and drags, and check that the
# selection moved and that a paint stroke reached the edit funnel.
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
# ROLLTEST_OUT keeps the run directory - the emulator's full log included - for
# a diagnostic run. Unset, it is a temp dir that is removed, so a normal run
# leaves nothing behind.
if [ -n "${ROLLTEST_OUT:-}" ]; then
	OUT="$ROLLTEST_OUT"; mkdir -p "$OUT"
else
	OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
fi

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
	-config dojo:RollPaintTrace=yes -config dojo:RollPaintProbe=yes \
	-config dojo:RollSlotTrace=yes \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 &
FC=$!
sleep 18
export DISPLAY="$DISP"

# PID-SCOPED, ALL THREE. NEVER `pkill -x i3`, `pgrep -x i3`, a bare
# `pkill -x Xvfb`, or `rm /run/user/*/i3/ipc-socket.*` - every one of those
# matches the USER'S OWN window manager and X server, not this test's throwaway
# pair, and killing them drops the user to a login screen.
#
# `[MEASURED 2026-09-09]` that happened, from ad-hoc cleanup typed outside this
# script: Xorg and i3 both restarted mid-session. scripts/docktest.sh already
# carried this warning and it was read past. It is repeated here because this
# script also starts an i3, so this is where the next person will need it.
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

# ---------------------------------------------------------------------------
# LOCATE THE COLUMNS BEFORE CLICKING ANYTHING.
#
# The row is ONE hit target spanning the table, and the panel decides the
# GESTURE from which column the mouse is over: the frame gutter selects, an
# input column paints. A fixed pixel offset therefore no longer picks a
# gesture - it picks whichever gesture that offset happens to land on.
#
# `[MEASURED 2026-09-09]` not hypothetical. The offset this script used for its
# plain click, wx+330, turned out to be an INPUT column. It only ever selected
# because a wrong gate (session::readOnly()) made painting impossible in a
# replay; correcting the gate would have silently turned the selection test
# into a paint test, and it would still have printed PASS.
#
# So sweep and ask the panel where column 0 begins and ends. The gutter is 30px
# left of its left edge, the paint target is its midpoint - both derived from
# what the emulator reports, never from arithmetic on column widths.
hovcol() {
	tr -d '\0' < "$OUT/out.log" | grep -a "ROLL HOVER:" | tail -1 \
		| sed -n 's/.*col=\(-*[0-9]*\).*/\1/p'
}
# `[MEASURED 2026-09-09]` a row is ~32px in this layout: a press at ROW1 and a
# shift-press at ROW2 (+64) are two frames apart. ROW5 must therefore be several
# rows down, not 40px - at 40px the paint drag never left the row it started on
# and committed a single frame while looking like a working stroke.
ROW1=$((wy + 360)); ROW2=$((wy + 424)); ROW5=$((wy + 360 + 128))
C0A=""; C0B=""
for dx in $(seq 120 6 760); do
	xdotool mousemove $((wx + dx)) $ROW1; sleep 0.25
	c=$(hovcol)
	if [ "${c:-}" = "0" ]; then
		[ -n "$C0A" ] || C0A=$dx
		C0B=$dx
	elif [ -n "$C0B" ]; then
		break
	fi
done
if [ -z "$C0A" ] || [ "$C0A" -lt 40 ]; then
	echo "rolltest: SKIP - never hovered the roll's first input column (layout changed?)"
	cleanup; exit $SKIP
fi
GUTX=$((wx + C0A - 30))
PNTX=$((wx + (C0A + C0B) / 2))
echo "rolltest: column 0 spans dx $C0A..$C0B; gutter click x=$GUTX, paint x=$PNTX"

# ---------------------------------------------------------------------------
# PHASE 1 - SELECTION, in the gutter.
#
# PRESS AND RELEASE AS SEPARATE STEPS, WITH A PAUSE BETWEEN THEM.
# `[MEASURED 2026-09-09]` `xdotool click 1` sends down+up faster than one frame,
# and an immediate-mode GUI samples button state ONCE PER FRAME - so ImGui never
# observes the down state and IsItemClicked never fires. The tell was that HOVER
# worked while clicks did not: motion is sampled continuously, clicks are edges.
xdotool mousemove $GUTX $ROW1; sleep 0.4
xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 1

xdotool keydown shift; sleep 0.2
xdotool mousemove $GUTX $ROW2; sleep 0.4
xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 0.4
xdotool keyup shift; sleep 2

# READ THE VERDICT NOW, not at the end. Phase 2 drags in the gutter too, so it
# OVERWRITES this evidence - and reading the last line after both phases judged
# phase 2's drag while naming phase 1 in the failure message. That is the wrong
# component named in the report, which is the expensive kind of wrong.
SEL1=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL SEL:" | tail -3)
SELN=$(echo "$SEL1" | tail -1 | sed -n 's/.*n=\([0-9]*\).*/\1/p')

# ---------------------------------------------------------------------------
# PHASE 2 - THE PAINT STROKE, and its control.
#
# Same reason as phase 1: roll_paint has an 18-claim self-test, and that proves
# the MODEL. Until a real drag in a real input column reaches Dojo::ApplyEdit,
# the stroke is a tested module with no customer - the exact shape of the two
# defects this tree found on 2026-09-09 (a panel registry with zero call sites;
# an edit funnel that refused a map every unit test had accepted).
paintcommits() { tr -d '\0' < "$OUT/out.log" | grep -ac "ROLL PAINT: commit" || true; }

# THE CONTROL, RUN FIRST so its evidence is unambiguous: the identical drag in
# the GUTTER must commit nothing. Without it, "a commit line appeared" is also
# what an unconditional log looks like, and one arm cannot tell a stroke that
# fires everywhere from one that fires only where it should.
xdotool mousemove $GUTX $ROW1; sleep 0.4
xdotool mousedown 1; sleep 0.4
xdotool mousemove $GUTX $ROW5; sleep 0.5
xdotool mouseup 1; sleep 1
GUTTER=$(paintcommits)

# THE STROKE: press in column 0, drag down in STEPS, release.
#
# STEPPED, NOT TELEPORTED. A real drag emits motion continuously, and one jump
# is a shape no user produces.
#
# `[CORRECTED 2026-09-09]` this block previously claimed that NO pointer motion
# is delivered while a button is held under Xvfb + i3, and concluded the script
# could not drive a stroke at all. That was wrong, and wrong in the worst way:
# the evidence for it was `dojo:MouseDragTrace`, which traces
# updateMousePositionWhileDragging() in core/sdl/sdl.cpp - the very function that
# was CAUSING it, by overwriting each fresh SDL_MOUSEMOTION with a stale
# SDL_GetGlobalMouseState read on every input pump. The instrument was reporting
# its own defect as a fact about X.
#
# The tell was visible and read past: the row under the cursor oscillated
# 10904, 10909, 10904, 10910, 10904, 10911 - the odd samples tracking the drag
# perfectly. "Nothing is moving" does not produce a monotonic sequence.
#
# Fixed, a drag now spans the rows it crosses, and this script asserts it.
geomof() { xdotool getwindowgeometry "$WID" 2>/dev/null | grep Position | head -1; }
G0=$(geomof)
xdotool mousemove $PNTX $ROW1; sleep 0.4
xdotool mousedown 1; sleep 0.5
for i in 1 2 3 4 5 6 7 8; do
	xdotool mousemove $PNTX $((ROW1 + i * 16)); sleep 0.12
done
sleep 0.4
xdotool mouseup 1; sleep 1.5
G1=$(geomof)
PAINTED=$(paintcommits)
# THE WINDOW MUST NOT HAVE MOVED. A floating window dragged by the WM keeps the
# cursor over the same widget however far the mouse travels, which is exactly
# what a stroke that will not extend looks like from the emulator's side.
if [ "$G0" != "$G1" ]; then
	echo "rolltest: window MOVED during the drag - $G0 -> $G1"
fi

xdotool key --window "$WID" comma >/dev/null 2>&1   # let it run on, so teardown is clean
sleep 1
cleanup; sleep 1

echo "$SEL1" | sed 's/^/  /'
n="${SELN:-}"

# THE ASSERTION IS THAT A RANGE APPEARED, not merely that something did. A plain
# click alone gives n=1, which a stuck or mis-read click could also produce; a
# shift-click must extend it.
if [ -z "${n:-}" ]; then
	echo "rolltest: SKIP - the roll never reported a selection (no click reached it)"
	exit $SKIP
fi
if [ "$n" -le 1 ] && [ "$n" -ne 0 ]; then
	echo "FAIL rolltest - a click selected $n row(s) but the shift-click did not extend it"
	exit 1
fi
if [ "$n" -eq 0 ]; then
	# NOT a failure of the shift-click: nothing reached the roll at all. Saying
	# "the shift-click did not extend it" here would name the wrong component.
	echo "rolltest: SKIP - no click reached the roll (selection never left 0)"
	exit $SKIP
fi
echo "  selection: a click and a shift-click selected $n rows"

# ---- the paint verdict ----------------------------------------------------
tr -d '\0' < "$OUT/out.log" | grep -a "ROLL PAINT:" | tail -4 | sed 's/^/  /'
if [ "${GUTTER:-0}" -ne 0 ]; then
	echo "FAIL rolltest - a GUTTER drag committed a paint edit ($GUTTER); the column gate does not hold"
	exit 1
fi
if [ "${PAINTED:-0}" -eq 0 ]; then
	echo "FAIL rolltest - a drag in column 0 committed nothing (stroke not wired to the funnel)"
	exit 1
fi
# THE ZERO IS NOT THE ASSERTION. ApplyEdit answers with the first frame it
# changed; a commit that changed nothing reports -1, and that is what a stroke
# painting a column it had already painted would look like.
first=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL PAINT: commit" | tail -1 \
		| sed -n 's/.*first=\(-*[0-9]*\).*/\1/p')
lo=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL PAINT: commit" | tail -1 \
		| sed -n 's/.*set \([0-9]*\)\.\.[0-9]*.*/\1/p')
hi=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL PAINT: commit" | tail -1 \
		| sed -n 's/.*set [0-9]*\.\.\([0-9]*\).*/\1/p')
span=$(( ${hi:-0} - ${lo:-0} + 1 ))
if [ -z "${first:-}" ] || [ "$first" -lt 0 ]; then
	echo "FAIL rolltest - the stroke reached the funnel but changed no frame (first=${first:-none})"
	exit 1
fi
# THE DRAG MUST HAVE COVERED THE ROWS IT CROSSED. Eight 16px steps over ~32px
# rows is four rows at minimum; anything less means the stroke stopped extending,
# which is what BOTH bugs found on 2026-09-09 looked like - the stale-position
# override, and two adjacent rows claiming one point. Neither changed the commit
# COUNT, so a count-only assertion passed through both.
if [ "$span" -lt 4 ]; then
	echo "FAIL rolltest - an 8-step drag committed only $span row(s) ($lo..$hi); the stroke stopped extending"
	exit 1
fi

# ---- the savestate gutter has a REAL host -----------------------------------
# The clip this script picks is required to have a .state beside it, so "no
# slot is anchored" is a failure and not a fact about the machine. Until
# 2026-09-09 there was no production roll::Host at all - setHost() was called
# only by a self-test - so the gutter had never shown a real slot and the panel
# printed "No host installed". Silence here is that state returning.
slots=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL SLOTS:" | tail -1)
if [ -z "$slots" ]; then
	echo "FAIL rolltest - the roll never reported a slot scan; no host is installed"
	exit 1
fi
echo "  ${slots##*N\[RENDERER\]: }"
anch=$(echo "$slots" | sed -n 's/.*anchored=\([0-9]*\).*/\1/p')
if [ -z "${anch:-}" ] || [ "$anch" -lt 1 ]; then
	echo "FAIL rolltest - the host scanned slots but anchored none, and this clip has a savestate"
	exit 1
fi

# ---- the multi-row stroke, proved in process --------------------------------
# NOT a duplicate of the click test and NOT a self-test: it drives the real
# begin/extendTo/build through the real funnel against the real movie, and it is
# the only thing here that can exercise a span, for the input reason above.
probe=$(tr -d '\0' < "$OUT/out.log" | grep -a "ROLL PAINTPROBE:" | tail -1)
if [ -z "$probe" ]; then
	# A SKIPPED CHECK IS NOT A PASSING ONE. The probe is one-shot and gated on a
	# loaded movie; silence means it never ran, which is not the same as PASS.
	echo "rolltest: SKIP - the stroke probe never ran (no movie, or the panel never drew)"
	exit $SKIP
fi
echo "  ${probe##*N\[RENDERER\]: }"
case "$probe" in
	*PASS*) ;;
	*) echo "FAIL rolltest - the multi-row stroke probe failed"; exit 1 ;;
esac
echo "PASS rolltest - $n rows selected; an 8-step paint drag committed $span rows from frame $first; a gutter drag committed none; the in-process stroke probe passed"
exit 0
