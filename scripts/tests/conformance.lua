--- conformance - run emuapi's OWN suite against the real emulator.
---
---   Run it:  scripts/testrun.sh scripts/tests/conformance.lua
---
--- WHY THIS EXISTS. emuapi ships four adapters and has only ever MEASURED two
--- of them: the mock and agnes, both driven by a runner that is itself Lua.
--- adapters/flycast.lua carries "[DERIVED, NOT RUN]" over its unsupported
--- table, meaning its claims about this emulator were read out of the source
--- rather than observed. A package with two hosts it has never run is a package
--- whose conformance result is an opinion for half its surface.
---
--- IT RUNS THE SUITE ITSELF, NOT A COPY OF IT. run-conformance.lua exists and
--- works, but it needs a runner to BE the frame loop, and here the emulator is
--- one. So this reproduces only the bootstrap - the module searcher and the
--- host name - and then lets vblank drive, which is the whole point: the checks
--- execute against real savestates, a real movie and real guest memory instead
--- of against a Lua model of them. A second copy of the suite would be a second
--- definition of the word "conforms", which is exactly what run-conformance.lua
--- refuses to become.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

--- WHERE emuapi LIVES, relative to this file. FLYCAST_TESTLIB is
--- <root>/scripts/lua/testlib.lua, so the repository root is two directories up
--- and the submodule sits beside it. Derived rather than hardcoded, so a
--- checkout at another path still finds it.
local root = os.getenv("FLYCAST_TESTLIB"):match("^(.*)/scripts/lua/testlib%.lua$")
local emuapiDir = root and (root .. "/emuapi")

local lines = {}
local function say(s) lines[#lines + 1] = s end

--- THE SAME SEARCHER run-conformance.lua installs, and for the same reason it
--- gives: mapping module names to files under a known root works whatever the
--- directory is called, where a package.path pattern only works when the
--- directory happens to be named "emuapi".
local function installSearcher(dir)
	table.insert(package.loaders or package.searchers, 1, function(name)
		local rest = name:match("^emuapi%.(.+)$")
		local file
		if name == "emuapi" then file = dir .. "/init.lua"
		elseif rest then      file = dir .. "/" .. rest:gsub("%.", "/") .. ".lua"
		else return nil end
		local chunk, err = loadfile(file)
		if chunk == nil then return "\n\tno file '" .. file .. "' (" .. tostring(err) .. ")" end
		return chunk
	end)
end

local api, loadErr
if emuapiDir == nil then
	loadErr = "could not derive the repository root from FLYCAST_TESTLIB"
else
	installSearcher(emuapiDir)
	_G.EMUAPI_HOST = "flycast"
	local ok, res = pcall(function() return require("emuapi").load("flycast") end)
	if ok then api = res else loadErr = tostring(res) end
end

--- The suite registers its own callbacks on load and reports from an exit hook.
local loadedSuite, suiteErr = false, nil
if api then
	local ok, err = pcall(function() require("emuapi.conformance") end)
	loadedSuite, suiteErr = ok, err
end

local n, reported = 0, false
local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1

	if n == 1 then
		t.ran({ ["frames are advancing"] = true })
		t.check("emuapi loads against the real emulator", api ~= nil, loadErr)
		if api == nil then t.finish(); return end
		t.check("the conformance suite loads", loadedSuite, suiteErr and tostring(suiteErr))
		if not loadedSuite then t.finish(); return end
		say("host " .. tostring(api.host) .. "  " .. _VERSION)
	end

	--- 400 frames is what run-conformance.lua gives the other hosts, so the
	--- suite gets the same number of chances to observe a frame boundary here.
	--- Fewer would make a SKIP here mean something different from a SKIP there.
	if reported or api == nil or not loadedSuite or n < 400 then return end
	reported = true

	--- FIRE THE EXIT DISPATCH OURSELVES. The host only does it at shutdown,
	--- which is after this harness has to have printed a verdict.
	local ok, err = pcall(function() api.adapter.driver.finish() end)
	t.check("the suite reports before the emulator shuts down", ok, err and tostring(err))

	--- The suite sets this in its own exit handler. Reading its marker rather
	--- than its counters is deliberate: duplicating the arithmetic would be a
	--- second opinion about what "conforms" means, which is the thing this file
	--- exists to avoid.
	local verdict = rawget(_G, "EMUAPI_CONFORMS")
	if verdict == nil then
		t.limit("flycast conformance",
			"the suite ran but published no verdict - EMUAPI_CONFORMS was never set")
		say("LIMIT no verdict published")
	else
		t.check("flycast CONFORMS to the emuapi interface", verdict == true,
			verdict and "CONFORMS" or "NON-CONFORMING - see the log above for FAIL lines")
		say(verdict and "CONFORMS" or "NON-CONFORMING")
	end
	t.finish()
end

local prevOverlay = flycast_callbacks.overlay
flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end
	if flycast.ui.Begin("conformance") then
		flycast.ui.Text(("frame %d"):format(n))
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
	end
	flycast.ui.End()
end
