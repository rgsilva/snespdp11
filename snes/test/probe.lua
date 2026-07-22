-- Probe: is the boot block in guest SRAM? is console output happening?
-- guest RAM = cart SRAM banks $20-$2F, 8K windows at $6000. Guest byte B
-- maps to bank $20+(B>>13), addr $6000+(B & $1FFF).
local frame = 0
local function gw(pa)  -- guest physical word at byte address pa
  local bank = 0x20 + (pa >> 13)
  local off = 0x6000 + (pa & 0x1FFF)
  return emu.read16((bank << 16) + off, emu.memType.snesMemory)
end
local function rom(byteoff)  -- v6 ROM disk lives at ROM byte 0x100000 => $D00000
  return emu.read16(0xD00000 + byteoff, emu.memType.snesMemory)
end
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 30 or frame == 300 or frame == 1200 then
    print(string.format("--- frame %d", frame))
    print(string.format("  magic=%04X icount=%d out=%d PC=%06o PS=%06o",
      emu.read16(0x100, emu.memType.snesWorkRam),
      emu.read16(0x102, emu.memType.snesWorkRam) +
        emu.read16(0x104, emu.memType.snesWorkRam) * 65536,
      emu.read16(0x106, emu.memType.snesWorkRam),
      emu.read16(0x10C, emu.memType.snesWorkRam),
      emu.read16(0x10E, emu.memType.snesWorkRam)))
    print(string.format("  guest[0..6] = %06o %06o %06o %06o", gw(0), gw(2), gw(4), gw(6)))
    print(string.format("  ROMdisk[0..6] = %06o %06o %06o %06o (want 012706 137000 ..)",
      rom(0), rom(2), rom(4), rom(6)))
    print(string.format("  guest@0137000 = %06o %06o", gw(0xBE00), gw(0xBE02)))
  end
  if frame > 1230 then emu.stop(0) end
end, emu.eventType.endFrame)
