#!/usr/bin/env bash
# Record a movie, replay it, and require the machine to walk the SAME PATH.
#
# THE GAP. Every other test here plays back a movie somebody already made --
# scripts/testrun.sh hardcodes `dojo:Replay=yes`. Nothing had ever recorded one,
# and the studio being ported is a re-record tool. AppendToReplay batches in
# FRAME_BATCH=120 chunks and silently discards up to 119 frames if the flush is
# missed; that path had no coverage at all.
#
# WHY IT STARTS FROM A SAVESTATE. Two reasons, and the second is the load-bearing
# one:
#   * [MEASURED 2026-09-08] v1 of this harness recorded 600 frames of the boot
#     sequence. The machine is on rails there, so a movie of neutral frames
#     replays identically to a movie of DIFFERENT neutral frames -- the round
#     trip passed without depending on the movie at all.
#   * replay.startRecording() refuses inside a replay session
#     (core/lua/lua.cpp: `if (dojo.play_match || dojo.replay.replay_loaded)`),
#     so the recording half cannot ride someone else's clip. It boots plain and
#     gets in-game by loading a state.
#
# THE ORACLE IS A PER-FRAME SEQUENCE, NOT AN ENDPOINT, and that is borrowed from
# nbneo-rr's state-torture (tools/state-torture.cpp): record a hash per frame and
# require the two runs to match element for element. An endpoint hash cannot tell
# a run that never diverged from one that diverged and came back, and it cannot
# name the frame where it happened.
#
# AND THE VACUITY GATE IS THE PRIMARY DEFENCE, also from that harness: the
# recorded sequence must contain more than one distinct hash. A frozen machine
# reproduces itself perfectly and proves nothing. v1 tried to discover this
# backwards by corrupting the movie and hoping something moved; asking forwards
# cannot be defeated by a bad offset calculation, which is exactly how the
# backwards version fooled itself.
#
# Exit: 0 pass, 1 fail, 77 skip.
set -uo pipefail
SKIP=77
ROM="${ROM:-/home/nbee/dev/davids_fly/NoBGM_VMU.cdi}"
HERE="$(cd "$(dirname "$0")" && pwd)"
EXE="$HERE/../build-dojo7/flycast"
DISP=":${RECORDTEST_DISPLAY:-76}"
WINDOW="${RECORDTEST_WINDOW:-60}"
OUT="$(mktemp -d)"

command -v Xvfb >/dev/null || { echo "recordtest: SKIP - no Xvfb"; exit $SKIP; }
[ -f "$ROM" ] || { echo "recordtest: SKIP - no ROM at $ROM"; exit $SKIP; }
[ -x "$EXE" ] || { echo "recordtest: SKIP - not built"; exit $SKIP; }

# A STATE TO START FROM, and it must be one the machine is MOVING in. Newest
# clip savestate wins; the vacuity gate below is what actually judges whether it
# was a good choice, so this only has to be a reasonable guess.
SEED=$(ls -t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.state 2>/dev/null | head -1)
[ -n "$SEED" ] && [ -f "$SEED" ] || {
	echo "recordtest: SKIP - no savestate in the replay library to seed from"
	echo "  (record a clip and save a state in it; boot-sequence recording is"
	echo "   vacuous, which is why this refuses rather than falling back to it)"
	exit $SKIP; }

mkdir -p "$OUT/config/flycast-dojo" "$OUT/data/flycast-dojo"
# Slot 0 is "<basename>.state" with no index suffix (oslib.cpp getSavestatePath).
cp "$SEED" "$OUT/data/flycast-dojo/NoBGM_VMU.state"
[ -f "$SEED.frame" ] && cp "$SEED.frame" "$OUT/data/flycast-dojo/NoBGM_VMU.state.frame"
echo "  seed state: $SEED"

cleanup() { pkill -f "Xvfb $DISP" 2>/dev/null; return 0; }
trap cleanup EXIT
Xvfb "$DISP" -screen 0 640x480x24 >/dev/null 2>&1 &
sleep 2

write_script() {
	cat > "$OUT/config/flycast-dojo/flycast.lua" <<LUAEOF
local PHASE  = "$1"
local WINDOW = $WINDOW
local out = "$OUT/" .. PHASE .. ".txt"
local function w(s) local f = io.open(out, "a"); if f then f:write(s .. "\n"); f:close() end end

local n, stage, first, taken = 0, "boot", nil, 0
local prev = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prev then prev() end
	n = n + 1
	local f = flycast.frame.count()

	if PHASE == "record" then
		-- Boot, then jump in-game with the seed state, THEN start the movie.
		if stage == "boot" then
			if n < 200 then return end
			if not pcall(flycast.savestate.load, 0) then
				w("err=could not load the seed state"); w("ok"); flycast.emulator.exit(); return
			end
			stage = "settle"; return
		end
		if stage == "settle" then
			-- A few frames for the load to land before the movie opens.
			if n < 220 then return end
			local started = flycast.replay.startRecording("")
			if not started then
				w("err=startRecording refused"); w("ok"); flycast.emulator.exit(); return
			end
			-- THE ANCHOR. A movie started mid-session has no frame 0, so it is
			-- replayable only paired with a state at its first frame. Saving it
			-- here is not bookkeeping - without it phase 2 has nothing to seek to.
			if not pcall(flycast.savestate.save, 0) then
				w("err=could not save the anchor state"); w("ok"); flycast.emulator.exit(); return
			end
			first = flycast.frame.count()
			w("first=" .. tostring(first))
			w("clip=" .. tostring(flycast.replay.currentPath()))
			w("recording=" .. tostring(flycast.replay.isRecording()))
			stage = "collect"; return
		end
	else
		-- Replay: AutoSeekState lands us on the anchor; start collecting there.
		if stage == "boot" then
			if flycast.frame.count() < 1 then return end
			if first == nil then first = flycast.frame.count(); w("first=" .. tostring(first)) end
			stage = "collect"
		end
	end

	if stage ~= "collect" then return end
	if taken >= WINDOW then return end
	taken = taken + 1
	w("H " .. tostring(f) .. " " .. tostring(flycast.savestate.hash()))
	if taken < WINDOW then return end

	if PHASE == "record" then flycast.replay.stopRecording() end
	w("ok")
	flycast.emulator.exit()
end
LUAEOF
}

