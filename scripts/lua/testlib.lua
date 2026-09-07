--- testlib - the assertion contract for flycast Lua tests.
---
---     local t = dofile(os.getenv("FLYCAST_TESTLIB"))
---     t.check("name", cond, "detail")
---     t.finish()          -- prints SUMMARY and done; the runner watches for them
---
--- Output goes through print(), which flycast already writes to
--- ~/.config/flycast-dojo/flycast-lua.log, flushed PER LINE
--- (core/lua/lua_console.cpp). No marker files: that log is the channel.
---
--- THE CONTRACT THE RUNNER RELIES ON, and each line of it was paid for:
---
---   * `done` is printed on a line of its OWN. The runner matches whole lines,
---     never substrings, because a log line that merely CONTAINS the word would
---     otherwise truncate a run into a false pass.
---   * `SUMMARY: N passed, M failed` is the verdict. A run that exits without
---     one is INCONCLUSIVE, not PASS - the commonest way a suite lies is a test
---     that never ran at all.
---   * `ran()` is the first thing every test should call. It asserts the test is
---     executing in the state it claims to test. See CLAUDE.md rule 1: "no clip
---     folder was created" is also what "the script never ran" looks like.
local M = { pass = 0, fail = 0, skipped = 0 }

function M.check(name, cond, detail)
	if cond then
		M.pass = M.pass + 1
		print(("  PASS  %s  %s"):format(name, detail or ""))
	else
		M.fail = M.fail + 1
		print(("  FAIL  %s  %s"):format(name, detail or ""))
	end
	return cond
end

--- A documented, tracked limitation: logged, counted, NOT failed. Borrowed from
--- fbneo-rr's limit(), which exists so a known gap stays visible instead of
--- being silently deleted or noisily failing forever.
function M.limit(name, why)
	M.skipped = M.skipped + 1
	print(("  LIMIT %s  %s"):format(name, why or ""))
end

--- Every test's first assertion: that it is running at all, and in the state it
--- says it is. Takes the conditions that must hold for the test to MEAN
--- anything; if one fails the run is a failure, not a quiet pass.
function M.ran(conditions)
	print("  RAN   the script is executing")
	for name, cond in pairs(conditions or {}) do
		M.check("precondition:" .. name, cond and true or false)
	end
end

function M.finish()
	print(("SUMMARY: %d passed, %d failed, %d limits"):format(M.pass, M.fail, M.skipped))
	print("done")
end

return M
