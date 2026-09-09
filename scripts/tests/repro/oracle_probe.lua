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
-- WHERE TO LOOK, measured rather than guessed. `[MEASURED 2026-09-09]` a
-- diagnostic sampling one word per 4 KB page across ALL 16 MB of main RAM saw
-- 0, 0, 0, 4, 1, 0 words change between sampled frames of a RUNNING game - and
-- the few that did were confined to 0x0C1FB000..0x0C2AE000. The working set is
-- small and dense, so wide sparse sampling reads almost entirely static memory.
-- Two earlier probes died of this: a 64 KB stride over 16 MB, and eight 512-byte
-- windows at round megabyte offsets, both of which returned the SAME
-- fingerprint every frame and would have "matched" across forks for free.
local BLOCKS  = { 0x0C1F8000, 0x0C210000, 0x0C230000, 0x0C250000,
                  0x0C270000, 0x0C290000, 0x0C2A0000, 0x0C2A8000 }
local PER     = 128     -- consecutive u32 per block (512 B each, 1024 reads)
-- EVERY FRAME, NOT EVERY FIFTH. The harness aligns the two forks by
-- cross-correlating the sequences, and sampling every Nth frame quantizes that
-- alignment to N-frame steps. `[MEASURED 2026-09-09]` at EVERY=5 the best
-- offset matched 4 of 24 samples - near noise - because the two builds start
-- emulating at different points relative to the seek and the true skew need not
-- be a multiple of 5. Identical emulators would fail that comparison. Sampling
-- every frame lets the correlation land on any offset; PER pays for it.
-- A LONG WINDOW, because the two forks' windows must OVERLAP in guest time
-- before any offset can align them. `[MEASURED 2026-09-09]` at SETTLE=90/SEQ=60
-- the best offset matched 0 of 20 - not subtle divergence, but two windows with
-- no guest time in common: our build auto-plays so its vblanks start at boot and
-- its window straddles the seek to frame 9928, while David's is paused until the
-- keypress and only starts counting once already AT 9928. Hundreds of frames of
-- skew cannot be bridged by an offset search over 60 samples.
local SETTLE  = 30
local SEQ     = 400

local n, rows, first, moved = 0, 0, nil, false

local function fingerprint()
	local h = 2166136261
	for w = 1, #BLOCKS do
		local base = BLOCKS[w]
		for i = 0, PER - 1 do
			local v = flycast.memory.read32(base + i * 4) or 0
			h = ((h ~ v) * 16777619) % 4294967296
		end
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
