--- RESTART THE MACHINE IN-PROCESS, from a callback, without deadlocking.
---
--- This is the regression test for emulator.restartLater(). Two separate
--- defects had to be fixed before a second boot could happen at all, and both
--- present as "the emulator wedged":
---
---   1. stopGame()/startGame() from a `vblank` callback DEADLOCK. vblank runs on
---      the emulation thread; stopping calls Emulator::stop -> checkStatus(true)
---      -> threadResult.get(), which blocks until that thread finishes. From
---      vblank that is the thread waiting for itself. restartLater posts to the
---      deferred point instead (core/deferred.h).
---   2. The headless one-shots in mainui.cpp fired once per PROCESS. A replay
---      boots PAUSED and headless has nobody to un-pause it, so the second boot
---      sat at GuiState::Paused forever - every trace identical to the first
---      boot right up to the missing "auto-play" line. They now re-arm on
---      Event::Terminate.
---
--- THE OBSERVER DOES NOT SURVIVE THE RESTART. Starting a game re-inits Lua, so
--- this script is torn down and re-loaded; boot 1 leaves a file behind and the
--- reloaded copy reads it to know which boot it is on. A script that tried to
--- hold the first boot's result in a local would simply never see boot 2.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local SAMPLE = 150      -- callbacks into each boot before sampling
local MARK   = (os.getenv("XDG_CONFIG_HOME") or "/tmp") .. "/restart_inprocess.mark"

local function readMark()
	local f = io.open(MARK, "r"); if not f then return nil end
	local v = f:read("*a"); f:close(); return v
end

local firstBoot = readMark()
local n = 0

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if n ~= SAMPLE then return end

	local frame, hash = flycast.frame.count(), tostring(flycast.savestate.hash())

	if firstBoot == nil then
		t.ran({
			["a movie is playing"]      = flycast.emulator.isReplay(),
			["restartLater exists"]     = type(flycast.emulator.restartLater) == "function",
			["frames are advancing"]    = n > 0,
		})
		local f = io.open(MARK, "w")
		f:write(("%d %s"):format(frame, hash))
		f:close()
		print(("  boot 1: frame=%d hash=%s - requesting restart"):format(frame, hash))
		flycast.emulator.restartLater()
		-- Nothing after this line in THIS interpreter is reached: the restart
		-- tears Lua down. Reaching boot 2 at all is the assertion.
		return
	end

	local f1, h1 = firstBoot:match("^(%d+) (%d+)$")
	print(("  boot 2: frame=%d hash=%s   (boot 1 was frame=%s hash=%s)"):format(
			frame, hash, tostring(f1), tostring(h1)))

	-- THE CLAIM. Before the fixes this line was unreachable: the run timed out
	-- with the game panel still drawing and no second boot.
	t.check("the machine restarted in-process and kept emulating",
			n == SAMPLE, ("second boot reached callback %d"):format(SAMPLE))
	t.check("the restart produced a real second boot, not a resumed first one",
			f1 ~= nil and tonumber(f1) == frame,
			("both boots sampled at frame %s"):format(tostring(f1)))

	-- REPORTED, NOT ASSERTED. Two boots reaching the same frame with different
	-- hashes is the cold-boot-twice signal (docs/STATE-COVERAGE.md), but these
	-- two boots are not yet a controlled pair: boot 2 loads a skip.map that
	-- boot 1 wrote, among other carried-over host state. Claiming residue from
	-- this would be claiming more than the setup supports.
	if h1 ~= hash then
		t.limit("cold-boot determinism",
			("boots differ at the same frame (%s vs %s) - NOT yet evidence of init "
			 .. "residue; the two boots are not a controlled pair"):format(h1, hash))
	end
	t.finish()
end
