--- emuapi - entry point for the cross-emulator Lua interface.
---
--- Detects the host emulator, loads its adapter, and installs the neutral
--- namespaces as globals. See ../lua_api_spec.lua for the interface itself.
---
---   local api = dofile("adapters/emuapi.lua").load()
---   local emu, joypad, gui = api.emu, api.joypad, api.gui
---
--- Loading: emulators typically run one script file and do not necessarily put
--- its directory on package.path, so this is written to work under a plain
--- dofile. Where require() works, prefer it.

local M = {}

--- Host detection MUST run before anything is installed: fbneo-rr already owns
--- the global `emu`, which is also a name this interface wants. `fba` is the
--- distinctive alias, so it is what we key on.
local function detectHost()
	if rawget(_G, "flycast") ~= nil then return "flycast" end
	if rawget(_G, "fba") ~= nil then return "fbneo" end
	return nil
end

--- Directory this file was loaded from, so sibling adapters can be found
--- without depending on the working directory.
--- Where this file was loaded from, so sibling adapters are found without
--- depending on the working directory. debug.getinfo gives the path dofile was
--- called with, which is absolute when the caller used SCRIPT_DIR and relative
--- otherwise; SCRIPT_DIR is the fallback for the relative case.
local function selfDir()
	local src = debug.getinfo(1, "S").source
	local dir = (src:sub(1, 1) == "@") and src:sub(2):match("^(.*[/\\])") or ""
	if dir == "" or dir:sub(1, 1) ~= "/" then
		local here = rawget(_G, "SCRIPT_DIR")
		if here then return here .. "/adapters/" end
	end
	return dir
end

--- Loading twice must not build two adapters. Each one installs its own
--- handlers into the host's callback table, so the second would silently
--- orphan the first's - a script that requires this alongside another that
--- does the same would find only one of them running. The instance is
--- therefore cached in the registry, which survives separate dofile() calls,
--- rather than in this file's locals, which do not.
local REGISTRY_KEY = "emuapi.instance"

