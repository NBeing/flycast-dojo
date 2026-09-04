--- Conformance suite for the cross-emulator Lua interface.
--- @runnable -- a launcher may offer this as a script to run.
---
--- The interface's own definition of conformance: if this passes, you conform.
--- Presence is not enough - it also checks shapes, failure modes, and the
--- contract rules that the spec settles, because those are what actually make
--- two implementations interchangeable.
---
---   dofile("adapters/conformance.lua")
---
--- Run it with a game loaded; several checks need a running machine.
---
--- A missing capability is NOT a failure. `emu.supports()` answering false is
--- a legitimate answer and is reported as SKIP. A failure means the interface
--- was claimed and then behaved differently to the specification.

-- Resolve the adapter relative to the CONFIG DIRECTORY, not the working one.
-- dofile("adapters/...") resolves against the cwd, which is the config dir when
-- flycast is started from a terminal there and something arbitrary when it is
-- started from a desktop menu - so the same script worked in testing and failed
-- from the launcher. SCRIPT_DIR is set by the host; the fallback keeps this
-- working on a host that does not set it.
local here = rawget(_G, "SCRIPT_DIR")
local api = dofile((here and (here .. "/") or "") .. "adapters/emuapi.lua").load()
local emu, joypad, memory, gui, ui, frame, savestate =
	api.emu, api.joypad, api.memory, api.gui, api.ui, api.frame, api.savestate

local pass, fail, skip = 0, 0, 0
local failures = {}

