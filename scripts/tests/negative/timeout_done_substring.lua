-- A line CONTAINING "done" must not end the run. Expect TIMEOUT, not pass.
local n = 0
flycast_callbacks = {}
flycast_callbacks.vblank = function() n = n + 1 if n == 200 then print("baseline done, continuing") end end
