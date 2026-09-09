--- DIFFERENTIAL-HISTORY RESTORE
---
--- One state, restored twice, with a long stretch of emulation in between.
--- The two restores must produce not just the same state but the same FUTURE:
--- twelve consecutive per-frame hashes, identical.
---
--- WHY THIS AND NOT verifyLoadedStateIdempotent. That probe compares
--- S(R(M)) to M - serialize, restore, re-serialize. It is structurally blind to
--- any region that is not registered, because a region that is never written to
--- the blob appears on neither side of the comparison and is trivially "stable".
--- docs/STATE-COVERAGE.md makes that formal, and notes savestate.hash() is the
--- same function, so it inherits the same blind spot.
---
--- This test attacks the blindness from the other side. It never asks what is
--- in the blob. It asks whether HISTORY LEAKS: if some execution-relevant state
--- lives outside the savestate and the interlude modifies it, the second restore
--- resumes on a machine that differs from the first, and the difference shows up
--- in the registered state within a few frames - where the hash CAN see it.
---
--- Unregistered state that never influences execution is, by construction,
--- unobservable and harmless. This test is exactly sensitive to the part that
--- matters.
---
--- SCOPE, stated because the previous investigation was careful about it: the
--- interlude is real emulation, not a targeted memory-card write. It proves
--- history does not leak across THIS interlude. It does not prove a VMU write
--- would survive the same treatment - that needs an interlude known to touch
--- persistent storage, which needs guest input the movie may not contain.
--- docs/STATE-COVERAGE.md found every suspect region registered, so this is the
--- regression test for that finding rather than a hunt for a new one.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local SETTLE = 200      -- frames before the snapshot, so the machine is moving
local SEQ    = 12       -- hashes per trial
local INTER  = 600      -- the interlude: real emulation between the two restores

-- Budget check: this consumes roughly SETTLE + 3*(SEQ + landing) + INTER frames. The clip is
-- seeked to a savestate near its end, so overrunning the movie ends the replay
-- and the vblank callback simply stops - which reads as a timeout, not a
-- failure. Keep the total well under the frames remaining after the seek.

local LANDCAP = 60      -- frames to wait for a deferred restore before giving up

local n, phase, step = 0, "settle", 0
local blob = nil
local lastFrame, landFrame = nil, {}
local seqs, order, cur = { A = {}, B = {}, C = {} }, { "A", "B", "C" }, 1
local pokeAddr, pokeOK = nil, false

local function sameSeq(x, y)
	if #x ~= #y or #x == 0 then return false, 0 end
	for i = 1, #x do if x[i] ~= y[i] then return false, i end end
	return true, 0
end

local function allSame(x)
	for i = 2, #x do if x[i] ~= x[1] then return false end end
	return true
end

--- Find a word of guest RAM that is currently zero. Poking an unused location
--- perturbs the serialized state (main RAM is written wholesale) without
--- corrupting anything the game is using, so trial C can run its twelve frames
--- without risking a hang that would read as INCONCLUSIVE.
local function findZeroWord()
	for a = 0x0C800000, 0x0C804000, 4 do
		if flycast.memory.read32(a) == 0 then return a end
	end
	return nil
