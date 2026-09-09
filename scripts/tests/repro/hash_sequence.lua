--- Print a hash sequence keyed by the GUEST frame number, for cross-process
--- comparison by scripts/reprotest.sh.
---
--- Lives under tests/repro/ rather than tests/ on purpose: `scripts/tests/*.lua`
--- is the default suite glob, and this script is not a test on its own. One run
--- of it asserts nothing about reproducibility - it takes TWO, in two
--- processes, and only the harness can see both.
---
--- KEYED BY frame.count(), NOT by callback count. Two processes do not start
--- their Lua at the same point in the boot, so vblank number N is not the same
--- machine state in both. The guest's own clock is the only key that means the
--- same thing on both sides - the same lesson differential_history.lua paid for
--- when a one-frame host offset read as a divergence.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local START = 100   -- guest frame to begin sampling at
local SEQ   = 12    -- samples

local rows, done_, poked = {}, false, false
local POKE = os.getenv("FLYCAST_REPRO_POKE") == "1"

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	if done_ then return end
	local f = flycast.frame.count()
	if f < START then return end

	if #rows == 0 then
		t.ran({
			["a movie is playing"]   = flycast.emulator.isReplay(),
			["frames are advancing"] = f > 0,
		})
		-- The sabotage arm. One perturbed word must change the sequence; if it
		-- does not, the comparison the harness makes cannot detect a difference
		-- either, and a matching pair of runs would prove nothing.
		if POKE then
			for a = 0x0C800000, 0x0C804000, 4 do
				if flycast.memory.read32(a) == 0 then
					flycast.memory.write32(a, 0xDEADBEEF)
					poked = flycast.memory.read32(a) == 0xDEADBEEF
					break
				end
			end
			t.check("the poke reached guest RAM", poked, "sabotage arm")
		end
	end

	-- One sample per distinct guest frame. A repeated frame number would key two
	-- different states to one label.
	if rows[#rows] == nil or rows[#rows].f ~= f then
		rows[#rows + 1] = { f = f, h = flycast.savestate.hash() }
		print(("REPRO %d %s"):format(f, tostring(flycast.savestate.hash())))
	end

	if #rows >= SEQ then
		-- Vacuity: a frozen machine emits SEQ identical hashes, and two frozen
		-- runs agree perfectly while proving nothing.
		local moving = false
		for i = 2, #rows do if rows[i].h ~= rows[1].h then moving = true end end
		t.check("the machine is MOVING while sampling", moving,
				"identical hashes would make the cross-process comparison vacuous")
		t.check("samples are on consecutive distinct frames",
				rows[#rows].f > rows[1].f,
				("%d..%d"):format(rows[1].f, rows[#rows].f))
		t.finish()
		done_ = true
	end
end
