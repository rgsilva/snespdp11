local frame, lastout, ringoff, line, all = 0, 0, nil, "", ""
emu.addEventCallback(function()
  frame = frame + 1
  local magic = emu.read16(0x100, emu.memType.snesWorkRam)
  if magic == 0xDEAD then print("HOST PANIC\n"..all..line); emu.stop(2); return end
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
  if all:find("mem =",1,true) then print("REACHED KERNEL (mem=) — MMU path OK"); emu.stop(0); return end
  if frame % 6000 == 0 then
    print(string.format("frame=%d ic=%d out=%d PC=%06o line=[%s]", frame,
      emu.read16(0x102,emu.memType.snesWorkRam)+emu.read16(0x104,emu.memType.snesWorkRam)*65536,
      out, emu.read16(0x10C,emu.memType.snesWorkRam), line))
  end
end, emu.eventType.endFrame)
