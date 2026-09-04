--- flycast-dojo adapter for the cross-emulator Lua interface.
---
--- Maps flycast.* onto the neutral surface. Everything the host already does
--- correctly is a thin forward; the interesting parts are the three mismatches
--- this file absorbs so scripts never see them:
---
---   * savestate slots are 0-based in the host and 1-based in the interface
---   * gui.* is an immediate x/y primitive, ImGui flow layout underneath
---   * joypad.getdown/getup do not exist in the host and are synthesised here
---
--- Verified against flycast-dojo (branch video-recording).

local host = assert(rawget(_G, "flycast"), "flycast adapter loaded on a non-flycast host")

local emu        = {}
local frame      = {}
local joypad     = {}
local memory     = {}
local savestate  = {}
local gui        = {}
local movie      = {}
local sound      = {}
local ui         = {}

--- emu ------------------------------------------------------------------
function emu.pause()          host.emulator.pause() end
function emu.unpause()        host.emulator.resume() end
function emu.exit()           host.emulator.exit() end
function emu.message(s)       host.emulator.displayNotification(tostring(s), 3000) end
function emu.romname()        return host.state.gameId end
function emu.screenwidth()    return host.state.display.width end
function emu.screenheight()   return host.state.display.height end
function emu.isrollback()     return host.state.isRollback() end
function emu.isonline()       return host.emulator.isOnline() end
function emu.isreplay()       return host.emulator.isReplay() end

--- The host has a fast-forward boolean rather than named modes, so "normal"
--- is off and anything else is on. A host with genuine speed tiers should map
--- them properly.
function emu.speedmode(mode)
	host.emulator.setSpeedMode(mode ~= nil and mode ~= "normal")
end

--- The host knows a game id but not a separate display name; reporting the id
--- is better than reporting nothing, and it is what a script keys on anyway.
function emu.gamename()       return host.state.gameId end

--- frame ----------------------------------------------------------------
function frame.count()        return host.state.getFrameNumber() end
function frame.confirmed()    return host.state.getConfirmedFrameNumber() end
function frame.resimsteps()   return host.state.getResimSteps() end

--- joypad ---------------------------------------------------------------
function joypad.buttons()             return host.input.buttonNames() end
function joypad.get(player)           return host.input.getButtonTable(player) end
function joypad.set(player, buttons)  host.input.setButtonTable(player, buttons) end
function joypad.setbutton(p, n, held) host.input.setButton(p, n, held) end
function joypad.getaxis(p, a)         return host.input.getAxis(p, a) end
function joypad.setaxis(p, a, v)      host.input.setAxis(p, a, v) end

--- Edge detection the host does not provide, synthesised from get().
---
--- Two snapshots taken at frame boundaries, not one: comparing live state
--- against a baseline updated *after* the callbacks run means a press made by
--- a script is already in the baseline by the time anything can observe it, so
--- the edge is never seen. Sampling both at the frame boundary also keeps
--- getdown stable for the whole frame, however many callbacks read it.
---
--- Rollback-safe for free: the host does not deliver frame callbacks for
--- re-simulated frames, so these only advance on confirmed frames. An adapter
--- over an emulator WITHOUT that guarantee would have to gate on
--- frame.confirmed() itself.
local thisFrame, lastFrame = {}, {}

local function edges(player, wantHeld)
	local now = thisFrame[player] or {}
	local was = lastFrame[player] or {}
	local out = {}
	for name, held in pairs(now) do
		out[name] = (held == wantHeld) and (was[name] ~= wantHeld)
	end
	return out
end

function joypad.getdown(player) return edges(player, true) end
function joypad.getup(player)   return edges(player, false) end

--- Runs at the frame boundary, before user callbacks.
local function snapshotInputs()
	lastFrame = thisFrame
	thisFrame = {}
	for player = 1, 4 do
		local ok, t = pcall(host.input.getButtonTable, player)
		if ok then thisFrame[player] = t end
	end
end

