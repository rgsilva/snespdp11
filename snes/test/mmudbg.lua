-- Focused MMU-on smoke test: MOV #123456,@#020000 with kernel page 1 mapped to
-- phys 040000. Confirms the asm core's MMU-on translation relocates the store.
local frame = 0
local function w(a) return emu.read16(a, emu.memType.snesWorkRam) end
emu.addEventCallback(function()
  frame = frame + 1
  local magic = w(0x100)
  if magic == 0xCAFE then
    local a, b, r7 = w(0x110), w(0x112), w(0x116)
    print(string.format("phys040000=%06o (want 0123456)  phys020000=%06o (want 0)  R7=%06o", a, b, r7))
    emu.stop((a == 42798 and b == 0) and 0 or 1)   -- 0123456 octal == 42798
  elseif magic == 0xDEAD then
    print("PANIC"); emu.stop(2)
  elseif frame > 600 then
    print("timeout"); emu.stop(3)
  end
end, emu.eventType.endFrame)
