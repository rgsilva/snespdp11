local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if frame % 1500 == 0 then
    print(string.format("f=%d PC=%06o v2p(177564)=%04X:%04X xcsrReads=%d ic=%d",
      frame, emu.read16(0x10C, emu.memType.snesWorkRam),
      emu.read16(0x11C, emu.memType.snesWorkRam), emu.read16(0x11A, emu.memType.snesWorkRam),
      emu.read16(0x112, emu.memType.snesWorkRam),
      emu.read16(0x102, emu.memType.snesWorkRam)+emu.read16(0x104, emu.memType.snesWorkRam)*65536))
  end
  if frame > 8000 then emu.stop(0) end
end, emu.eventType.endFrame)
