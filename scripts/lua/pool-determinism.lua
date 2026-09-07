-- Rule out the ruler. Two controls added to the pool test:
--   H0  = hash IMMEDIATELY after restore, before any frames run.
--         If H0 differs, the restore itself is non-deterministic and the
--         300-frame window is irrelevant.
--   fc  = the MACHINE's own movie-frame counter at start and end, so we can
--         prove each member executed the same number of guest frames rather
--         than the same number of host vblanks.
local n, blob, phase = 0, nil, 0
local RUN = 300
local H0, HN, FC0, FC1 = {}, {}, {}, {}
local member = 0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
    n = n + 1
    if phase == 0 and n == 600 then
        blob = flycast.savestate.tostring()
        print("FWD5: snapshot "..#blob.." bytes")
        phase, n = 1, 0
    elseif phase == 1 then
        member = member + 1
        flycast.savestate.fromstring(blob)
        H0[member]  = flycast.savestate.hash()
        FC0[member] = flycast.frame.count()
        phase, n = 2, 0
    elseif phase == 2 and n == RUN then
        HN[member]  = flycast.savestate.hash()
        FC1[member] = flycast.frame.count()
        print(string.format("FWD5: member %d  H0=%s  frames %d->%d (%d)  HN=%s",
              member, tostring(H0[member]), FC0[member], FC1[member],
              FC1[member]-FC0[member], tostring(HN[member])))
        if member < 3 then phase, n = 1, 0 else
            local h0same = (H0[1]==H0[2] and H0[2]==H0[3])
            local fcsame = ((FC1[1]-FC0[1])==(FC1[2]-FC0[2]) and (FC1[2]-FC0[2])==(FC1[3]-FC0[3]))
            local hnsame = (HN[1]==HN[2] and HN[2]==HN[3])
            print("FWD5: start states identical? "..tostring(h0same))
            print("FWD5: same guest frames run?  "..tostring(fcsame))
            print("FWD5: end states identical?   "..tostring(hnsame))
            phase = 3
        end
    end
end
