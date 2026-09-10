--- reactive - the observation layer, across the two threads it exists to span.
---
---   Run it:  scripts/testrun.sh scripts/tests/reactive.lua
---
--- WHAT IT IS FOR. A host runs the frame callback on the emulation thread and
--- the draw callback on the render thread, and flycast says so when a script
--- gets it wrong: "drawing is only allowed from the overlay callback ... Buffer
--- what you want to draw and emit it from overlay." Every script then hand-rolls
--- that buffer. `emuapi/components/reactive.lua` is the buffer, written once.
---
--- THE CLAIM THAT MATTERS IS A PAIR, and the first version of it was wrong in
--- an instructive way. It asserted that `gui.text` is refused from a signal
--- handler and allowed from a drawer - and the refusal PASSED FOR THE WRONG
--- REASON, because on this interface `gui.text` is a stub that always raises
--- and tells you to use the painter's surface. The permission half is what
--- caught it: the same call failed on BOTH sides, so the pair disagreed with
--- itself.
---
--- The real distinction is the SURFACE. Only a painter is handed one, so a
--- signal handler has nothing to draw with - the split is structural, not
--- checked. And a surface is FRAME-SCOPED: kept past its paint it raises,
--- which is the hazard worth asserting, since drawing into "whatever picture
--- happens to be up" is exactly what it prevents.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local root = os.getenv("FLYCAST_TESTLIB"):match("^(.*)/scripts/lua/testlib%.lua$")
table.insert(package.loaders or package.searchers, 1, function(name)
	local rest = name:match("^emuapi%.(.+)$")
	local file
	if name == "emuapi" then file = root .. "/emuapi/init.lua"
	elseif rest then        file = root .. "/emuapi/" .. rest:gsub("%.", "/") .. ".lua"
	else return nil end
	return loadfile(file) or ("\n\tno file '" .. file .. "'")
end)
_G.EMUAPI_HOST = "flycast"

local api = require("emuapi").load("flycast")
local rx  = require("emuapi.components.reactive").new(api)

--- Counters the claims are made against. Closures rather than globals so a
--- stray global from another test cannot satisfy one of these by accident.
local observeRuns, drawRuns = 0, 0
local onFires, onSeen = 0, nil
local drawAllowed, staleRefused = nil, nil
local keptSurface = nil
local streamSeen, streamSeenOnDraw = 0, 0
--- What a LATER observer sees of an EARLIER one's write, which is the whole
--- point of publishing as one step: it makes observer ORDER not matter.
local sawGet, sawPeek, orderChecks = nil, nil, 0

--- A signal driven by the frame count, so it changes on a schedule this test
--- controls rather than on whatever the game happens to be doing.
local tick    = rx.signal(0)
local isTenth = tick:map(function(v) return (v % 10) == 0 end)
local events  = rx.stream(4)		--- deliberately small, to reach the cap

isTenth:on(function(v)
	onFires = onFires + 1
	onSeen = v
	--- THE REFUSAL HALF, from the observe side, using a surface a painter kept.
	--- A signal handler is given none of its own, so the only way to reach a
	--- draw from here is to smuggle one - and that must fail.
	if staleRefused == nil and keptSurface ~= nil then
		staleRefused = not (pcall(function() keptSurface:text(4, 4, "stale") end))
	end
end)

rx.observe(function()
	observeRuns = observeRuns + 1
	tick:set(observeRuns)
end)

--- A SECOND observer, registered AFTER the first, reading what it wrote.
rx.observe(function()
	if orderChecks < 5 and observeRuns > 2 then
		orderChecks = orderChecks + 1
		--- :get() is the published value - LAST frame's - so this observer does
		--- not depend on having run after the other one.
		sawGet  = tick:get()
		--- :peek() is the live one, for an observer that deliberately wants
		--- this frame's write.
		sawPeek = tick:peek()
	end
end)

rx.observe(function()
	--- More emissions than the cap, so the drop path is reached rather than
	--- assumed. A stream that never overflows cannot prove it counts drops.
	for _ = 1, 3 do events:emit(observeRuns) end
end)

