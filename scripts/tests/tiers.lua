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

	--- IRREVERSIBLE. A script that could widen its own tier would not be
	--- declaring anything.
	local okAgain = pcall(st.declare, "full")
	report("declaring twice raises", not okAgain)
	report("...and did not widen anything", st.tier() == "observer")

	--- A malformed tier is a caller bug, per the spec's failure tier 1.
	report("an unknown tier name raises", not (pcall(st.declare, "wizard")))

	t.finish()
end
