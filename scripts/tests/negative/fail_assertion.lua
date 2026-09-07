local t = dofile(os.getenv("FLYCAST_TESTLIB"))
local n = 0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
	n = n + 1
	if n == 200 then
		t.ran({})
		t.check("this assertion is false on purpose", 1 == 2, "expected FAIL")
		t.finish()
	end
end
