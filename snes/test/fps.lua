local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 7200 then emu.stop(0) end
end, emu.eventType.endFrame)
