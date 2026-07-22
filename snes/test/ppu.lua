local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 1500 then
    local function vw(wa) return emu.read16(wa*2, emu.memType.snesVideoRam) end
    -- space (0x20) should be all-zero; 'S' (0x53) has a real glyph
    print("font space(0x20) 16w: ")
    local s=""
    for i=0,15 do s=s..string.format("%04X ", vw(0x20*16+i)) end
    print("  "..s)
    print("font 'A'(0x41) 16w:")
    s=""
    for i=0,15 do s=s..string.format("%04X ", vw(0x41*16+i)) end
    print("  "..s)
    print("tilemap row0 [0..8]: ")
    s=""
    for i=0,8 do s=s..string.format("%04X ", vw(0x4000+i)) end
    print("  "..s)
    local st = emu.getState()
    if st and st.ppu then
      print("bgMode="..tostring(st.ppu.bgMode).." forcedBlank="..tostring(st.ppu.forcedBlank))
    end
    emu.stop(0)
  end
end, emu.eventType.endFrame)
