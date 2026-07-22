-- Diagnostic: raw dumps of the mailbox + ring head while booting.
local frame = 0
local function hexdump(off, n)
  local s = ""
  for i = 0, n - 1 do
    s = s .. string.format("%02x", emu.read(off + i, emu.memType.snesWorkRam))
    if i % 2 == 1 then s = s .. " " end
  end
  return s
end
emu.addEventCallback(function()
  frame = frame + 1
  if frame % 60 == 0 and frame <= 7200 then
    print("frame " .. frame)
    print("  MB 0100: " .. hexdump(0x100, 16))
    print("  MB 0110: " .. hexdump(0x110, 16))
    print("  pre-ring 1ff0: " .. hexdump(0x1ff0, 20))
    print("  ring 2004: " .. hexdump(0x2004, 48))
    local t = ""
    for i = 0, 47 do
      local c = emu.read(0x2004 + i, emu.memType.snesWorkRam)
      t = t .. ((c >= 32 and c < 127) and string.char(c) or ".")
    end
    print("  ring text: [" .. t .. "]")
  end
  if frame > 7260 then emu.stop(0) end
end, emu.eventType.endFrame)
