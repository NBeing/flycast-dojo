-- Exercise flycast.replay.* against the real engine.
--
-- The flush assertion is the point. AppendToReplay writes in batches of
-- FRAME_BATCH (120) and frames are MAPLE_FRAME_SIZE (28) bytes, so recording
-- exactly 150 frames tells a working stopRecording (150 frames on disk, 4200
-- bytes of frame data) apart from one that forgot FlushReplay (120 frames,
-- 3360) with 840 bytes of daylight between them. A size check alone would not.
local n, phase = 0, 0
local WARM, REC = 300, 150
local path, f0
flycast_callbacks = {}
flycast_callbacks.vblank = function()
    n = n + 1
    if phase == 0 and n == WARM then
        print("RB: isRecording before start = "..tostring(flycast.replay.isRecording()))
        print("RB: currentPath before start = '"..tostring(flycast.replay.currentPath()).."'")
        local ok = flycast.replay.startRecording("luatest")
        print("RB: startRecording -> "..tostring(ok))
        print("RB: isRecording after start  = "..tostring(flycast.replay.isRecording()))
        path = flycast.replay.currentPath()
        print("RB: PATH="..tostring(path))
        -- a second start must REFUSE rather than make a second clip folder
        print("RB: startRecording again -> "..tostring(flycast.replay.startRecording("luatest")))
        f0 = flycast.frame.count()
        phase, n = 1, 0
    elseif phase == 1 and n == REC then
        local ran = flycast.frame.count() - f0
        flycast.replay.stopRecording()
        print("RB: recorded guest frames = "..ran)
        print("RB: isRecording after stop  = "..tostring(flycast.replay.isRecording()))
        print("RB: currentPath after stop  = '"..tostring(flycast.replay.currentPath()).."'")
        print("RB: DONE PATH="..tostring(path).." FRAMES="..ran)
        phase = 2
    end
end
