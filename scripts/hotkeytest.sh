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
	-config dojo:AutoLoadNetState=no -config dojo:HotkeyTrace=yes \
	"$ROM" > "$OUT/probe.log" 2>&1 & PC=$!
sleep 10
kill "$PC" 2>/dev/null; kill "$XPID" 2>/dev/null
sleep 2
kill -0 "$PC" 2>/dev/null    && kill -9 "$PC" 2>/dev/null
kill -0 "$XPID" 2>/dev/null  && kill -9 "$XPID" 2>/dev/null
sleep 1

# ---------------------------------------------------------------------------
# THE DEFAULTS ARM, and it is here because its absence hid a real defect.
#
# `[MEASURED 2026-09-10]` every TAS action shipped UNBOUND. The registry, the
# audit, this test and the cheat sheet all worked, over six actions nobody could
# press - and THIS TEST COULD NOT SEE IT, because it writes its own mapping file
# below. A fixture that supplies the precondition cannot detect the precondition
# missing.
#
# The probe pass above ran with an EMPTY config directory, so its log is exactly
# the out-of-the-box state. Read the defaults out of it.
defaults=$(tr -d '\0' < "$OUT/probe.log" | grep -a "HOTKEY BOUND: \[Keyboard\]")
if [ -z "$defaults" ]; then
	echo "hotkeytest: SKIP - the probe logged no default bindings at all"
	exit $SKIP
fi
unbound=$(printf '%s\n' "$defaults" | grep -ac "unbound" || true)
echo "  out of the box, on a fresh config:"
printf '%s\n' "$defaults" | sed 's/.*HOTKEY BOUND: /    /'
ndef=$(printf '%s\n' "$defaults" | wc -l)
if [ "$ndef" -lt 6 ]; then
	echo "FAIL hotkeytest - only $ndef TAS actions are bound by default; a hotkey"
	echo "                  nobody can press is a hotkey that does not exist"
	exit 1
fi
# AND THEY MUST BE NAMEABLE. A default stored as a chord that cannot be rendered
# would show in the settings window and the cheat sheet as a raw number.
if printf '%s\n' "$defaults" | grep -aq " ? (code"; then
	echo "FAIL hotkeytest - a default binding has no name:"
	printf '%s\n' "$defaults" | grep -a " ? (code" | sed 's/^/    /'
	exit 1
fi

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
# BOUND ONLY AS A CHORD: 62 is F5, +0x10000 is Shift. The States window has no
# plain-key binding here on purpose - so "all 6 bound actions reached the
# dispatch" cannot pass unless the modifier survived the whole path, and the
# emulator's own HOTKEY BOUND line has to print it as "Shift+F5".
bind3 = 65598:btn_slot_picker
bind4 = 61:btn_savestate_slot_prev
bind5 = 60:btn_gen_archive
# A CHORD: Shift+F6. 63 is F6, 0x10000 (65536) is KEY_MOD_SHIFT, so 65599 is the
# two together - one ordinary number, which is the point of packing the modifier
# into the code's high bits.
bind6 = 65599:btn_fforward
bind7 = 59:btn_hotkey_help
bind8 = 58:btn_step
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

# THE LOG, FLATTENED ONCE TO A FILE - never piped into a matcher.
#
# `[MEASURED 2026-09-10]` this was `log() { tr -d '\0' < "$OUT/out.log"; }` and
# every check was `log | grep -aq ...`. Under `set -o pipefail` that is a RACE:
# grep -q exits the instant it matches, tr takes SIGPIPE, and the PIPELINE
# reports 141 - failure - even though the match succeeded. Whether it happens
# depends on whether tr finished writing first, which depends on HOW BIG THE LOG
# IS, which has nothing to do with what is being checked.
#
# It showed up as a sabotage failing the wrong claim: the broken build logged 54
# scrub events instead of 10, the bigger log lost the race, and seven actions
# that are plainly in the log were reported as never reaching the dispatch.
#
# `[CORRECTED 2026-09-11]` the first write-up of this said the passing runs were
# "green by luck". THAT IS BACKWARDS, and measuring it settles it:
#
#     cat big.txt | grep -q MATCH   -> 141   (a match, reported as failure)
#     cat big.txt | grep -q NOPE    -> 1     (correct)
#
# pipefail can only turn a success into a failure, never the reverse. So this
# race produces FALSE FAILURES - loud ones - and the harm is flakiness and
# misattribution, not a check that silently agrees with everything.
#
# CLAUDE.md already carries this family - "$? was reading tail at the end of a
# pipeline". Same trap, other end of the pipe. There is no pipeline now.
flatten() { tr -d '\0' < "$OUT/out.log" > "$OUT/flat.log"; }
log() { flatten; cat "$OUT/flat.log"; }
has() { flatten; grep -aq "$1" "$OUT/flat.log"; }
countOf() { flatten; grep -ac "$1" "$OUT/flat.log" || true; }