end

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1

	if phase == "settle" then
		if n < SETTLE then return end
		t.ran({
			["a movie is playing"]       = flycast.emulator.isReplay(),
			["frames are advancing"]     = flycast.frame.count() > 0,
			["the deferred API exists"]  = type(flycast.savestate.snapshotLater) == "function",
			["the memory API exists"]    = type(flycast.memory.read32) == "function",
		})
		flycast.savestate.snapshotLater()
		phase = "snapping"

	elseif phase == "snapping" then
		blob = flycast.savestate.takeSnapshot()
		if blob ~= "" then
			t.check("the snapshot is a real state", #blob > 1000000, #blob .. " bytes")
			phase, step = "restore", 0
		end

	elseif phase == "restore" then
		flycast.savestate.restoreLater(blob)
		lastFrame = flycast.frame.count()
		phase, step = "landing", 0

	elseif phase == "landing" then
		-- ALIGN ON THE GUEST'S OWN CLOCK, not on a fixed number of host frames.
		-- A restore makes the frame counter jump BACKWARDS; that edge is the
		-- moment the machine is at the restored state, and it is the only
		-- alignment that means the same thing in every trial.
		--
		-- [MEASURED 2026-09-08] the first version of this waited a fixed 8
		-- frames, on the reasoning that adaptive alignment would hide a real
		-- divergence. That reasoning was wrong, and the test said so: trial A
		-- ended at frame 81 and trial B at 82, so hash k of one was compared
		-- against hash k+1 of the other and "history leaks" failed at hash 1.
		-- WHEN THE HOST SERVICES A DEFERRED RESTORE IS HOST SCHEDULING, not
		-- guest state, and a test of the guest must not be sensitive to it.
		step = step + 1
		local f = flycast.frame.count()
		if not (lastFrame and f < lastFrame) then
			lastFrame = f
			if step >= LANDCAP then
				t.check("the restore landed within " .. LANDCAP .. " frames", false,
						"trial " .. order[cur] .. " never saw the frame counter go back")
				t.finish(); phase = "done"
			end
			return
		end
		landFrame[order[cur]] = f
		-- Trial C is the sensitivity arm: perturb ONE word of guest RAM after
		-- the restore has landed, standing in for "a region was not restored".
		if order[cur] == "C" then
			pokeAddr = findZeroWord()
			if pokeAddr then
				local before = flycast.savestate.hash()
				flycast.memory.write32(pokeAddr, 0xDEADBEEF)
				pokeOK = flycast.memory.read32(pokeAddr) == 0xDEADBEEF
				t.check("the poke reached guest RAM", pokeOK,
						("addr=0x%08X"):format(pokeAddr))
				t.check("poking guest RAM changes the state hash",
						flycast.savestate.hash() ~= before,
						"otherwise the hash cannot see RAM and proves nothing")
			else
				t.limit("sensitivity arm", "no zero word found in the scanned range")
			end
		end
		phase, step = "collect", 0

	elseif phase == "collect" then
		step = step + 1
		seqs[order[cur]][step] = flycast.savestate.hash()
		if step < SEQ then return end
		print(("  .. trial %s  landed at frame %s  first=%s last=%s"):format(
				order[cur], tostring(landFrame[order[cur]]),
				tostring(seqs[order[cur]][1]), tostring(seqs[order[cur]][SEQ])))
		if order[cur] == "A" then
			-- THE VACUITY GATE. Twelve identical hashes would make every
			-- comparison below true for free. A frozen machine must not pass.
			t.check("the machine is MOVING during a trial",
					not allSame(seqs.A),
					"twelve identical hashes would make this test vacuous")
			cur, phase, step = 2, "interlude", 0
		elseif order[cur] == "B" then
			local ok, at = sameSeq(seqs.A, seqs.B)
			t.check("history does not leak: same state, same future, across "
					.. INTER .. " frames of emulation",
					ok, ok and "12/12 hashes identical"
					   or ("first difference at hash " .. at))
			cur, phase, step = 3, "restore", 0
		else
			local same = sameSeq(seqs.A, seqs.C)
			if pokeOK then
				-- The can-fail arm, asserted rather than performed by hand: if
				-- ONE perturbed word does not move the sequence, the comparison
				-- above cannot detect an unrestored region either, and its pass
				-- means nothing.
				t.check("the comparison DETECTS a one-word difference",
						not same, "otherwise the claim above is unfalsifiable")
			else
				t.limit("sensitivity arm", "the poke did not land")
			end
			-- The frame counter is itself restored state. If the trials
			-- resumed at different frames, the sequences were never comparable
			-- and every verdict above is about alignment, not about history.
			t.check("every trial resumed at the same frame",
					landFrame.A == landFrame.B and landFrame.B == landFrame.C,
					("A=%s B=%s C=%s"):format(tostring(landFrame.A),
							tostring(landFrame.B), tostring(landFrame.C)))
			t.finish()
			phase = "done"
		end

	elseif phase == "interlude" then
		step = step + 1
		if step == 1 then
			t.check("the interlude starts from the state under test",
					seqs.A[1] ~= nil, "trial A produced hashes")
		end
		if step < INTER then return end
		t.check("the interlude actually moved the machine",
				flycast.savestate.hash() ~= seqs.A[1],
				"an interlude that changes nothing tests nothing")
		phase, step = "restore", 0
	end
end
