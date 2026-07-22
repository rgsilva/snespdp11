# snespdp11 — project goal & status

## Goal

Boot **real 1975 UNIX v6** on a stock Super Nintendo by emulating a **PDP-11/40**
(the machine v6 shipped on) in 65816 code. The guest's 128 KB of core lives in
cart SRAM, the root filesystem in cart ROM, and swap in the audio
coprocessor's RAM. It's the third entry in the "what else can a SNES run"
series (emu8086, snespico8, snesmac).

## Status

| Milestone | Status |
|---|---|
| **M0** scaffold + v6 images + references | ✅ 2026-07-21 |
| **M1** portable C core boots v6 on desktop | ✅ 2026-07-21 — login, `ed`, **`cc hello.c`**, games all work (`M1-notes.md`). Boot → `login:` measured at 13.09 M instructions. |
| **M2** guest image pipeline (rk1-swap kernel, mkrom, CoW overlay) | ✅ 2026-07-21 — kernel rebuilt in-guest with the 1975 toolchain; overlay peak **13.8 K** measured (`M2-notes.md`) |
| **M3** SNES port | ✅ Phase A (all CPU checks pass on SNES) + Phase B (v6 boots on real SNES: guest RAM in SRAM, root in ROM) + **64-col Mode 5 console** (`console-notes.md`) |
| **M4** 65816 asm core | 🔨 **the hand-asm PDP-11 core boots UNIX v6 to the `#` shell on real SNES hardware** at ~2× the C core. Full opcode set + all addressing modes + **MMU translation** run in asm, with the proven C core as a fallback for cold ops; **80/80** CPU checks. Remaining: a batched run loop for more speed, and an on-screen keyboard so you can type. (`M4-notes.md`) |
| **M5** demo + free-ROM release | — |

### How the M4 asm core works (one paragraph)

`snes/fastcore.s` is a hand-written 65816 interpreter that keeps the PDP-11
registers in the 65816 direct page and executes the common instruction path
(all double- and single-operand ops, all branches, all 8 addressing modes, and
KT11-D MMU translation) directly. Anything it doesn't implement — EIS, traps,
device I/O, MMU aborts — cleanly *falls back* to the original C core (`c_step`),
so correctness is inherited from the proven interpreter while the hot path runs
in asm. It is validated instruction-for-instruction against the desktop C core
(the oracle) by an 80-check suite run on the SNES, and end-to-end by booting v6.

## Layout

- `core-c/` — portable C99 PDP-11/40 + EIS + KT11-D MMU, KL11/KW11/RK11;
  `tests/` hand-assembled instruction checks (the correctness oracle)
- `snes/` — the 65816 port: `fastcore.s` (asm core), `asmglue.c` (asm↔C seam),
  `console.c` (Mode 5 64-col console), `machine.c` (host glue / run loop),
  `bus.c`/`cpu.c` reused from `core-c/`, `test/*.lua` (Mesen harnesses)
- `tools/` — `v6fs.py` (v6 fs walker), `mkboot.py`, `mkrom.py`, `ptydrive.py`
  (scripted boots)
- `roms/` — TUHS v6 images (Dennis v6root/v6src, Wellsch tape), Caldera-licensed
- `docs/` — milestone notes (`M1`–`M4`), `console-notes.md`, and `assets/`

## Toolchain

The desktop core is plain C99 (any compiler). The SNES build uses the
[Calypsi](https://www.calypsi.cc/) 65816 C compiler + assembler (proprietary,
free for personal/hobby use), the same toolchain as snespico8/snesmac. UNIX v6
is distributed under the 2002 Caldera "Ancient UNIX" license and can ship in a
free public ROM.