# EMU_BTN_PIANO_ROLL's value, counted out of gamepad.h. The trace prints the id
# as a hex number and a literal here would rot the first time an id is inserted
# above it - which is exactly the class of drift scripts/hotkeyaudit.py exists
# for, so this file must not add a fresh instance of it.
PIANO_ROLL_OFF=$(sed -n '/EMU_BUTTONS *= *0x3000000,/,/Real axes/p' "$ROOT/core/input/gamepad.h" \
	| sed -n 's/^[[:space:]]\{1,\}\(EMU_[A-Z0-9_]*\),.*/\1/p' \
	| grep -an '^EMU_BTN_PIANO_ROLL$' | cut -d: -f1)
[ -n "${PIANO_ROLL_OFF:-}" ] || {
	echo "hotkeytest: SKIP - could not find EMU_BTN_PIANO_ROLL in gamepad.h"; cleanup; exit $SKIP; }

# EVERY ACTION'S ID, counted out of gamepad.h - the trace prints them as hex
# and a literal here would rot the first time an id is inserted above one.
# EMU_BUTTONS is 0x3000000 and the enumerators that follow it are consecutive.
idof() {	# $1 = EMU_BTN_* name -> its id in hex, or empty
	local off
	off=$(sed -n '/EMU_BUTTONS *= *0x3000000,/,/Real axes/p' "$ROOT/core/input/gamepad.h" \
		| sed -n 's/^[[:space:]]\{1,\}\(EMU_[A-Z0-9_]*\),.*/\1/p' \
		| grep -an "^$1\$" | cut -d: -f1)
	[ -n "$off" ] && printf '%x' $((0x3000000 + off))
}

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
if has "PANEL TOGGLE:"; then
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
if ! has "HOTKEY: .*guistate=$GUISTATE_PAUSED"; then
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
# THE OTHER THREE TAS ACTIONS. `dojo:HotkeyTrace` logs EVERY action that
# reaches the dispatch with its id, so breadth here costs one keypress each and
# needs no per-action observable - no panel trace for one, no slot trace for
# another, no archive log for a third. That is what makes covering all of them
# affordable, and coverage is what a hotkey REGISTRY would need before its
# migration could be trusted (docs/HOTKEYS.md).
press shift+F5	# btn_slot_picker - bound ONLY as a chord
press F4	# btn_savestate_slot_prev
press F3	# btn_gen_archive
press F2	# btn_hotkey_help - toggles the cheat sheet panel
# ---- CHORDS -------------------------------------------------------------
# `[PORTED 2026-09-10]` keyboard chords, from the TAS fork, where they are what
# makes ~30 TAS actions fit on one keyboard.
press shift+F6	# BOUND chord -> btn_fforward, and NOT btn_piano_roll
press shift+F3	# UNBOUND chord -> must fall back to plain F3, btn_gen_archive

# RULE 2: RELEASE WITH THE CODE YOU PRESSED WITH.
# Let go of the modifier BEFORE the key. Without the rule the press goes out as
# Shift+F6 and the release as plain F6 - so the chord's action is never told it
# was released and stays held forever, while a release arrives for a key that
# was never pressed. xdotool's `key shift+F6` cannot show this; the events have
# to be ordered by hand.
xdotool keydown shift; sleep 0.2
xdotool keydown F6;    sleep 0.2
xdotool keyup shift;   sleep 0.2
xdotool keyup F6;      sleep 1.5

press F7	# THE CONTROL: bound to nothing in the mapping above

