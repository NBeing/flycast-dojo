#!/usr/bin/env bash
# Verify flycast.replay.* against the real engine, positive and negative.
#
# The negative half is the point and it took four attempts to make honest:
#
#  1. "no clip folder was created" is ALSO what you get when the Lua never ran.
#     Every run therefore writes /tmp/rb-marker.txt BEFORE calling anything, so
#     "did not run" and "refused" cannot look alike. The first PASS here was a
#     false pass caught only by adding this.
#  2. A replay boots FROZEN (Replay::Init arms dojo.stepping) and lua::overlay()
#     lives inside gui_display_osd(), which the renderers call from their
#     present - no guest frames, no present, so neither vblank nor overlay ever
#     fires. Use the `start` event, and unfreeze with AutoSeekState.
#  3. Boot stalls outright downloading a netplay savestate ("save url: ...
#     Remote file not found") and never reaches Emulator::start(), so Lua never
#     initialises. The netplay leftovers must be forced off - the "blocked
#     headless boots" hazard CLAUDE.md already warns about.
#
# PASS requires the marker to say startRecording=false AND the clip-folder count
# to be unchanged. Either alone proves nothing.
set -uo pipefail
BIN="${1:-$(dirname "$0")/../build-dojo7/flycast}"
ROM="${2:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FLYR="${3:-}"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/flycast-dojo/replays"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}/flycast-dojo"
[ -n "$FLYR" ] || { echo "usage: $0 <bin> <rom> <existing .flyr>"; exit 1; }

# WORK ON A COPY. Opening a clip is not read-only: tas_clip reconciles and
# REWRITES clip.json next to the movie, so pointing this at the in-repo fixture
# leaves scripts/fixtures/clip.json modified in the working tree every run.
WORK=$(mktemp -d)
cp "$FLYR" "$WORK/" 2>/dev/null
[ -f "$(dirname "$FLYR")/clip.json" ] && cp "$(dirname "$FLYR")/clip.json" "$WORK/" 2>/dev/null
FLYR="$WORK/$(basename "$FLYR")"

SAVED=""
if [ -f "$CFG/flycast.lua" ]; then SAVED=$(mktemp); cp "$CFG/flycast.lua" "$SAVED"; fi
restore() {
	if [ -n "$SAVED" ]; then cp "$SAVED" "$CFG/flycast.lua"; rm -f "$SAVED"; else rm -f "$CFG/flycast.lua"; fi
	[ -n "${WORK:-}" ] && rm -rf "$WORK"
}
trap restore EXIT

rm -f /tmp/rb-marker.txt
cat > "$CFG/flycast.lua" <<'LUA'
local done = false
local function probe(via)
    if done then return end
    done = true
    local f = io.open("/tmp/rb-marker.txt", "w")
    local rec0 = tostring(flycast.replay.isRecording())
    local ok   = flycast.replay.startRecording("SHOULD_NOT_EXIST")
    f:write("ran=yes via="..via.." isRecording_before="..rec0..
            " startRecording="..tostring(ok)..
            " isRecording_after="..tostring(flycast.replay.isRecording())..
            " currentPath="..tostring(flycast.replay.currentPath()).."\n")
    f:close()
    if ok then flycast.replay.stopRecording() end
end
flycast_callbacks = {}
flycast_callbacks.start  = function() probe("start")  end
flycast_callbacks.vblank = function() probe("vblank") end
LUA

BEFORE=$(ls -d "$DATA"/*/*/ 2>/dev/null | wc -l)
env -u I3SOCK -u SWAYSOCK -u WAYLAND_DISPLAY DISPLAY="${ISOTEST_DISPLAY:-:99}" \
  "$BIN" -config dojo:Replay=yes -config "dojo:ReplayFilename=$FLYR" \
  -config dojo:UiIni=no -config dojo:AutoSeekState=0 \
  -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
  "$ROM" >/tmp/rb-neg.log 2>&1 &
PID=$!; sleep 35; kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null
AFTER=$(ls -d "$DATA"/*/*/ 2>/dev/null | wc -l)

M=$(cat /tmp/rb-marker.txt 2>/dev/null || true)
echo "marker: ${M:-<none>}"
echo "clip folders: $BEFORE -> $AFTER"
if [ -z "$M" ]; then echo "FAIL: script never ran - result is meaningless"; exit 1; fi
case "$M" in *"startRecording=false"*) ;; *) echo "FAIL: recording started during playback"; exit 1;; esac
[ "$BEFORE" = "$AFTER" ] || { echo "FAIL: a clip folder was created during playback"; exit 1; }
# currentPath non-empty + isRecording false proves play_match was true (GGPO is
# off), i.e. the guard was exercised in the state it exists for - not skipped.
case "$M" in *"isRecording_after=false"*"currentPath=/"*) echo "PASS: refused, and isRecording correctly false with a file attached";; *) echo "WARN: guard state unconfirmed";; esac