--- memory ---------------------------------------------------------------
--- Two named spaces. "main" is the SH4's own virtual address space, passed
--- through unchanged. "sound" is the AICA/ARM7 view, which on this hardware is
--- a window in the SH4 space at 0x00800000 - so the adapter presents it with
--- its own 0-based addresses and applies the offset. A script asking for
--- sound-CPU address 0x100 does not need to know that.
---
--- Verified: writes at SH4 0x00800400 read back through the same window.
local ARAM_WINDOW = 0x00800000
local ARAM_SIZE   = 0x00200000		-- 2 MB on Dreamcast

local function makeSpace(translate)
	local sp = {}
	function sp.readbyte(a)      return host.memory.read8(translate(a)) end
	function sp.readword(a)      return host.memory.read16(translate(a)) end
	function sp.readdword(a)     return host.memory.read32(translate(a)) end
	function sp.writebyte(a, v)  host.memory.write8(translate(a), v) end
	function sp.writeword(a, v)  host.memory.write16(translate(a), v) end
	function sp.writedword(a, v) host.memory.write32(translate(a), v) end
	return sp
end

local spaces = {
	main  = makeSpace(function(a) return a end),
	sound = makeSpace(function(a)
		if type(a) ~= "number" or a < 0 or a >= ARAM_SIZE then
			error(string.format("sound address out of range (0..0x%X)", ARAM_SIZE - 1), 3)
		end
		return ARAM_WINDOW + a
	end),
}

--- Watches. The host answers "did this range change since you last asked", so
--- a poll each frame is the intended use. Returned as an object because a bare
--- integer handle invites passing the wrong one.
local Watch = {}
Watch.__index = Watch

function Watch:changed()  return host.memory.watchChanged(self.id) end
function Watch:release()
	if self.id ~= nil then
		host.memory.watchRelease(self.id)
		self.id = nil
	end
end

function memory.watch(addr, len)
	return setmetatable({ id = host.memory.watchCreate(addr, len or 1) }, Watch)
end

function memory.unwatch(w)
	if type(w) == "table" and w.release then w:release()
	else host.memory.watchRelease(w) end
end

function memory.getregister(name)      return host.memory.getRegister(name) end
function memory.setregister(name, v)  host.memory.setRegister(name, v) end

function memory.spaces() return { "main", "sound" } end

function memory.space(name)
	local sp = spaces[name]
	if sp == nil then
		error("unknown address space '" .. tostring(name)
			.. "'; see memory.spaces()", 2)
	end
	return sp
end

--- Unqualified access is the main space.
memory.readbyte      = spaces.main.readbyte
memory.readword      = spaces.main.readword
memory.readdword     = spaces.main.readdword
memory.writebyte     = spaces.main.writebyte
memory.writeword     = spaces.main.writeword
memory.writedword    = spaces.main.writedword

--- savestate ------------------------------------------------------------
--- The interface is 1-based throughout; the host's slots are 0-based. The
--- shift lives here, which is the whole point of an adapter.
local SLOTS = 10

local function slotToHost(slot)
	if type(slot) ~= "number" or slot < 1 or slot > SLOTS then
		error("savestate slot must be between 1 and " .. SLOTS, 3)
	end
	return slot - 1
end

function savestate.save(slot) host.emulator.saveState(slotToHost(slot)) end
function savestate.load(slot) host.emulator.loadState(slotToHost(slot)) end

--- States as strings, and a cheap identity for them. hash() takes an optional
--- state; with no argument it hashes the live machine, so two peers can
--- compare per frame and find the first one where they diverge.
function savestate.save_mem()  return host.emulator.saveStateString() end
function savestate.load_mem(s) host.emulator.loadStateString(s) end
function savestate.hash(s)     return host.emulator.hashState(s) end

