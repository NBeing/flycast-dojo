--- The input tracer's PROBE (dojo:InputTrace). Lives under tests/open/ because it
--- needs a launch flag the default suite does not pass; scripts/inputtracetest.sh
--- runs it with `-config dojo:InputTrace=yes` and judges the EMULATOR log - the
--- trace lines are NOTICE_LOG, not Lua output - so this file only drives the pad:
--- neutral for 240 vblanks, P1 A held for 60, neutral again. The harness then
--- asserts a `TAS INPUT: P1 kcode=~0x4` line exists and that there are FEW of
--- them (a change prints once; an unchanged hold repeats only every 5 s; neutral
--- frames print nothing at all).
local t = dofile(os.getenv("FLYCAST_TESTLIB"))
local A = 4			-- DC_BTN_A (core/input/gamepad.h: 1 << 2)
local n, phase = 0, 0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if phase == 0 and n == 240 then
		t.ran({
			["a movie is playing"]   = flycast.emulator.isReplay(),
			["frames are advancing"] = flycast.frame.count() > 0,
			["the pad API exists"]   = type(flycast.input.pressButtons) == "function",
		})
		flycast.input.pressButtons(1, A)
		print(("INPUT TRACE PROBE: P1 A pressed at frame %d"):format(flycast.frame.count()))
		phase = 1
	elseif phase == 1 and n == 300 then
		flycast.input.releaseButtons(1, A)
		print(("INPUT TRACE PROBE: P1 A released at frame %d"):format(flycast.frame.count()))
		phase = 2
	elseif phase == 2 and n == 360 then
		t.check("the probe pressed P1 A for 60 vblanks and released it (the trace itself is judged from the emulator log)", true)
		t.finish()
		phase = 3
	end
end