rx.draw(function(s)
	drawRuns = drawRuns + 1
	--- THE PERMISSION HALF: the same call, from the side that is handed a
	--- surface.
	if drawAllowed == nil and s ~= nil then
		drawAllowed = pcall(function() s:text(4, 4, "from a drawer") end)
	end
	keptSurface = s		--- deliberately kept, to be used too late below
end)

events:each(function(_, s)
	streamSeen = streamSeen + 1
	--- A stream handler runs on the DRAW side and is handed the surface, which
	--- is the half of the design that says events are buffered on one thread
	--- and painted on the other.
	if s ~= nil and pcall(function() s:text(4, 20, "from a stream handler") end) then
		streamSeenOnDraw = streamSeenOnDraw + 1
	end
end)

rx.attach()

--- The report runs as one more observer, after enough frames that both sides
--- have certainly run.
local done = false
rx.observe(function()
	--- WAIT FOR BOTH SIDES, rather than assuming the draw side has kept up.
	--- `[MEASURED 2026-09-10]` it had not: the first version reported after 90
	--- observed frames and found ONE draw, because the renderer's present - and
	--- so the overlay - starts later than the emulation thread does. Asserting
	--- against a side that has barely run measures the warm-up, not the layer.
	---
	--- Capped so a host that never draws FAILS rather than hanging: a test that
	--- waits forever reports nothing, which is worse than reporting a gap.
	if done then return end
	if observeRuns < 90 or (drawRuns < 10 and observeRuns < 600) then return end
	done = true

	t.ran({ ["the observe side ran"] = observeRuns > 0 })

	t.check("the draw side ran too", drawRuns > 0, tostring(drawRuns))
	--- NOT EQUAL, and deliberately not asserted as equal: the two callbacks are
	--- driven by different clocks, and requiring them to match would be
	--- asserting something the host never promised.
	t.check("both sides advanced independently", observeRuns > 0 and drawRuns > 0,
		observeRuns .. " observes, " .. drawRuns .. " draws")

	--- THE PAIR.
	t.check("a painter is handed a surface and CAN draw", drawAllowed == true,
		tostring(drawAllowed))
	t.check("...and that surface is dead once its paint is over",
		staleRefused == true, tostring(staleRefused))
	t.check("...which is what a signal handler would have to smuggle to draw",
		keptSurface ~= nil)

	--- A signal fires on CHANGE, not per frame. Over 90 frames a tenth-of-a-tick
	--- predicate flips roughly 18 times; the claim is that it is far fewer than
	--- the frames, which a per-frame implementation could not satisfy.
	t.check("a signal handler fires on CHANGE, not every frame",
		onFires > 0 and onFires < observeRuns / 2,
		onFires .. " fires in " .. observeRuns .. " frames")
	t.check("...and is handed the new value", type(onSeen) == "boolean")

	--- A derived signal settles within the same frame as its source.
	--- THE GUARANTEE THE live/shown SPLIT BUYS, and the only claim that can
	--- tell it from a signal that publishes on set(): a later observer reads
	--- the PUBLISHED value, so it sees a coherent previous frame rather than a
	--- half-updated current one - which is what makes observer order irrelevant.
	t.check("a later observer sees the PUBLISHED value, not this frame's write",
		sawGet ~= nil and sawPeek ~= nil and sawPeek == sawGet + 1,
		"get=" .. tostring(sawGet) .. " peek=" .. tostring(sawPeek))

	t.check("a derived signal agrees with its source",
		isTenth:get() == ((tick:get() % 10) == 0),
		tostring(tick:get()) .. " -> " .. tostring(isTenth:get()))

	--- Streams: drained on the draw side, and over the cap they DROP and count.
	t.check("stream events reach their handler", streamSeen > 0, tostring(streamSeen))
	t.check("...on the draw side, where drawing is allowed",
		streamSeenOnDraw == streamSeen,
		streamSeenOnDraw .. " of " .. streamSeen)
	t.check("a stream over its cap drops rather than growing",
		events:dropped_count() > 0, tostring(events:dropped_count()))
	t.check("...and never queues more than its cap",
		events:pending() <= 4, tostring(events:pending()))

	t.finish()
end)
