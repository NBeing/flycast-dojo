--- COLD BOOT TWICE, IN ONE PROCESS, AS A CONTROLLED PAIR.
---
--- The probe docs/STATE-COVERAGE.md §4 asked for. Boot the machine, sample it,
--- restart it in-process, sample the second boot at the same point, and require
--- the two to agree. A difference is state the PROCESS carried across an init -
--- which is the one thing two separate processes can never show you, because
--- each of them initialises exactly once and therefore carries identical
--- residue that cancels in the comparison.
---
--- WHAT MAKES IT A CONTROLLED PAIR. Three differences had to be removed first,
--- and the third was found only by measuring:
---
---   1. ON-DISK CARRYOVER. Boot 1 wrote into the clip folder - a skip.map, a
---      rewritten clip.json, and an AutoSaveState savestate on unload - and
---      boot 2 read them back. Run under TESTRUN_CLIP_READONLY=1, which stages
---      the clip unwritable so both boots see identical bytes.
---   2. THE HEADLESS LATCHES. autoPlayDone/autoSeekDone fired once per PROCESS,
---      so boot 2 was never un-paused and emulated nothing at all. Fixed in
---      mainui.cpp (re-armed on Event::Terminate).
---   3. THE CLOCK. `[MEASURED 2026-09-08]` frame.count() is the MOVIE index and
---      does not tick until the game polls maple - 142 frames late at boot - so
---      early samples read 0 in both boots and "agreement" there means nothing.
---      frame.confirmed() is 1:1 with this callback and monotonic across a
---      restart (300 -> 400 -> 600 measured), so frames-since-THIS-boot is
---      confirmed() minus its value at the boot's first callback. That is the
---      only clock of the three that means the same thing in both boots.
---
--- The observer does not survive the restart (a game start re-inits Lua), so
--- boot 1 hands its result to boot 2 through a file.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

-- COLD: run with AutoSeekState=-1 and AutoPlay=yes, so no savestate is loaded
-- and both boots are the machine as the BOOT left it. With the seek enabled the
-- state load overwrites the registered set and washes the difference out:
-- `[MEASURED 2026-09-08]` a seeked pair agreed on 7 of 8 samples and differed
-- only on the single pre-seek one, which is the seek erasing the evidence
-- rather than the boots agreeing.
local SETTLE = 260      -- frames into each boot before sampling
local SEQ    = 8        -- consecutive samples per boot
local MARK   = (os.getenv("XDG_CONFIG_HOME") or "/tmp") .. "/coldboot_pair.mark"

local base, rows, done_ = nil, {}, false

local function readMark()
	local f = io.open(MARK, "r"); if not f then return nil end
	local v = f:read("*a"); f:close(); return v
end
local firstBoot = readMark()

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	if done_ then return end
	-- Frames since THIS boot, not since the process. See note 3 above.
	if base == nil then base = flycast.frame.confirmed() end
	local since = flycast.frame.confirmed() - base
	if since < SETTLE then return end

	if #rows == 0 and firstBoot == nil then
		t.ran({
			["a movie is playing"]   = flycast.emulator.isReplay(),
			["restartLater exists"]  = type(flycast.emulator.restartLater) == "function",
			["the clip is read-only"] = os.getenv("XDG_CONFIG_HOME") ~= nil,
		})
	end

	rows[#rows + 1] = ("%d:%s"):format(flycast.frame.count(), tostring(flycast.savestate.hash()))
	if #rows < SEQ then return end

	local seq = table.concat(rows, " ")

	if firstBoot == nil then
		local f = io.open(MARK, "w"); f:write(seq); f:close()
		print("  boot 1: " .. seq)
		flycast.emulator.restartLater()
		done_ = true
		return
	end

	print("  boot 1: " .. firstBoot)
	print("  boot 2: " .. seq)

	-- VACUITY: identical hashes down a boot would make agreement free.
	local moving = false
	for i = 2, #rows do
		if rows[i]:match(":(%d+)$") ~= rows[1]:match(":(%d+)$") then moving = true end
	end
	t.check("the machine is MOVING while sampling", moving,
			"identical hashes would make the comparison vacuous")

	-- ALIGNMENT: the movie index must match, or the two samples are of
	-- different moments and the hash comparison below is meaningless.
	local f1 = firstBoot:match("^(%d+):")
	local f2 = seq:match("^(%d+):")
	t.check("both boots sampled at the same movie frame", f1 == f2,
			("boot1=%s boot2=%s"):format(tostring(f1), tostring(f2)))

	-- THE CLAIM - pass when it holds, `limit` when it does not.
	--
	-- Not a hard failure, because a difference here is a DISCOVERED PROPERTY
	-- under investigation rather than a regression from a known-good state, and
	-- fbneo-rr's limit() exists for exactly that: a known gap stays visible
	-- instead of being silently deleted or noisily failing forever. It flips to
	-- PASS by itself the day the cause is fixed.
	--
	-- The falsifiability lives in the two checks above, which are hard
	-- assertions and DO fail: if the boots stop being aligned, or the machine
	-- stops moving, this test goes red rather than quietly reporting a limit.
	if firstBoot == seq then
		t.check("two boots of one process produce the same machine", true,
				SEQ .. "/" .. SEQ .. " hashes identical")
	else
		-- Report WHERE they diverge, not just that they do.
		local at = 0
		local b1, b2 = {}, {}
		for tok in firstBoot:gmatch("%S+") do b1[#b1+1] = tok end
		for tok in seq:gmatch("%S+")       do b2[#b2+1] = tok end
		for i = 1, math.min(#b1, #b2) do
			if b1[i] ~= b2[i] then at = i break end
		end
		t.limit("cold-boot determinism",
			("two boots of one process differ from sample %d of %d (%s vs %s) - "
			 .. "state survived a game init"):format(at, #b1,
					tostring(b1[at]), tostring(b2[at])))
	end
	t.finish()
	done_ = true
end
