#!/usr/bin/env bash
# isotest - run flycast for automated testing WITHOUT touching the real desktop.
#
# WHY THIS EXISTS
#
# fbneo-rr/CLAUDE.md already states the rule, learned the hard way there:
#
#   "Automated/offscreen testing must not touch the user's real desktop."
#   "$I3SOCK/$SWAYSOCK silently override $DISPLAY ... talks to the *real* i3
#    on :1 and reports success, even though nothing is running on :3. This
#    already bit the codebase once (moved the user's Firefox)."
#
# and it names a harness (lemalta) as the single launch/screenshot/teardown
# entry point, precisely so nobody hand-rolls xdotool again. lemalta is
# FBNeo-specific, so this is the flycast-shaped equivalent of the same idea.
#
# THE ORDER OF PREFERENCE, and it matters
#
#   1. Drive it from Lua. flycast's Lua surface can start/stop capture, read
#      and write memory, press buttons, save and load state. fbneo-rr's whole
#      dev-scripts/ directory works this way. NO display interaction at all,
#      so nothing can be stolen and nothing depends on window focus.
#   2. If you genuinely need to see a UI element, use this script: a private
#      Xvfb, with the WM sockets stripped.
#   3. Synthetic input on the real display: never.
#
# Most of what looks like it needs a mouse does not. Reach for (1) first.
#
# USAGE
#   scripts/isotest.sh run  <rom> [seconds]        boot, screenshot, exit
#   scripts/isotest.sh key  <rom> <key> [seconds]  boot, send a key, screenshot
#   scripts/isotest.sh lua  <rom> <script> [secs]  boot with a Lua script
#   scripts/isotest.sh stop                        tear the display down
#
# Screenshots land in $OUT (default /tmp/flycast-isotest).

set -uo pipefail

DISPLAY_NUM="${ISOTEST_DISPLAY:-:99}"
GEOM="${ISOTEST_GEOM:-1280x1024x24}"
OUT="${ISOTEST_OUT:-/tmp/flycast-isotest}"
BIN="${ISOTEST_BIN:-$(dirname "$0")/../build/flycast}"

mkdir -p "$OUT"

# The whole point: no I3SOCK/SWAYSOCK, and a DISPLAY that is not the user's.
iso() { env -u I3SOCK -u SWAYSOCK -u WAYLAND_DISPLAY DISPLAY="$DISPLAY_NUM" "$@"; }

ensure_display() {
	if ! iso xdpyinfo >/dev/null 2>&1; then
		echo "isotest: starting Xvfb on $DISPLAY_NUM ($GEOM)"
		nohup Xvfb "$DISPLAY_NUM" -screen 0 "$GEOM" >"$OUT/xvfb.log" 2>&1 &
		disown
		for _ in $(seq 1 20); do
			iso xdpyinfo >/dev/null 2>&1 && break
			sleep 0.5
		done
	fi
	iso xdpyinfo >/dev/null 2>&1 || { echo "isotest: no display"; exit 1; }
}

# POLICY, separate from mechanism - lemalta splits these deliberately
# (Target.may_synthesise_input vs Host.send_keys) and the split is the point:
# a safe key-sending primitive is still unsafe if pointed at a live desktop.
#
# Synthetic input is allowed ONLY on a display this script provisioned.
assert_isolated() {
	if [ "$DISPLAY_NUM" = "${REAL_DISPLAY:-:1}" ]; then
		echo "isotest: REFUSING - $DISPLAY_NUM is the real session"; exit 1
	fi
	if [ -n "${I3SOCK:-}${SWAYSOCK:-}" ] && [ "${ISOTEST_ALLOW_WM_SOCKETS:-}" != "1" ]; then
		: # iso() strips them per command; this just records that they are present
	fi
}

# A window manager on the offscreen display. Without one nothing takes focus,
# so SDL discards every key and a UI test silently proves nothing.
ensure_wm() {
	# A running WM sets _NET_SUPPORTING_WM_CHECK on the root window. That is the
	# EWMH-defined probe and needs no wmctrl (not installed here); the previous
	# check used a malformed `xdotool search --class .` that printed a usage
	# error every run and never detected anything.
	if iso xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id"; then
		return
	fi
	# A MINIMAL CONFIG IS MANDATORY, not a nicety.
	#
	# `i3` with no -c reads ~/.config/i3/config - the user's real one - and runs
	# every `exec` line in it. Measured here: it started xss-lock (a screen
	# LOCKER) and an alacritty, and would have gone on to firefox, code,
	# nm-applet, nitrogen --restore, dex --autostart and setup-layout. Killing
	# i3 then orphaned xss-lock to init.
	#
	# An offscreen WM exists to give windows focus. It must bring nothing else.
	cat >"$OUT/wm.conf" <<'WMCONF'
# minimal: focus and mapping only. NO exec lines, NO bar, NO keybindings.
font pango:monospace 8
WMCONF
	for wm in i3 openbox fluxbox matchbox-window-manager; do
		if command -v "$wm" >/dev/null 2>&1; then
			# i3 on the offscreen display must resolve via that display's root
			# atom, which is exactly why iso() strips I3SOCK - see _i3_env().
			if [ "$wm" = "i3" ]; then
				nohup env -u I3SOCK -u SWAYSOCK DISPLAY="$DISPLAY_NUM" \
					i3 -c "$OUT/wm.conf" >"$OUT/wm.log" 2>&1 &
			else
				nohup env -u I3SOCK -u SWAYSOCK DISPLAY="$DISPLAY_NUM" "$wm" \
					>"$OUT/wm.log" 2>&1 &
			fi
			WM_PID=$!
			disown
			echo "$WM_PID" >"$OUT/wm.pid"
			sleep 2
			echo "isotest: window manager $wm on $DISPLAY_NUM (pid $WM_PID, minimal config)"
			return
		fi
	done
	echo "isotest: WARNING - no WM available; keys will not reach the window"
}

