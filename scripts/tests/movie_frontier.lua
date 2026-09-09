--- movie_frontier - the two end-of-movie detectors must agree.
---
---   Run it:  scripts/testrun.sh scripts/tests/movie_frontier.lua
---
--- WHY. `Dojo::MovieEnd()` fixed the METRIC - "one past the last authored key"
--- rather than a frame count. It did not fix the +-1, and after it landed the
--- two live detectors still disagreed:
---
---   dojo.cpp   frame_number == MovieEnd() - 1   emu thread, BEFORE the frame
---   gui.cpp    frame_number == MovieEnd()       render thread, AFTER it
---
--- Both were "already converted", neither was obviously wrong, and nothing
--- owned the relationship between them. core/dojo/movie.h owns it now, and this
--- asserts the property that ownership is supposed to buy.
---
--- ITS FIRST ASSERTION IS THAT IT RAN. Without that, "the script never loaded"
--- and "the detector never fired" produce identical evidence - which is the
--- failure this project has paid for more than once.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local n, stage = 0, 1
local lines = {}
local function say(s) lines[#lines + 1] = s end
local function report(name, cond, detail)
	t.check(name, cond, detail); say((cond and "PASS  " or "FAIL  ") .. name)
end

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	if stage ~= 1 or n < 240 then return end	-- let the auto-seek land
	stage = 2

	t.ran({ ["frames are advancing"] = n > 0 })

	local len   = flycast.movie.length()
	local frame = flycast.frame.count()

	--- NON-VACUITY FIRST. A movie of zero length makes every frontier claim
	--- below trivially true, and a frame counter at zero means nothing played.
	report("the movie has a frontier at all", type(len) == "number" and len > 0,
		"length " .. tostring(len))
	report("and we are somewhere inside it", frame > 0 and frame < len,
		("frame %s of %s"):format(tostring(frame), tostring(len)))
	if not (type(len) == "number" and len > 0) then t.finish(); return end

	--- THE FRONTIER IS ONE PAST THE LAST AUTHORED FRAME, which is the whole
	--- definition MovieEnd() exists to hold. So the last frame must be
	--- authored and the frontier itself must not be.
	report("the frame just below the frontier IS authored",
		flycast.movie.has(len - 1) == true, "frame " .. (len - 1))
	report("the frontier itself is NOT authored",
		flycast.movie.has(len) == false, "frame " .. len)

	--- AND A FRAME PAST IT IS ALSO NOT. Without this, "not authored" would be
	--- satisfied by a movie that reports nothing authored anywhere.
	report("nor is a frame beyond it", flycast.movie.has(len + 60) == false)

	--- THE SPARSE HALF IS NOT DRIVEN HERE, AND THIS SAYS SO RATHER THAN
	--- IMPLYING COVERAGE BY SILENCE. A movie whose first frame is not 0 cannot
	--- be replayed at all today - TODOS.md and core/rend/mainui.cpp both record
	--- it, and three patches did not change it - so the route that would prove
	--- MovieEnd() beats size() on a sparse movie is closed. The dense case is
	--- what is measured, and on a dense movie the two are equal by definition,
	--- which is precisely why this test cannot catch a regression to size().
	t.limit("the sparse case",
		"a movie not keyed from frame 0 cannot be replayed today (TODOS.md), so "
		.. "the case where MovieEnd() and size() DIFFER is undrivable from here")
	say("LIMIT sparse case undrivable - see TODOS.md")

	t.finish()
end

local prevOverlay = flycast_callbacks.overlay
flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end
	if flycast.ui.Begin("movie frontier") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
	end
	flycast.ui.End()
end
