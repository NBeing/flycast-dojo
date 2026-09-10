--- memwatch - "did this region change?", once per confirmed frame.
---
---   Run it:  scripts/testrun.sh scripts/tests/memwatch.lua
---
--- WHAT IT IS FOR. A trainer wants to know when a value changed without reading
--- it every frame - and for anything bigger than a few bytes, "read it every
--- frame" means pulling the region through the Lua stack sixty times a second.
--- The host compares against a shadow copy and answers with a revision counter;
--- the script only reads bytes when that moves.
---
--- WHAT IT IS NOT, said here because the backlog entry this came from proposed
--- something else. There is no dirty-page filter: page protection is armed only
--- under GGPO, and its page lists are DRAINED by the rollback path, so reading
--- them would steal a frame's delta and corrupt a rollback. And there is no PC
--- context - this says a region changed, never which instruction changed it.
--- core/lua/luawatch.h has the evidence for both.
---
--- THE CLAIM THAT MATTERS IS NEGATIVE, and it is made WITHOUT WRITING to guest
--- memory - scribbling on a running game's RAM to prove a watch works is a poor
--- trade. Two watches on the same busy region, one removed: the removed one
--- must stop while the live one keeps moving. A registry that ignored `unwatch`,
--- or one that incremented everything blindly, fails that and passes a
--- positive-only test.
local t = dofile(os.getenv("FLYCAST_TESTLIB"))

local mem = flycast.memory

--- The SH4 stack region, which an earlier cross-fork probe in this repo found
--- to be the hottest part of guest RAM. A watch here is expected to move; the
--- test asserts that rather than assuming it, and says so if it does not.
local BUSY, LEN = 0x0CF00000, 256

local n, stage = 0, 1
local liveId, deadId
local revAtRemoval, comparesAtRemoval

local prevVblank = flycast_callbacks and flycast_callbacks.vblank
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if prevVblank then prevVblank() end
	n = n + 1

	if stage == 1 and n >= 120 then
		stage = 2
		liveId = mem.watch(BUSY, LEN)
		deadId = mem.watch(BUSY, LEN)
		t.ran({ ["frames are advancing"] = n > 0 })
		t.check("a watch gets an id", type(liveId) == "number" and liveId >= 0,
			tostring(liveId))
		t.check("two watches get different ids", liveId ~= deadId,
			tostring(liveId) .. " / " .. tostring(deadId))
		--- PRIMING: the first tick fills the shadow and is not a change, so a
		--- script arming a watch to wait for something does not fire at once.
		t.check("a fresh watch starts at revision 0", mem.watchRevision(liveId) == 0)
		return
	end

	if stage == 2 and n >= 180 then
		stage = 3
		--- The positive half. If this region turns out not to move, the test
		--- says so rather than quietly passing the negative half alone.
		t.check("a watch on a busy region notices changes",
			mem.watchRevision(liveId) > 0, tostring(mem.watchRevision(liveId)))
		t.check("the two watches agree, being the same region",
			mem.watchRevision(liveId) == mem.watchRevision(deadId),
			mem.watchRevision(liveId) .. " vs " .. mem.watchRevision(deadId))

		revAtRemoval = mem.watchRevision(liveId)
		comparesAtRemoval = mem.watchCompares()
		t.check("removing a watch answers true once",
			mem.unwatch(deadId) == true and mem.unwatch(deadId) == false)
		return
	end

	if stage == 3 and n >= 260 then
		stage = 4
		--- THE NEGATIVE HALF, and the reason the test is shaped this way.
		t.check("a removed watch stops answering", mem.watchRevision(deadId) == 0,
			tostring(mem.watchRevision(deadId)))
		t.check("...while the live one kept moving",
			mem.watchRevision(liveId) > revAtRemoval,
			revAtRemoval .. " -> " .. mem.watchRevision(liveId))
		t.check("one watch remains", mem.watchCount() == 1, tostring(mem.watchCount()))
		--- The tick really ran, rather than the counter being incremented
		--- somewhere convenient.
		t.check("comparisons accumulated across frames",
			mem.watchCompares() > comparesAtRemoval,
			comparesAtRemoval .. " -> " .. mem.watchCompares())

		--- THE PRICE IS REPORTED, NOT ARGUED ABOUT. A number, so a reader can
		--- judge what size of watch a frame can afford instead of guessing.
		local us, peak = mem.watchMicros(), mem.watchMaxMicros()
		t.check("the cost of a tick is measured", type(us) == "number" and us >= 0,
			string.format("%.1f us last, %.1f us peak for %d bytes", us, peak, LEN))
		--- 16.6 ms is one frame at 60 Hz. A watch costing anything near that is
		--- a bug in the mechanism, not a slow machine.
		t.check("...and a 256-byte watch costs far less than a frame", peak < 2000,
			string.format("%.1f us peak", peak))

		--- The cap is a refusal, not a silent truncation.
		t.check("an oversized watch is REFUSED", not pcall(mem.watch, BUSY, 1024 * 1024))
		t.check("a zero-length watch is refused", not pcall(mem.watch, BUSY, 0))

		mem.unwatch(liveId)
		t.finish()
	end
end
