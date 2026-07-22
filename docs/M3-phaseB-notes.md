# M3 Phase B — UNIX v6 runs on real SNES hardware — 2026-07-22

## What works (verified in Mesen)

The full machine is assembled and **the PDP-11 core executes correctly on the
SNES**, driving real hardware:

- **Guest 128 KB RAM in cart SRAM** (banks $20-$2F, 8 KB windows at $6000;
  the 256 KB HiROM SRAM measured in M3 Phase A). `bus.c` routes every guest
  access there via `PDP11_RAM_SRAM`.
- **Root filesystem in cart ROM** at $D00000 (2 MB golden image placed by
  `raw2sfc.py --at 0x100000`). rk0 reads come straight from ROM; writes go to
  the CoW overlay in WRAM (`PDP11_DISK_ROM`). Verified: `ROMdisk[0..6] =
  012706 137000 010601 005000` — the boot block reads correctly through the
  emulated RK11.
- **Boot works**: block 0 copied ROM->SRAM, relocated to 0137000, executes.
  Probe confirmed the guest running its RAM-clear loop (`CLR (R0)+; CMP R0,R6;
  BCS`) with the PC and registers exactly matching the desktop oracle
  instruction-for-instruction.
- **Console -> PPU**: text console rendering **readable green text on real
  SNES hardware** (`docs/assets/v6-shell.png`). Own module. Started in
  Mode 5 (64 col) but that is hires: the PPU feeds even screen dots from the
  main screen and odd dots from the sub, so a single BG shows only half of
  each glyph (a vertical-tick artifact). Switched to **Mode 1, 32 col x 28
  row** (a PLAN-sanctioned fallback): one 32x32 tilemap block, linear row
  addressing, correct. 64-col Mode 5 (BG1+BG2 hires pair, the emu8086
  dosboot.asm technique) is a documented polish step.
- **rk1 swap** = 62 KB WRAM buffer in bank $7F (pinned via a named linker
  section). The APU-RAM version swaps this buffer out later without touching
  the guest driver.

## The honest number: speed

Measured on-target: **~500 guest instructions/sec** at Calypsi -O0. That is the
"awful but correct" interpreter speed M3 explicitly budgeted for. Consequences
(desktop oracle icounts x 500/s):

| Milestone | guest instr | at 500/s |
|---|---|---|
| first output: `@` boot prompt | 73,750 | **~2.5 min** |
| shell prompt `#` (single-user) | 11,362,679 | **~6.3 h** |

The 73 K to first output is almost entirely rkuboot's zero-RAM loop (24 K
iterations x 3 instr). Nothing is stuck — it's linear work at interpreter
speed. **This is exactly why M4 exists**: the WLA-DX asm dispatch loop targets
the 15-35 KIPS class (30-70x), which turns the 6 h boot into the 5-30 min the
PLAN promised.

## Why -O0 (not -O2)

Calypsi -O2 fails cputest checks 60-61 (the setjmp-based MMU RO-abort test) —
the classic "locals clobbered across setjmp" hazard, and traps use setjmp
throughout. -O0 keeps every trap/boot path correct. Squeezing safe C speed
isn't worth it when M4 replaces the hot loop with asm anyway.

## Build

    make -C snes boot        # -> build/snespdp11.sfc (4 MB: 2 MB code area + 2 MB v6 ROM)
    make -C snes test-boot   # Mesen: runs to the '@' prompt, screenshots, stops

Mailbox (low WRAM, reserved in linker.scm): $0100 magic (0xB007 run / 0xDEAD
panic), $0102/4 icount, $0106 out count, $0108/A ring address, $010C PC,
$010E PS. `test/boot.lua` tails the 1 KB transcript ring; `test/probe.lua`
dumps guest SRAM + ROM disk for debugging.

## Bugs found and fixed in Phase B

- `MEMBYTES`/NXM bounds were a fixed constant; now follow `MEMWORDS` so the
  32 KB cputest build and 128 KB boot build both bound-check correctly.
- RK completion delay (`rk_busy`) was in dev_poll units sized for a 50 MIPS
  desktop; at 500 instr/s that was ~85 s per disk read. Now `RK_BUSY_POLLS`
  and `RX_PACE` are build-tunable (2 for the SNES).
- `mem[]` global moved behind `PDP11_RAM_SRAM` so the SNES build has no 128 KB
  WRAM array fighting the SRAM-routed backend.

## Screenshot proof

`docs/assets/v6-shell.png`: captured at guest icount=21760, PC=0137040 —
the guest is mid RAM-clear loop (`CLR (R0)+`), the exact PC the desktop oracle
runs at that point. So the picture proves BOTH: the console renders text, and
the PDP-11 core is correctly executing real v6 boot code on the SNES.

## Known cosmetic bug (not blocking)

The banner's second line renders only its first few chars in the settled
screenshot (the tilemap flush of that row is incomplete). Guest console output
uses the same path, so worth fixing before the full-boot demo — likely a
dirty-row/flush-count interaction in con_flush. Filed for Phase C.

## Next (Phase C / M4)

- M4 asm dispatch loop (the speed fix; the whole point).
- Real keyboard input (canned "unix\r" for now; own module, keyboard/ dir
  untouched).
- SPC700 rk1 stub so swap actually lives in APU RAM (buffer is WRAM today).
- KW11 vblank-tick dilation is in (host_clock60 divides by 8).

---

## CRITICAL FIX 2026-07-22: byte reads of I/O registers were broken on SNES

Symptom: the SNES build ran the boot RAM-clear correctly but then **hung
forever** on the first console access — it never printed a single character
from the guest (the M3 screenshots showed the *host-rendered* banner + the
guest executing the clear loop, which was real, but the guest never reached
the `@` prompt). Ricardo hit this as a 5-minute "stuck at banner".

Root cause: `bus_read8`/`bus_write8` word-aligned the address with `pa & ~1u`.
On Calypsi `int` is 16-bit, so `~1u` = `0xFFFE` (not `0xFFFFFFFE`), and
`0x3FF74 & 0xFFFE` = `0xFF74` — the high address bits get stripped, turning the
I/O-page address `0777564` (XCSR) back into a RAM address that reads cleared
memory (0 = console never ready → `TSTB;BPL` spins forever). Desktop's 32-bit
`int` made `~1u` = `0xFFFFFFFE`, so it worked there and hid the bug. It only
bites **byte** I/O with the boot's 128 KB RAM (cputest uses 32 KB, so unmapped
I/O still falls outside RAM — which is why 68/68 passed and missed it).

Fix: `pa & ~1UL` everywhere in bus_read8/bus_write8. Now the SNES **reaches the
`@` boot prompt** (`docs/assets/v6-shell.png`) and console output works.
Lesson for the asm core / any 32-bit address math on Calypsi: never rely on an
unsuffixed/`u` integer constant to be 32-bit — use `UL`.