function M.load(hostOverride)
	local existing = rawget(_G, REGISTRY_KEY)
	if existing ~= nil then
		return existing
	end
	local host = hostOverride or detectHost()
	if host == nil then
		error("emuapi: unrecognised host emulator; pass a name to emuapi.load()")
	end

	local path = selfDir() .. host .. ".lua"
	local chunk, err = loadfile(path)
	if chunk == nil then
		error("emuapi: no adapter for '" .. host .. "' (" .. tostring(err) .. ")")
	end
	local adapter = chunk()

	--- Namespaces are returned, NOT installed. Installing them as globals
	--- cannot work everywhere: fbneo-rr already owns emu, memory, input,
	--- joypad, savestate, movie and gui, so overwriting them would break both
	--- its existing scripts and this adapter, which calls through those very
	--- tables. Scripts bind locally instead - see install() below.
	for name, tbl in pairs(adapter.namespaces) do
		M[name] = tbl
	end

	--- supports() is derived from the tables that were actually loaded, so it
	--- cannot drift from reality the way a hand-maintained list does. A name
	--- that exists but is explicitly unimplemented is declared in
	--- adapter.unsupported and answers false.
	local unsupported = adapter.unsupported or {}
	function M.emu.supports(name)
		if unsupported[name] then return false end
		local ns, fn = tostring(name):match("^([%w_]+)%.([%w_]+)$")
		if ns == nil then return false end
		local t = M[ns]
		return type(t) == "table" and type(rawget(t, fn)) == "function"
	end

	--- Bumped on breaking change. See LUA_TODO.md item 8.
	function M.emu.apiversion() return 0 end

	--- ---------------------------------------------------------------
	--- DECLARATION - what this script is allowed to do
	--- ---------------------------------------------------------------
	--- Declaration and discovery are ORTHOGONAL, not alternatives, and the
	--- spec says so because conflating them is how a host ends up with
	--- neither. emu.supports() is a PORTABILITY question - does this host
	--- have the function. declare() is an AUTHORISATION question - may this
	--- script call it here, now, in this session. A host can answer yes to
	--- the first and no to the second, and a script needs both answers.
	---
	--- Three tiers, ordered:
	---   observer  reads and draws. Cannot touch the machine.
	---   mutator   drives input and writes memory.
	---   full      savestates, recording, emulator control.
	---
	--- WHY A SCRIPT WOULD LIMIT ITSELF. Because that is what makes it
	--- portable to a session that limits it involuntarily. flycast-dojo today
	--- refuses Lua ENTIRELY when online (core/nullDC.cpp: lua::init is called
	--- only when !settings.network.online), which is the bluntest possible
	--- version of this: an overlay that could not desync anything is refused
	--- alongside one that could. When the host grows tiers, the mechanism
	--- below is what enforces them, and a script that already declares
	--- observer runs unchanged.
	---
	--- The granted tier may be NARROWER than the one asked for, and that is
	--- deliberately not an error - a script that can degrade should be able
	--- to, and one that cannot can say so itself:
	---
	---     local tier = emu.declare{ tier = "mutator" }
	---     if tier ~= "mutator" then  -- observe only, do not drive input
	local TIERS = { observer = 1, mutator = 2, full = 3 }

	--- What each name NEEDS, by interface name rather than by host binding, so
	--- this list is the spec's and not one adapter's. Anything absent needs
	--- only "observer": reading and drawing are the floor.
	local REQUIRES = {
		["joypad.set"] = "mutator",       ["joypad.setbutton"] = "mutator",
		["joypad.setaxis"] = "mutator",   ["memory.writebyte"] = "mutator",
		["memory.writeword"] = "mutator", ["memory.writedword"] = "mutator",
		["memory.setregister"] = "mutator",
		["savestate.save"] = "full",      ["savestate.load"] = "full",
		["savestate.save_mem"] = "full",  ["savestate.load_mem"] = "full",
		["movie.record"] = "full",        ["movie.stop"] = "full",
		["emu.pause"] = "full",           ["emu.unpause"] = "full",
		["emu.speedmode"] = "full",       ["emu.exit"] = "full",
		["emu.frameadvance"] = "full",    ["emu.run"] = "full",
	}

	--- Undeclared means full, so every script written before this existed keeps
	--- working. Declaring is opting IN to being restricted.
	local granted = "full"
	local declared = false

	--- A host with no authorisation model of its own permits everything, which
	--- is the honest answer rather than a pretend restriction. When a host
	--- gains one it caps here.
	local function hostCeiling()
		if type(M.emu.isonline) == "function" and M.emu.isonline() then
			return "observer"
		end
		return "full"
	end

	function M.emu.declare(req)
		if type(req) ~= "table" then
			error("emu.declare: expected a table, got " .. type(req), 2)
		end
		local want = req.tier
		if TIERS[want] == nil then
			error("emu.declare: unknown tier '" .. tostring(want)
					.. "' (observer, mutator, full)", 2)
		end
		if declared then
			error("emu.declare: already declared '" .. granted
					.. "'; declaring twice is a bug, not a widening", 2)
		end
		local ceiling = hostCeiling()
		granted = (TIERS[want] < TIERS[ceiling]) and want or
				((TIERS[ceiling] < TIERS[want]) and ceiling or want)
		declared = true

		--- Enforcement wraps the namespaces AFTER the adapter built them, so an
		--- adapter needs no knowledge of tiers to be governed by them.
		for name, need in pairs(REQUIRES) do
			local ns, fn = name:match("^([%w_]+)%.([%w_]+)$")
			local t = M[ns]
			if type(t) == "table" and type(rawget(t, fn)) == "function" then
				if TIERS[need] > TIERS[granted] then
					rawset(t, fn, function()
						error(name .. " needs tier '" .. need
								.. "' but this script declared '" .. granted
								.. "'", 2)
					end)
				end
			end
		end
		return granted
	end

	--- The tier actually in force. Never nil: an undeclared script is "full".
	function M.emu.tier() return granted end

	M.host = host
	M.adapter = adapter
	rawset(_G, REGISTRY_KEY, M)
	return M
