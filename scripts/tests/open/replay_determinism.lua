--- replay_determinism - does a machine restored from a state walk the SAME PATH
--- it walked the first time?
---
---   Run it:  scripts/testrun.sh scripts/tests/replay_determinism.lua
---
--- THE CLAIM NOTHING ELSE MAKES. deferredslot proves a deferred save/load moves
--- the machine and leaves it running. slots proves a save/load round trip leaves
--- the machine byte-identical AT THAT INSTANT. Neither asks the question every
--- re-record feature rests on: run forward from a restored state, and do you get
--- the SAME SEQUENCE you got the first time?
---
--- `[MEASURED 2026-09-12]` that gap is not theoretical. scripts/recordtest.sh
--- records a movie and replays it expecting hash-for-hash equality, and when it
--- was finally unblocked it diverged one frame in. Two candidate explanations -
--- the harness misaligns its two runs, or the emulator does not reproduce a
--- timeline across a restore - and recordtest cannot tell them apart, because it
--- compares two PROCESSES and everything differs between them.
---
--- This compares two passes in ONE process over the SAME frames, so nothing
--- differs except the restore. It is the control recordtest needs.
---
--- WHAT IT DOES NOT COVER, said out loud: the inputs come from the replay clip
--- both times, so this shows a restore reproduces PLAYBACK. It does not show
--- that a freshly RECORDED movie replays the same - that is recordtest's job,
--- and this exists to tell you whether to believe its verdict.

local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local ss    = flycast.savestate
local SLOT  = 6			-- not 0 (BASE) and not 5 (deferredslot's)
local SPAN  = 70		-- frames compared on each pass

local n, stage = 0, 1
local prevFrame, steady = -1, 0
local savedAt = 0
local passA, passB = {}, {}
local aCount, bCount = 0, 0

local function sample(into)
	local f = flycast.frame.count()
	if into[f] == nil then
		into[f] = ss.hash()
		return true
	end
	-- REPEATS ARE DROPPED, NOT OVERWRITTEN. `[MEASURED 2026-09-12]` the movie
	-- index does not advance on every vblank - it ticks when the guest polls
	-- maple - so two samples can share a frame number. Keeping the LAST of them
	-- silently compares different moments; keeping the FIRST compares the same
	-- one on both passes.
	return false
end

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	local f = flycast.frame.count()

	if stage == 1 then
		--- STEADY PLAYBACK, not a frame count - deferredslot's lesson. A seek is
		--- a jump, so it resets the run, and only real playback satisfies this.
		if prevFrame >= 0 and f == prevFrame + 1 then steady = steady + 1 else steady = 0 end
		prevFrame = f
		if steady < 60 then return end
		stage = 2
		t.ran({ ["the movie is playing steadily"] = steady >= 60 })
		savedAt = f
		ss.saveSlotLater(SLOT)
		return
	end

	if stage == 2 then
		-- LET THE DEFERRED SAVE LAND. It runs at the next drain, not here. Kept
		-- SHORT on purpose: pass A has to start sampling close to the save, or
		-- the two passes only overlap far downstream and "first divergence" can
		-- no longer tell an unfaithful restore from a slow drift.
		if f < savedAt + 5 then return end
		stage = 3
		return
	end

	if stage == 3 then					-- PASS A: forward from the save
		if sample(passA) then aCount = aCount + 1 end
		if aCount < SPAN then return end
		stage = 4
		ss.loadSlotLater(SLOT)
		return
	end

	if stage == 4 then					-- wait for the restore to land
		if f > savedAt + 10 then return end
		stage = 5
		return
	end

	if stage == 5 then					-- PASS B: forward from the restore
		if sample(passB) then bCount = bCount + 1 end
		if bCount < SPAN then return end
		stage = 6

		--- NON-VACUITY FIRST, and it is the assertion that matters most here. A
		--- frozen machine reproduces itself perfectly, so "the two passes agree"
		--- is satisfied trivially by a machine that never moved.
		local distinct = {}
		local nDistinct = 0
		for _, h in pairs(passA) do
			if distinct[h] == nil then distinct[h] = true; nDistinct = nDistinct + 1 end
		end
		t.check("the machine actually changed across the window",
				nDistinct > 1, nDistinct .. " distinct hashes in " .. aCount .. " frames")

		--- AND THE TWO PASSES MUST COVER THE SAME FRAMES, or "they agree" is a
		--- claim about an empty intersection.
		local common, same, firstBad = 0, 0, nil
		for f2, h in pairs(passA) do
			if passB[f2] ~= nil then
				common = common + 1
				if passB[f2] == h then
					same = same + 1
				elseif firstBad == nil or f2 < firstBad then
					firstBad = f2
				end
			end
		end
		t.check("the two passes cover the same frames",
				common >= SPAN / 2, common .. " frames in common")

		t.check("a restored machine walks the same path",
				firstBad == nil and common > 0,
				firstBad and ("first divergence at frame " .. firstBad
						.. ": " .. tostring(passA[firstBad])
						.. " then " .. tostring(passB[firstBad]))
					or (same .. "/" .. common .. " frames identical"))

		t.finish()
	end
end
