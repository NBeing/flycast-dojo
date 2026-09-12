--- tiers - the authorisation model, from a script's side.
---
---   Run it:  scripts/testrun.sh scripts/tests/tiers.lua
---
--- WHY THIS EXISTS. `emuapi/spec.lua` specifies an authorisation tier -
--- off / observer / mutator / full - and records that no host had ever
--- implemented it, so the contract had never been exercised anywhere. The
--- motivating case is netplay: upstream flycast-dojo refuses Lua ENTIRELY when
--- online, which the spec calls "the bluntest possible version of this", since
--- an overlay that could not desync anything is refused alongside a script that
--- could.
---
--- core/lua/luatier.cpp has an 18-claim self-test, and A SELF-TEST PROVES THE
--- MODEL AND NEVER THE BINDINGS. This is the other half: it asks the questions
--- through `flycast.state` the way a script would, and calls a guarded function
--- to see it actually refuse.
---
--- THE NEGATIVE CLAIMS ARE THE POINT. "declaring observer returns observer"
--- passes against a host that stores a string and enforces nothing. Only a
--- refused call proves there is a model behind it.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local n, stage = 0, 1
local function report(name, cond, detail) t.check(name, cond, detail) end

local st = flycast.state

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	if stage ~= 1 or n < 60 then return end
	stage = 2
	t.ran({ ["frames are advancing"] = n > 0 })

	--- The two questions the spec insists are different: what this host HAS,
	--- and what it will let this script DO.
	local okCap, cap = pcall(st.capability)
	report("the host answers what the session allows",
		okCap and type(cap) == "string" and cap ~= "", tostring(cap))
	local okTier, tier = pcall(st.tier)
	report("...and what is in force right now",
		okTier and type(tier) == "string" and tier ~= "", tostring(tier))

	--- UNDECLARED MEANS FULL, so every script written before tiers existed keeps
	--- working. Offline, that is what should be in force.
	report("an undeclared script is not restricted offline", tier == "full", tostring(tier))
	report("can() agrees with the tier",
		st.can("memory.read") == true and st.can("savestate.save") == true)

	--- A capability nobody classified needs FULL - the safe direction for an
	--- omission is refusing a script, not letting one through.
	report("an unclassified capability is not granted by default",
		st.can("wizardry.summon") == (tier == "full"))

	--- DECLARING NARROWS, ONCE.
	local okDecl, granted = pcall(st.declare, "observer")
	report("declaring observer is honoured", okDecl and granted == "observer", tostring(granted))
	report("...and the tier in force follows it", st.tier() == "observer")

	--- THE CLAIM A STRING-STORING HOST WOULD FAIL. An observer may draw and
	--- read; it may not own the session.
	local before = st.refusals()
	local okSave = pcall(flycast.savestate.save, 0)
	report("an observer is REFUSED a savestate", not okSave)
	report("...and the refusal is counted", st.refusals() == before + 1,
		tostring(st.refusals()) .. " vs " .. tostring(before))
	report("...while reading and drawing are still allowed",
		st.can("memory.read") and st.can("ui.text"))

	--- AND THE DEFERRED ROUTE TO THE SAME POWER IS REFUSED TOO.
	---
	--- `[MEASURED 2026-09-12]` it was not. The tier table carries
	--- `{ "savestate.", Tier::Full }`, which reads as covering the namespace,
	--- but the table only says what a name NEEDS - enforcement is per call site
	--- and there were exactly TWO, save and load. snapshotLater, restoreLater,
	--- loadSlotLater and saveSlotLater had none, so an observer refused
	--- savestate.load could restore the machine through loadSlotLater instead
	--- and the refusal counter never moved.
	---
	--- These four are the pooling/deferred route, which is the one that will
	--- GROW, so the claim is written per binding rather than as "the namespace
	--- is gated" - a future fifth binding must fail this, not inherit it.
	for _, name in ipairs({ "snapshotLater", "restoreLater", "loadSlotLater", "saveSlotLater" }) do
		local n0 = st.refusals()
		local arg = (name == "restoreLater") and "" or 0
		local ok = pcall(flycast.savestate[name], arg)
		report("an observer is REFUSED savestate." .. name, not ok)
		report("...and savestate." .. name .. "'s refusal is counted",
			st.refusals() == n0 + 1, tostring(st.refusals()) .. " vs " .. tostring(n0))
	end

	--- IRREVERSIBLE. A script that could widen its own tier would not be
	--- declaring anything.
	local okAgain = pcall(st.declare, "full")
	report("declaring twice raises", not okAgain)
	report("...and did not widen anything", st.tier() == "observer")

	--- A malformed tier is a caller bug, per the spec's failure tier 1.
	report("an unknown tier name raises", not (pcall(st.declare, "wizard")))

	---------------------------------------------------------------------------
	--- AND THROUGH THE NEUTRAL INTERFACE.
	---
	--- `emuapi/conformance.lua` runs inside this emulator and reports
	--- CONFORMS - but it SKIPS this one, and says exactly why:
	---
	---   "answering: emu.declare was not called: it changes the tier in force
	---    for the rest of the run"
	---
	--- Which is correct of a suite that has to leave the session as it found
	--- it, and is why `MEMORY.md` records the authorisation tier as never
	--- exercised on any host. A tier is one-way within a session, so exercising
	--- it needs a run that is ALLOWED to end narrowed. This is that run: the
	--- declaration above already happened, and everything below is measured
	--- through the neutral names rather than flycast's.
	local root = os.getenv("FLYCAST_TESTLIB"):match("^(.*)/scripts/lua/testlib%.lua$")
	if root then
		table.insert(package.loaders or package.searchers, 1, function(name)
			local rest = name:match("^emuapi%.(.+)$")
			local file
			if name == "emuapi" then file = root .. "/emuapi/init.lua"
			elseif rest then        file = root .. "/emuapi/" .. rest:gsub("%.", "/") .. ".lua"
			else return nil end
			local chunk = loadfile(file)
			return chunk or ("\n\tno file '" .. file .. "'")
		end)
		_G.EMUAPI_HOST = "flycast"
		local okLoad, api = pcall(function() return require("emuapi").load("flycast") end)
		report("emuapi loads against this emulator", okLoad and api ~= nil,
			okLoad and "" or tostring(api))
		if okLoad and api and api.emu then
			local e = api.emu
			--- TWO ENTRY POINTS, ONE CEILING. emuapi owns the neutral
			--- declaration and wraps its own namespaces; flycast owns the
			--- session ceiling and enforces at its own bindings. They are not
			--- two copies of one model - they are two doors capped by the same
			--- fact, and THAT fact is what must agree.
			report("emu.capability is the host's own ceiling, not a guess",
				e.capability() == st.capability(),
				tostring(e.capability()) .. " vs " .. tostring(st.capability()))

			--- The neutral declaration is emuapi's, and it has not been used in
			--- this run yet - so it starts unrestricted, exactly as the spec's
			--- compatibility rule requires.
			report("an undeclared script is unrestricted at the neutral layer",
				e.tier() == "full", tostring(e.tier()))

			--- AND IT IS CAPPED BY THE HOST - tested so that it can FAIL.
			---
			--- "declaring full offline grants full" is true of an emuapi that
			--- never asks the host anything, so asserting it proves nothing.
			--- The session here is offline and the real ceiling is "full", so
			--- the only way to make the question discriminate is to change the
			--- answer the host gives and require emuapi to have used it.
			---
			--- Stubbing is legitimate rather than a cheat: hostCeiling() looks
			--- capability() up at call time, which is exactly the coupling under
			--- test. An implementation that guessed from isonline() would answer
			--- "full" here and fail.
			local realCap = e.capability
			e.capability = function() return "observer" end
			local granted = e.declare{ tier = "full" }
			e.capability = realCap
			report("a neutral declaration is capped by what the HOST reports",
				granted == "observer", tostring(granted))
			report("...and the host's real ceiling is restored for what follows",
				e.capability() == st.capability())

			--- ONCE ONLY, at the neutral layer too - and this is emuapi's own
			--- rule, not a forwarded one.
			report("declaring twice raises at the neutral layer",
				not pcall(e.declare, { tier = "observer" }))
			report("...and a non-table request is refused",
				not pcall(e.declare, "observer"))
		end
	end

	t.finish()
end
