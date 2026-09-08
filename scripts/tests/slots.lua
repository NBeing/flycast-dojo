--- slots - every slot the host says it has must actually be usable.
---
---   Run it:  scripts/testrun.sh scripts/tests/slots.lua
---
--- WHY THIS EXISTS. core/lua/lua.cpp held three opinions about how many
--- savestate slots there are:
---
---   slotCount()        100   (MAX_SAVESTATE_SLOTS)
---   savestateAnchor    1..100
---   luaSavestateSlot   0..9  <- save/load refused everything above 9
---
--- So 90 slots were inspectable and unreachable, and nothing noticed, because
--- no test had ever saved to a slot at all. The States window addresses all 100
--- and the F4 wall shows them, so this was not theoretical.
---
--- THE SHAPE OF THE CHECK MATTERS. "save to slot 0 works" would have passed
--- throughout. The claim has to be tied to the number the host REPORTS -
--- whatever slotCount() says, the last of them must work - so the check cannot
--- drift out of step with the answer it is checking.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local n, stage = 0, 1
local lines = {}
local function say(s) lines[#lines + 1] = s end
local function report(name, cond, detail)
	t.check(name, cond, detail)
	say((cond and "PASS  " or "FAIL  ") .. name)
end

local ss = flycast.savestate

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	if stage ~= 1 or n < 240 then return end	-- let the auto-seek land first
	stage = 2
	t.ran({ ["frames are advancing"] = n > 0 })

	local ok, count = pcall(ss.slotCount)
	report("the host reports a slot count", ok and type(count) == "number" and count > 0,
		ok and tostring(count) or tostring(count))
	if not (ok and type(count) == "number") then t.finish(); return end

	--- flycast's own slots are 0-based: slot 0 is BASE, 1..count-1 are
	--- checkpoints. (The neutral interface is 1-based and its adapter shifts;
	--- that shift is not this test's business.)
	local last = count - 1

	--- THE BUG. Before the fix this raised
	--- "savestate slot must be between 0 and 9".
	local okSave, saveErr = pcall(ss.save, last)
	report("the LAST slot the host reports can be SAVED to", okSave,
		okSave and ("slot " .. last) or tostring(saveErr))

	--- AND IT MUST COME BACK. A save that writes nothing loadable would pass
	--- the line above. Hashing across the round trip is what makes it a claim
	--- about the state rather than about the call returning.
	if okSave then
		local before = ss.hash()
		local okLoad, loadErr = pcall(ss.load, last)
		report("and LOADED back", okLoad, okLoad and ("slot " .. last) or tostring(loadErr))
		if okLoad then
			report("the machine is unchanged across the round trip",
				ss.hash() == before, ("%s -> %s"):format(tostring(before), tostring(ss.hash())))
		end
	end

	--- THE CONTROL. Without this, "save did not raise" is also satisfied by a
	--- host that lost its bounds check entirely - which is one plausible way to
	--- "fix" the bug above and is strictly worse than the bug.
	report("a slot ABOVE the reported count is still refused",
		not pcall(ss.save, count), "slot " .. count)
	report("a negative slot is still refused", not pcall(ss.save, -1))

	t.finish()
end

local prevOverlay = flycast_callbacks.overlay
flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end
	if flycast.ui.Begin("slots") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
	end
	flycast.ui.End()
end
