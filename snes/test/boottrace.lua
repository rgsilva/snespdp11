local frame = 0
local last_ic = -1
emu.addEventCallback(function()
  frame = frame + 1
  if frame % 600 == 0 then
    local ic = emu.read16(0x102, emu.memType.snesWorkRam) +
               emu.read16(0x104, emu.memType.snesWorkRam) * 65536
    local out = emu.read16(0x106, emu.memType.snesWorkRam)
    local pc = emu.read16(0x10C, emu.memType.snesWorkRam)
    local ps = emu.read16(0x10E, emu.memType.snesWorkRam)
    print(string.format("frame=%d icount=%d dIc=%d out=%d PC=%06o PS=%06o",
      frame, ic, ic - last_ic, out, pc, ps))
    last_ic = ic
  end
  if frame > 12000 then emu.stop(0) end
end, emu.eventType.endFrame)
