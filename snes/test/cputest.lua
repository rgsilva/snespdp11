-- Mesen2 testrunner: poll the cputest mailbox at WRAM $0100.
-- MB[0]=$0100 magic (0xCAFE done, 0xDEAD panic), MB[1]=$0102 checks,
-- MB[2]=$0104 failures, MB[3]=$0106 live progress counter.
local frame = 0

emu.addEventCallback(function()
  frame = frame + 1
  local magic = emu.read16(0x100, emu.memType.snesWorkRam)
  if magic == 0xCAFE then
    local checks = emu.read16(0x102, emu.memType.snesWorkRam)
    local fails = emu.read16(0x104, emu.memType.snesWorkRam)
    print(string.format("RESULT checks=%d failures=%d (frame %d)", checks, fails, frame))
    for i = 1, math.min(fails, 8) do
      print(string.format("  fail #%d at check %d, got %06o",
        i, emu.read16(0x110 + 2*(i-1), emu.memType.snesWorkRam),
        emu.read16(0x120 + 2*(i-1), emu.memType.snesWorkRam)))
    end
    emu.stop(fails == 0 and 0 or 1)
  elseif magic == 0xDEAD then
    print("PANIC in core (host_panic hit)")
    emu.stop(2)
  elseif frame % 600 == 0 then
    print(string.format("progress checks=%d frame=%d",
      emu.read16(0x106, emu.memType.snesWorkRam), frame))
  end
  if frame > 18000 then
    print("TIMEOUT after 5 min; progress checks=" ..
      emu.read16(0x106, emu.memType.snesWorkRam))
    emu.stop(3)
  end
end, emu.eventType.endFrame)