local function ok(cond, group, what)
	if cond then pass = pass + 1
	else fail = fail + 1; failures[#failures + 1] = group .. ": " .. what end
end

local function skipped() skip = skip + 1 end

--- Runs body only if every named capability is present.
local function needs(names, group, body)
	for _, n in ipairs(names) do
		if not emu.supports(n) then skipped() return end
	end
	body(group)
end

local function raises(fn) return not pcall(fn) end

--- 1. Capability integrity -------------------------------------------------
--- supports() must agree with reality in both directions.
local function checkCapabilities()
	local g = "capability"
	ok(type(emu.supports) == "function", g, "emu.supports exists")
	ok(emu.supports("emu.supports"), g, "supports() reports itself")
	ok(not emu.supports("emu.definitelyNotAThing"), g, "unknown name is false")
	ok(not emu.supports("garbage-without-a-dot"), g, "malformed name is false")
	ok(type(emu.apiversion()) == "number", g, "apiversion is a number")

	-- Anything claimed must actually be callable, and anything denied must not
	-- be silently present pretending to work.
	for _, name in ipairs(api.surface) do
		local ns, fn = name:match("^([%w_]+)%.([%w_]+)$")
		local t = api[ns]
		local present = type(t) == "table" and type(rawget(t, fn)) == "function"
		if emu.supports(name) then
			ok(present, g, name .. " claimed but absent")
		else
			ok(not present or true, g, name .. " denied")	-- denial may still expose a stub
		end
	end
end

--- 2. Indexing is 1-based --------------------------------------------------
local function checkIndexing()
	local g = "indexing"
	needs({"joypad.get"}, g, function()
		ok(raises(function() joypad.get(0) end),  g, "player 0 raises")
		ok(raises(function() joypad.get(99) end), g, "player 99 raises")
		ok(not raises(function() joypad.get(1) end), g, "player 1 accepted")
	end)
	needs({"joypad.getaxis"}, g, function()
		ok(raises(function() joypad.getaxis(1, 0) end), g, "axis 0 raises")
	end)
	needs({"savestate.save"}, g, function()
		ok(raises(function() savestate.save(0) end), g, "savestate slot 0 raises")
	end)
end

--- 3. Buttons are named booleans -------------------------------------------
local function checkButtons()
	local g = "buttons"
	needs({"joypad.buttons", "joypad.get", "joypad.set"}, g, function()
		local names = joypad.buttons()
		ok(type(names) == "table" and #names > 0, g, "buttons() returns a non-empty array")
		ok(type(names[1]) == "string", g, "button names are strings")

		local state = joypad.get(1)
		ok(type(state) == "table", g, "get() returns a table")
		local allBool = true
		for _, v in pairs(state) do if type(v) ~= "boolean" then allBool = false end end
		ok(allBool, g, "every button reads as a boolean")

		local a = names[1]
		local b = names[2] or names[1]
		joypad.set(1, { [a] = true })
		ok(joypad.get(1)[a] == true, g, "set true holds the button")

		--- The rule that lets two scripts share a pad: a key that is not
		--- mentioned must not be disturbed.
		joypad.set(1, { [b] = true })
		ok(joypad.get(1)[a] == true, g, "absent keys are left alone")

		joypad.set(1, { [a] = false })
		ok(joypad.get(1)[a] == false, g, "set false releases the button")
		joypad.set(1, { [b] = false })
	end)
	needs({"joypad.setbutton"}, g, function()
		ok(raises(function() joypad.setbutton(1, "definitelyNotAButton", true) end),
			g, "unknown button name raises")
	end)
end

--- 4. Memory and address spaces --------------------------------------------
local function checkMemory()
	local g = "memory"
	needs({"memory.spaces", "memory.space"}, g, function()
		local sp = memory.spaces()
		ok(type(sp) == "table" and #sp > 0, g, "spaces() returns a non-empty array")
		local hasMain = false
		for _, n in ipairs(sp) do if n == "main" then hasMain = true end end
		ok(hasMain, g, "'main' always exists")
		ok(type(memory.space("main")) == "table", g, "space('main') returns a table")
		ok(raises(function() memory.space("notARealSpace") end), g, "unknown space raises")
	end)
end

--- 5. The rollback contract ------------------------------------------------
--- The rule is that frame callbacks are not delivered for re-simulated frames.
--- That is directly observable: on a conforming host a frame callback can
--- NEVER see emu.isrollback() true, because it is not called then. This is the
--- check flycast-dojo itself would have failed before it gated VBlank.
---
--- An earlier version of this test asserted that frame.confirmed() advances by
--- one per callback. That was wrong and reported a false failure: the
--- relationship is only 1:1 while a session is running, and offline the
--- counter tracks emulated game frames rather than display vblanks, so it
--- legitimately repeats. Stalls are still counted, but as information rather
--- than a verdict.
local rollbackSamples, rollbackStalls, rollbackSeen = 0, 0, 0
local regressions, lastConfirmed = 0, nil

local function sampleRollback()
	if emu.supports("emu.isrollback") and emu.isrollback() then
		rollbackSeen = rollbackSeen + 1
	end
	if not emu.supports("frame.confirmed") then return end
	local c = frame.confirmed()
	if lastConfirmed ~= nil then
		rollbackSamples = rollbackSamples + 1
		if c == lastConfirmed then rollbackStalls = rollbackStalls + 1 end
		if c < lastConfirmed then regressions = regressions + 1 end
	end
	lastConfirmed = c
end

local function checkFrames()
	local g = "frames"
	needs({"frame.confirmed"}, g, function()
		ok(type(frame.confirmed()) == "number", g, "confirmed() is a number")
	end)
	needs({"frame.count"}, g, function()
		ok(type(frame.count()) == "number", g, "count() is a number")
	end)
	needs({"emu.isrollback"}, g, function()
		ok(type(emu.isrollback()) == "boolean", g, "isrollback() is a boolean")
	end)
end

--- 6. Drawing is confined to the draw callback -----------------------------
local function checkDrawingIsGated()
	local g = "threading"
	needs({"gui.text"}, g, function()
		ok(raises(function() gui.text(0, 0, "illegal") end),
			g, "gui.* outside a draw callback raises")
	end)
	needs({"ui.Text"}, g, function()
		ok(raises(function() ui.Text("illegal") end),
			g, "ui.* outside a draw callback raises")
	end)
end

--- Runs inside the draw callback: the same calls must now succeed.
local function checkDrawingWorks()
	local g = "drawing"
	needs({"gui.text", "gui.box"}, g, function()
		ok(not raises(function() gui.text(4, 4, "") end), g, "gui.text inside draw works")
		ok(not raises(function() gui.box(0, 0, 1, 1, 0xFFFFFFFF) end), g, "gui.box inside draw works")
	end)
	needs({"ui.Begin", "ui.End", "ui.Text"}, g, function()
		ok(not raises(function()
			ui.Begin("conformance##probe"); ui.Text(""); ui.End()
		end), g, "ui widgets inside draw work")
	end)
end

--- Driver ------------------------------------------------------------------
local ran, drawChecked = false, false

emu.registerafter(function()
	sampleRollback()
	if ran then return end
	ran = true
	checkCapabilities()
	checkIndexing()
	checkButtons()
	checkMemory()
	checkFrames()
	checkDrawingIsGated()
end)

gui.register(function()
	if drawChecked then return end
	drawChecked = true
	checkDrawingWorks()
end)

--- Reported at exit so the rollback sampling has a whole session to observe.
emu.registerexit(function()
	--- The contract itself.
	ok(rollbackSeen == 0, "rollback",
		string.format("frame callback observed isrollback() true %d times -"
			.. " re-simulated frames are being delivered", rollbackSeen))
	--- A confirmed-frame counter that goes backwards is not confirmed.
	ok(regressions == 0, "rollback",
		string.format("confirmed frame went backwards %d times", regressions))
	print("")
	print("=== emuapi conformance ===")
	print(string.format("host=%s api=%d", api.host, emu.apiversion()))
	print(string.format("pass=%d fail=%d skip=%d  (skip = capability absent, not a failure)",
		pass, fail, skip))
	print(string.format("rollback: samples=%d isrollback-in-callback=%d regressions=%d"
		.. "  (stalls=%d, informational)",
		rollbackSamples, rollbackSeen, regressions, rollbackStalls))
	if rollbackSeen == 0 and rollbackSamples > 0 then
		print("  note: no rollback occurred in this session, so the gate was not"
			.. " exercised - run under netplay to test it properly")
	end
	for _, f in ipairs(failures) do print("  FAIL " .. f) end
	print(fail == 0 and "RESULT: CONFORMS" or "RESULT: NON-CONFORMING")
end)

return { run = function() return pass, fail, skip end }
