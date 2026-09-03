--- fbneo-rr adapter for the cross-emulator Lua interface.
---
--- UNVERIFIED. Written from the API surface observed in src/burner/lua/, not
--- run against the emulator. Treat every mapping here as a proposal.
---
--- This file exists mainly to show the shape of the hard case. fbneo-rr's
--- input model is not a differently-spelled version of per-player buttons, it
--- is a different data model: joypad.get(which) IGNORES its argument and
--- returns one flat table of every game input, keyed by the driver's own
--- string - "P1 Fire 1", "P2 Up", "P1 Coin". Conforming means splitting that
--- by player and mapping driver names onto canonical button names, which is
--- the single largest conformance cost identified so far.

local host = assert(rawget(_G, "fba") or rawget(_G, "emu"),
	"fbneo adapter loaded on a non-fbneo host")

--- The native `emu` global is about to be shadowed by ours, so keep a handle.
local native = host

local emu, frame, joypad, memory, savestate, gui, movie = {}, {}, {}, {}, {}, {}, {}

--- emu ------------------------------------------------------------------
function emu.pause()        native.pause() end
function emu.unpause()      native.unpause() end
function emu.message(s)     native.message(tostring(s)) end
function emu.romname()      return native.romname() end
function emu.gamename()     return native.gamename() end
function emu.screenwidth()  return native.screenwidth() end
function emu.screenheight() return native.screenheight() end
function emu.isonline()     return native.isonline() end
function emu.isreplay()     return native.isreplay() end
function emu.frameadvance() native.frameadvance() end
function emu.speedmode(m)   native.speedmode(m) end
function emu.registerbefore(fn) native.registerbefore(fn) end
function emu.registerafter(fn)  native.registerafter(fn) end
function emu.registerexit(fn)   native.registerexit(fn) end

--- Rollback. fbneo-rr already models this explicitly, so these are forwards
--- rather than the synthesis flycast needed.
function emu.isrollback()   return native.is_resim() end
function frame.count()      return native.framecount() end
function frame.confirmed()  return native.frame_confirmed() end
function frame.resimsteps() return native.resim_steps() end

--- joypad ---------------------------------------------------------------
--- The impedance mismatch. Driver input names are per-driver, so a complete
--- adapter needs a name table per game, or a heuristic plus an override file.
--- What follows is the heuristic; it will be wrong for some drivers and the
--- override path is the real answer.
---
---   "P1 Fire 1"  -> player 1, "a"
---   "P2 Up"      -> player 2, "up"
---   "P1 Coin"    -> player 1, "coin"
local FIRE_ORDER = { "a", "b", "c", "d", "x", "y", "z" }

local function canonical(rest)
	local lower = rest:lower()
	local fire = lower:match("^fire%s*(%d+)$")
	if fire then return FIRE_ORDER[tonumber(fire)] end
	if lower == "up" or lower == "down" or lower == "left" or lower == "right"
		or lower == "start" or lower == "coin" then
		return lower
	end
	return nil		-- unmapped: exposed under its raw name, see below
end

local function splitByPlayer(flat)
	local out = { {}, {}, {}, {} }
	for name, value in pairs(flat) do
		local p, rest = name:match("^[Pp](%d)%s+(.+)$")
		if p then
			local player = tonumber(p)
			if player >= 1 and player <= 4 then
				local key = canonical(rest)
				--- An unmapped input is still reachable under its raw name
				--- rather than silently dropped - losing inputs would be worse
				--- than exposing a non-portable key.
				out[player][key or rest] = value
			end
		end
	end
	return out
end

function joypad.get(player)
	return splitByPlayer(native.joypad and native.joypad.get() or {})[player] or {}
end

function joypad.getdown(player)
	return splitByPlayer(rawget(_G, "joypad_native_getdown")() )[player] or {}
end

--- joypad.buttons() must report what THIS driver has, which is only knowable
--- from a live driver - hence a query rather than a constant list.
function joypad.buttons()
	local seen, names = joypad.get(1), {}
	for name in pairs(seen) do names[#names + 1] = name end
	table.sort(names)
	return names
end

--- memory ---------------------------------------------------------------
--- Where flycast presents its second space as an offset window, fbneo already
--- has genuinely separate accessors - the _audio suffixed family. Same
--- interface either way, which is the point: a script asks for a space by name
--- and never learns whether it is a distinct CPU or a window.
local nm = native.memory

local spaces = {
	main = {
		readbyte      = function(a)    return nm.readbyte(a) end,
		readword      = function(a)    return nm.readword(a) end,
		readdword     = function(a)    return nm.readdword(a) end,
		writebyte     = function(a, v) nm.writebyte(a, v) end,
		writeword     = function(a, v) nm.writeword(a, v) end,
		writedword    = function(a, v) nm.writedword(a, v) end,
	},
	audio = {
		readbyte      = function(a)    return nm.readbyte_audio(a) end,
		readword      = function(a)    return nm.readword_audio(a) end,
		readdword     = function(a)    return nm.readdword_audio(a) end,
		writebyte     = function(a, v) nm.writebyte_audio(a, v) end,
		writeword     = function(a, v) nm.writeword_audio(a, v) end,
		writedword    = function(a, v) nm.writedword_audio(a, v) end,
	},
}

function memory.spaces() return { "main", "audio" } end

function memory.space(name)
	local sp = spaces[name]
	if sp == nil then
		error("unknown address space '" .. tostring(name) .. "'; see memory.spaces()", 2)
	end
	return sp
end

memory.readbyte   = spaces.main.readbyte
memory.readword   = spaces.main.readword
memory.readdword  = spaces.main.readdword
memory.writebyte  = spaces.main.writebyte
memory.writeword  = spaces.main.writeword
memory.writedword = spaces.main.writedword

function memory.getregister(n)   return nm.getregister(n) end
function memory.setregister(n,v) nm.setregister(n, v) end

--- savestate ------------------------------------------------------------
--- Confirm fbneo's slot base before trusting this; the interface is 1-based.
function savestate.save(slot)  native.savestate.save(slot) end
function savestate.load(slot)  native.savestate.load(slot) end
function savestate.save_mem()  return native.savestate.save_mem() end
function savestate.load_mem(s) native.savestate.load_mem(s) end
function savestate.hash(s)     return native.savestate.hash(s) end

--- gui ------------------------------------------------------------------
--- Already immediate primitives, so these are near-direct.
function gui.text(x, y, s)          native.gui.text(x, y, tostring(s)) end
function gui.box(x, y, x2, y2, c)   native.gui.box(x, y, x2, y2, c) end
function gui.line(x, y, x2, y2, c)  native.gui.line(x, y, x2, y2, c) end
function gui.register(fn)           native.gui.register(fn) end

--- movie ----------------------------------------------------------------
function movie.framecount() return native.movie.framecount() end
function movie.mode()       return native.movie.mode() end
function movie.stop()       native.movie.stop() end

return {
	namespaces = {
		emu = emu, frame = frame, joypad = joypad, memory = memory,
		savestate = savestate, gui = gui, movie = movie,
	},
	unsupported = {
		["memory.watch"] = true,	-- has stronger per-access hooks instead
	},
}