launch() {
	local phase="$1"; shift
	write_script "$phase"
	XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" \
		setsid "$EXE" -config dojo:UiIni=no -config dojo:NativeConsole=no \
			-config dojo:StartupPrompt=no -config dojo:GamePanel=no \
			-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no \
			-config dojo:Receiving=no \
			"$@" "$ROM" > "$OUT/$phase.log" 2>&1 &
	local pid=$! waited=0
	while [ $waited -lt 300 ]; do
		grep -q '^ok$' "$OUT/$phase.txt" 2>/dev/null && break
		kill -0 $pid 2>/dev/null || break
		sleep 2; waited=$((waited + 2))
	done
	sleep 2
	# KILL THE GROUP, NOT THE PID. setsid above gave the emulator its own process
	# group; killing only the pid we were handed leaves it running, and the next
	# run then competes with it for the display and the sandbox. scripts/testrun.sh
	# records the same lesson at its own launch site. [MEASURED 2026-09-08] this
	# left three orphaned emulators behind over a debugging session.
	kill -- -$pid 2>/dev/null || kill $pid 2>/dev/null
	wait $pid 2>/dev/null
	grep -q '^ok$' "$OUT/$phase.txt" 2>/dev/null
}

field() { sed -n "s/^$2=//p" "$OUT/$1.txt" 2>/dev/null | head -1; }
fail()  { echo "FAIL recordtest - $1"; exit 1; }

# ---- phase 1: record -------------------------------------------------------
launch record || fail "the recording session never finished (see $OUT/record.log)"
ERR=$(field record err); [ -z "$ERR" ] || fail "$ERR"
CLIP=$(field record clip); FIRST=$(field record first)
echo "  recorded: $WINDOW frames from movie frame $FIRST"
echo "  clip:     $CLIP"
[ -n "$CLIP" ] && [ -f "$CLIP" ] || fail "no .flyr was written"
echo "  size:     $(stat -c%s "$CLIP") bytes"

# ---- THE VACUITY GATE, before any comparison -------------------------------
# A frozen machine reproduces itself perfectly. If the recorded window holds one
# distinct hash, the comparison below cannot fail and must not be reported as a
# pass. This is the check v1 lacked.
DISTINCT=$(grep -c '^H ' "$OUT/record.txt")
UNIQUE=$(awk '/^H /{print $3}' "$OUT/record.txt" | sort -u | wc -l)
echo "  window:   $DISTINCT frames, $UNIQUE distinct state hashes"
[ "$DISTINCT" -eq "$WINDOW" ] || fail "collected $DISTINCT of $WINDOW frames"
if [ "$UNIQUE" -le 1 ]; then
	echo "VACUOUS recordtest - the machine did not change across $WINDOW frames,"
	echo "  so replaying it identically proves nothing. Seed from a state where"
	echo "  the game is actually running."
	exit 1
fi

# ---- phase 2: replay it ----------------------------------------------------
rm -f "$OUT/replay.txt"
launch replay -config dojo:AutoPlay=yes -config dojo:AutoSeekState=0 \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIP" \
	|| fail "the replay session never finished (see $OUT/replay.log)"
echo "  replayed: $(grep -c '^H ' "$OUT/replay.txt") frames from movie frame $(field replay first)"

# ---- element for element ---------------------------------------------------
# Joined on the FRAME NUMBER, not on position, so a run that skipped or repeated
# a frame is a mismatch rather than a silent shift.
awk '/^H /{a[$2]=$3} END{for (k in a) print k, a[k]}' "$OUT/record.txt" | sort -n > "$OUT/a.txt"
awk '/^H /{b[$2]=$3} END{for (k in b) print k, b[k]}' "$OUT/replay.txt" | sort -n > "$OUT/b.txt"
COMMON=$(join "$OUT/a.txt" "$OUT/b.txt" | wc -l)
[ "$COMMON" -ge $((WINDOW / 2)) ] || fail "the two runs share only $COMMON frames; they did not cover the same range"
DIFF=$(join "$OUT/a.txt" "$OUT/b.txt" | awk '$2 != $3 {print; exit}')
if [ -n "$DIFF" ]; then
	echo "FAIL recordtest - replaying the recording took a DIFFERENT path"
	echo "       first divergence at frame $(echo "$DIFF" | awk '{print $1}'):"
	echo "       recorded $(echo "$DIFF" | awk '{print $2}'), replayed $(echo "$DIFF" | awk '{print $3}')"
	exit 1
fi
echo "PASS recordtest - $COMMON frames recorded and replayed to the same state, hash for hash"
echo "     ($UNIQUE distinct states in the window, so the comparison had something to catch)"
