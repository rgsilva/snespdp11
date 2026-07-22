-- Mesen2 testrunner: watch snespdp11 boot UNIX v6 on real SNES hardware.
-- Tails the transcript ring (address published in the mailbox). M3 Phase B
-- success bar = the guest reaches the '@' boot prompt (proves guest RAM in
-- SRAM, ROM-disk reads, and console->PPU all work end to end). Full boot to
-- '#' is ~11.4M guest instr => hours at the -O0 C rate; that's M4's job.
local frame = 0
local lastout = 0
local ringoff = nil
local line = ""
local all = ""

local function shot(name)
  local png = emu.takeScreenshot()
  local hex = png:gsub(".", function(c) return string.format("%02x", c:byte()) end)
  print("SHOT " .. name .. " " .. hex)
end

emu.addEventCallback(function()
  frame = frame + 1
  local magic = emu.read16(0x100, emu.memType.snesWorkRam)
  if magic == 0xDEAD then
    print("HOST PANIC; transcript:\n" .. all .. line)
    shot("panic.png"); emu.stop(2); return
  end
  if magic ~= 0xB007 then return end
  if not ringoff then
    local base = emu.read16(0x108, emu.memType.snesWorkRam) +
                 emu.read16(0x10A, emu.memType.snesWorkRam) * 65536
    ringoff = base - 0x7E0000
  end
  local out = emu.read16(0x106, emu.memType.snesWorkRam)
  while lastout ~= out do
    local c = emu.read(ringoff + (lastout % 1024), emu.memType.snesWorkRam)
    lastout = (lastout + 1) % 65536
    if c == 10 or c == 13 then
      if #line > 0 then print("TTY| " .. line); all = all .. line .. "\n" end
      line = ""
    elseif c >= 32 and c < 127 then
      line = line .. string.char(c)
    end
  end
  if all:find("@", 1, true) or line:find("@", 1, true) then
    print("SUCCESS: reached '@' boot prompt on SNES (guest RAM+ROM disk+console OK)")
    print("last line: [" .. line .. "]")
    shot("v6-atprompt.png"); emu.stop(0); return
  end
  if frame % 1800 == 0 then
    local ic = emu.read16(0x102, emu.memType.snesWorkRam) +
               emu.read16(0x104, emu.memType.snesWorkRam) * 65536
    print(string.format("progress frame=%d icount=%d (~%.0f%% to '@')",
      frame, ic, ic / 73750 * 100))
  end
  if frame > 40000 then
    print("did not reach '@' in budget; transcript:\n" .. all .. line)
    emu.stop(3)
  end
end, emu.eventType.endFrame)
