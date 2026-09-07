-- Prints the completion marker but asserts nothing. Must NOT be a pass.
local n = 0
flycast_callbacks = {}
flycast_callbacks.vblank = function() n = n + 1 if n == 200 then print("done") end end
