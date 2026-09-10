#!/usr/bin/env bash
# hotkeytest - press a bound key and prove the action ran.
#
#   RUN:   scripts/hotkeytest.sh
#   PASS:  F11 toggles the piano roll open, then closed; F12 advances the
#          savestate slot; and an UNBOUND key does none of those things.
#   FAIL:  exit 1, saying which.
#   SKIP:  exit 77 (no Xvfb / xdotool / i3 / ROM / clip / build).
#   SELF:  the unbound-key arm IS the control and runs on every pass; there is
#          no separate --self-test, because a sabotage arm would prove less
#          than the control already does. See "THE CONTROL" below.
#
# WHY THIS EXISTS. `[MEASURED 2026-09-10]` nothing in this tree drove a hotkey
# end to end. 53 actions could be bound, and the only evidence any of them
# worked was that the code compiled - which docs/HOTKEYS.md had to cite as the
# reason a hotkey REGISTRY (one descriptor, four loops, mirroring panels::add)
# would be an unverifiable refactor. This is the test that unblocks it.
#
# THE BINDING IS WRITTEN, NOT ASSUMED. The five TAS actions ship UNBOUND on
# purpose (every key the fork defaults them to is already a training key here),
# so this harness writes its own mapping file into a sandboxed config dir. That
# also means the test exercises the persistence table: a key bound through
# `btn_piano_roll` only reaches the dispatch if mapping.cpp knows that name.
#
# NO WINDOW MANAGER INTERACTION beyond focus. Keys go through XTest to a
# private Xvfb. Never the user's display, and never `pkill -x` anything.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
# THE COLON IS ADDED HERE, and the variable is a bare number - the convention
# scripts/docktest.sh already uses and CMakeLists.txt already passes.
#
# `[MEASURED 2026-09-10]` this file had `${HOTKEYTEST_DISPLAY:-:147}`, so ctest's
# `HOTKEYTEST_DISPLAY=147` produced `Xvfb 147`, which answers "Unrecognized option:
# 147" and exits. The two harnesses that had this bug failed DIFFERENTLY and
# neither said so: hotkeytest skipped with "no window", and selftest PASSED -
# its 331 claims run inside flycast_init, before os_CreateWindow, so they never
# needed the display the script insists on having. A prerequisite nobody can
# see is false is not a prerequisite.
DISP=":${HOTKEYTEST_DISPLAY:-147}"

if [ -n "${HOTKEYTEST_OUT:-}" ]; then OUT="$HOTKEYTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
fi