end

--- Optional convenience for a host with room for them: publish the namespaces
--- as globals. Refuses rather than clobbers, because silently replacing a name
--- another script owns is the worst outcome available - and on fbneo-rr, where
--- seven of them are taken, it will refuse every time. That is the correct
--- answer there; use local binding instead.
---
---     local api = dofile("adapters/emuapi.lua").load()
---     local emu, joypad = api.emu, api.joypad
---
--- A file-scope local shadows a global for that file alone, so nothing else in
--- the Lua state is disturbed and it behaves the same on every host.
function M.install(opts)
	opts = opts or {}
	local prefix = opts.prefix or ""
	local taken = {}
	for name in pairs(M.adapter.namespaces) do
		if rawget(_G, prefix .. name) ~= nil then
			taken[#taken + 1] = prefix .. name
		end
	end
	if #taken > 0 and not opts.force then
		error("emuapi.install: these globals are already in use: "
			.. table.concat(taken, ", ")
			.. ". Bind locally instead, or pass a prefix.", 2)
	end
	for name, tbl in pairs(M.adapter.namespaces) do
		_G[prefix .. name] = tbl
	end
	return M
end

--- Every name the interface defines, for a conformance report. Kept here
--- rather than in an adapter so each adapter is measured against the same list.
M.surface = {
	"emu.pause", "emu.unpause", "emu.exit", "emu.message", "emu.romname",
	"emu.gamename", "emu.screenwidth", "emu.screenheight", "emu.isonline",
	"emu.isreplay", "emu.isrollback", "emu.frameadvance", "emu.speedmode",
	"emu.registerbefore", "emu.registerafter", "emu.registerexit", "emu.run",
	"emu.declare", "emu.tier",
	"frame.count", "frame.confirmed", "frame.resimsteps",
	"joypad.buttons", "joypad.get", "joypad.set", "joypad.setbutton",
	"joypad.getdown", "joypad.getup", "joypad.getaxis", "joypad.setaxis",
	"memory.spaces", "memory.space",
	"memory.readbyte", "memory.readword", "memory.readdword",
	"memory.writebyte", "memory.writeword", "memory.writedword",
	"memory.getregister", "memory.setregister", "memory.watch",
	"savestate.save", "savestate.load", "savestate.save_mem",
	"savestate.load_mem", "savestate.hash",
	"gui.text", "gui.box", "gui.boxfill", "gui.line", "gui.register", "gui.scale",
	"gui.rgba",
	-- ImGui baseline profile
	"ui.Begin", "ui.End", "ui.Text", "ui.TextColored", "ui.Button", "ui.SameLine",
	"ui.Checkbox", "ui.Selectable", "ui.SliderFloat", "ui.SliderInt",
	"ui.InputText", "ui.Separator", "ui.Spacing", "ui.SetNextWindowPos",
	"ui.SetNextWindowSize", "ui.GetMousePos", "ui.IsMouseClicked",
	"ui.IsMouseDown", "ui.IsMouseReleased", "ui.Image",
	"movie.record", "movie.stop", "movie.mode", "movie.framecount",
	"sound.voicecount", "sound.outputrate",
}

--- Prints which of the surface this host implements. The real conformance
--- suite (LUA_TODO.md Part 3) also checks shapes and failure modes; this only
--- reports presence.
function M.report()
	local have, miss = 0, {}
	for _, name in ipairs(M.surface) do
		if M.emu.supports(name) then have = have + 1 else miss[#miss + 1] = name end
	end
	print(string.format("emuapi: host=%s api=%d  %d/%d implemented",
		M.host, M.emu.apiversion(), have, #M.surface))
	if #miss > 0 then print("  missing: " .. table.concat(miss, " ")) end
	return have, miss
end

return M
