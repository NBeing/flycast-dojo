--- emptyslot - loading a slot that holds nothing must not stop the emulator,
--- and must not change it either.
---
--- `[OPEN 2026-09-13]` THE SECOND HALF FAILS. The plan's claim - the emulator
--- keeps running - holds on both code paths. But a failed load MUTATES the
--- machine: the blob differs at byte 284 of Sh4Context, which `gdb ptype /o`
--- names `old_sr`, the saved status register. Measured twice, at the same
--- context offset in blobs of different sizes, with the instrument controlled:
--- four serializes with NO load between them are byte-identical, so this is the
--- load and not the hashing.
---
--- Why that matters for a TAS tool rather than being a curiosity: a stray load
--- onto an empty slot is a thing a user does by accident, and if it perturbs a
--- register it can desync a movie that was otherwise fine. "It did nothing" is
--- what the notification claims.
---
--- `[SOURCE]` dc_loadstate returns before touching the machine when the file
--- will not open - WARN, a notification, return - and luaSavestateSlot calls it
--- directly with no emu.stop()/start() around it. So the mechanism is NOT
--- obvious from the code, which is why this is recorded as open rather than
--- explained.
---
---   Run it:  scripts/testrun.sh scripts/tests/emptyslot.lua
---
--- ONE OF docs/TEST-PLAN.md SECTION 2's re-record claims, and the cheapest of
--- them. A TAS artist cycles slots constantly and will land on an empty one; the
--- cost of getting this wrong is not a refused load, it is a dead emulator with
--- a movie in it.
---
--- NOTHING COVERED THIS. scripts/tests/slots.lua asserts that a slot ABOVE the
--- reported count is refused and that a negative one is refused - both are
--- out-of-RANGE. An empty slot is IN range and simply has no file, which is a
--- different path entirely. emuapi's conformance suite says so out loud: it
--- reports no `probe.emptyslot` because a real slot may hold a user's state.
--- Inside a sandbox with its own XDG_DATA_HOME that objection does not apply.
---
--- WHY THE EMULATOR COULD PLAUSIBLY STOP. `[SOURCE]` dc_loadstate RETURNS when
--- the file will not open - it does not throw, and its own comment says so
--- because that surprised someone before. gui_loadState wraps the call in
--- emu.stop() / emu.start(); any early return between those two that skipped the
--- restart would leave a stopped machine, and "the load did nothing" and "the
--- load killed the emulator" look identical from a script that only checks for
--- an exception.
---
--- BOTH PATHS, because they are different code. savestate.load goes straight to
--- dc_loadstate; savestate.loadSlotLater posts to deferred::drain() and goes
--- through gui_loadState, which is the one that stops and restarts.

local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local ss    = flycast.savestate
local EMPTY = 42			-- in range (slotCount is 100) and written by nothing here

local n, stage = 0, 1
local prevFrame, steady = -1, 0
local atTry, hashBefore, hashSteady, hashAfter = 0, nil, nil, nil
local directRan, laterAt = false, 0

--- A SEEK RESTARTS THE TEST, it does not count as progress.
---
--- `[MEASURED 2026-09-13]` the first version of this asserted "the emulator kept
--- running" as `f > laterAt` and passed with `120 -> 9929`. The machine had not
--- kept running - scripts/testrun.sh passes dojo:AutoSeekState=0 to EVERY Lua
--- test and that seek fires on WALL CLOCK about two seconds in, so it landed
--- mid-test and jumped the movie 9,809 frames. The claim was satisfied by the
--- very event that invalidates it.
local blobBefore, blobAfter = nil, nil
local lastF, restarts = nil, 0

