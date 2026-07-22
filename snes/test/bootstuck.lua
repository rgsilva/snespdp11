-- Detect the asm core's stuck '@' loop and dump the guest code + registers.
-- Guest RAM is linear in Mesen saveRam (offset == guest physical addr).
local frame, stuck, lastpc = 0, 0, -1
local SR = emu.memType.snesSaveRam
local WR = emu.memType.snesWorkRam
local function w16(a) return emu.read16(a, WR) end
local function gword(pa) return emu.read(pa, SR) + emu.read(pa+1, SR)*256 end

emu.addEventCallback(function()
  frame = frame + 1
  if w16(0x100) ~= 0xB007 then return end
  local pc = w16(0x10C)               -- MB[6] = R7
  if pc == lastpc then stuck = stuck + 1 else stuck = 0; lastpc = pc end
  -- once icount is well past '@' (>200k) and PC has been parked, dump
  local ic = w16(0x102) + w16(0x104)*65536
  if ic > 300000 and stuck > 30 and pc >= 0x8000 then
    print(string.format("STUCK at PC=%06o (0x%04X)  PS=%06o  ic=%d", pc, pc, w16(0x10E), ic))
    print(string.format("R0=%06o R1=%06o R2=%06o R3=%06o", w16(0x110),w16(0x112),w16(0x114),w16(0x116)))
    print(string.format("R4=%06o R5=%06o R6=%06o R7=%06o", w16(0x118),w16(0x11A),w16(0x11C),w16(0x11E)))
    local lo = pc - 040   -- dump ~48 words around PC
    if lo < 0 then lo = 0 end
    for a = lo, pc + 040, 2 do
      local mark = (a == pc) and " <== PC" or ""
      print(string.format("  %06o: %06o%s", a, gword(a), mark))
    end
    emu.stop(0); return
  end
  if frame > 40000 then print("no stuck state detected"); emu.stop(3) end
end, emu.eventType.endFrame)
