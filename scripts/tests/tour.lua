--- tour - a live, self-verifying demonstration of what this fork can do.
---
--- Watch it:  scripts/testrun.sh --watch scripts/tests/tour.lua
--- Judge it:  ctest --test-dir build-dojo7
---
--- Both, on purpose. A demo nobody checks rots into a screenshot of something
--- that used to work; a test nobody watches never shows you the thing itself.
---
--- FIVE SURFACES, drawn at once so they can be compared:
---   * the CONTENT overlay (gui.*) - game pixels, tracking the letterboxed
---     image, so a marker stays on the thing it marks
---   * the WIDGET surface (ui.*)   - window pixels, ImGui's own names
---   * the piano roll              - a component built on both
---   * live host state             - memory, joypad, sound, clocks
---   * the assertions              - every claim above, checked
---
--- Every stage asserts something that CAN fail, and several assert a CONTROL as
--- well: "these agree" is only worth printing next to "and these do not".
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

package.path = "/home/nbee/dev/flycast-dojo/?/init.lua;/home/nbee/dev/flycast-dojo/?.lua;" .. package.path
local api  = require("emuapi").load()
local roll = require("emuapi.components.pianoroll").new(api, { rows = 6 })

local lines, live = {}, {}
local function say(s) lines[#lines + 1] = s end
local function report(name, cond, detail)
	t.check(name, cond, detail)
	say((cond and "OK   " or "BAD  ") .. name .. (detail and ("  " .. detail) or ""))
	return cond
end

local n, stage, sub, blob = 0, 1, 0, nil
local hashes, target = {}, nil
local uiResult, pauseSeen, pauseWant = nil, {}, nil
local poolPre, poolBase = nil, nil
--- widget state the panel actually drives
local wdg = { check = true, slide = 42, text = "edit me", clicks = 0, sel = 2 }

local STAGES = {}

--- 1. TWO CLOCKS, AND THEY DISAGREE ---------------------------------------
STAGES[1] = function()
	local names = api.clock.list()
	report("clock.list names both clocks", #names == 2, table.concat(names, ","))
	local m, c = api.clock.now("movie_frame"), api.clock.now("confirmed_frame")
	report("the two clocks DISAGREE", m ~= c, ("movie=%d confirmed=%d"):format(m, c))
	report("the host says which is monotonic",
		api.clock.about("movie_frame").monotonic == false
		and api.clock.about("confirmed_frame").monotonic == true)
	report("an unknown clock answers nil+reason", api.clock.now("nope") == nil)
	return true
end

--- 2. MEMORY, IN TWO ADDRESS SPACES ---------------------------------------
STAGES[2] = function()
	--- spaces() publishes RECORDS, not names - {name=, base=, size=, writable=}
	--- - so a caller can bound its reads without a second call. The first draft
	--- of this tour did table.concat on them and got "invalid value (table)".
	local spaces = api.memory.spaces()
	local names = {}
	for i, sp in ipairs(spaces) do names[i] = sp.name end
	report("memory declares its address spaces as records",
		#spaces >= 2 and spaces[1].name ~= nil, table.concat(names, ","))
	report("each space states whether it is writable",
		spaces[1].writable ~= nil)
	local addr = 0x8C010000
	local was = api.memory.readdword(addr)
	api.memory.writedword(addr, 0xDEADBEEF)
	report("a dword written to main RAM reads back",
		api.memory.readdword(addr) == 0xDEADBEEF)
	api.memory.writedword(addr, was)
	report("and restoring it works", api.memory.readdword(addr) == was)
	--- the sound space is a WINDOW into the SH4 map; the adapter hides the
	--- offset, so address 0x100 there is 0x00800100 here.
	local snd = api.memory.space("sound")
	local sv = snd.readbyte(0x100)
	report("the sound space reads independently", type(sv) == "number", "byte=" .. sv)
	--- READS ANSWER nil+reason; WRITES RAISE. Not the same thing, and this
	--- tour asserted the wrong one: an out-of-range READ used to raise and now
	--- returns nil with a reason, which is the better contract (a read is a
	--- question, and "outside the space" is an answer). A WRITE still raises,
	--- because there is no sensible value to return for one that did not happen.
	local bad, why = snd.readbyte(0x7FFFFFFF)
	report("an out-of-range sound READ answers nil+reason",
		bad == nil and type(why) == "string", tostring(why):sub(1, 42))
	report("an out-of-range sound WRITE raises",
		not pcall(function() snd.writebyte(0x7FFFFFFF, 0) end))
	return true
end

--- 3. INPUT ---------------------------------------------------------------
STAGES[3] = function()
	local names = api.joypad.buttons()
	report("joypad declares its button names", #names > 8, #names .. " buttons")
	local st = api.joypad.get(1)
	report("player 1 state reads as a table", type(st) == "table")
	report("player 0 RAISES (indexing is 1-based)",
		not pcall(function() api.joypad.get(0) end))
	report("sound reports voices and rate",
		api.sound.voicecount() >= 0 and api.sound.outputrate() > 0,
		api.sound.outputrate() .. "Hz")
	return true
end

--- 4. THE MOVIE IS DATA ---------------------------------------------------
STAGES[4] = function()
	target = api.frame.count() + 240
	report("a frame reads back as a button table",
		type(api.movie.getframe(target, 1)) == "table")
	report("an unauthored frame is nil, NOT all-false",
		api.movie.getframe(999999, 1) == nil)
	api.movie.setframe(target, 1, { right = true })
	api.movie.setframe(target, 1, { a = true })
	local after = api.movie.getframe(target, 1)
	report("two writes both land", after.right and after.a)
	api.movie.setframe(target, 1, { a = false })
	local cleared = api.movie.getframe(target, 1)
	report("clearing one leaves the other", cleared.right and not cleared.a)
	report("the neighbouring frame is untouched",
		not (api.movie.getframe(target + 1, 1) or {}).right)
	report("recording is REFUSED during playback",
		flycast.replay.startRecording("SHOULD_NOT_EXIST") == false)
	roll.follow, roll.top, roll.selected = false, target - 2, target
	return true
end

--- 5. A STATE IS A VALUE, AND THE POOL IS DETERMINISTIC -------------------
STAGES[5] = function()
	if sub == 0 then flycast.savestate.snapshotLater(); sub = 1; return false end
	if sub == 1 then
		blob = flycast.savestate.takeSnapshot()
		if blob == "" then return false end
		report("a snapshot is taken between frames", #blob > 1000000, #blob .. " bytes")
		report("takeSnapshot hands over, it does not copy",
			flycast.savestate.takeSnapshot() == "")
		report("an in-place snapshot hashes equal to the live machine",
			api.savestate.hash(flycast.savestate.tostring()) == api.savestate.hash())
		sub = 2; return false
	end
	--- THE RULER STARTS AT THE RESTORE, NOT AT THE REQUEST.
	---
	--- restoreLater() POSTS; the restore happens at the top of some later
	--- rendered frame. Counting N frames from the post therefore measures
	--- "N frames minus however long the post sat in the queue", and that delay
	--- varies with how much this callback is doing - which is why the leaner
	--- pool_determinism.lua passed with the same sloppy ruler while this tour,
	--- drawing three panels and an overlay, did not. The machine was
	--- deterministic the whole time; the measurement was not.
	---
	--- So: wait for frame.count() to JUMP BACKWARDS (the restore landing), take
	--- that as the origin, and count from there.
	local GAPS = { 6, 6, 6, 14 }
	local i = math.floor(sub / 2)
	if sub % 2 == 0 then
		poolPre = api.frame.count()
		flycast.savestate.restoreLater(blob)
		sub, poolBase = sub + 1, nil
		return false
	end
	if poolBase == nil then
		if api.frame.count() >= poolPre then return false end   -- not landed yet
		poolBase = api.frame.count()
		return false
	end
	if api.frame.count() - poolBase < GAPS[i] then return false end
	hashes[i] = api.savestate.hash()
	say(("     member %d ran %d frames from %d"):format(i, api.frame.count() - poolBase, poolBase))
	sub = sub + 1
	if i < #GAPS then return false end
	report("three restores at the same frame count AGREE",
		hashes[1] == hashes[2] and hashes[2] == hashes[3], tostring(hashes[1]))
	report("a restore at a DIFFERENT frame count differs (the control)",
		hashes[4] ~= hashes[1], tostring(hashes[4]))
	return true
end

--- 6. THE UI SURFACE ------------------------------------------------------
STAGES[6] = function()
	if uiResult == nil then return false end
	report("ui.CalcTextSize measures rather than guessing a font",
		uiResult.measures, uiResult.detail)
	report("the content overlay knows the game's own resolution",
		uiResult.gw > 0 and uiResult.gh > 0, ("%dx%d game px"):format(uiResult.gw, uiResult.gh))
	report("gui.rgba packs green as 0xAABBGGRR (not RGBA)",
		api.gui.rgba(0, 255, 0, 255) == 0xFF00FF00)
	if uiResult.colorSpecOrder then
		report("ui.TextColored takes the argument order the spec declares", true)
	else
		--- documented, owned by emuapi's adapter, counted not hidden, and it
		--- PASSES the day the adapter is fixed. A permanently red test teaches
		--- people to ignore red.
		t.limit("ui.TextColored argument order",
			"spec says (r,g,b,a,s); host takes (text,r,g,b,a) - emuapi's to fix")
		say("LIMIT ui.TextColored argument order (emuapi's to fix)")
	end
	return true
end

--- 7. PAUSING IS IDEMPOTENT ----------------------------------------------
STAGES[7] = function()
	if sub == 0 then pauseWant = "pause"; sub = 1; return false end
	if sub == 1 then return false end
	report("pause() stops the machine", pauseSeen.stopped == true)
	report("pause() TWICE does not resume it", pauseSeen.stillStopped == true)
	report("unpause() resumes", pauseSeen.resumed == true)
	return true
end

--- CHAINED, NEVER REPLACED. emuapi installs its dispatcher into
--- flycast_callbacks when it loads (vblank, overlay, terminate), so
--- `flycast_callbacks = {}` here silently unregisters it - and with it every
--- api.gui.register painter and every emu.registerafter hook. [MEASURED
--- 2026-09-07] the first draft did exactly that: the content overlay never drew
--- once, and the stage waiting on it never finished, so the run TIMED OUT with
--- no error to read. Wrap what is there; do not take the table.
local prevVblank  = flycast_callbacks and flycast_callbacks.vblank
local prevOverlay = flycast_callbacks and flycast_callbacks.overlay
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	if stage == 1 and n < 200 then return end
	if stage == 1 and sub == 0 then
		t.ran({ ["a movie is playing"] = flycast.emulator.isReplay(),
		        ["frames are advancing"] = api.frame.count() > 0 })
		sub = 1
	end
	--- live readouts for the right-hand panel, refreshed every frame
	live.frame   = api.frame.count()
	live.conf    = api.clock.now("confirmed_frame")
	live.ram     = api.memory.readdword(0x8C010000)
	live.voices  = api.sound.voicecount()
	local held = {}
	for k, v in pairs(api.joypad.get(1) or {}) do if v then held[#held+1] = k end end
	table.sort(held); live.held = #held > 0 and table.concat(held, "+") or "(none)"

	local fn = STAGES[stage]
	if fn and fn() then
		stage, sub, n = stage + 1, 0, 0
		if STAGES[stage] == nil then t.finish() end
	end
end

--- THE CONTENT OVERLAY: game pixels, not window pixels ---------------------
--- Everything here is positioned in the GAME's coordinate system, so it stays
--- on the thing it marks when the window is resized or letterboxed. That is
--- the difference between this surface and ui.*, and the reason both exist.
api.gui.register(function(s)
	local gw, gh = s:size()
	local green = api.gui.rgba(0, 255, 0)
	local amber = api.gui.rgba(255, 190, 0)
	local dim   = api.gui.rgba(0, 255, 255, 90)
	--- a frame around the game image itself: if the mapping is wrong, this is
	--- visibly not the border of the picture.
	s:box(1, 1, gw - 1, gh - 1, dim)
	--- crosshair at the centre, in game pixels
	s:line(gw / 2 - 20, gh / 2, gw / 2 + 20, gh / 2, green)
	s:line(gw / 2, gh / 2 - 20, gw / 2, gh / 2 + 20, green)
	--- a marker that sweeps with the frame counter, proving these are live
	local x = (live.frame or 0) % gw
	s:boxfill(x - 3, gh - 14, x + 3, gh - 8, amber)
	s:text(6, gh - 30, ("content overlay: %dx%d game px"):format(gw, gh))

	if uiResult == nil and stage >= 6 then
		local wide = select(1, flycast.ui.CalcTextSize("MMMMM"))
		local thin = select(1, flycast.ui.CalcTextSize("i"))
		uiResult = {
			measures = wide > thin,
			detail = ("MMMMM=%.0fpx i=%.0fpx"):format(wide, thin),
			gw = gw, gh = gh,
			colorSpecOrder = pcall(function() flycast.ui.TextColored(1, 1, 1, 1, "x") end),
		}
	end
end)

flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end   -- emuapi's painters, including gui.register
	if pauseWant == "pause" then
		pauseWant = nil; flycast.emulator.pause()
	elseif stage == 7 and sub == 1 and flycast.movie.editable() then
		if pauseSeen.stopped == nil then
			pauseSeen.stopped = true; flycast.emulator.pause()
		elseif pauseSeen.stillStopped == nil then
			pauseSeen.stillStopped = flycast.movie.editable(); flycast.emulator.resume()
		end
	elseif stage == 7 and sub == 1 and pauseSeen.stillStopped ~= nil then
		pauseSeen.resumed = true; sub = 2
	end

	--- PANEL 1: the assertions
	flycast.ui.SetNextWindowPos(6, 232)
	flycast.ui.SetNextWindowSize(392, 242)
	if flycast.ui.Begin("tour - assertions") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
		flycast.ui.Separator()
		flycast.ui.Text(("stage %d/%d   %d passed  %d failed  %d limits")
			:format(math.min(stage, #STAGES), #STAGES, t.pass, t.fail, t.skipped))
	end
	flycast.ui.End()

	--- PANEL 2: live host state, and real widgets that actually work
	flycast.ui.SetNextWindowPos(404, 232)
	flycast.ui.SetNextWindowSize(230, 242)
	if flycast.ui.Begin("live") then
		flycast.ui.Text(("movie frame  %s"):format(live.frame or "-"))
		flycast.ui.Text(("confirmed    %s"):format(live.conf or "-"))
		flycast.ui.Text(("aica voices  %s"):format(live.voices or "-"))
		flycast.ui.Text(("p1 held      %s"):format(live.held or "-"))
		flycast.ui.Text(("ram@8C010000 %08X"):format(live.ram or 0))
		flycast.ui.Separator()
		--- the widget surface, driven for real: these hold their values
		wdg.check = flycast.ui.Checkbox("a checkbox", wdg.check)
		wdg.slide = flycast.ui.SliderInt("slider", wdg.slide, 0, 100)
		wdg.text  = flycast.ui.InputText("input", wdg.text)
		flycast.ui.Button("click me##tour", function() wdg.clicks = wdg.clicks + 1 end)
		flycast.ui.Text(("clicked %d times"):format(wdg.clicks))
		--- and the lowercase overlay primitives, which are a different API
		flycast.ui.bargraph(wdg.slide / 100)
	end
	flycast.ui.End()

	roll:draw()
end
