local n=0; flycast_callbacks={}
flycast_callbacks.vblank=function() n=n+1
  if n==600 then flycast.savestate.save(0); print("MK: saved slot 0") end end
