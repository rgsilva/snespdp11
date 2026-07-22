-- Boot to the shell and capture a screenshot of it working (the demo runs
-- "ls -l /usr/games"). Prints the PNG as hex ("SHOT <hex>") to decode to a file.
local frame, lastout, ringoff, line, all = 0, 0, nil, "", ""
local wait_after = -1
local function shot(name)
  local png = emu.takeScreenshot()
  local hex = png:gsub(".", function(c) return string.format("%02x", c:byte()) end)
  print("SHOT " .. name .. " " .. hex)
end
emu.addEventCallback(function()
  frame = frame + 1
  local magic = emu.read16(0x100, emu.memType.snesWorkRam)
  if magic == 0xDEAD then print("PANIC\n"..all); emu.stop(2); return end
  if magic ~= 0xB007 then return end
  if not ringoff then
    ringoff = emu.read16(0x108, emu.memType.snesWorkRam) +
              emu.read16(0x10A, emu.memType.snesWorkRam)*65536 - 0x7E0000
  end
  local out = emu.read16(0x106, emu.memType.snesWorkRam)
  while lastout ~= out do
    local c = emu.read(ringoff + (lastout % 1024), emu.memType.snesWorkRam)
    lastout = (lastout + 1) % 65536
    if c==10 or c==13 then if #line>0 then print("TTY| "..line); all=all..line.."\n" end; line=""
    elseif c>=32 and c<127 then line=line..string.char(c) end
  end
  -- the demo's "ls -l /usr/games" is echoed (uppercase) reliably; shoot a while
  -- after so any listing has rendered too.
  if wait_after < 0 and all:upper():find("GAMES", 1, true) then
    wait_after = frame + 1500
    print("saw the ls command; will shoot after it runs")
  end
  if wait_after > 0 and frame >= wait_after then
    print("FULL BOOT: v6 booted to the shell in asm; transcript:\n"..all)
    shot("v6-shell.png"); emu.stop(0); return
  end
  if frame % 12000 == 0 then
    print(string.format("frame=%d ic=%d line=[%s]", frame,
      emu.read16(0x102,emu.memType.snesWorkRam)+emu.read16(0x104,emu.memType.snesWorkRam)*65536, line))
  end
end, emu.eventType.endFrame)