--- ui - the ImGui baseline profile ---------------------------------------
--- Forwarded under ImGui's own names, which the host already uses. Legal only
--- inside a gui.register callback; the host raises otherwise.
for _, name in ipairs({
	"Begin", "End", "Text", "TextColored", "Button", "SameLine",
	"Checkbox", "Selectable", "SliderFloat", "SliderInt", "InputText",
	"Separator", "Spacing", "SetNextWindowPos", "SetNextWindowSize",
	"GetMousePos", "IsMouseClicked", "IsMouseDown", "IsMouseReleased",
	"GetScale",
}) do
	ui[name] = host.ui[name]
end

--- gui - content overlay, in GAME pixels ---------------------------------
--- Legal only inside a gui.register callback - the host raises otherwise,
--- because these run on the render thread and the frame callbacks do not.
---
--- Coordinates are game pixels with the origin at the top-left of the game
--- image, so a mark stays on the thing it marks when the window is resized or
--- letterboxed. The host reports the image's rectangle and the logical game
--- resolution; the mapping is here.
local function mapper()
	local vx, vy, vw, vh = host.state.getGameViewport()
	local gw, gh = host.state.getGameResolution()
	local sx, sy = vw / gw, vh / gh
	return function(x, y) return vx + x * sx, vy + y * sy end, sx, sy
end

--- Game-pixel size of one screen pixel, for callers that need to scale a
--- thickness or a font.
function gui.scale()
	local _, sx, sy = mapper()
	return sx, sy
end
--- Reset every frame, so the Nth draw call each frame keeps a stable ImGui id.
--- Letting it grow monotonically mints a new window per frame and leaks ImGui
--- state for thousands of windows.
local drawSeq = 0

local function inWindow(x, y, w, h, body)
	drawSeq = drawSeq + 1
	host.ui.beginWindow("##emuapi" .. drawSeq, x, y, w or 220, h or 44)
	body()
	host.ui.endWindow()
end

--- THE CONSTRUCTOR EXISTS SO NO PORTABLE SCRIPT WRITES A PACKED LITERAL.
--- Packing is 0xAABBGGRR here and 0xRRGGBBAA on nbneo-rr, and the two agree on
--- exactly one colour a person is likely to test with: opaque red is
--- 0xFF0000FF under BOTH. Greys agree too. So a script that hardcodes numbers
--- passes its first test on either host and renders wrong on one of them the
--- moment it uses green or blue. Channels in, packing hidden.
function gui.rgba(r, g, b, a)
	if a == nil then a = 255 end
	local function chan(v, name)
		if type(v) ~= "number" or v < 0 or v > 255 then
			error("gui.rgba: " .. name .. " must be 0-255, got " .. tostring(v), 2)
		end
		return math.floor(v)
	end
	r, g, b, a = chan(r, "r"), chan(g, "g"), chan(b, "b"), chan(a, "a")
	return a * 0x1000000 + b * 0x10000 + g * 0x100 + r
end

function gui.text(x, y, s)
	local to = mapper()
	local px, py = to(x, y)
	inWindow(px, py, nil, nil, function() host.ui.text(tostring(s)) end)
end

--- Outline only. gui.boxfill is the filled variant.
function gui.box(x, y, x2, y2, colour)
	local to = mapper()
	local px, py = to(x, y)
	local qx, qy = to(x2, y2)
	--- The host's rect takes fill and border separately; gui.box is an
	--- outline, so the fill is fully transparent.
	host.ui.rect(px, py, qx - px, qy - py, 0x00000000, colour or 0xFFFFFFFF)
end

function gui.boxfill(x, y, x2, y2, fill, border)
	local to = mapper()
	local px, py = to(x, y)
	local qx, qy = to(x2, y2)
	host.ui.rect(px, py, qx - px, qy - py, fill or 0x80FFFFFF, border or 0x00000000)
end

function gui.line(x, y, x2, y2, colour)
	local to = mapper()
	local px, py = to(x, y)
	local qx, qy = to(x2, y2)
	host.ui.line(px, py, qx, qy, colour or 0xFFFFFFFF)
end

--- movie ----------------------------------------------------------------
function movie.record(path) return host.replay.startRecording(path or "") end
function movie.stop()       host.replay.stopRecording() end
function movie.mode()
	if host.replay.isRecording() then return "record" end
	if host.emulator.isReplay() then return "playback" end
	return nil
