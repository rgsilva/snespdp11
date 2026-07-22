-- Measure interpreter KIPS: sample icount over frames, report instr/sec.
local frame = 0
local samples = {}
emu.addEventCallback(function()
  frame = frame + 1
  if frame == 300 or frame == 600 or frame == 900 then
    local ic = emu.read16(0x102, emu.memType.snesWorkRam) +
               emu.read16(0x104, emu.memType.snesWorkRam) * 65536
    samples[frame] = ic
    print(string.format("frame %d: icount=%d", frame, ic))
  end
  if frame == 900 then
    -- frames 300->900 = 600 frames = 10.0 emulated seconds
    local di = samples[900] - samples[300]
    print(string.format("KIPS = %.2f  (%.0f guest instr/emu-sec, %d instr in 10 emu-sec)",
      di / 10000.0, di / 10.0, di))
    emu.stop(0)
  end
end, emu.eventType.endFrame)
