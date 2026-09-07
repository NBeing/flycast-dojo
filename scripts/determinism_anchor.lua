--- determinism_anchor.lua - prove a replay reproduces, instead of assuming it.
---
--- THE IDEA
---
--- A movie is a recipe, not a video: it stores "press Punch on frame 107" and
--- replays by re-running the game. Whether that reproduces the original is
--- normally discovered by a human watching a combo drop 3000 frames in.
---
--- A savestate is a photograph of the machine. So: photograph the machine at
--- frame N, replay the movie from power-on, photograph again at frame N, and
--- compare. Equal means frames 0..N reproduced EXACTLY - proven, not hoped.
---
--- With several anchors the first mismatch BISECTS the movie and names the
--- stretch where determinism broke. And because a TAS clip already carries up
--- to 100 savestates, an existing combo library is already a corpus of
--- assertion points: this runs over work that already exists, with nothing new
--- authored.
---
--- WHY LUA AND NOT C++
---
--- Everything here goes through emuapi, so it is a conformance test for ANY
--- host that implements the interface, not a flycast feature. A second
--- emulator claiming to be deterministic can be held to the same file.
---
--- Run:  require("emuapi"); dofile("scripts/determinism_anchor.lua")
---       then drive it from the Lua console:  anchor.record(600) / anchor.check(600)

local api       = require("emuapi").load()
local emu       = api.emu
local frame     = api.frame
local savestate = api.savestate

local M = {}
_G.anchor = M

-- Anchors live beside the script, keyed by movie identity so two clips do not
-- collide. Falls back to the rom name where the host exposes no movie id.
local anchors = {}

local function movieId()
	return (emu.romname and emu.romname()) or "unknown"
end

local function requireHash()
	if not emu.supports("savestate.hash") then
		error("host cannot fingerprint state: savestate.hash is unsupported.\n"
			.. "This is the Tier 3 primitive - without it determinism cannot be "
			.. "tested, only asserted.", 2)
	end
end

--- Record the machine's fingerprint at the current frame.
---
--- Call this while the movie is at a known-good point - typically right after
--- saving the state you use as the seek target.
function M.record(atFrame)
	requireHash()
	atFrame = atFrame or frame.count()

	local key = movieId()
	anchors[key] = anchors[key] or {}
	anchors[key][atFrame] = savestate.hash()

	emu.message(string.format("anchor: frame %d -> %s", atFrame, anchors[key][atFrame]))
	return anchors[key][atFrame]
end

--- Compare the live machine against a recorded anchor.
---
--- Returns true / false / nil (no anchor for that frame). A false is the
--- interesting case: frames 0..atFrame did NOT reproduce.
function M.check(atFrame)
	requireHash()
	atFrame = atFrame or frame.count()

	local key = movieId()
	local expected = anchors[key] and anchors[key][atFrame]
	if not expected then
		emu.message(string.format("anchor: nothing recorded for frame %d", atFrame))
		return nil
	end

	local actual = savestate.hash()
	local ok = (actual == expected)
	emu.message(string.format("anchor frame %d: %s", atFrame, ok and "MATCH" or "DIVERGED"))
	if not ok then
		print(string.format(
			"DETERMINISM FAILURE at frame %d\n  expected %s\n  actual   %s\n"
			.. "  Frames 0..%d did not reproduce. Check the anchor below this one "
			.. "to narrow the range.", atFrame, expected, actual, atFrame))
	end
	return ok
end

--- Bisect: given anchors at several frames, report the FIRST that diverges.
---
--- This is the payoff. A single mismatch says "something broke". An ordered set
--- says "it broke between frame 600 and frame 900", which is a debuggable
--- statement. Requires the caller to seek the movie to each frame in turn -
--- `seek` is a callback so this file stays host-agnostic.
function M.bisect(frames, seek)
	requireHash()
	table.sort(frames)
	for _, f in ipairs(frames) do
		seek(f)
		local ok = M.check(f)
		if ok == false then
			print(string.format("FIRST DIVERGENCE at anchor %d", f))
			return f
		end
	end
	print("all anchors matched: the movie reproduces end to end")
	return nil
end

--- Settle a "does this setting affect sync?" question empirically.
---
--- The honest way to classify the options in determinism.cpp's
--- needsMeasurement() list: run the same stretch twice with the option flipped
--- and see whether the machine ends up in the same place. Reasoning about
--- whether rend.DupeFrames is host-side pacing is guesswork; this is not.
function M.compareRuns(nframes, advance)
	requireHash()
	local a = savestate.hash()
	for _ = 1, nframes do advance() end
	local after1 = savestate.hash()

	print(("start %s -> after %d frames %s"):format(a, nframes, after1))
	print("now flip the setting under test, restore the same state, and re-run.")
	print("Same start hash + different end hash = the setting is sync-critical.")
	return after1
end

print("determinism_anchor loaded. anchor.record(f) / anchor.check(f) / anchor.bisect(t, seek)")
return M
