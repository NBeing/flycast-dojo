--- deferredslot - save and load a numbered slot from the DEFERRED point, and
--- prove the machine both moved and survived.
---
---   Run it:  scripts/testrun.sh scripts/tests/deferredslot.lua
---
--- WHY THIS EXISTS. savestate.loadSlotLater / saveSlotLater have been in the
--- tree as BISECT PROBES, unshipped, because core/lua/lua.cpp recorded that
--- loading a savestate from deferred::drain() wedges the emulator. That was
--- `[CORRECTED 2026-09-10]`: the wedge was a bad STATE - one written from the
--- emulation thread - and not a bad PLACE. A normally-saved state loads there
--- and the machine keeps running.
---
--- The correction was made with a C++ probe. A probe is not a test, so this is
--- the test: it is what stood between those two bindings and being supported.
---
--- THREE THINGS IT HAS TO ASSERT, and the first two are where this class of
--- check usually goes wrong:
---
---   FRAMES WERE ADVANCING BEFORE. A wedged emulator and one that was never
---   running produce the same evidence afterwards.
---
---   THE LOAD ACTUALLY MOVED THE MACHINE. `[MEASURED 2026-09-10]` the C++ probe
---   that preceded this reported "LOADED" while doing nothing at all, because
---   it loaded a state whose frame was the frame it was already on. The test
---   therefore lets the movie run WELL PAST the save before loading it back,
---   and requires the frame number to fall.
---
---   THE MACHINE KEPT RUNNING AFTERWARDS. That is what "wedge" means, and
---   returning from the call does not answer it. Checked a second later.
--- MADE TO FAIL, TWICE `[MEASURED 2026-09-10]`:
---
---   loadSlotLater does nothing        -> 3 passed, 2 failed
---   the slot argument is ignored      -> 3 passed, 2 failed
---
--- Both break the same two claims, and one of those is `[OPEN]`: sabotage B
--- loads slot 0 instead of 5, so the machine should have jumped to slot 0's
--- frame (9928) and failed the LANDING claim with a WRONG frame. Instead it did
--- not move at all.
---
--- `[NARROWED 2026-09-10]` three explanations are ruled out, each by a log line
--- that is absent from the sabotage run:
---
---   NOT REFUSED.   gui_loadState() warns when its `gui_state == Closed &&
---                  savestateAllowed()` guard turns it away. No warning - so
---                  the call went through and did the stop/load/start.
---   NOT THE SLOT.  the sabotage logs the slot it used: 0, as intended.
---   NOT A MISSING FILE. dc_loadstate RETURNS EARLY on one it cannot open,
---                  with "Failed to load state - could not open %s" and a
---                  "Save state not found" notification. Neither appears.
---
--- So slot 0 was opened and loaded, and the movie index stayed at 10529 anyway,
--- while the identical load from a C++ probe moved a machine 10400 -> 9928.
--- Still not established. The remaining suspect is how dojo.frame_number is
--- restored - it comes from the .frame sidecar rather than the state blob - but
--- that is a guess, and this note is a list of what is KNOWN not to be the
--- cause rather than a theory about what is.
---
--- The test is sound either way - a broken loadSlotLater fails it both times -
--- but the diagnosis it prints for a wrong-slot bug is less precise than
--- intended, and that is worth knowing before trusting the message over the
--- code.

local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local ss   = flycast.savestate
local SLOT = 5			-- not 0: slot 0 is BASE and the clip already has one

