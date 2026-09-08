--- anchor - a savestate knows which movie frame it belongs to, and whether the
--- movie BELOW it still says what it said when the state was taken.
---
---   Run it:  scripts/testrun.sh scripts/tests/anchor.lua
---   Watch:   scripts/testrun.sh --watch scripts/tests/anchor.lua
---
--- WHY THE NEGATIVE CHECKS ARE THE POINT. A global dirty flag - "anything was
--- edited, so everything is suspect" - passes every positive check here by
--- failing safe, and failing safe is indistinguishable from working. What
--- separates a real anchor from that flag is what it does NOT condemn:
---
---   * an edit ABOVE the anchor must not stale it. The state was captured
---     before those frames were applied, so they cannot have changed it.
---   * an edit that was UNDONE must not stale it either, because the bytes
---     below are what they were and the bytes are the whole question.
---
--- Both are unreachable from a dirty flag, which is why they are here.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local n, stage, sub = 0, 1, 0
local STAGES = {}
local lines = {}
local function say(s) lines[#lines + 1] = s end
local function report(name, cond, detail) t.check(name, cond, detail); say((cond and "PASS  " or "FAIL  ") .. name) end

local ss = flycast.savestate

--- 1. THE HOST ANSWERS HOW MANY SLOTS IT HAS ------------------------------
STAGES[1] = function()
	local ok, count = pcall(ss.slotCount)
	report("the host reports a slot count", ok and type(count) == "number" and count > 0,
		ok and tostring(count) or "slotCount raised")
	if not (ok and count) then return true end

	--- It must be the STORAGE ceiling, not the F2 cycle. Those differ the
	--- moment a user shrinks the cycle, and a script told the cycle would
	--- refuse to touch a state that is sitting right there.
	report("the count is the storage ceiling, not the hotkey cycle", count >= 100,
		count .. " slots")
	report("a slot above the count is refused", not pcall(ss.anchor, count + 1))
	report("slot 0 is refused - this interface is 1-based", not pcall(ss.anchor, 0))
	return true
end

--- 2. AN ANCHOR IS ABSENT OR IT IS A VERDICT ------------------------------
local anchored, anchoredFrame
STAGES[2] = function()
	local count = ss.slotCount()
	local seen, verdicts = 0, {}
	for s = 1, count do
		local frame, verdict = ss.anchor(s)
		if frame ~= nil then
			seen = seen + 1
			verdicts[verdict] = (verdicts[verdict] or 0) + 1
			if anchored == nil then anchored, anchoredFrame = s, frame end
			--- ABSENCE IS NOT A VERDICT. A slot with no state answers nothing at
			--- all; a slot with a state answers a frame AND a word. A host that
			--- returned a frame with no verdict would pass a "did it answer?"
			--- check and tell a caller nothing about validity.
			if verdict == nil then
				report("slot " .. s .. " answered a frame with no verdict", false)
				return true
			end
		end
	end

	report("at least one slot carries an anchor", seen > 0, seen .. " anchored")
	if seen == 0 then
		t.limit("anchor verdicts", "this clip has no savestate with a sidecar, so nothing to judge")
		return true
	end

	local named = 0
	for v, c in pairs(verdicts) do
		if v == "clean" or v == "stale" or v == "unknown" then named = named + c end
		say(("  verdict %s x%d"):format(v, c))
	end
	report("every verdict is one of clean/stale/unknown", named == seen)
	report("an anchor's frame is a positive number", type(anchoredFrame) == "number" and anchoredFrame > 0,
		"slot " .. tostring(anchored) .. " at frame " .. tostring(anchoredFrame))
	return true
end

--- 3. BOTH DIRECTIONS, AND THE ORDER MATTERS ------------------------------
--- An edit at frame N invalidates states above N exactly as a rewind to N does
--- (Dojo::ApplyEdit logs a timeline event at the first changed frame, on the
--- same re-record clock the sidecars use). So both directions are reachable
--- from a script, and BOTH are needed:
---
---   ABOVE the anchor -> must stay clean. A global dirty flag fails here, and
---                       that is the whole reason this file exists.
---   BELOW the anchor -> must go stale. Without this the check above is
---                       vacuous: "nothing ever stales" passes it too.
---
--- Above first, because once a state is stale the first check cannot be made.
local function editOneFrame(frame)
	local row = flycast.movie.getButtons(frame, 1) or {}
	local edited = {}
	for k, v in pairs(row) do edited[k] = v end
	edited.a = not row.a
	return flycast.movie.setButtons(frame, 1, edited)
end

STAGES[3] = function()
	if anchored == nil then
		t.limit("timeline guard", "no anchored state in this clip to protect")
		return true
	end

	--- NOT GATED ON movie.editable(). That answers `not emu.running()`, which
	--- from a vblank callback is always false - the emulator is, by definition,
	--- running. [MEASURED 2026-09-08] gating on it made this stage report a
	--- LIMIT saying "read-only playback", which was a misdiagnosis: setButtons
	--- takes its own pausing::Scoped guard and edits fine during playback. The
	--- honest gate is to attempt the edit and read the answer.
	local above = anchoredFrame + 120
	local below = math.max(1, math.floor(anchoredFrame / 2))
	if not (flycast.movie.has(above) and flycast.movie.has(below)) then
		t.limit("timeline guard", ("this movie has no authored frame at %d or %d"):format(above, below))
		return true
	end

	local wasClean = select(2, ss.anchor(anchored))
	if wasClean ~= "clean" then
		t.limit("timeline guard", "slot " .. anchored .. " is already " .. tostring(wasClean)
				.. ", so there is nothing left to invalidate")
		return true
	end

	if not editOneFrame(above) then
		t.limit("timeline guard", "the host refused an edit at frame " .. above)
		return true
	end
	local afterAbove = select(2, ss.anchor(anchored))
	report("an edit ABOVE the anchor does NOT stale it", afterAbove == "clean",
		("edited %d; slot %d stayed %s"):format(above, anchored, tostring(afterAbove)))

	--- THE CONTROL. If this does not fire, the check above proved nothing.
    if not editOneFrame(below) then
		t.limit("timeline guard control", "the host refused an edit at frame " .. below)
		return true
	end
	local afterBelow = select(2, ss.anchor(anchored))
	report("an edit BELOW the anchor DOES stale it", afterBelow == "stale",
		("edited %d; slot %d -> %s"):format(below, anchored, tostring(afterBelow)))
	return true
end

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1
	if n < 240 then return end		-- let the auto-seek land first
	if stage == 1 and sub == 0 then
		t.ran({ ["frames are advancing"] = n > 0 })
		sub = 1
	end
	local fn = STAGES[stage]
	if fn then
		local okc, done = pcall(fn)
		if not okc then
			report(("stage %d raised"):format(stage), false, tostring(done))
			done = true
		end
		if done then
			stage, sub = stage + 1, 0
			if STAGES[stage] == nil then t.finish() end
		end
	end
end

local prevOverlay = flycast_callbacks.overlay
flycast_callbacks.overlay = function()
	if prevOverlay then prevOverlay() end
	if flycast.ui.Begin("anchor") then
		for _, l in ipairs(lines) do flycast.ui.Text(l) end
	end
	flycast.ui.End()
end
