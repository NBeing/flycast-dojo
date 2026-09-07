-- THE ACTUAL POOL GUARANTEE: two machines restored from the same blob, each
-- run forward the same number of frames, must agree. (Comparing a restored
-- machine against a never-restored one is a different, stronger claim.)
local n, blob, h = 0, nil, {}
local RUN, phase = 300, 0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
    n = n + 1
    if phase == 0 and n == 600 then
        blob = flycast.savestate.tostring()
        flycast.savestate.fromstring(blob)
        print("FWD3: snapshot + restore #1")
        phase, n = 1, 0
    elseif phase == 1 and n == RUN then
        h[1] = flycast.savestate.hash()
        print("FWD3: member A -> "..tostring(h[1]))
        flycast.savestate.fromstring(blob)
        phase, n = 2, 0
    elseif phase == 2 and n == RUN then
        h[2] = flycast.savestate.hash()
        print("FWD3: member B -> "..tostring(h[2]))
        flycast.savestate.fromstring(blob)
        phase, n = 3, 0
    elseif phase == 3 and n == RUN then
        h[3] = flycast.savestate.hash()
        print("FWD3: member C -> "..tostring(h[3]))
        print("FWD3: VERDICT " .. ((h[1] == h[2] and h[2] == h[3])
              and "POOL-SAFE (all restores agree)" or "DIVERGED"))
        phase = 4
    end
end
