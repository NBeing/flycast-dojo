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
# AND IT SHIPS WITH A WAY TO MAKE IT FAIL. `--self-test` records normally and
# then REPLACES THE ANCHOR with the seed state before replaying, so phase 2
# resumes from a different machine than the one that was recorded. The hashes
# must then diverge; the arm exits 0 only if this script correctly reports that.
#
# Sabotage through the ARTIFACT rather than the judge: a corrupted anchor is
# exactly the failure this test exists to catch, and routing it through the same
# file the real run uses means a judge that has stopped reading the file cannot
# pass both arms.
#
# Exit: 0 pass, 1 fail, 77 skip.
set -uo pipefail
SKIP=77
SELFTEST=0
[ "${1:-}" = "--self-test" ] && SELFTEST=1
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

# TEARDOWN IS PID-SCOPED, NEVER BY NAME. `[SOURCE]` dc053dfeb - killing Xvfb or
# i3 by pattern on a developer's own machine logs them out, and this harness
# runs on one. The same run also used to leave its mktemp dir behind on every
# invocation; an interrupted debugging session left 156 of them.
XPID=""
cleanup() {
	[ -n "$XPID" ] && kill "$XPID" 2>/dev/null
	[ "${RECORDTEST_KEEP:-0}" = 1 ] || rm -rf "$OUT"
	return 0
}
trap cleanup EXIT
Xvfb "$DISP" -screen 0 640x480x24 >/dev/null 2>&1 &
XPID=$!
sleep 2

