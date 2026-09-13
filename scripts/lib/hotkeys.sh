# hotkeys.sh - ask the EMULATOR which key an action is bound to.
#
#   . "$ROOT/scripts/lib/hotkeys.sh"
#   key=$(hk_binding "$log" "States Window")   # -> "shift+F4", or empty
#
# WHY NOT JUST PRESS F4. Because the binding is CONFIGURABLE, and a harness that
# hardcodes a key is a second copy of a fact the emulator already owns - which
# CLAUDE.md rule 4 exists to prevent, and which rots in the quiet direction: the
# day a default moves, the test presses a key that does nothing and reports the
# FEATURE broken.
#
# `[SOURCE]` core/input/gamepad_device.cpp logs one line per registered action at
# startup:
#
#   HOTKEY BOUND: [Keyboard] States Window            Shift+F4 (code 65597)
#   HOTKEY BOUND: [Keyboard] Hotkey List              unbound
#
# so the mapping in force - defaults, or a profile a harness pinned - is readable
# from the emulator's own output with no second table anywhere.
#
# THE TWO WAYS TO USE THIS, and they answer different questions:
#
#   DISCOVER (this file) - press whatever is bound. Tests the path a user has,
#   whatever they have configured, and fails loudly if an action is unbound.
#
#   PIN A PROFILE (scripts/hotkeytest.sh) - write a mapping file, then press the
#   keys you wrote. Deterministic, and necessary when the test needs a key that
#   is NOT bound by default. But note what it costs: `[MEASURED 2026-09-10]`
#   every TAS action once shipped unbound and hotkeytest could not see it,
#   because a fixture that supplies the precondition cannot detect the
#   precondition missing. Pinning needs a separate defaults arm; discovering
#   does not.

# hk_binding <logfile> <action label> -> xdotool key string on stdout, or empty.
hk_binding() {
	local log="$1" label="$2" name
	name=$(tr -d '\0' < "$log" 2>/dev/null \
		| sed -n "s/.*HOTKEY BOUND: \[[^]]*\] *${label}  *\(.*\) (code .*/\1/p" \
		| tail -1)
	[ -n "$name" ] || return 0
	# The emulator names a chord "Shift+F4"; xdotool wants "shift+F4". Only the
	# MODIFIER is lowercased - "f4" is a different keysym from "F4" to xdotool.
	printf '%s\n' "$name" \
		| sed 's/Shift+/shift+/g; s/Ctrl+/ctrl+/g; s/Alt+/alt+/g'
}

# hk_is_unbound <logfile> <action label> -> 0 if the emulator says "unbound"
hk_is_unbound() {
	tr -d '\0' < "$1" 2>/dev/null | grep -aq "HOTKEY BOUND: \[[^]]*\] *$2  *unbound"
}

# hk_press <xdotool key string, e.g. "shift+F4">
#
# DECOMPOSED, never `xdotool key shift+F4`. `[SOURCE]` scripts/hotkeytest.sh and
# scripts/rolltest.sh both learned this separately: xdotool sends a combined
# chord faster than one emulated frame, and the host samples input state per
# frame, so the modifier and the key are never simultaneously true when it looks.
# Nothing arrives - not even a HOTKEY: trace - which reads as "the binding does
# not work" rather than "the gesture was never delivered".
hk_press() {
	local combo="$1" parts key i n
	IFS='+' read -ra parts <<< "$combo"
	n=${#parts[@]}
	key="${parts[$((n-1))]}"
	for ((i = 0; i < n - 1; i++)); do xdotool keydown "${parts[$i]}"; sleep 0.25; done
	xdotool keydown "$key"; sleep 0.35
	xdotool keyup   "$key"; sleep 0.25
	for ((i = n - 2; i >= 0; i--)); do xdotool keyup "${parts[$i]}"; sleep 0.25; done
	sleep 1.5
}

# hk_code <logfile> <action label> -> the numeric code the emulator reported
hk_code() {
	tr -d '\0' < "$1" 2>/dev/null \
		| sed -n "s/.*HOTKEY BOUND: \[[^]]*\] *$2  *.* (code \([0-9]*\)).*/\1/p" | tail -1
}

# hk_keyboard_mappings <logfile> -> one mapping FILENAME per keyboard device
#
# `[SOURCE]` the emulator names the file it looked for:
#   INPUT MAPPING: Keyboard has no mapping file (wanted SDL_Keyboard.cfg) - built-in defaults
#
# THERE IS USUALLY MORE THAN ONE. `[MEASURED 2026-09-13]` this machine presents
# "Keyboard" and "Kinesis Freestyle2 PC - KB800", and the built-in defaults land
# on the FIRST while the second reports every TAS action `unbound`. A harness
# that presses the default chord gets nothing at all - not even a HOTKEY trace -
# if the synthetic event is routed to the other device. Only keyboards: a mouse
# has no F4, and writing a mapping for one replaces its defaults with keys it
# cannot produce.
hk_keyboard_mappings() {
	tr -d '\0' < "$1" 2>/dev/null \
		| sed -n 's/.*INPUT MAPPING: \(.*\) has no mapping file (wanted \(.*\)) - built-in.*/\1\t\2/p' \
		| grep -ai "keyboard\|kb" | cut -f2 | sort -u
}

# hk_pin <configdir> <logfile> <code> <option name>
#
# Writes a mapping for EVERY keyboard device binding <code> to <option>, so the
# action responds whichever device the synthetic input is routed to. The code is
# DISCOVERED from the emulator rather than chosen here - so if a default moves,
# the harness follows it instead of pinning a stale number and testing nothing.
hk_pin() {
	local cfgdir="$1" log="$2" code="$3" opt="$4" f
	mkdir -p "$cfgdir/mappings"
	hk_keyboard_mappings "$log" | while IFS= read -r f; do
		[ -n "$f" ] || continue
		cat > "$cfgdir/mappings/$f" <<CFG
[emulator]
mapping_name = harness
dead_zone = 10
saturation = 100
rumble_power = 100
version = 3

[digital]
bind0 = $code:$opt
CFG
	done
}
