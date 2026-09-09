--- Calling stopGame()/startGame() from a `vblank` callback must REFUSE, not hang.
---
--- vblank is dispatched from Emulator::vblank() on the emulation thread.
--- Emulator::stop() calls checkStatus(true) -> threadResult.get(), which blocks
--- until the emulation thread finishes - so from vblank it is the thread waiting
--- for itself. `[MEASURED 2026-09-08]` before the guard, this exact script never
--- returned from the call: the process kept drawing, the log kept its last line,
--- and nothing anywhere said why.
---
--- A guard that turns an undiagnosable hang into one error line is worth a test
--- of its own, because the regression is not a wrong answer - it is no answer.
--- If this test ever TIMES OUT rather than fails, the guard is gone.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local CALL_AT, CHECK_AT = 100, 200
local n, returned = 0, false

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1

	if n == CALL_AT then
		t.ran({
			["frames are advancing"] = n > 0,
			["restartLater is the offered alternative"] =
					type(flycast.emulator.restartLater) == "function",
		})
		flycast.emulator.stopGame()
		-- Reaching this line at all is the assertion: the call returned instead
		-- of joining the thread it was running on.
		returned = true
	end

	if n == CHECK_AT then
		t.check("stopGame() from vblank RETURNED instead of deadlocking", returned)
		-- ...and the machine is still emulating, so the guard refused the call
		-- rather than half-performing it.
		t.check("the emulator kept running afterwards", n == CHECK_AT,
				("reached callback %d"):format(CHECK_AT))
		t.finish()
	end
end
