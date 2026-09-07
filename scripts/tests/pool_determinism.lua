--- Two restores of one state, taken and restored BETWEEN FRAMES, must agree.
--- This is the guarantee docs/SPIKE-machine-pool.md establishes; the test exists
--- so a regression in the deferred path is caught rather than rediscovered.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))
local n, phase, blob, member, w = 0, 0, nil, 0, 0
local hashes = {}
flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if phase == 0 and n == 400 then
		t.ran({
			["a movie is playing"]  = flycast.emulator.isReplay(),
			["frames are advancing"] = flycast.frame.count() > 0,
			["the deferred API exists"] = type(flycast.savestate.snapshotLater) == "function",
		})
		flycast.savestate.snapshotLater(); phase = 1
	elseif phase == 1 then
		blob = flycast.savestate.takeSnapshot()
		if blob ~= "" then
			t.check("snapshot is non-trivial", #blob > 1000000, #blob .. " bytes")
			t.check("takeSnapshot hands over", flycast.savestate.takeSnapshot() == "")
			phase = 2
		end
	elseif phase == 2 then
		member = member + 1
		flycast.savestate.restoreLater(blob); phase, w = 3, 0
	elseif phase == 3 then
		w = w + 1
		if w == 6 then
			hashes[member] = flycast.savestate.hash()
			print(("  .. member %d hash=%s frame=%d"):format(member, tostring(hashes[member]), flycast.frame.count()))
			if member < 3 then phase = 2 else
				t.check("three restores agree",
					hashes[1] == hashes[2] and hashes[2] == hashes[3],
					table.concat({tostring(hashes[1]), tostring(hashes[2]), tostring(hashes[3])}, " "))
				t.finish()
				phase = 4
			end
		end
	end
end