local n, stage = 0, 1
local prevFrame, steady = -1, 0
local savedAt, ranAhead, loadedAt, afterLoad = 0, 0, 0, 0
local lines = {}
local function say(s) lines[#lines + 1] = s end
local function report(name, cond, detail)
	t.check(name, cond, detail)
	say((cond and "PASS  " or "FAIL  ") .. name)
end

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	local f = flycast.frame.count()

	if stage == 1 then
		--- WAIT FOR STEADY PLAYBACK, not for a frame count.
		---
		--- `[MEASURED 2026-09-10]` an earlier version waited 240 vblanks and
		--- called that "let the auto-seek land first". It does not: the seek
		--- landed AFTER, so the state was saved at frame 100 and the machine
		--- then jumped to 9928 on its own. Every later claim was then about a
		--- sequence nobody intended, and the test still went green - which is
		--- worse than the bug it was hiding.
		---
		--- The gate is that the movie index has advanced by EXACTLY ONE for 60
		--- consecutive frames. A seek is a jump, so it resets the run; steady
		--- playback is the only thing that satisfies it. Clip-independent, so
		--- no frame number is written into this file.
		if prevFrame >= 0 and f == prevFrame + 1 then
			steady = steady + 1
		else
			steady = 0
		end
		prevFrame = f
		if steady < 60 then return end
		stage = 2
		-- NON-VACUITY, asserted before anything else: a machine that was never
		-- running cannot be shown to have been wedged by this test.
		-- NON-VACUITY, and it is about the MOVIE advancing rather than the
		-- callback firing: a paused emulator still delivers vblanks.
		t.ran({ ["the movie is playing steadily"] = steady >= 60 })
		savedAt = f
		ss.saveSlotLater(SLOT)
		say("saved slot " .. SLOT .. " at frame " .. f)
		return
	end

	if stage == 2 then
		-- RUN WELL PAST THE SAVE. If the load were issued from near the saved
		-- frame, "it loaded" and "it did nothing" would look identical - which
		-- is exactly how the probe before this test reported a false success.
		if f < savedAt + 600 then return end
		stage = 3
		ranAhead = f
		-- NAMED FOR WHAT IT ASSERTS. `[MEASURED 2026-09-10]` this used to say
		-- "the movie ran on past the save", and what actually carries the
		-- machine away from the save point here is the AutoSeekState jump, not
		-- playback. The property the later claims need is only that we are FAR
		-- from it - by 600 frames, however we got there - so that a load and a
		-- no-op cannot produce the same frame number.
		report("the machine is far from the saved frame when the load is asked for",
			f > savedAt + 500, savedAt .. " -> " .. f)
		ss.loadSlotLater(SLOT)
		say("asked to load slot " .. SLOT .. " at frame " .. f)
		loadedAt = n
		return
	end

	if stage == 3 then
		-- CATCH THE DROP ON THE VBLANK IT HAPPENS, not 120 vblanks later.
		--
		-- Waiting first and then measuring makes the landing point useless: the
		-- machine keeps emulating after the load, so by the time you look it
		-- has moved on by however long you waited, and the tolerance has to be
		-- widened to cover that. `[MEASURED 2026-09-10]` at a 300-frame
		-- tolerance a load of the WRONG slot - slot 0, at frame 9928 - lands
		-- within it and the claim passes. A check that cannot tell slot 5 from
		-- slot 0 is not checking the slot.
		if f < ranAhead then
			stage = 4
			afterLoad = f
			report("the load MOVED the machine back", true, ranAhead .. " -> " .. f)
			-- TIGHT, because a load is exact: the state carries the frame it
			-- was taken at, so the only slack is the vblank this was noticed on.
			report("...and landed at the frame it was saved from",
				math.abs(f - savedAt) <= 30, "saved " .. savedAt .. ", landed " .. f)
			return
		end
		-- A load that never lands must FAIL, not hang the suite forever.
		if n > loadedAt + 600 then
			stage = 4
			afterLoad = f
			report("the load MOVED the machine back", false,
				"600 vblanks after the request, still at " .. f)
			report("...and landed at the frame it was saved from", false, "never landed")
		end
		return
	end

	if stage == 4 then
		if f <= afterLoad and n < loadedAt + 700 then return end
		if n >= loadedAt + 700 and f <= afterLoad then
			report("the emulator kept running after the load", false,
				"stuck at " .. f)
			t.finish()
			stage = 5
			return
		end
		stage = 5
		-- THE WEDGE CHECK. Reaching this callback at all already means the
		-- emulation thread is alive; the frame number moving means it is
		-- actually emulating.
		report("the emulator kept running after the load", f > afterLoad,
			afterLoad .. " -> " .. f)
		t.finish()
	end
end

local prevOverlay = flycast_callbacks.overlay
flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end
	if flycast.ui.Begin("deferredslot") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
	end
	flycast.ui.End()
end
