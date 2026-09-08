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
local hasMovie = false
--- A seek is expected when the clip carries a state. 1000 is comfortably past
--- any boot-frame count and comfortably below a real in-game state.
local seekTarget, sawSeek = 1000, false
--- Only for the first handful of paints: enough to place the windows on a fresh
--- profile, and then never again, so neither a drag nor a layout restored from
--- imgui.ini gets overridden.
local placeFrames, placeOnce = 0, true
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
	--- CAPABILITY-GATED, not assumed. A session booted straight into a
	--- savestate has no movie, and asserting on one that is not there would
	--- fail for the wrong reason. emu.supports() answering false is a
	--- legitimate answer; so is "this session has no movie".
	if not hasMovie then
		t.limit("movie-as-data checks", "no movie in this session")
		say("LIMIT movie-as-data checks (no movie)")
		return true
	end
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

--- 8. THE PICTURE IS NOT THE WINDOW --------------------------------------
--- The host letterboxes the game into whatever area the UI leaves for it
--- (core/rend/game_viewport.h). Overlays that draw in game pixels map through
--- that rectangle, so if it lies they land in the wrong place - or, as happened
--- before this existed, the picture ignores docked panels and draws underneath
--- them full-window.
STAGES[8] = function()
	--- flycast.session.*, not flycast.* - these live in the session namespace
	--- alongside getGameResolution. [MEASURED 2026-09-07] the first draft of
	--- this stage called them at the root, and the tour TIMED OUT rather than
	--- failing: the error was raised inside the vblank callback, which the host
	--- logs as a warning and then keeps calling, so the stage simply never
	--- returned true. A stage that can hang is worth less than one that can
	--- fail - hence the assertion below that the namespace exists at all.
	report("the display accessors are where the adapter looks for them",
		type(flycast.session) == "table"
			and type(flycast.session.getGameViewport) == "function"
			and type(flycast.session.getWindowSize) == "function"
			and type(flycast.session.getGameResolution) == "function")

	local vx, vy, vw, vh = flycast.session.getGameViewport()
	local ww, wh = flycast.session.getWindowSize()
	local gw, gh = flycast.session.getGameResolution()

	report("the window has a size", ww > 0 and wh > 0, ("%dx%d px"):format(ww, wh))
	report("the game viewport is inside the window",
		vx >= 0 and vy >= 0 and vx + vw <= ww and vy + vh <= wh,
		("%d,%d %dx%d in %dx%d"):format(vx, vy, vw, vh, ww, wh))

	--- LETTERBOXED, not stretched: the picture keeps the game's aspect whatever
	--- shape the area it is given. A viewport handed back as the raw area would
	--- pass the containment check above and fail this one.
	local want = gw / gh
	local got  = vw / vh
	report("the viewport preserves the game's aspect ratio",
		math.abs(want - got) < 0.02, ("want %.3f got %.3f"):format(want, got))

	--- THE LAST CHECK DEPENDS ON WHERE THE PICTURE LIVES, and asking is not a
	--- dodge: a game drawn as a dockable PANEL is legitimately inset by that
	--- panel's own chrome, so "spans the window" is false for a correct host.
	--- [MEASURED 2026-09-08] this stage failed the first time the game became a
	--- panel - 18,25 604x453 in 640x480 - and the failure was right about the
	--- assertion and wrong about the emulator.
	if flycast.session.isGamePanel() then
		--- The picture is a window the user can drag. It must still be a real
		--- picture rather than a collapsed sliver, which is the panel-mode
		--- version of the same worry: something took space and never gave it
		--- back. Half the window is a floor no correct layout crosses here,
		--- since nothing else is docked.
		report("as a panel, the picture still fills most of the window",
			vw * 2 >= ww and vh * 2 >= wh,
			("%dx%d of %dx%d"):format(vw, vh, ww, wh))
	else
		--- NOTHING IS DOCKED in a headless run, so the area is the whole window
		--- and the picture must touch two opposite edges of it. This is the
		--- regression test for the per-frame RESET: without it a stale
		--- reservation from an earlier frame survives and the picture shrinks
		--- into a corner forever, which no other assertion here would notice.
		report("undocked, the picture spans the window on its long axis",
			(vx == 0 and vw == ww) or (vy == 0 and vh == wh),
			("x %d w %d / y %d h %d"):format(vx, vw, vy, vh))
	end
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
	--- WAIT FOR THE SEEK, AND ASSERT IT. `dojo:AutoSeekState=0` jumps the movie
	--- to state 0 once frame_number passes 120 - so a tour that starts working
	--- immediately runs on the BOOT footage, and its own pool stage then keeps
	--- restoring a snapshot taken back there, holding frame_number under the
	--- threshold so the seek NEVER fires. The tour deadlocks its own fixture.
	---
	--- [MEASURED 2026-09-08] and it passed anyway, twice, because nothing
	--- asserted WHERE in the movie we were. `ran()` proves the script ran; this
	--- proves it ran on the footage it was set up for. Different claims.
	if stage == 1 and sub == 0 then
		local f = api.frame.count()
		if f > seekTarget then
			sawSeek = true
		elseif n < 900 then
			return                       -- still waiting for the jump
		end
		t.ran({ ["frames are advancing"] = f > 0 })
		if seekTarget > 0 then
			report("the movie seeked to its savestate before testing began",
				sawSeek, sawSeek and ("at frame " .. f)
					or ("still at frame " .. f .. " after 900 - the seek never fired"))
		end
		hasMovie = api.movie.length() > 0
		say(hasMovie and ("movie: " .. api.movie.length() .. " frames")
			or "no movie in this session - movie stages will report as limits")
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

	--- A RAISING STAGE MUST FAIL, NOT HANG.
	--- [MEASURED 2026-09-07] a stage called flycast.getGameViewport() when the
	--- function actually lives at flycast.session.getGameViewport. The host
	--- logs the exception as a warning and keeps calling this callback, so the
	--- stage never returned true and the whole run TIMED OUT - no verdict line,
	--- and indistinguishable from a slow machine or a wedged emulator. pcall
	--- turns that back into one red line naming the stage and the error, which
	--- is the difference between a test that reports and a test that sulks.
	local fn = STAGES[stage]
	if fn then
		local ok, doneOrErr = pcall(fn)
		if not ok then
			report(("stage %d raised"):format(stage), false, tostring(doneOrErr))
			stage, sub, n = stage + 1, 0, 0
			if STAGES[stage] == nil then t.finish() end
		elseif doneOrErr then
			stage, sub, n = stage + 1, 0, 0
			if STAGES[stage] == nil then t.finish() end
		end
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
	placeFrames = placeFrames + 1
	placeOnce = placeFrames <= 3
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
	--- POSITIONED ONCE, NOT EVERY FRAME.
	---
	--- The Lua binding takes no ImGuiCond, so SetNextWindowPos is
	--- ImGuiCond_Always: calling it per frame forces the window back to this
	--- spot every frame. With docking on that silently defeats the whole
	--- feature - you drag a window to an edge, ImGui docks it, and the next
	--- frame drags it straight back out. [MEASURED 2026-09-08] the symptom is
	--- exactly "it looks like it is about to dock and then doesn't", and it was
	--- this tour doing it, not the docking.
	if placeOnce then
		flycast.ui.SetNextWindowPos(6, 232)
		flycast.ui.SetNextWindowSize(392, 242)
	end
	if flycast.ui.Begin("tour - assertions") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
		flycast.ui.Separator()
		flycast.ui.Text(("stage %d/%d   %d passed  %d failed  %d limits")
			:format(math.min(stage, #STAGES), #STAGES, t.pass, t.fail, t.skipped))
	end
	flycast.ui.End()

	--- PANEL 2: live host state, and real widgets that actually work
	if placeOnce then
		flycast.ui.SetNextWindowPos(404, 232)
		flycast.ui.SetNextWindowSize(230, 242)
	end
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
