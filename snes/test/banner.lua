local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 2000 then
    local png = emu.takeScreenshot()
    local hex = png:gsub(".", function(c) return string.format("%02x", c:byte()) end)
    print(string.format("icount=%d PC=%06o magic=%04X",
  emu.read16(0x102, emu.memType.snesWorkRam)+emu.read16(0x104, emu.memType.snesWorkRam)*65536,
  emu.read16(0x10C, emu.memType.snesWorkRam), emu.read16(0x100, emu.memType.snesWorkRam)))
print("SHOT banner.png " .. hex)
    emu.stop(0)
  end
end, emu.eventType.endFrame)
