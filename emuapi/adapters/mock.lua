--- mock - a fake emulator that implements the interface, and nothing else.
---
--- WHY THIS EXISTS. Before it, the conformance suite had only ever run on one
--- emulator, against an adapter written by the same person who wrote the
--- interface. 133 passing checks in that arrangement measure agreement between
--- two halves of one head. Any assumption that leaked from flycast into the
--- "neutral" layer was invisible, because the only thing checking neutrality
--- was the thing that was not neutral.
---
--- So this is a second implementation with no emulator behind it: a few
--- kilobytes of Lua that keeps a little RAM, a pad, a frame counter and a
--- draw phase. If the suite passes here AND on flycast, the parts it checks
--- are portable. If it fails here, either the spec assumed something only
--- flycast does, or this is wrong - and both are worth knowing.
---
--- It is also the reference an adapter author reads: the smallest thing that
--- conforms, with the contract rules implemented rather than described.
---
--- It is NOT auto-detected. You ask for it by name:
---
---     require("emuapi").load("mock")
---
--- because a fake that could be selected automatically would stand in for a
--- real host that failed to be recognised, and a green suite that tested
--- nothing is the exact failure this package keeps writing rules about.

local emu, frame, joypad, memory   = {}, {}, {}, {}
local savestate, gui, ui           = {}, {}, {}
local movie, sound                 = {}, {}

--- The machine ----------------------------------------------------------
--- Addresses are the machine's own, per the spec: this one is flat and starts
--- at 0. It deliberately does NOT resemble an SH4, so a suite that only works
--- against flycast addresses fails here rather than passing by luck.
local MAIN_BASE, MAIN_SIZE  = 0x1000, 0x4000
local SOUND_BASE, SOUND_SIZE = 0x0000, 0x0400

local M = {
	main  = {},          -- sparse byte store
	sound = {},
	frame = 0,           -- displayed frames
	confirmed = 0,
	resim = 0,
	rollback = false,
	pads = { {}, {} },
	regs = { pc = 0x100, sp = 0x200 },
	drawing = false,     -- true only inside a draw callback
	running = true,
	speed = "normal",
	paused = false,
	movieMode = nil,
	movieFrames = 0,
}

local BUTTONS = { "a", "b", "x", "y", "start", "up", "down", "left", "right" }

