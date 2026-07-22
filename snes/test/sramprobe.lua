-- Report how much distinct cart SRAM Mesen backs (see sramprobe.c).
local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if emu.read16(0x100, emu.memType.snesWorkRam) == 0xCAFE then
    local kb = emu.read16(0x104, emu.memType.snesWorkRam)
    print(string.format("SRAM: %d distinct 8K windows = %d KB",
      emu.read16(0x102, emu.memType.snesWorkRam), kb))
    emu.stop(kb >= 128 and 0 or 1)
  end
  if frame > 600 then print("TIMEOUT") emu.stop(2) end
end, emu.eventType.endFrame)
