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
---
--- WHAT THE DIVERGENCE ACTUALLY IS `[MEASURED 2026-09-12]`, from the blob diff
--- this file now does:
---
---   ONE BYTE out of 27,793,699. Not a wrong path - a wrong NUMBER. Every other
---   byte of the machine is identical at the diverging frame.
---
---   It is Sh4Context::cycle_counter, SERMAP offset sh4.cntx + 308, and the two
---   runs read 188 and 186. Two SH4 cycles of phase.
---
--- AND WHICH SIDE IS ODD `[MEASURED 2026-09-12]`, from the RD_BOTH_RESTORED
--- control: with BOTH passes restored from the same slot, 56/56 frames are
--- identical, hash for hash. A restore reproduces itself perfectly. The
--- asymmetry is the CONTINUING machine - one that was saved and carried on
--- differs from one restored out of that save.
---
--- That inverts the diagnosis. The restore is not lossy at reproducing; the
--- save does not capture, or the load discards, host residue the continuing
--- machine still has. The suspects are exactly dc_loadstate's invalidation
--- list - custom_texture, the ARM recompiler flush, mmu_flush_table, bm_Reset,
--- memwatch, mmu_set_state, sh4_cpu.ResetCache, KillTex - every one of which
--- the restored machine has cleared and the continuing one does not.
---
--- Three more things it is NOT, each measured rather than argued:
---
---   NOT a regression from the 2026-09-12 scheduler work. The tree as it stood
---   before that commit fails identically, with the same two hashes.
---   NOT a race. The same two hashes on two different builds.
---   NOT the inputs. The two passes are asserted to receive the same kcode on
---   every shared frame - 56/56 - so the movie playhead, which is host state and
---   NOT part of a savestate, is delivering identically. Without that claim the
---   whole result would be ambiguous: two machines byte-identical at frame N
---   cannot diverge at N+5 unless something outside the blob feeds them, and a
---   mis-seeked playhead is the obvious candidate. It is not that.
---   NOT the dynarec. -config config:Dynarec.Enabled=no diverges too, at frame
---   69 with its own pair of hashes - so it is not compiled-block boundaries
---   shifting after dc_loadstate resets the block cache, which was the leading
---   theory. (Without the section prefix that flag is silently rejected; the
---   run was checked for the rejection before its result was believed.)
---
--- `[CORRECTED 2026-09-12]` the first reading of that offset said byte 424 -
--- inside the 136 bytes of `u64 raw[64-8]` padding that no named field of
--- Sh4Context covers - and a change to stop serializing that padding was
--- written, built and measured before the mapping was rechecked. It was wrong
--- by 116 bytes: the SERMAP block used came from a DIFFERENT serialization than
--- the one the hash uses. This tree emits several sizes, so match SERMAP's END
--- against the blob length before trusting an offset from it. The padding
--- change fixed nothing and was reverted. The near-miss is the useful part -
--- 424 is padding and would have made this test a proxy failure with the
--- emulator innocent, 308 is a register and makes it real, and the two readings
--- are indistinguishable without checking the END.