end

function movie.framecount() return host.emulator.getReplayFrameCount() end

--- sound -----------------------------------------------------------------
function sound.voicecount() return host.emulator.getVoiceCount() end
function sound.outputrate() return host.emulator.getOutputRate() end

--- callbacks ------------------------------------------------------------
--- The host dispatches through a well-known table; the interface uses
--- registration functions, which allow more than one subscriber.
local drawCallbacks, frameCallbacks, beforeCallbacks, exitCallbacks = {}, {}, {}, {}

function gui.register(fn)        drawCallbacks[#drawCallbacks + 1] = fn end
function emu.registerafter(fn)   frameCallbacks[#frameCallbacks + 1] = fn end
function emu.registerexit(fn)    exitCallbacks[#exitCallbacks + 1] = fn end

--- registerbefore and registerafter share a hook here, and that is correct
--- rather than a shortcut. The host dispatches at the frame boundary, and at a
--- boundary "after frame N" and "before frame N+1" are the same instant: there
--- is no input latch in between, so input set from either lands on the coming
--- frame. What the two names buy on this host is ORDER - before-callbacks run
--- first, so a script that injects input runs ahead of one that reads state.
---
--- A host with a genuinely separate pre-simulation hook should use it instead;
--- the ordering guarantee is the same either way.
function emu.registerbefore(fn)  beforeCallbacks[#beforeCallbacks + 1] = fn end

--- Frame stepping.
---
--- emu.frameadvance() suspends until the next confirmed frame. It is only
--- legal inside a body started by emu.run(), which drives it one resume per
--- frame - so a script reads as a straight line while still advancing exactly
--- one frame at a time:
---
---     emu.run(function()
---         while true do
---             joypad.set(1, { a = true })
---             emu.frameadvance()
---             joypad.set(1, { a = false })
---             emu.frameadvance()
---         end
---     end)
---
--- Rollback-safe for free: the resume happens on the host's frame callback,
--- which is not delivered for re-simulated frames, so one advance is one
--- confirmed frame.
local stepper = nil

function emu.run(body)
	if type(body) ~= "function" then
		error("emu.run expects a function", 2)
	end
	stepper = coroutine.create(body)
end

function emu.frameadvance()
	if coroutine.isyieldable() then
		coroutine.yield()
	else
		error("emu.frameadvance() is only legal inside emu.run()", 2)
	end
end

local function stepOnce()
	if stepper == nil then return end
	if coroutine.status(stepper) == "dead" then stepper = nil return end
	local ok, err = coroutine.resume(stepper)
	if not ok then
		print("emuapi: emu.run body error: " .. tostring(err))
		stepper = nil
	end
end

local function runAll(list)
	for _, fn in ipairs(list) do
		local ok, err = pcall(fn)
		if not ok then print("emuapi callback error: " .. tostring(err)) end
	end
end

rawset(_G, "flycast_callbacks", rawget(_G, "flycast_callbacks") or {})
_G.flycast_callbacks.overlay   = function()
	drawSeq = 0
	runAll(drawCallbacks)
end
_G.flycast_callbacks.terminate = function() runAll(exitCallbacks) end
--- Host-gated to confirmed frames, so this is rollback-safe.
_G.flycast_callbacks.vblank    = function()
	snapshotInputs()
	runAll(beforeCallbacks)
	stepOnce()
	runAll(frameCallbacks)
end

return {
	namespaces = {
		emu = emu, frame = frame, joypad = joypad, memory = memory,
		savestate = savestate, gui = gui, ui = ui, movie = movie, sound = sound,
	},
	--- Present in the interface, deliberately not implemented here. Declared so
	--- emu.supports() answers false rather than the name simply being absent.
	unsupported = {
		["memory.registerwrite"] = true,   -- SH4 dynarec inlines memory access
		["memory.registerexec"]  = true,   -- breakpoints patch guest memory; desyncs
		["ui.Image"]             = true,   -- needs host texture management
	},
}