# flycast does NOT set _NET_WM_PID, so `xdotool search --pid` finds nothing and
# a caller that trusts it concludes "no window" for a window that is right
# there. Match the title instead; lemalta does the same (fbneo_window_ids).
find_window() {
	iso xdotool search --name "[Ff]lycast" 2>/dev/null | tail -1
}

shoot() {  # shoot <pid> <name>
	local pid="$1" name="$2" wid
	wid=$(find_window)
	if [ -n "$wid" ]; then
		iso import -window "$wid" "$OUT/$name.png" 2>/dev/null
	else
		iso import -window root "$OUT/$name.png" 2>/dev/null
	fi
	echo "isotest: $OUT/$name.png"
}

launch() {  # launch <rom> [extra flycast args...]
	local rom="$1"; shift
	iso "$BIN" "$rom" "$@" >"$OUT/run.log" 2>&1 &
	echo $!
}

cmd="${1:-}"; shift || true
assert_isolated

case "$cmd" in
run)
	ensure_display
	rom="$1"; secs="${2:-20}"
	pid=$(launch "$rom"); sleep "$secs"
	shoot "$pid" "run"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	;;
key)
	ensure_display
	rom="$1"; key="$2"; secs="${3:-20}"
	ensure_wm
	pid=$(launch "$rom"); sleep "$secs"
	wid=$(find_window)
	# lemalta's send_keys contract, copied rather than reinvented:
	#
	#   "Window-scoped on purpose, never display-global. A global key or click
	#    goes wherever focus happens to be, which on an attached display is
	#    whatever the person is actually using -- that has gone wrong here
	#    before."
	#      -- anita/lemalta/lemalta.py, Host.send_keys
	#
	# and its mechanism: `if not wid: return False   # never fall back to global`
	if [ -z "$wid" ]; then
		echo "isotest: no flycast window found - REFUSING to send a global key"
		kill "$pid" 2>/dev/null; exit 1
	fi
	# A bare Xvfb has no WM, so nothing ever takes focus and SDL ignores keys.
	# ensure_wm() starts one on the offscreen display; this is the piece that
	# was missing, not the key-sending.
	iso xdotool windowfocus --sync "$wid" 2>/dev/null
	sleep 1
	iso xdotool key --window "$wid" --clearmodifiers "$key"; sleep 3
	shoot "$pid" "key_$key"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	;;
lua)
	ensure_display
	rom="$1"; script="$2"; secs="${3:-40}"
	# LuaFileName resolves through get_readonly_config_path(), so it is a NAME
	# inside the config dir, not a path. Copy in, run, and always restore -
	# clobbering the user's flycast.lua is a real hazard, not a hypothetical.
	cfgdir="${XDG_CONFIG_HOME:-$HOME/.config}/flycast-dojo"
	saved=""
	if [ -f "$cfgdir/flycast.lua" ]; then
		saved="$OUT/flycast.lua.saved"; cp "$cfgdir/flycast.lua" "$saved"
		echo "isotest: preserved your flycast.lua -> $saved"
	fi
	cp "$script" "$cfgdir/flycast.lua"
	pid=$(launch "$rom"); sleep "$secs"
	shoot "$pid" "lua"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	if [ -n "$saved" ]; then cp "$saved" "$cfgdir/flycast.lua"; else rm -f "$cfgdir/flycast.lua"; fi
	echo "isotest: config dir restored"
	;;
stop)
	# Kill the WM's CHILDREN first: killing i3 alone orphans whatever it
	# spawned to init, where nothing will ever clean it up.
	if [ -f "$OUT/wm.pid" ]; then
		wmpid=$(cat "$OUT/wm.pid")
		pkill -P "$wmpid" 2>/dev/null
		kill "$wmpid" 2>/dev/null
		rm -f "$OUT/wm.pid"
		echo "isotest: window manager stopped"
	fi
	pkill -f "Xvfb $DISPLAY_NUM" && echo "isotest: display stopped" || echo "isotest: nothing to stop"
	;;
*)
	sed -n '2,40p' "$0"; exit 1
	;;
esac