local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local ss    = flycast.savestate
local SLOT  = 6			-- not 0 (BASE) and not 5 (deferredslot's)
local SPAN  = 70		-- frames compared on each pass

local n, stage = 0, 1
local prevFrame, steady = -1, 0
local savedAt = 0
local reloaded = false
local passA, passB = {}, {}
local inA,   inB   = {}, {}
local aCount, bCount = 0, 0

--- THE BLOB AT THE FIRST SHARED FRAME, from each pass. A hash says the two
--- machines differ; it cannot say WHERE, and "where" is the whole question once
--- the divergence is known to be deterministic. Keyed to the first frame pass A
--- samples, because that is the earliest frame both passes cover and the
--- earliest evidence is the least contaminated by whatever the difference goes
--- on to cause.
--- WHICH frame, by default the first pass A samples. RD_BLOB_FRAME overrides
--- it, because the interesting frame is the FIRST DIVERGING one and that is not
--- known until a run has been done: keeping a blob per frame would be ~28 MB
--- each. So run once to learn the frame, then again pointing here.
local blobWant = tonumber(os.getenv("RD_BLOB_FRAME") or "") or nil

--- RD_BOTH_RESTORED=1 restores the slot before pass A as well, so the two
--- passes differ in NOTHING - not even in having been restored. It is the
--- control that says which half is odd: if both-restored agrees, a restore
--- reproduces itself and the asymmetry is the continuing machine; if it still
--- diverges, the noise is host-side and is not about restoring at all.
local bothRestored = os.getenv("RD_BOTH_RESTORED") == "1"
local blobFrame, blobA, blobB = nil, nil, nil

--- FIRST DIFFERING OFFSET, coarse then fine. A byte loop over ~28 MB in Lua is
--- far too slow and a binary search over string.sub copies the whole blob on
--- every step; 64 KB blocks cost one pass over the data, then one byte scan of
--- one block.
local BLOCK = 65536

--- EVERY differing offset, not just the first. `[MEASURED 2026-09-12]` the first
--- one landed 112 bytes past the last named field of Sh4Context - inside the
--- tail that exists only because the union is `u64 raw[56]` while the struct
--- covers 312 of its 448 bytes. If that is ALL that differs then the machine is
--- reproducing and savestate.hash() is the thing that is wrong, which is the
--- opposite conclusion from the one the first measurement invited.
local function allDiffs(a, b, cap)
	local out, n = {}, math.min(#a, #b)
	local i = 1
	while i <= n and #out < cap do
		local j = math.min(i + BLOCK - 1, n)
		if a:sub(i, j) ~= b:sub(i, j) then
			for k = i, j do
				if a:byte(k) ~= b:byte(k) then
					out[#out + 1] = k - 1
					if #out >= cap then break end
				end
			end
		end
		i = j + 1
	end
	return out
end

local function firstDiff(a, b)
	local n = math.min(#a, #b)
	local i = 1
	while i <= n do
		local j = math.min(i + BLOCK - 1, n)
		if a:sub(i, j) ~= b:sub(i, j) then
			for k = i, j do
				if a:byte(k) ~= b:byte(k) then return k end
			end
			return i					-- unreachable unless sub/byte disagree
		end
		i = j + 1
	end
	if #a ~= #b then return n + 1 end	-- identical prefix, different length
	return nil
end

local function sample(into, inputs)
	local f = flycast.frame.count()
	if into[f] == nil then
		--- THE INPUT IS RECORDED BESIDE THE HASH, and it is the control that
		--- decides what the divergence MEANS. Two machines byte-identical at
		--- frame N cannot diverge at N+5 unless something outside the blob
		--- feeds them, and the movie playhead is exactly that: kcode comes from
		--- the replay stream, whose position is host state and is NOT part of a
		--- savestate. If the inputs differ, the emulator is deterministic and
		--- the seek put the playhead somewhere else - a completely different
		--- bug from "the emulator does not reproduce".
		inputs[f] = flycast.input.getButtons(1)
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
		if bothRestored and savedAt > 0 and f >= savedAt + 5 and not reloaded then
			reloaded = true
			ss.loadSlotLater(SLOT)
			return
		end
		-- LET THE DEFERRED SAVE LAND. It runs at the next drain, not here. Kept
		-- SHORT on purpose: pass A has to start sampling close to the save, or
		-- the two passes only overlap far downstream and "first divergence" can
		-- no longer tell an unfaithful restore from a slow drift.
		if f < savedAt + 5 then return end
		stage = 3
		return
	end

	if stage == 3 then					-- PASS A: forward from the save
		if sample(passA, inA) then
			aCount = aCount + 1
			if blobA == nil and (blobWant == nil or f == blobWant) then
				blobFrame = f; blobA = ss.tostring()
			end
		end
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
		if sample(passB, inB) then
			bCount = bCount + 1
			if f == blobFrame and blobB == nil then blobB = ss.tostring() end
		end
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
		--- DID THEY GET THE SAME INPUTS? Asserted BEFORE the path claim, because
		--- if this fails the path claim is about a different experiment.
		local inCommon, inSame, firstInBad = 0, 0, nil
		for f2, v in pairs(inA) do
			if inB[f2] ~= nil then
				inCommon = inCommon + 1
				if inB[f2] == v then inSame = inSame + 1
				elseif firstInBad == nil or f2 < firstInBad then firstInBad = f2 end
			end
		end
		t.check("the two passes received the same inputs",
				firstInBad == nil and inCommon > 0,
				firstInBad and ("first differing input at frame " .. firstInBad
						.. ": " .. string.format("0x%x", inA[firstInBad])
						.. " vs " .. string.format("0x%x", inB[firstInBad]))
					or (inSame .. "/" .. inCommon .. " frames identical"))

		t.check("the two passes cover the same frames",
				common >= SPAN / 2, common .. " frames in common")

		t.check("a restored machine walks the same path",
				firstBad == nil and common > 0,
				firstBad and ("first divergence at frame " .. firstBad
						.. ": " .. tostring(passA[firstBad])
						.. " then " .. tostring(passB[firstBad]))
					or (same .. "/" .. common .. " frames identical"))

		--- AND WHERE. Reported as a detail on a claim that can only PASS, because
		--- this is diagnosis rather than a rule - the rule is the claim above,
		--- and a second failing claim about the same defect would double-count
		--- it. `dojo:StateMapLog=yes` prints SERMAP lines that turn this offset
		--- into a subsystem name.
		local where
		if blobA == nil or blobB == nil then
			where = "no blob captured at frame " .. tostring(blobFrame)
		else
			local off = firstDiff(blobA, blobB)
			if off == nil then
				where = "the two blobs at frame " .. tostring(blobFrame)
						.. " are IDENTICAL (" .. #blobA .. " bytes)"
			else
				local d = allDiffs(blobA, blobB, 400)
				local lo, hi = d[1], d[#d]
				where = ("%d+ differing bytes in %d, frame %s; span %d..%d; first %d vs %d")
						:format(#d, #blobA, tostring(blobFrame), lo, hi,
								blobA:byte(off), blobB:byte(off))
			end
		end
		t.check("the blobs were compared", true, where)

		t.finish()
	end
end