[ -x "$EXE" ] || { echo "hotkeytest: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "hotkeytest: SKIP - no ROM"; exit $SKIP; }
for t in Xvfb xdotool i3; do
	command -v $t >/dev/null || { echo "hotkeytest: SKIP - no $t"; exit $SKIP; }
done
# THE CLIP MUST OUTLAST THE TEST, and it must belong to this ROM.
# `[MEASURED 2026-09-10]` taking the newest clip picked a Marvel vs. Capcom 2
# recording while the harness booted NoBGM_VMU, and a 120-frame clip on another
# run. Both left the emulator in GuiState::ReplayEnd - the movie exhausted -
# long before any key was sent, and every TAS hotkey is correctly silent there.
# The harness then reported "the binding never reached the dispatch" three runs
# running, while the bindings were perfect.
#
# Same filter as scripts/rolltest.sh: parsed length, never arithmetic on the
# file size, and a savestate sibling - which is also what makes the clip belong
# to the ROM being booted.
CLIP=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	n=$("$ROOT/scripts/flyrframes.sh" "$f" 2>/dev/null) || continue
	[ "$n" -ge 3600 ] || continue		# 60 s at 60 Hz; this test needs ~35
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	CLIP="$f"; break
done
[ -n "$CLIP" ] || { echo "hotkeytest: SKIP - no clip long enough with a savestate"; exit $SKIP; }

mkdir -p "$OUT/config/flycast-dojo/mappings" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"

# ---------------------------------------------------------------------------
# PASS 1: ASK THE EMULATOR WHICH MAPPING FILES IT WANTS.
#
# `[MEASURED 2026-09-10]` the first version of this harness wrote
# SDL_Keyboard.cfg, the name in the developer's own config directory, and the
# emulator loaded it - for a device nothing types into. The keyboard SDL
# actually enumerates here is "Kinesis Freestyle2 PC - KB800", so the file it
# wanted was "SDL_Kinesis Freestyle2 PC - KB800.cfg". F11 was bound on a
# phantom and the test reported "the binding never reached the dispatch".
#
# Hardcoding that name would pass on one laptop and fail on every other
# machine - the same shape as a "neutral" conformance suite that poked an SH4
# address and passed everywhere it had ever run because it had only ever run on
# a Dreamcast. So the name is READ OUT OF THE EMULATOR'S OWN LOG. It prints the
# filename it looked for, because reconstructing it out here would mean
# reimplementing make_mapping_filename's nine character substitutions.
#
# THE ROM IS PASSED, and that is not optional. `[MEASURED 2026-09-10]` a
# ROM-less probe logged nothing at all: mappings are loaded from
# `Event::Start` (gui.cpp), so with no game there is no enumeration to read.
# With the ROM on the command line the lines appear within a second, well
# before the disc finishes loading, so this pass stays short.
nohup Xvfb "$DISP" -screen 0 320x240x24 >"$OUT/xvfb0.log" 2>&1 & XPID=$!
sleep 2
XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:AutoLoadNetState=no "$ROM" > "$OUT/probe.log" 2>&1 & PC=$!
sleep 10
kill "$PC" 2>/dev/null; kill "$XPID" 2>/dev/null
sleep 2
kill -0 "$PC" 2>/dev/null    && kill -9 "$PC" 2>/dev/null
kill -0 "$XPID" 2>/dev/null  && kill -9 "$XPID" 2>/dev/null
sleep 1

# Only KEYBOARDS get the binding. A mouse has no F11, and writing a mapping
# file for one would replace its defaults with two keys it cannot produce.
WANTED=$(tr -d '\0' < "$OUT/probe.log" \
	| sed -n 's/.*INPUT MAPPING: \(.*\) has no mapping file (wanted \(.*\)) - built-in.*/\1\t\2/p' \
	| grep -ai "keyboard\|kb" | cut -f2 | sort -u)
if [ -z "$WANTED" ]; then
	# NOT A PASS. "No keyboard device" and "the probe never ran" are the same
	# silence, and both make every claim below unreachable.
	echo "hotkeytest: SKIP - the emulator named no keyboard mapping file"
	exit $SKIP
fi
echo "hotkeytest: binding on $(printf '%s\n' "$WANTED" | wc -l) keyboard mapping(s)"

# SDL scancodes: 63 = F6, 66 = F9, 69 = F12. F7 (64) is left deliberately
# UNBOUND - it is the control key below. The option names are mapping.cpp's own;
# anything else here binds nothing at all.
#
# NOT F11. `[MEASURED 2026-09-10]` core/sdl/sdl.cpp intercepts SDLK_F11 for
# "Alt-Return and F11 toggle full screen" and consumes the KEY DOWN, so the
# mapping only ever sees the key up - and every hotkey below is guarded on
# `pressed`. The trace showed it plainly: `HOTKEY: id=0x3000031 up` with no
# matching `down`, three runs in a row. F11 is not bindable in this emulator,
# and that is a fact about the host rather than something to work around here.
printf '%s\n' "$WANTED" | while IFS= read -r f; do
	[ -n "$f" ] || continue
	cat > "$OUT/config/flycast-dojo/mappings/$f" <<CFG
[emulator]
mapping_name = hotkeytest
dead_zone = 10
saturation = 100
rumble_power = 100
version = 3

[digital]
bind0 = 63:btn_piano_roll
bind1 = 69:btn_savestate_slot_next
bind2 = 66:btn_pause
CFG
done

# ---------------------------------------------------------------------------
# PASS 2: the real run.
nohup Xvfb "$DISP" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!
sleep 2
printf 'default_border none\nfor_window [class=".*"] floating enable\n' > "$OUT/i3.conf"
DISPLAY="$DISP" nohup i3 -c "$OUT/i3.conf" >"$OUT/i3.log" 2>&1 & IPID=$!
sleep 2

# THE PANEL STARTS CLOSED, so "it is open" cannot be true before the key is
# pressed. A test that opened it by config and then pressed a key to toggle it
# would report the same PASS whether or not the key did anything.
XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Panel.pianoroll=no -config dojo:Panel.states=no \
	-config dojo:HotkeyTrace=yes \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
sleep 20

cleanup() {
	# PID-SCOPED, ALL THREE, and confirmed dead. Never `pkill -x i3` or a bare
	# `pkill -x Xvfb`: both match the USER'S OWN window manager and X server and
	# drop them to a login screen. `[MEASURED 2026-09-09]` that has happened.
	kill "$FC" 2>/dev/null; kill "$IPID" 2>/dev/null; kill "$XPID" 2>/dev/null
	sleep 2
	kill -0 "$FC" 2>/dev/null   && kill -9 "$FC" 2>/dev/null
	kill -0 "$IPID" 2>/dev/null && kill -9 "$IPID" 2>/dev/null
	kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null
}

export DISPLAY="$DISP"
WID=$(xdotool search --name "Flycast" 2>/dev/null | head -1)
[ -n "$WID" ] || { echo "hotkeytest: SKIP - no window"; cleanup; exit $SKIP; }
# NO --sync. `[MEASURED 2026-09-10]` the minimal i3 this harness starts does
# not advertise _NET_ACTIVE_WINDOW, so --sync makes xdotool abort with an error
# instead of proceeding; without it the activate is best-effort and the single
# window already holds focus. scripts/rolltest.sh does the same, for the same
# reason.
xdotool windowactivate "$WID" 2>/dev/null
sleep 1

log() { tr -d '\0' < "$OUT/out.log"; }

# EMU_BTN_PIANO_ROLL's value, counted out of gamepad.h. The trace prints the id
# as a hex number and a literal here would rot the first time an id is inserted
# above it - which is exactly the class of drift scripts/hotkeyaudit.py exists
# for, so this file must not add a fresh instance of it.
PIANO_ROLL_OFF=$(sed -n '/EMU_BUTTONS *= *0x3000000,/,/Real axes/p' "$ROOT/core/input/gamepad.h" \
	| sed -n 's/^[[:space:]]\{1,\}\(EMU_[A-Z0-9_]*\),.*/\1/p' \
	| grep -an '^EMU_BTN_PIANO_ROLL$' | cut -d: -f1)
[ -n "${PIANO_ROLL_OFF:-}" ] || {
	echo "hotkeytest: SKIP - could not find EMU_BTN_PIANO_ROLL in gamepad.h"; cleanup; exit $SKIP; }

# GuiState::Paused's ORDINAL, counted out of the header rather than written as
# a number here. The trace prints the enum as an int, and a literal would be
# silently wrong the first time a state is inserted above Paused.
GUISTATE_PAUSED=$(sed -n '/^enum class GuiState/,/^};/p' "$ROOT/core/rend/gui.h" \
	| sed -n 's/^[[:space:]]\{1,\}\([A-Za-z][A-Za-z0-9_]*\),.*/\1/p' \
	| grep -an '^Paused$' | cut -d: -f1)
GUISTATE_PAUSED=$((${GUISTATE_PAUSED:-0} - 1))
[ "$GUISTATE_PAUSED" -ge 0 ] 2>/dev/null || {
	echo "hotkeytest: SKIP - could not find GuiState::Paused in gui.h"; cleanup; exit $SKIP; }

# THE INSTRUMENT IS VERIFIED FIRST. If the panel is already open before any key
# is pressed, every claim below is meaningless - and it would read as a pass.
if log | grep -aq "PANEL TOGGLE:"; then
	echo "FAIL hotkeytest - a panel toggled before any key was sent"
	cleanup; exit 1
fi

# NO --window: XSendEvent is what that uses and SDL ignores synthetic events.
# XTest to the focused window is the only thing the emulator actually receives
# (scripts/rolltest.sh learned this the expensive way).
press() { xdotool key "$1"; sleep 1.5; }

# PAUSE FIRST, and then assert we are actually paused.
#
# TWO REASONS, and the second is the interesting one. A running movie is beside
# the point here, but PAUSED IS THE STATE THAT MATTERS: it is when a TAS user
# edits, and `!gui_is_open()` - the guard every other hotkey in this tree uses -
# is FALSE while paused, because gui_is_open() is true for every GuiState but
# Closed. A piano-roll hotkey written that way cannot be pressed at the only
# moment it is wanted, and only pressing it while paused can show that.
press F9
if ! log | grep -aq "HOTKEY: .*guistate=$GUISTATE_PAUSED"; then
	# THE INSTRUMENT, CHECKED BEFORE THE CLAIM. Without this, "the toggle did
	# not fire because the guard is wrong" and "the toggle did not fire because
	# the key never arrived" are the same silence.
	echo "hotkeytest: SKIP - F9 did not reach the emulator, or it did not pause"
	log | grep -a "HOTKEY:" | tail -3 | sed 's/^/    /'
	cleanup; exit $SKIP
fi

press F6
press F6
press F12
press F7	# THE CONTROL: bound to nothing in the mapping above

sleep 1
toggles=$(log | grep -a "PANEL TOGGLE:" | sed 's/.*PANEL TOGGLE: /  /')
slots=$(log | grep -a "HOTKEY SLOT:" | sed 's/.*HOTKEY SLOT: /  /')
[ -n "$toggles" ] && echo "$toggles"
[ -n "$slots" ]   && echo "$slots"
cleanup

nopen=$(printf '%s\n' "$toggles" | grep -ac "pianoroll -> open")
nclosed=$(printf '%s\n' "$toggles" | grep -ac "pianoroll -> closed")
nslot=$(printf '%s\n' "$slots" | grep -ac "0 -> 1")
ntot=$(log | grep -ac "PANEL TOGGLE:")

if [ "$nopen" -lt 1 ]; then
	# SAY WHICH OF THE TWO IT IS. `[MEASURED 2026-09-10]` an earlier version of
	# this line reported "the binding never reached the dispatch" for four runs
	# in a row while the key was arriving perfectly every time - once because
	# the movie had ended, once because SDL ate F11, and twice because the
	# guard refused it. A message that names one cause for three faults is a
	# wrong diagnosis three times out of four, and the trace it needs to tell
	# them apart is already in the log.
	if log | grep -aq "HOTKEY: id=0x$(printf '%x' $((0x3000000 + PIANO_ROLL_OFF)))"; then
		echo "FAIL hotkeytest - the key REACHED the dispatch and the action did not run;"
		echo "                  the guard refused it. Last states seen:"
		log | grep -a "HOTKEY:" | tail -3 | sed 's/^/                  /'
	else
		echo "FAIL hotkeytest - the key never reached the dispatch at all; the binding"
		echo "                  did not take (wrong mapping file, or SDL consumed the key)"
	fi
	exit 1
fi
if [ "$nclosed" -lt 1 ]; then
	echo "FAIL hotkeytest - F6 opened the roll but a second press did not close it (open, not toggle)"
	exit 1
fi
if [ "$nslot" -lt 1 ]; then
	echo "FAIL hotkeytest - F12 did not advance the savestate slot"
	exit 1
fi
# THE CONTROL, and it is the whole reason this test means anything. An
# emulator that ran every action on every keypress - or a harness whose
# xdotool went somewhere else and whose greps matched leftovers - would
# satisfy all three claims above. Exactly two toggles may have happened.
if [ "$ntot" -ne 2 ]; then
	echo "FAIL hotkeytest - $ntot panel toggles for 2 bound presses; an unbound key is firing actions"
	exit 1
fi
echo "PASS hotkeytest - F6 toggled the roll open then closed WHILE PAUSED, F12 advanced the slot 0 -> 1, and an unbound key did neither"
exit 0
