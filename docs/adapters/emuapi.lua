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
local function selfDir()
	local src = debug.getinfo(1, "S").source
	return (src:sub(1, 1) == "@") and src:sub(2):match("^(.*[/\\])") or ""
end

function M.load(hostOverride)
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

	M.host = host
	M.adapter = adapter
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
	"frame.count", "frame.confirmed", "frame.resimsteps",
	"joypad.buttons", "joypad.get", "joypad.set", "joypad.setbutton",
	"joypad.getdown", "joypad.getup", "joypad.getaxis", "joypad.setaxis",
	"memory.spaces", "memory.space",
	"memory.readbyte", "memory.readword", "memory.readdword",
	"memory.writebyte", "memory.writeword", "memory.writedword",
	"memory.getregister", "memory.setregister", "memory.watch",
	"savestate.save", "savestate.load", "savestate.save_mem",
	"savestate.load_mem", "savestate.hash",
	"gui.text", "gui.box", "gui.line", "gui.register",
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