# ---- TAP vs HOLD, LAST ---------------------------------------------------
# "Tap for one frame - hold to scrub in slow motion" `[SOURCE]` the TAS fork's
# help text for EMU_BTN_STEP. The two halves are one claim: a tap that advanced
# many, or a hold that advanced one, are both the feature not working, and
# either alone is satisfied by a step key that does nothing at all.
#
# LAST, AND THAT IS NOT TIDINESS. `[MEASURED 2026-09-10]` a latched hold does
# not merely keep scrubbing - it runs the movie to its end, which puts the
# emulator in GuiState::ReplayEnd where every hotkey is correctly refused. With
# this arm in the middle, sabotaging release() failed the run at "bound keys
# that never reached the dispatch" instead of at the latch check written for it:
# a real failure, attributed to the wrong cause. Nothing after this arm can be
# poisoned by it now.
press F1		# a TAP
xdotool keydown F1; sleep 1.5; xdotool keyup F1; sleep 1.5
# THE LATCH CHECK. "It scrubbed a lot" is satisfied BY a latch, so counting
# frames cannot be the whole claim. Sample the total twice with the key long
# since up: a hold that was released cannot have moved between them.
scrubA=$(countOf "HOTKEY STEP: scrub")
sleep 1.5
scrubB=$(countOf "HOTKEY STEP: scrub")

sleep 1
toggles=$(log | grep -a "PANEL TOGGLE:" | sed 's/.*PANEL TOGGLE: /  /')
slots=$(log | grep -a "HOTKEY SLOT:" | sed 's/.*HOTKEY SLOT: /  /')
[ -n "$toggles" ] && echo "$toggles"
[ -n "$slots" ]   && echo "$slots"
cleanup

# EVERY BOUND ACTION MUST HAVE REACHED THE DISPATCH. One line per action, and
# the id is derived, so a renamed or reordered enumerator fails loudly here
# rather than silently checking nothing.
missing=""
for a in EMU_BTN_PIANO_ROLL EMU_BTN_SAVESTATE_SLOT_NEXT EMU_BTN_SLOT_PICKER \
		EMU_BTN_SAVESTATE_SLOT_PREV EMU_BTN_GEN_ARCHIVE EMU_BTN_PAUSE; do
	id=$(idof "$a")
	if [ -z "$id" ]; then
		echo "FAIL hotkeytest - $a is not in gamepad.h; this check is not checking it"
		exit 1
	fi
	has "HOTKEY: id=0x$id " || missing="$missing $a"
done
# The chord's target. Bound ONLY as Shift+F6, so seeing it at all proves the
# modifier reached the mapping layer packed into the code.
chordId=$(idof EMU_BTN_FFORWARD)
has "HOTKEY: id=0x$chordId " || missing="$missing EMU_BTN_FFORWARD(chord)"
if [ -n "$missing" ]; then
	echo "FAIL hotkeytest - bound keys that never reached the dispatch:$missing"
	log | grep -a "HOTKEY:" | tail -8 | sed 's/^/    /'
	exit 1
fi
echo "  all 7 bound actions reached the dispatch"

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
	if has "HOTKEY: id=0x$(printf '%x' $((0x3000000 + PIANO_ROLL_OFF)))"; then
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
# THE CONTROL, and it is the whole reason this test means anything.
#
# NOT a count of toggles - a count is satisfied by the wrong actions firing the
# right number of times. The trace names every action that reached the dispatch,
# so the check is a SET COMPARISON: exactly the six ids that were bound, and
# nothing else. An emulator running every action on every keypress, or a harness
# whose keys went somewhere else and whose greps matched leftovers, shows up as
# an id that is not in the mapping file.
bound=""
for a in EMU_BTN_PIANO_ROLL EMU_BTN_SAVESTATE_SLOT_NEXT EMU_BTN_SLOT_PICKER \
		EMU_BTN_SAVESTATE_SLOT_PREV EMU_BTN_GEN_ARCHIVE EMU_BTN_PAUSE \
		EMU_BTN_FFORWARD EMU_BTN_HOTKEY_HELP EMU_BTN_STEP; do
	bound="$bound 0x$(idof "$a")"
done
saw=$(log | grep -aoE "HOTKEY: id=0x[0-9a-f]+" | sed 's/.*id=//' | sort -u)
extra=""
for id in $saw; do
	case " $bound " in *" $id "*) ;; *) extra="$extra $id" ;; esac
done
if [ -n "$extra" ]; then
	echo "FAIL hotkeytest - actions fired that were never bound:$extra"
	echo "                  bound were:$bound"
	exit 1