--- FIRST DIFFERING BYTE, coarse then fine - the same shape
--- scripts/tests/open/replay_determinism.lua uses. A hash says two machines
--- differ; only the offset says WHERE, and "where" is the whole question when
--- an operation that should be a no-op is not one.
local BLOCK = 65536
local function firstDiff(a, b)
	if a == nil or b == nil then return nil end
	local n = math.min(#a, #b)
	local i = 1
	while i <= n do
		local j = math.min(i + BLOCK - 1, n)
		if a:sub(i, j) ~= b:sub(i, j) then
			for k = i, j do
				if a:byte(k) ~= b:byte(k) then return k end
			end
		end
		i = j + 1
	end
	if #a ~= #b then return n + 1 end
	return nil
end
local function jumped(f)
	local j = lastF ~= nil and (f < lastF or f > lastF + 100)
	lastF = f
	return j
end

--- ONCE EACH, even though a seek can restart the run. Without this a claim is
--- recorded twice and can be green in one attempt and red in another, which is
--- two verdicts for one question.
local said = {}
local rawCheck = t.check
t.check = function(name, cond, detail)
	if said[name] then return end
	said[name] = true
	return rawCheck(name, cond, detail)
end

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	local f = flycast.frame.count()

	if jumped(f) and stage > 1 then
		restarts = restarts + 1
		stage, steady, prevFrame, lastF = 1, 0, -1, f
		return
	end

	if stage == 1 then
		--- STEADY PLAYBACK, not a frame count - deferredslot's lesson. A seek is
		--- a jump, so it resets the run, and only real playback satisfies this.
		if prevFrame >= 0 and f == prevFrame + 1 then steady = steady + 1 else steady = 0 end
		prevFrame = f
		if steady < 60 then return end
		stage = 2
		--- NON-VACUITY FIRST. A machine that was never running cannot be shown
		--- to have survived anything.
		t.ran({ ["the movie is playing steadily"] = steady >= 60 })
		t.check("the slot is addressable at all", EMPTY < ss.slotCount(),
			"slot " .. EMPTY .. " of " .. ss.slotCount())
		--- HASHED TWICE BEFORE THE LOAD, so "unchanged" means something. A
		--- serialize is not perfectly side-effect-free - `[SOURCE]` sh4_mmr.cpp
		--- calls sh4_sched_ffts() before writing the SH4 context - so one
		--- baseline sample cannot tell a canonicalising first read from a real
		--- change. Two equal samples establish the machine is quiet.
		--- THE INSTRUMENT FIRST. `[SOURCE]` sh4_mmr.cpp calls sh4_sched_ffts()
		--- before writing the SH4 context, so a "read-only" hash WRITES to
		--- Sh4cntx.sh4_sched_next and sh4_sched_ffb. If that makes consecutive
		--- samples differ, then "the machine changed after the load" is a
		--- statement about hashing, not about loading - and every hash-based
		--- claim in this tree would be measuring its own instrument.
		---
		--- Four samples with NO load between them, matching the four the real
		--- measurement takes.
		local c1, c2 = ss.hash(), ss.hash()
		local cb = ss.tostring()
		local c3 = ss.hash()
		local ca = ss.tostring()
		t.check("hashing does not perturb the machine",
			c1 == c2 and c3 == c2 and cb == ca,
			tostring(c1) .. " / " .. tostring(c2) .. " / " .. tostring(c3)
				.. (cb == ca and "; blobs equal" or "; BLOBS DIFFER with no load at all"))

		atTry = f
		hashBefore = ss.hash()
		hashSteady = ss.hash()
		blobBefore = ss.tostring()
		--- PATH ONE: straight to dc_loadstate.
		directRan = pcall(ss.load, EMPTY)
		hashAfter = ss.hash()
		blobAfter = ss.tostring()
		return
	end

	if stage == 2 then
		--- A SECOND LATER. Returning from the call does not answer "is it still
		--- running" - that is what wedge means, and it is checked by time.
		if f < atTry + 60 and n < 300 then return end
		stage = 3
		t.check("the emulator kept running after a direct load of an empty slot",
			f > atTry and f <= atTry + 300,
			atTry .. " -> " .. f .. " (call " .. (directRan and "returned" or "raised") .. ")")
		--- AND IT WAS REALLY EMPTY. Taken in the SAME callback as the load, so
		--- no frame ran in between and any change is the load's doing.
		---
		--- `[CORRECTED 2026-09-13]` this read `ss.hash() ~= hashBefore or true`,
		--- which is a CONSTANT - the exact `ok(not present or true, ...)` shape
		--- CLAUDE.md records as having been reviewed, committed and run hundreds
		--- of times while unable to fail. It asserted nothing, and it asserted
		--- it about the wrong instant.
		local off = firstDiff(blobBefore, blobAfter)
		t.check("the baseline is quiet, and nothing was restored",
			hashBefore == hashSteady and hashAfter == hashSteady,
			tostring(hashBefore) .. " / " .. tostring(hashSteady)
				.. " -> " .. tostring(hashAfter)
				.. (off and ("; first differing byte at offset " .. (off - 1)
						.. " of " .. #blobBefore) or "; blobs identical"))
		laterAt = f
		--- PATH TWO: the deferred route, through gui_loadState's stop/start.
		ss.loadSlotLater(EMPTY)
		return
	end

	if stage == 3 then
		if f < laterAt + 60 and n < 600 then return end
		stage = 4
		--- ADVANCED BY PLAYBACK, not by a jump: the jump guard above restarts
		--- the test instead of letting a seek satisfy this.
		t.check("the emulator kept running after a deferred load of an empty slot",
			f > laterAt and f <= laterAt + 300,
			laterAt .. " -> " .. f .. " (" .. restarts .. " restart(s) after a seek)")
		t.finish()
	end
end
