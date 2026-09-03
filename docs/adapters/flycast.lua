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

--- emu ------------------------------------------------------------------
function emu.pause()          host.emulator.pause() end
function emu.unpause()        host.emulator.resume() end
function emu.exit()           host.emulator.exit() end
function emu.message(s)       host.emulator.displayNotification(tostring(s), 3000) end
function emu.romname()        return host.state.gameId end
function emu.screenwidth()    return host.state.display.width end
function emu.screenheight()   return host.state.display.height end
function emu.isrollback()     return host.state.isRollback() end

--- frame ----------------------------------------------------------------
function frame.count()        return host.state.getFrameNumber() end
function frame.confirmed()    return host.state.getConfirmedFrameNumber() end

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
function memory.readbyte(a)      return host.memory.read8(a) end
function memory.readword(a)      return host.memory.read16(a) end
function memory.readdword(a)     return host.memory.read32(a) end
function memory.writebyte(a, v)  host.memory.write8(a, v) end
function memory.writeword(a, v)  host.memory.write16(a, v) end
function memory.writedword(a, v) host.memory.write32(a, v) end

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

--- gui ------------------------------------------------------------------
--- Legal only inside a gui.register callback - the host raises otherwise,
--- because these run on the render thread and the frame callbacks do not.
---
--- The interface draws immediate primitives at game-pixel coordinates; the
--- host draws ImGui flow layout inside a window. Each primitive therefore
--- opens a borderless window at the requested point. Workable, but a native
--- implementation on a draw list would be cheaper - the case for moving this
--- one to a native binding later.
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

function gui.text(x, y, s)
	inWindow(x, y, nil, nil, function() host.ui.text(tostring(s)) end)
end

function gui.box(x, y, x2, y2, colour)
	host.ui.rect(x, y, x2 - x, y2 - y, colour or 0xFFFFFFFF)
end

function gui.line(x, y, x2, y2, colour)
	host.ui.line(x, y, x2, y2, colour or 0xFFFFFFFF)
end

--- movie ----------------------------------------------------------------
function movie.record(path) return host.replay.startRecording(path or "") end
function movie.stop()       host.replay.stopRecording() end
function movie.mode()
	if host.replay.isRecording() then return "record" end
	return nil
end

--- callbacks ------------------------------------------------------------
--- The host dispatches through a well-known table; the interface uses
--- registration functions, which allow more than one subscriber.
local drawCallbacks, frameCallbacks, exitCallbacks = {}, {}, {}

function gui.register(fn)        drawCallbacks[#drawCallbacks + 1] = fn end
function emu.registerafter(fn)   frameCallbacks[#frameCallbacks + 1] = fn end
function emu.registerexit(fn)    exitCallbacks[#exitCallbacks + 1] = fn end

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
	runAll(frameCallbacks)
end

return {
	namespaces = {
		emu = emu, frame = frame, joypad = joypad, memory = memory,
		savestate = savestate, gui = gui, movie = movie,
	},
	--- Present in the interface, deliberately not implemented here. Declared so
	--- emu.supports() answers false rather than the name simply being absent.
	unsupported = {
		["memory.registerwrite"] = true,   -- SH4 dynarec inlines memory access
		["memory.registerexec"]  = true,   -- breakpoints patch guest memory; desyncs
		["emu.frameadvance"]     = true,   -- no host primitive yet
		["sound.voicecount"]     = true,
	},
}