local function checkPlayer(p)
	if type(p) ~= "number" or p < 1 or p > #M.pads then
		error("player must be 1.." .. #M.pads .. ", got " .. tostring(p), 3)
	end
end

--- Spaces are records of {store, base, size}, so bounds are known rather than
--- assumed - see NBNEO_SURVEY.md A6 for why that matters.
local SPACES = {
	main  = { store = M.main,  base = MAIN_BASE,  size = MAIN_SIZE },
	sound = { store = M.sound, base = SOUND_BASE, size = SOUND_SIZE },
}

local function spaceOf(name)
	local sp = SPACES[name]
	if sp == nil then error("unknown address space: " .. tostring(name), 3) end
	return sp
end

--- Reading an unmapped address returns nil, not zero. Failure tier 2: zero is
--- a legitimate value and returning it for "not mapped" turns a missing
--- feature into wrong data. flycast-dojo returns 0 here and records the
--- deviation; the mock does it the way the spec says so the difference stays
--- visible.
local function rd(sp, addr)
	if type(addr) ~= "number" then error("address must be a number", 3) end
	if addr < sp.base or addr >= sp.base + sp.size then
		return nil, string.format("address 0x%X is outside %d bytes at 0x%X",
				addr, sp.size, sp.base)
	end
	return sp.store[addr] or 0
end

local function wr(sp, addr, v)
	if type(addr) ~= "number" then error("address must be a number", 3) end
	if type(v) ~= "number" then error("value must be a number", 3) end
	if addr < sp.base or addr >= sp.base + sp.size then
		error(string.format("address 0x%X is outside %d bytes at 0x%X",
				addr, sp.size, sp.base), 3)
	end
	sp.store[addr] = v % 256
end

--- emu ------------------------------------------------------------------
function emu.romname()      return "MOCKROM" end
function emu.gamename()     return "Mock Machine" end
function emu.screenwidth()  return 320 end
function emu.screenheight() return 240 end
function emu.isonline()     return false end
function emu.isreplay()     return M.movieMode == "playback" end
function emu.isrollback()   return M.rollback end
function emu.pause()        M.paused = true end
function emu.unpause()      M.paused = false end
function emu.exit()         M.running = false end
function emu.message(s)     M.lastMessage = tostring(s) end
function emu.speedmode(m)
	if m ~= "normal" and m ~= "turbo" then
		error("speedmode must be 'normal' or 'turbo', got " .. tostring(m), 2)
	end
	M.speed = m
end

--- frame ----------------------------------------------------------------
function frame.count()      return M.frame end
function frame.confirmed()  return M.confirmed end
function frame.resimsteps() return M.resim end

--- joypad ---------------------------------------------------------------
function joypad.buttons()
	local out = {}
	for i, n in ipairs(BUTTONS) do out[i] = n end
	return out
end

local function isButton(name)
	for _, n in ipairs(BUTTONS) do if n == name then return true end end
	return false
end

function joypad.get(player)
	checkPlayer(player)
	local out = {}
	for _, n in ipairs(BUTTONS) do out[n] = M.pads[player][n] or false end
	return out
end

function joypad.set(player, tbl)
	checkPlayer(player)
	if type(tbl) ~= "table" then
		error("joypad.set: expected a table of button names, got " .. type(tbl), 2)
	end
	--- ABSENT LEAVES THE BUTTON ALONE. Only named keys move, which is what
	--- lets two scripts drive different buttons without fighting.
	for name, want in pairs(tbl) do
		if not isButton(name) then
			error("unknown button name: " .. tostring(name), 2)
		end
		M.pads[player][name] = want and true or false
	end
end

function joypad.setbutton(player, name, pressed)
	checkPlayer(player)
	if not isButton(name) then error("unknown button name: " .. tostring(name), 2) end
	M.pads[player][name] = pressed and true or false
end

--- Edge detection over the previous frame's snapshot.
local prevPads = { {}, {} }
local function edges(player, wantDown)
	checkPlayer(player)
	local now, before, out = M.pads[player], prevPads[player], {}
	for _, n in ipairs(BUTTONS) do
		local a, b = now[n] or false, before[n] or false
		out[n] = wantDown and (a and not b) or ((not a) and b)
	end
	return out
end
function joypad.getdown(p) return edges(p, true) end
function joypad.getup(p)   return edges(p, false) end

function joypad.getaxis(player, axis)
	checkPlayer(player)
	if type(axis) ~= "number" or axis < 1 or axis > 2 then
		error("axis must be 1..2, got " .. tostring(axis), 2)
	end
	return 0
end
function joypad.setaxis(player, axis, value)
	checkPlayer(player)
	if type(axis) ~= "number" or axis < 1 or axis > 2 then
		error("axis must be 1..2, got " .. tostring(axis), 2)
	end
end

--- memory ---------------------------------------------------------------
function memory.spaces()
	local out = {}
	for name in pairs(SPACES) do out[#out + 1] = name end
	table.sort(out)
	return out
end

function memory.space(name)
	local sp = spaceOf(name)
	return {
		readbyte   = function(a) return rd(sp, a) end,
		writebyte  = function(a, v) wr(sp, a, v) end,
		readword   = function(a)
			local lo, err = rd(sp, a); if lo == nil then return nil, err end
			local hi = rd(sp, a + 1) or 0
			return lo + hi * 256
		end,
		readdword  = function(a)
			local b0, err = rd(sp, a); if b0 == nil then return nil, err end
			return (b0 or 0) + (rd(sp, a+1) or 0) * 0x100
					+ (rd(sp, a+2) or 0) * 0x10000 + (rd(sp, a+3) or 0) * 0x1000000
		end,
		size = sp.size,
		base = sp.base,
	}
end

function memory.readbyte(a)  return rd(SPACES.main, a) end
function memory.readword(a)  return memory.space("main").readword(a) end
function memory.readdword(a) return memory.space("main").readdword(a) end
function memory.writebyte(a, v)  wr(SPACES.main, a, v) end
function memory.writeword(a, v)  wr(SPACES.main, a, v % 256); wr(SPACES.main, a+1, math.floor(v/256) % 256) end
function memory.writedword(a, v)
	for i = 0, 3 do wr(SPACES.main, a + i, math.floor(v / (256 ^ i)) % 256) end
end

function memory.getregister(name)
	if M.regs[name] == nil then error("unknown register: " .. tostring(name), 2) end
	return M.regs[name]
end
function memory.setregister(name, v)
	if M.regs[name] == nil then error("unknown register: " .. tostring(name), 2) end
	M.regs[name] = v
end

--- Watches compare CONTENT, not writes: rewriting the same bytes reports
--- nothing, which is what a script watching a value wants.
local watches, nextWatch = {}, 1
function memory.watch(addr, len)
	if type(len) ~= "number" or len <= 0 or len > 0x10000 then
		error("watch length must be 1..65536, got " .. tostring(len), 2)
	end
	local function snap()
		local t = {}
		for i = 0, len - 1 do t[i] = SPACES.main.store[addr + i] or 0 end
		return t
	end
	local id = nextWatch; nextWatch = nextWatch + 1
	watches[id] = { addr = addr, len = len, last = snap(), polls = 0, changes = 0 }
	local h = {}
	function h:changed()
		local w = watches[id]
		if w == nil then error("watch has been released", 2) end
		w.polls = w.polls + 1
		local now, diff = snap(), false
		for i = 0, len - 1 do if now[i] ~= w.last[i] then diff = true break end end
		w.last = now
		if diff then w.changes = w.changes + 1 end
		return diff
	end
	--- Non-vacuity, in the interface rather than only in the tests: a watch
	--- that has never seen a change is indistinguishable from one watching
	--- the wrong address unless it says how often it looked.
	function h:polls()   return watches[id] and watches[id].polls or 0 end
	function h:changes() return watches[id] and watches[id].changes or 0 end
	function h:release() watches[id] = nil end
	return h
end

--- savestate ------------------------------------------------------------
local slots = {}
local function serialise()
	local parts = { tostring(M.frame), "|" }
	for a = MAIN_BASE, MAIN_BASE + MAIN_SIZE - 1 do
		local v = M.main[a]
		if v ~= nil then parts[#parts + 1] = string.format("%x:%x;", a, v) end
	end
	return table.concat(parts)
end
local function deserialise(s)
	if type(s) ~= "string" then error("state must be a string", 3) end
	local head, body = s:match("^(%d+)|(.*)$")
	if head == nil then return nil, "not a mock savestate" end
	M.frame = tonumber(head)
	M.main = {}
	SPACES.main.store = M.main
	for a, v in body:gmatch("(%x+):(%x+);") do
		M.main[tonumber(a, 16)] = tonumber(v, 16)
	end
	return true
end

local function checkSlot(n)
	if type(n) ~= "number" or n < 1 or n > 9 then
		error("savestate slot must be 1..9, got " .. tostring(n), 3)
	end
end
function savestate.save(slot) checkSlot(slot); slots[slot] = serialise(); return true end
function savestate.load(slot)
	checkSlot(slot)
	if slots[slot] == nil then return nil, "slot " .. slot .. " is empty" end
	return deserialise(slots[slot])
end
function savestate.save_mem() return serialise() end
function savestate.load_mem(s) return deserialise(s) end
function savestate.hash(s)
	s = s or serialise()
	--- djb2, because a hash that is stable within a run is all this needs.
	local h = 5381
	for i = 1, #s do h = (h * 33 + s:byte(i)) % 0x100000000 end
	return string.format("%08x", h)
end

--- gui - game pixels, legal only inside a draw callback -----------------
local drawn = {}
local function mustBeDrawing(fn)
	if not M.drawing then
		error(fn .. " is only legal inside a gui.register callback", 3)
	end
end
function gui.rgba(r, g, b, a)
	if a == nil then a = 255 end
	local function chan(v, n)
		if type(v) ~= "number" or v < 0 or v > 255 then
			error("gui.rgba: " .. n .. " must be 0-255, got " .. tostring(v), 3)
		end
		return math.floor(v)
	end
	r, g, b, a = chan(r, "r"), chan(g, "g"), chan(b, "b"), chan(a, "a")
	--- 0xAABBGGRR, the interface's packing. See spec.lua.
	return a * 0x1000000 + b * 0x10000 + g * 0x100 + r
end
function gui.text(x, y, s)  mustBeDrawing("gui.text"); drawn[#drawn+1] = {"text", x, y, s} end
function gui.box(x, y, x2, y2, c)  mustBeDrawing("gui.box");  drawn[#drawn+1] = {"box", x, y, x2, y2, c} end
function gui.boxfill(x, y, x2, y2, f, b) mustBeDrawing("gui.boxfill"); drawn[#drawn+1] = {"boxfill"} end
function gui.line(x, y, x2, y2, c) mustBeDrawing("gui.line"); drawn[#drawn+1] = {"line"} end
function gui.scale() return 1 end

--- ui - widgets, same gate ---------------------------------------------
local UI_NAMES = {
	"Begin", "End", "Text", "TextColored", "Button", "SameLine", "Checkbox",
	"Selectable", "SliderFloat", "SliderInt", "InputText", "Separator",
	"Spacing", "SetNextWindowPos", "SetNextWindowSize", "GetMousePos",
	"IsMouseClicked", "IsMouseDown", "IsMouseReleased", "GetScale",
}
for _, n in ipairs(UI_NAMES) do
	ui[n] = function(...)
		mustBeDrawing("ui." .. n)
		if n == "Begin" then return true end
		if n == "Checkbox" or n == "Selectable" then
			local _, v = ...
			return v and true or false, false
		end
		if n == "SliderFloat" or n == "SliderInt" or n == "InputText" then
			local _, v = ...
			return v, false
		end
		if n == "GetMousePos" then return 0, 0 end
		if n == "GetScale" then return 1 end
		if n:sub(1, 7) == "IsMouse" then return false end
		return nil
	end
end

--- movie / sound --------------------------------------------------------
function movie.mode()       return M.movieMode end
function movie.framecount() return M.movieFrames end
function movie.record(path) M.movieMode = "record"; return true end
function movie.stop()       M.movieMode = nil end
function sound.voicecount() return 8 end
function sound.outputrate() return 44100 end

--- The driver the host would normally be --------------------------------
--- A real host dispatches these; here the test runner does, which is the
--- point: the callback contract can be exercised with no emulator at all.
local hooks = { before = {}, after = {}, exit = {}, draw = {} }

--- Registration returns a handle carrying delivery counts. Implemented here
--- as well as in flycast's adapter, because a contract implemented once is a
--- description of one implementation.
local function registrar(list, what)
	return function(fn)
		if type(fn) ~= "function" then
			error(what .. ": expected a function, got " .. type(fn), 2)
		end
		local rec = { fn = fn, hits = 0, faults = 0, dead = false }
		list[#list + 1] = rec
		return {
			hits       = function() return rec.hits end,
			faults     = function() return rec.faults end,
			unregister = function() rec.dead = true end,
		}
	end
end
emu.registerbefore = registrar(hooks.before, "emu.registerbefore")
emu.registerafter  = registrar(hooks.after,  "emu.registerafter")
emu.registerexit   = registrar(hooks.exit,   "emu.registerexit")
gui.register       = registrar(hooks.draw,   "gui.register")

local function dispatch(list)
	local sweep = false
	for i = 1, #list do
		local rec = list[i]
		if rec ~= nil and not rec.dead then
			rec.hits = rec.hits + 1
			local ok, err = pcall(rec.fn)
			if not ok then
				rec.faults = rec.faults + 1
				--- BOUNDED, AND IT SAYS SO. A subscriber that throws every
				--- frame produced one line per frame - 400 lines of identical
				--- text burying the report underneath it. An error stream with
				--- no ceiling is its own denial of service.
				---
				--- The count is not bounded, only the printing, so nothing is
				--- lost: handle:faults() still has the real number.
				if rec.faults <= 3 then
					print("mock: callback error: " .. tostring(err))
					if rec.faults == 3 then
						print("mock: further errors from this subscriber are"
							.. " counted but not printed - see handle:faults()")
					end
				end
			end
		end
		if rec ~= nil and rec.dead then sweep = true end
	end
	if sweep then
		local keep = {}
		for _, rec in ipairs(list) do if not rec.dead then keep[#keep+1] = rec end end
		for i = #list, 1, -1 do list[i] = nil end
		for i, rec in ipairs(keep) do list[i] = rec end
	end
end

--- Frame stepping, the same coroutine shape the flycast adapter uses.
local stepper = nil
function emu.run(body)
	if type(body) ~= "function" then
		error("emu.run: expected a function, got " .. type(body), 2)
	end
	stepper = coroutine.create(body)
end
function emu.frameadvance()
	if coroutine.isyieldable and coroutine.isyieldable() then
		coroutine.yield()
	elseif coroutine.running() then
		coroutine.yield()
	else
		error("emu.frameadvance() is only legal inside emu.run()", 2)
	end
end

--- Advance one frame: snapshot input edges, run the hooks, then draw.
function M.step()
	for p = 1, #M.pads do
		local copy = {}
		for k, v in pairs(M.pads[p]) do copy[k] = v end
		prevPads[p] = copy
	end
	M.frame = M.frame + 1
	M.confirmed = M.confirmed + 1
	--- Something changes in RAM every frame, so a watch over it is not vacuous.
	M.main[MAIN_BASE] = M.frame % 256
	M.regs.pc = 0x100 + (M.frame % 0x40)

	dispatch(hooks.before)
	if stepper ~= nil and coroutine.status(stepper) ~= "dead" then
		local ok, err = coroutine.resume(stepper)
		if not ok then print("mock: emu.run body error: " .. tostring(err)); stepper = nil end
	end
	dispatch(hooks.after)

	M.drawing = true
	drawn = {}
	dispatch(hooks.draw)
	M.drawing = false
end

function M.finish() dispatch(hooks.exit) end
function M.drawCount() return #drawn end

return {
	namespaces = {
		emu = emu, frame = frame, joypad = joypad, memory = memory,
		savestate = savestate, gui = gui, ui = ui, movie = movie, sound = sound,
	},
	--- Where the suite may safely poke. Addresses are per-system by design, so
	--- a portable suite cannot hardcode one - it asks the adapter.
	probe = {
		readable  = MAIN_BASE,
		writable  = MAIN_BASE + 0x10,
		unmapped  = MAIN_BASE + MAIN_SIZE + 0x100,
		space     = "sound",
	},
	unsupported = {
		["memory.registerwrite"] = true,
		["memory.registerexec"]  = true,
		["ui.Image"]             = true,
	},
	--- The driver, so a runner can advance this machine without an emulator.
	driver = M,
}
