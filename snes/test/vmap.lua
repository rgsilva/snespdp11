local frame = 0
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 1500 then
    local function vw(wa) return emu.read16(wa*2, emu.memType.snesVideoRam) end
    local b2, b1 = "", ""
    for i=0,11 do b2 = b2 .. string.format("%04X ", vw(0x0000+i)) end
    for i=0,11 do b1 = b1 .. string.format("%04X ", vw(0x0400+i)) end
    print("BG2 map $0000: " .. b2)
    print("BG1 map $0400: " .. b1)
    -- BG1 font: char 'n'=0x6E -> tiles 2*0x6E=0xDC (blank) and 0xDD (glyph)
    print("BG1 font tile 0xDD (glyph half of 'n') w0-3: " ..
      string.format("%04X %04X %04X %04X", vw(0x3000+0xDD*16), vw(0x3000+0xDD*16+1),
                    vw(0x3000+0xDD*16+2), vw(0x3000+0xDD*16+3)))
    emu.stop(0)
  end
end, emu.eventType.endFrame)
