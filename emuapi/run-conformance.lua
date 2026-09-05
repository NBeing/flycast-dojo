--- Run the conformance suite against the mock host, with no emulator.
---
---   RUN:   lua emuapi/run-conformance.lua        (from the repo root)
---          lua run-conformance.lua               (from inside emuapi/)
---   PASS:  "RESULT: CONFORMS" and a non-zero exit status of 0.
---   FAIL:  any FAIL line, or exit status 1.
---
--- WHAT THIS PROVES, and what it does not.
---
--- It proves the suite and the neutral layer run with no emulator behind them:
--- that nothing in either reaches for a flycast global, an SH4 address, or a
--- Dreamcast button name. Before this existed the suite had only ever run on
--- one host, against an adapter by the same author, so "133 checks pass" was
--- agreement between two halves of one head.
---
--- It does NOT prove the flycast adapter is correct, and it is not a
--- substitute for running in the emulator. Both should pass. When they
--- disagree, that disagreement is the finding.

local root = arg and arg[0] and arg[0]:match("^(.*)[/\\][^/\\]+$") or "."
--- The package may be reached as <root>/emuapi or as <root> itself, depending
--- on where this was invoked from. Both are put on the path rather than
--- guessed between.
package.path = table.concat({
	root .. "/?.lua",
	root .. "/?/init.lua",
	root .. "/../?.lua",
	root .. "/../?/init.lua",
	package.path,
}, ";")

--- Ask for the mock by name. It is never detected: a fake that could be
--- selected automatically would stand in for a real host that failed to be
--- recognised.
_G.EMUAPI_HOST = "mock"

local api = require("emuapi").load("mock")
local driver = api.adapter.driver

print(string.format("running conformance against host '%s' (%s)",
		api.host, _VERSION))

--- The suite registers its own callbacks and reports from registerexit, so the
--- runner's whole job is to be the frame loop the emulator would have been.
require("emuapi.conformance")

local FRAMES = 400
for _ = 1, FRAMES do driver.step() end
driver.finish()

--- The suite prints its own report; the runner turns it into an exit status so
--- CI can fail on it. Reaching in for the counters would duplicate the report's
--- own arithmetic, so the marker it already prints is the source of truth.
os.exit(_G.EMUAPI_CONFORMS and 0 or 1)
