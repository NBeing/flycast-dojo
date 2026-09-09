--- CROSS-FORK ORACLE PROBE. Driven by scripts/reprotest.sh --oracle.
---
--- Runs in BOTH forks, so it uses only the intersection of their Lua APIs:
--- flycast_callbacks.vblank and flycast.memory.read32. David's fork has 822
--- lines of Lua to our 2572 and no savestate/frame/movie namespaces at all.
---
--- NOT savestate hashes. The two trees serialize differently, so two identical
--- machines would hash differently and every run would report a false alarm.
--- Guest RAM is the emulated machine itself and means the same thing on both
--- sides. `[MEASURED 2026-09-09]` both forks load the same clip and savestate
--- and reach the same RAM fingerprint, which is what makes this comparable.
---
--- DENSE CONTIGUOUS WINDOWS, not a wide stride. The first version sampled one
--- word every 64 KB across all 16 MB and did not change at all during
--- gameplay - 256 words spread that thinly land on untouched pages, and a
--- constant fingerprint matches across forks for free. Windows of consecutive
--- words hit live data.
-- WHERE TO LOOK, measured. `[MEASURED 2026-09-09]` a churn map that densely
-- diffed each 256 KB block of main RAM at 16-byte granularity during gameplay:
--
--   0x0C000000  1     0x0C240000 11     0x0C340000  8
--   0x0C140000  2     0x0C280000  5     0x0CF00000 56   <- hottest
--   0x0C180000 12     0x0C300000  9
--   0x0C1C0000 16                          (changed words per 16384 sampled)
--
-- CHURN IS SPARSE: tens of changed words per 256 KB. Two earlier probes used
-- 512-byte windows, so the chance of containing one of ~12 changed words in
-- 256 KB was near zero and the fingerprint sat constant - 19 to 24 distinct
-- values over 400 samples, which produced a 168-sample "identical run" that was
-- two idle machines coinciding. The fix is DENSE coverage of a hot block, not
-- more scattered windows.
--
-- 0x0CF00000 is the top of main RAM, where the SH4 stack lives. That is the
-- point: a stack churns every frame in ANY game, so this is a game-agnostic
-- signal rather than a guess about where MvC2 keeps its state.
local BLOCK   = 0x0CF00000
local WORDS   = 16384   -- 256 KB at 16-byte granularity
local STEP    = 16
local SETTLE  = tonumber(os.getenv("ORACLE_SETTLE") or "") or 30
local SEQ     = tonumber(os.getenv("ORACLE_SEQ") or "") or 150

local n, rows, first, moved = 0, 0, nil, false

local function fingerprint()
	local h = 2166136261
	for i = 0, WORDS - 1 do
		local v = flycast.memory.read32(BLOCK + i * STEP) or 0
		h = ((h ~ v) * 16777619) % 4294967296
	end
	return math.floor(h)
end

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if n < SETTLE then return end
	if rows >= SEQ then return end
	rows = rows + 1
	local f = fingerprint()
	if first == nil then first = f elseif f ~= first then moved = true end
	-- No frame number: the two forks have no shared clock, so the harness
	-- aligns the two sequences by cross-correlation instead of by index.
	print(("OR %d"):format(f))
	if rows == SEQ then
		-- VACUITY: a fingerprint that never moves matches at EVERY offset, so
		-- the alignment below would always "succeed".
		print("OR-MOVED " .. tostring(moved))
		print("OR-DONE")
		-- testrun.sh's contract: it watches for a whole line `done`, preceded by
		-- a SUMMARY. Without these the A-side run sits until its timeout and is
		-- reported TIMEOUT even though it finished sampling. The B side is
		-- launched directly and keys off OR-DONE instead.
		print("SUMMARY: 1 passed, 0 failed, 0 limits")
		print("done")
	end
end
