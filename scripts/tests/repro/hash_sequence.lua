--- Print a hash sequence keyed by the GUEST frame number, for cross-process
--- comparison by scripts/reprotest.sh.
---
--- Lives under tests/repro/ rather than tests/ on purpose: `scripts/tests/*.lua`
--- is the default suite glob, and this script is not a test on its own. One run
--- of it asserts nothing about reproducibility - it takes TWO, in two
--- processes, and only the harness can see both.
---
--- KEYED BY frame.count(), NOT by callback count. Two processes do not start
--- their Lua at the same point in the boot, so vblank number N is not the same
--- machine state in both. The guest's own clock is the only key that means the
--- same thing on both sides - the same lesson differential_history.lua paid for
--- when a one-frame host offset read as a divergence.
---
--- THE RECORD `[2026-09-17]` (harness #2, docs/TEST-PLAN.md §5.4):
---     REPRO <frame> <machine-hash> in=<input-digest> c=<p1>/<p2>
---   machine-hash   flycast.savestate.hash() - the serialized machine, the same
---                  domain oracle::machineHash covers (core/dojo/oracle.h).
---   input-digest   a digest of the MOVIE ROW at this frame for BOTH players
---                  (flycast.movie.getButtons(f, 1) and (f, 2)), button names
---                  sorted so the digest is canonical; `absent` when the movie
---                  holds no row here (nil from getButtons - "no data" and
---                  "nothing pressed" are different facts). The hash is a
---                  multiplicative string hash mod 2^32 (h = h*31 + byte),
---                  chosen because it needs no bit operators (exact in doubles,
---                  so it is the same under any Lua the tree links). It travels
---                  so the harness can tell an INPUT desync (the two processes
---                  did not feed the guest the same row - not emulation
---                  nondeterminism) from a MACHINE divergence (same row, different
---                  state - which is).
---   c=p1/p2        the MvC2 combo counters, read at the addresses the harness
---                  resolved BY NAME from SPREADSHEET.json (Combo_Meter_HitsToOpponent)
---                  and passed in as FLYCAST_REPRO_COMBO_P1/P2 (flycast 0x8C.. form).
---                  This script hardcodes no address; unset => `c=-/-` and a note.
---                  A game-state series riding in the same sequence - "it comboed
---                  on frame N" next to "the machine was identical on frame N".
---
--- ENV: FLYCAST_REPRO_START (default 100)  FLYCAST_REPRO_SEQ (default 12)
---      FLYCAST_REPRO_POKE=1        the sabotage arm: poke one word at the FIRST sample
---      FLYCAST_REPRO_POKE_AT=<f>   poke at guest frame f instead (a harness places the
---                                  divergence where it wants it; implies the arm)
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local START = tonumber(os.getenv("FLYCAST_REPRO_START")) or 100   -- guest frame to begin sampling at
local SEQ   = tonumber(os.getenv("FLYCAST_REPRO_SEQ")) or 12      -- samples

local rows, done_, poked = {}, false, false
local POKE_AT = tonumber(os.getenv("FLYCAST_REPRO_POKE_AT"))
local POKE = os.getenv("FLYCAST_REPRO_POKE") == "1" or POKE_AT ~= nil
local COMBO_P1 = tonumber(os.getenv("FLYCAST_REPRO_COMBO_P1") or "")
local COMBO_P2 = tonumber(os.getenv("FLYCAST_REPRO_COMBO_P2") or "")
local said_combo = false

-- h = h*31 + byte, mod 2^32. Exact in IEEE doubles (h*31 + 255 < 2^53), no bit ops.
local function strhash(s)
	local h = 5381
	for i = 1, #s do h = (h * 31 + s:byte(i)) % 4294967296 end
	return ("%08x"):format(h)
end

-- The row at frame f for both players, as ONE canonical string. Button names are
-- sorted so two processes that enumerate the table in different orders still
-- digest the same row. nil (no row) is spelled out, never digested as empty.
local function inputDigest(f)
	local parts = {}
	for player = 1, 2 do
		local b = flycast.movie.getButtons(f, player)
		if b == nil then return "absent" end
		local names = {}
		for name in pairs(b) do names[#names + 1] = name end
		table.sort(names)
		for _, name in ipairs(names) do
			parts[#parts + 1] = ("%d:%s=%d"):format(player, name, b[name] and 1 or 0)
		end
	end
	return strhash(table.concat(parts, ","))
end

local function comboPair()
	if COMBO_P1 == nil or COMBO_P2 == nil then
		if not said_combo then
			print("REPRO-NOTE combo addresses not supplied (FLYCAST_REPRO_COMBO_P1/P2); c=-/-")
			said_combo = true
		end
		return "-/-"
	end
	return ("%d/%d"):format(flycast.memory.read8(COMBO_P1), flycast.memory.read8(COMBO_P2))
end

-- The sabotage arm. One perturbed word must change the sequence; if it does not,
-- the comparison the harness makes cannot detect a difference either, and a
-- matching pair of runs would prove nothing.
local function poke()
	for a = 0x0C800000, 0x0C804000, 4 do
		if flycast.memory.read32(a) == 0 then
			flycast.memory.write32(a, 0xDEADBEEF)
			poked = flycast.memory.read32(a) == 0xDEADBEEF
			break
		end
	end
	t.check("the poke reached guest RAM", poked, "sabotage arm")
end

flycast_callbacks = {}
flycast_callbacks.vblank = function()
	if done_ then return end
	local f = flycast.frame.count()
	if f < START then return end

	if #rows == 0 then
		t.ran({
			["a movie is playing"]   = flycast.emulator.isReplay(),
			["frames are advancing"] = f > 0,
		})
		if POKE and POKE_AT == nil then poke() end
	end
	-- FLYCAST_REPRO_POKE_AT: the divergence is placed at a chosen guest frame, so a
	-- harness can make two armed runs diverge at DIFFERENT frames and prove its
	-- classifier tells "moves" from "fixed".
	if POKE and POKE_AT ~= nil and not poked and f >= POKE_AT then poke() end

	-- One sample per distinct guest frame. A repeated frame number would key two
	-- different states to one label.
	if rows[#rows] == nil or rows[#rows].f ~= f then
		local h = tostring(flycast.savestate.hash())
		rows[#rows + 1] = { f = f, h = h }
		print(("REPRO %d %s in=%s c=%s"):format(f, h, inputDigest(f), comboPair()))
	end

	if #rows >= SEQ then
		-- Vacuity: a frozen machine emits SEQ identical hashes, and two frozen
		-- runs agree perfectly while proving nothing.
		local moving = false
		for i = 2, #rows do if rows[i].h ~= rows[1].h then moving = true end end
		t.check("the machine is MOVING while sampling", moving,
				"identical hashes would make the cross-process comparison vacuous")
		t.check("samples are on consecutive distinct frames",
				rows[#rows].f > rows[1].f,
				("%d..%d"):format(rows[1].f, rows[#rows].f))
		t.finish()
		done_ = true
	end
end