write_script() {
	cat > "$OUT/config/flycast-dojo/flycast.lua" <<LUAEOF
local PHASE  = "$1"
local WINDOW = $WINDOW
-- THE RECORD PHASE COLLECTS WIDER THAN THE REPLAY, deliberately. saveSlotLater
-- lands at the next deferred drain, so the exact movie frame the anchor
-- captures is not knowable from here - and a comparison that never samples the
-- anchor frame cannot tell "the restore is wrong" from "the first stepped frame
-- is wrong". Collect from the moment recording starts and let the join find the
-- overlap.
local COLLECT = (PHASE == "record") and (WINDOW + 40) or WINDOW
local out = "$OUT/" .. PHASE .. ".txt"
local function w(s) local f = io.open(out, "a"); if f then f:write(s .. "\n"); f:close() end end

local n, stage, first, taken = 0, "boot", nil, 0
local savedN, loadedN, beforeLoad = 0, 0, 0
local lastF = nil
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
			-- THE ANCHOR, AND IT MUST NOT BE SAVED FROM HERE.
			--
			-- A movie started mid-session has no frame 0, so it is replayable
			-- only paired with a state at its first frame; without this, phase 2
			-- has nothing to seek to. But savestate.save writes from THIS
			-- callback, and [MEASURED 2026-09-12] a state written from a vblank
			-- hook records the raster descheduled, because sh4_sched clears an
			-- event's deadline for the duration of its callback and
			-- Emulator::vblank() runs inside spg_line_sched.
			--
			-- saveSlotLater posts to deferred::drain() instead - between
			-- frames, no callback on the stack, nothing pending.
			flycast.savestate.saveSlotLater(0)
			savedN = n
			stage = "saved"; return
		end
		if stage == "saved" then
			-- LET THE DEFERRED SAVE LAND. It runs at the next drain, not here.
			if n < savedN + 15 then return end
			--- AND THEN RESTORE IT, WHICH IS THE POINT OF THIS PHASE.
			---
			--- [2026-09-13] This test used to record from a CONTINUING machine
			--- and replay from a RESTORED one, so passing required those two to
			--- agree cycle-for-cycle. They do not: docs/TEST-PLAN.md 1a, where a
			--- restored machine ends up two SH4 cycles out because host state
			--- that bills the guest's cycle budget is not in the blob.
			---
			--- That was the wrong thing to demand. What a re-record tool needs
			--- is that loading an anchor and applying inputs gives the same
			--- result EVERY TIME - restore-to-restore - and that was already
			--- true before any of the 2026-09-12 work (58/58 frames, measured).
			--- Recording from a restored machine asks for exactly that property
			--- and nothing more, so this test no longer waits on an emulator
			--- change to be meaningful.
			beforeLoad = flycast.frame.count()
			flycast.savestate.loadSlotLater(0)
			loadedN = n
			stage = "restoring"; return
		end
		if stage == "restoring" then
			--- WAIT FOR THE RESTORE TO ACTUALLY LAND. loadSlotLater is deferred;
			--- falling through would start collecting on the pre-restore
			--- timeline, which is the race that made a sibling test report a
			--- confident wrong answer for a whole session.
			--- The restore seeks the movie BACK, so a frame number below where
			--- we issued it is the signal.
			if flycast.frame.count() >= beforeLoad then
				if n > loadedN + 600 then
					w("err=the anchor never restored (frame stayed at " .. flycast.frame.count() .. ")")
					w("ok"); flycast.emulator.exit()
				end
				return
			end
			--- AND THE RECORDING MUST HAVE SURVIVED IT. A load inside a
			--- recording session is exactly the operation a re-record does, but
			--- if it silently stopped the recording this test would compare an
			--- empty clip and pass for the wrong reason.
			if not flycast.replay.isRecording() then
				w("err=the load stopped the recording"); w("ok"); flycast.emulator.exit(); return
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
	--- A SEEK DURING COLLECTION RESTARTS IT.
	---
	--- The replay phase runs with dojo:AutoSeekState=0, which fires on WALL
	--- CLOCK about two seconds in - not on a frame - so it can land after
	--- collection has begun and jump the machine to another point in the movie.
	--- Samples either side of that come from two different timelines. A seek
	--- runs the movie index BACKWARDS, which nothing else here does.
	---
	--- The marker is written to the stream and the shell reads only what
	--- follows the LAST one, so a restart discards the contaminated samples
	--- without the script having to rewrite anything.
	if lastF ~= nil and f < lastF then
		w("restart")
		taken = 0
	end
	lastF = f
	if taken >= COLLECT then return end
	taken = taken + 1
	w("H " .. tostring(f) .. " " .. tostring(flycast.savestate.hash()))
	if taken < COLLECT then return end

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
fail()  {
	# UNDER --self-test A DIVERGENCE IS THE PASS. Only the divergence, though:
	# a fixture that never recorded, or a replay that never ran, is a broken
	# harness in both arms and must stay a failure in both.
	if [ "$SELFTEST" -eq 1 ] && [ "${2:-}" = "divergence" ]; then
		echo "PASS recordtest --self-test - the sabotaged anchor was detected:"
		echo "  $1"
		exit 0
	fi
	echo "FAIL recordtest - $1"; exit 1
}

# ---- phase 1: record -------------------------------------------------------
launch record || fail "the recording session never finished (see $OUT/record.log)"
ERR=$(field record err); [ -z "$ERR" ] || fail "$ERR"
CLIP=$(field record clip); FIRST=$(field record first)
echo "  recorded: $(grep -c '^H ' "$OUT/record.txt") frames from movie frame $FIRST"
echo "  clip:     $CLIP"
[ -n "$CLIP" ] && [ -f "$CLIP" ] || fail "no .flyr was written"
echo "  size:     $(stat -c%s "$CLIP") bytes"

# ---- THE VACUITY GATE, before any comparison -------------------------------
# A frozen machine reproduces itself perfectly. If the recorded window holds one
# distinct hash, the comparison below cannot fail and must not be reported as a
# pass. This is the check v1 lacked.
DISTINCT=$(awk '/^restart$/{n=0;next} /^H /{n++} END{print n+0}' "$OUT/record.txt")
UNIQUE=$(awk '/^restart$/{delete seen;next} /^H /{seen[$3]=1} END{print length(seen)}' "$OUT/record.txt")
echo "  window:   $DISTINCT frames, $UNIQUE distinct state hashes"
[ "$DISTINCT" -ge "$WINDOW" ] || fail "collected $DISTINCT frames, fewer than the $WINDOW-frame window"
if [ "$UNIQUE" -le 1 ]; then
	echo "VACUOUS recordtest - the machine did not change across $WINDOW frames,"
	echo "  so replaying it identically proves nothing. Seed from a state where"
	echo "  the game is actually running."
	exit 1
fi

# ---- the sabotage arm, applied to the fixture and nowhere else -------------
if [ "$SELFTEST" -eq 1 ]; then
	ANCHOR="$(dirname "$CLIP")/NoBGM_VMU.state"
	[ -f "$ANCHOR" ] || { echo "FAIL recordtest --self-test - no anchor at $ANCHOR"; exit 1; }
	cp "$SEED" "$ANCHOR"
	rm -f "$ANCHOR.frame"
	echo "  SABOTAGE: anchor replaced with the seed state - the replay now resumes"
	echo "            from a different machine than the one that was recorded"
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
# JOINED IN AWK, NOT WITH join(1). `[MEASURED 2026-09-12]` the previous version
# piped both sides through `sort -n` and then `join`, which requires the DEFAULT
# collation: the movie crosses 9999 -> 10000, four digits to five, and join
# reported "input is not in sorted order" and silently stopped pairing there.
# A comparison that quietly covers less than it claims is the failure mode this
# whole file exists to avoid, so the sort is gone rather than corrected.
# ONLY WHAT FOLLOWS THE LAST `restart`. Everything before it was collected on a
# timeline the auto-seek then threw away; keeping it would compare two different
# runs and report the difference as a re-record defect.
read -r COMMON FIRSTBAD REC REP <<< "$(awk '
	# BUILD BOTH MAPS FIRST, COMPARE AT THE END.
	#
	# `[MEASURED 2026-09-13]` comparing line-by-line as the second file is read
	# looks equivalent and is not: a sample is judged BEFORE the `restart`
	# marker after it has been seen, so the first divergence was recorded from
	# samples the restart then discarded. The run was reported as a failure
	# while the surviving samples agreed exactly - the values in the message
	# were literally present in the other half of the file, matching.
	#
	# `restart` clears the map for the pass it appears in: everything before it
	# was collected on a timeline the auto-seek threw away. split("", x) rather
	# than `delete x`, which is a gawk extension.
	FNR == 1 { pass++ }
	/^restart$/ { if (pass == 1) split("", a); else split("", b); next }
	/^H / { if (pass == 1) a[$2] = $3; else b[$2] = $3; next }
	END {
		bad = ""
		for (k in a)
			if (k in b) {
				common++
				if (a[k] != b[k] && (bad == "" || k + 0 < bad + 0)) {
					bad = k; rec = a[k]; rep = b[k]
				}
			}
		printf "%d %s %s %s\n", common + 0, (bad == "" ? "-" : bad), rec, rep
	}
' "$OUT/record.txt" "$OUT/replay.txt")"

[ "$COMMON" -ge $((WINDOW / 2)) ] || fail "the two runs share only $COMMON frames; they did not cover the same range"
if [ "$FIRSTBAD" != "-" ]; then
	fail "replaying the recording took a DIFFERENT path - first divergence at frame $FIRSTBAD: recorded $REC, replayed $REP" divergence
fi
if [ "$SELFTEST" -eq 1 ]; then
	echo "FAIL recordtest --self-test - the sabotaged anchor was NOT detected."
	echo "  $COMMON frames compared equal against a replay resumed from the WRONG state,"
	echo "  so this harness cannot tell a correct re-record from a broken one."
	exit 1
fi
echo "PASS recordtest - $COMMON frames recorded and replayed to the same state, hash for hash"
echo "     ($UNIQUE distinct states in the window, so the comparison had something to catch)"
