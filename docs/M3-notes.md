# M3 status — Phase A done 2026-07-21

## Phase A: the core is correct ON the SNES ✅

`snes/` builds the untouched `core-c/` with **Calypsi 5.17** (vendored in
snespico8, snesmac's exact Makefile shape, `-O0` = the correctness baseline
per snespico8/snesmac precedent) into a 128 KB HiROM .sfc. The 68-check CPU
suite runs on-console with results in a **mailbox at WRAM $0100** polled by
Mesen Lua (`make -C snes test-cputest`):

    RESULT checks=68 failures=0

First run was 68/2: **`(int8_t)` casts of out-of-range values behave
differently on Calypsi than clang.** MOVB-to-register sign extension failed —
and the *same cast pattern* sat in `op_branch`, i.e. every backward branch
would have silently broken (the unit suite only branches forward; the boot
loader is what needs backward BCS — we'd have "mystery-hung" in Phase B).
Both replaced with explicit bit math. This is exactly why Phase A runs the
suite on-target before attempting boot.

Build facts: setjmp/longjmp supported by Calypsi clib ✓ (trap machinery
unchanged). Core builds with `-DPDP11_SNES -DPDP11_NOFILE -DMEMWORDS=040000
-DOV_CAP=4 -DINQ_SIZE=512` (32 KB guest RAM for the test ROM; bus NXM bounds
now follow MEMWORDS). Mesen path: `--testrunner rom.sfc script.lua`.

## Phase B plan (next)

1. **SRAM probe ROM first** (the snespico8 trap): header requests 256 K;
   snesmac's HiROM header claims Mesen backs ≥128 K — verify with a Lua probe.
   If true, the guest's full 128 K lives in cart SRAM and WRAM is all host.
2. Guest-memory routing: replace flat `mem[]` with WRAM/SRAM-routed access
   (bus.c already funnels everything through bus_read16/write16).
3. rk0 from ROM: embed `roms/v6-golden.rom` via `raw2sfc.py --at 0x100000`
   (→ $D00000, fixed far pointer); overlay pool already written + measured.
4. Mode 5 64×28 console module (own module — keyboard/ and basic-rom/ stay
   untouched) + canned boot input ("unix\r", "root\r") — real input later.
5. SPC700 rk1 stub (crib emu8086's spc_midi/spc_beep port protocol).
6. KW11 dilation: tick from NMI/vblank counter, ~10–15 Hz guest.
7. Exit: single-user `#` (or login) in Mesen, however slow.

## Numbers

- cputest.sfc: 128 KB (code+tests); full suite ≈ 2080 frames ≈ 35 s emulated.
  (Not a KIPS measure — dominated by the 34 × 32 KB memset in the harness.
  A dedicated bench ROM comes with the M4 baseline.)
