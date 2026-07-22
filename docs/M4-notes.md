# M4 — asm hot loop: the ceiling is proven — 2026-07-22

## Result: hand-asm interpreter is ~12x the C core

Measured on the boot RAM-clear hot loop (`CLR (R0)+; CMP R0,R6; BCS`), same
program for both, Mesen testrunner:

| Core | KIPS | notes |
|---|---|---|
| C interpreter, Calypsi -O0 | **0.92** | baseline |
| C interpreter, Calypsi -O2 | 0.92 | **identical to -O0** — C opt can't help |
| **hand-asm, regs in direct page** | **10.96** | `snes/fastcore.s`, ~12x |

The asm core runs the loop correctly and indefinitely (icount linear:
54272 -> 109568 -> 163840 over frames 300/600/900).

## Why C can't win and asm can

The C `cpu` register file is a global struct in **far** memory (bank $7E), so
every guest instruction pays far loads/stores for R0..R7/PS that no optimizer
can eliminate — hence -O2 == -O0 exactly. The asm core puts the PDP-11
register file in the 65816 **direct page** (D -> `fastregs`, a bank-0 near
global), so register access is 3-4 cycles instead of far 6+, and there is no
per-op C call/return overhead. That structural change is the whole win.

## What this does to boot time

Real boot adds console/clock overhead (measured ~0.65 KIPS for the C boot),
but the interpreter core is the dominant cost. Scaling by the 12x:

| | C -O0 | asm (this probe) | asm + full opt (proj.) |
|---|---|---|---|
| boot -> '@' (73.7 K instr) | ~2.5 min | **~12 s** | few s |
| boot -> shell '#' (11.36 M) | ~6 h | **~30 min** | 5-15 min |

**~30 min lands inside the PLAN's 5-30 min target** — and this probe still has
obvious headroom: the memory helpers are `jsl`/`rtl` calls (a full-bank compute
every access), dispatch is a 5-way compare chain, and `op` is re-loaded from DP
several times. The real integrated core will use a jump table and inline the
common addressing modes, pushing toward the 15-35 KIPS band's upper end.

## fastcore.s — what it is

A minimal but *real* interpreter (not hardcoded): fetches the opcode from guest
SRAM, decodes by mask, dispatches, executes, with correct PDP-11 flag semantics
(incl. CMP's C = borrow = !hwcarry). Covers exactly the hot-loop opcodes (MOV
imm->reg, CLR (Rn)+, CMP reg,reg, BCS, BR) plus general SRAM addressing
(bank $20+(A>>13), off $6000|(A&$1FFF)). Unhandled opcodes trip a 0xDEAD
mailbox halt — which is how the mis-assembled bench program got caught (the C
core silently executed the garbage; the asm core stopped on it).

Build/run: `make -C snes benchfast && make -C snes test-benchfast`.

## Next (M4 continuation): the integrated core

- Grow fastcore.s to full opcode + addressing-mode coverage; jump-table
  dispatch; inline the hot memory path.
- MMU-on translation in asm (fast path: cache the current PAR).
- C fallback (`jsl` into the existing handlers) for rare ops: EIS, MFPI/MTPI,
  traps, RESET. Spill DP regs -> `cpu` struct at the boundary, reload on return.
- Wire into machine.c: replace the C `cpu_step` inner loop with `fast_run`,
  keep C for the cold paths. Re-run the 68 unit checks + full boot on Mesen.
