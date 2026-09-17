--- The auto-purge PROBE (dojo:PurgeStale). Under tests/open/ because it needs a
--- launch flag; scripts/purgestaletest.sh runs it with PurgeStale=yes and judges the
--- EMULATOR log. Shape: save slot 7 at frame F; edit the movie at F-20 (an edit
--- invalidates every state above its frame, same clock as a rewind - the rewind
--- log grows). No explicit pause: savestate.save uses the emulator's own
--- stop-do-restart shape and movie.setButtons takes a pause reason for the
--- duration of the edit (lua.cpp) - and a paused machine fires no vblank, so a
--- probe that paused itself would never get its next callback. `[MEASURED
--- 2026-09-17]` the first draft did exactly that and timed out. The tick must
--- then delete slot 7 and say so: `TAS: auto-purged 1 stale state after guard
--- event #N (BASE kept)`.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))
local n, phase, F = 0, 0, 0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if phase == 0 and n == 200 then
		t.ran({
			["a movie is playing"]   = flycast.emulator.isReplay(),
			["frames are advancing"] = flycast.frame.count() > 0,
		})
		F = flycast.frame.count()
		flycast.savestate.save(7)
		print(("PURGE PROBE: saved slot 7 at frame %d"):format(F))
		local ok = flycast.movie.setButtons(F - 20, 1, { a = true })
		print(("PURGE PROBE: edited frame %d -> %s"):format(F - 20, tostring(ok)))
		t.check("the edit below slot 7's frame was applied", ok == true)
		phase = 1
	elseif phase == 1 and n >= 320 then
		t.check("probe done (the purge itself is judged from the emulator log)", true)
		t.finish()
		phase = 2
	end
end
