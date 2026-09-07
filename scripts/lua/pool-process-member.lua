-- ONE restore per process. If separate processes agree, a process-per-machine
-- pool is safe even though an in-process one is not.
local n,phase=0,0; flycast_callbacks={}
flycast_callbacks.vblank=function() n=n+1
  if phase==0 and n==300 then
    flycast.savestate.load(0); print("R1: restored, H0="..tostring(flycast.savestate.hash())
      .." fc="..flycast.frame.count()); phase,n=1,0
  elseif phase==1 and n==300 then
    print("R1: after 300 -> HN="..tostring(flycast.savestate.hash()).." fc="..flycast.frame.count()); phase=2 end end
