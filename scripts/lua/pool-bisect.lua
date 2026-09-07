-- Where do two pool members diverge? Keep each member's FINAL blob, then
-- binary-search the first differing byte. SERMAP maps that offset to a
-- subsystem, exactly as it did for the round-trip bugs.
local n, blob, phase, member = 0, nil, 0, 0
local RUN, fin = 300, {}
local function firstDiff(a, b)
    if a == b then return -1 end
    local lo, hi = 1, math.min(#a, #b)          -- first index where they differ
    while lo < hi do
        local mid = math.floor((lo + hi) / 2)
        if a:sub(1, mid) == b:sub(1, mid) then lo = mid + 1 else hi = mid end
    end
    return lo - 1                                -- 0-based, to match SERMAP
end
flycast_callbacks = {}
flycast_callbacks.vblank = function()
    n = n + 1
    if phase == 0 and n == 600 then
        blob = flycast.savestate.tostring(); phase, n = 1, 0
    elseif phase == 1 then
        member = member + 1
        flycast.savestate.fromstring(blob); phase, n = 2, 0
    elseif phase == 2 and n == RUN then
        fin[member] = flycast.savestate.tostring()
        print("FWD7: member "..member.." final captured")
        if member < 3 then phase, n = 1, 0 else
            print("FWD7: len1="..#fin[1].." len2="..#fin[2].." len3="..#fin[3])
            print("FWD7: first diff 1v2 at offset "..firstDiff(fin[1], fin[2]))
            print("FWD7: first diff 2v3 at offset "..firstDiff(fin[2], fin[3]))
            phase = 3
        end
    end
end