fi
echo "  no action fired that was not bound ($(printf '%s\n' "$saw" | wc -l) distinct ids seen)"

# RULE 1, THE SAFETY ONE: an UNBOUND chord must fall through to the plain key.
# Shift+F3 is bound to nothing, so it has to arrive as plain F3 - btn_gen_archive
# - which was pressed once already, making TWO. If the raw key were swallowed
# whenever a modifier happened to be held, holding Shift during play would eat
# game inputs, and that is what makes this the claim worth having.
gaId=$(idof EMU_BTN_GEN_ARCHIVE)
gaDown=$(countOf "HOTKEY: id=0x$gaId down")
if [ "$gaDown" -ne 2 ]; then
	echo "FAIL hotkeytest - an unbound chord did not fall back to the plain key:"
	echo "                  expected 2 presses of EMU_BTN_GEN_ARCHIVE (F3, then shift+F3), saw $gaDown"
	exit 1
fi
echo "  an unbound chord fell through to the plain key"

# RULE 2's claim: every action that went DOWN also came back UP. A chord whose
# release was misrouted leaves its target down forever, which in the case of
# fast-forward means the emulator never returns to normal speed.
stuck=""
for id in $saw; do
	d=$(countOf "HOTKEY: id=$id down")
	u=$(countOf "HOTKEY: id=$id up")
	[ "$d" -eq "$u" ] || stuck="$stuck $id(down=$d,up=$u)"
done
if [ -n "$stuck" ]; then
	echo "FAIL hotkeytest - actions went down more often than they came up:$stuck"
	echo "                  a chord released under a different code strands its target"
	exit 1
fi
echo "  every action that went down came back up"

# THE CHEAT SHEET OPENED. Reaching the dispatch is not the same as the panel
# appearing - the registry could enumerate it, the key could arrive, and
# panels::toggle could still be pointed at an id nobody registered, which logs
# loudly and does nothing.
if ! printf '%s\n' "$toggles" | grep -aq "hotkeys -> open"; then
	echo "FAIL hotkeytest - the hotkey cheat sheet never opened"
	printf '%s\n' "$toggles" | sed 's/^/    /'
	exit 1
fi
echo "  the hotkey cheat sheet opened"

# TAP vs HOLD, from the emulator's own trace of what the step key did.
taps=$(countOf "HOTKEY STEP: tap")
scrub=$(log | grep -aoE "HOTKEY STEP: scrub \+[0-9]+" | sed 's/.*+//' | awk '{n+=$1} END {print n+0}')
if [ "$taps" -lt 1 ]; then
	echo "FAIL hotkeytest - the step key never registered a tap"
	exit 1
fi
if [ "$scrub" -lt 2 ]; then
	echo "FAIL hotkeytest - holding the step key scrubbed $scrub frames; a hold that"
	echo "                  advances one frame is a tap, which is the feature missing"
	exit 1
fi
if [ "$scrubA" -ne "$scrubB" ]; then
	echo "FAIL hotkeytest - the scrub kept going after the key came up"
	echo "                  ($scrubA scrub events, then $scrubB a second later)"
	echo "                  a hold whose release was lost latches forever - which is"
	echo "                  what stranded the fork's scrub at End of Replay"
	exit 1
fi
echo "  the step key taps ($taps) and scrubs ($scrub frames while held)"
echo "  ...and the scrub stopped when the key came up ($scrubA events, still $scrubB)"

# ...AND HAS SOMETHING TO SHOW. The panel falls back to "No keyboard that can
# name its keys" when it finds no device able to name a scancode, and an open
# panel saying that looks exactly like a working one from out here.
sheet=$(log | grep -a "HOTKEY PANEL: reading bindings" | tail -1)
case "$sheet" in
	*"[(none)]"*|"")
		echo "FAIL hotkeytest - the cheat sheet opened but found no keyboard to read"
		[ -n "$sheet" ] && echo "    ${sheet##*N\[RENDERER\]: }"
		exit 1 ;;
esac
echo "  ${sheet##*N\[RENDERER\]: }"
echo "PASS hotkeytest - seven bound actions all reached the dispatch WHILE PAUSED, the roll toggled open then closed, the slot went 0 -> 1, and nothing fired that was not bound"
exit 0
