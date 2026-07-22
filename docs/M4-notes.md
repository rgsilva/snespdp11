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

---

## Update 2026-07-22: the compiler-qualifier shortcut is a DEAD END

Idea: skip hand-asm by moving the register file to the direct page via Calypsi
type qualifiers, letting the C compiler emit DP-fast register access (attacking
the proven far-register bottleneck) while keeping 100% of the C correctness
(MMU, EIS, traps). Refactored R[]/PS/SR0 out of the `Cpu` struct into standalone
globals for this. Result: **both Calypsi qualifiers are broken for this use.**

| Qualifier | Outcome |
|---|---|
| `__tiny` (direct page) | **compiler crash**: "Non-exhaustive patterns in mem8Reg" |
| `__near` (bank 0, 16-bit) | **miscompiles**: 37-56 cputest failures (flags read 0) |

Both *looked* fast on the bench (1.84 KIPS, 2x) — but the bench only checks that
icount advances, not correctness, so it was measuring broken execution. The
cputest 68-check suite on the SNES is what caught it. Desktop always passed
because the qualifiers are `#define`d empty off Calypsi.

**Conclusion: there is no compiler shortcut.** DP-register speed must come from
hand-asm managing the 65816 D register itself (which is exactly what the
fastcore.s ceiling probe already does, and why it hit 12x). The register file
is now cleanly extracted as plain globals `R[]/PS/SR0` (kept — good groundwork
for the asm boundary), correctness re-verified 68/68 desktop + SNES.

## The remaining M4 build (full integrated asm core)

Scoped for a dedicated effort (large; slow Mesen validation cycles):
1. Grow fastcore.s from the 5-opcode probe to full coverage: all double-op
   (MOV/CMP/BIT/BIC/BIS/ADD/SUB +byte), single-op (CLR..ASL/SWAB/SXT/ADC/SBC),
   all 8 addressing modes ×byte/word, branches, JMP/JSR/RTS/SOB.
2. MMU translation in asm: read SR0; if on, read par/pdr from the `cpu` struct
   via long addressing (rare-ish; a cached-PAR fast path helps).
3. C fallback via `jsl`: EIS (MUL/DIV/ASH/ASHC), MFPI/MTPI, EMT/TRAP/RTI/RTT/
   HALT/WAIT/RESET, traps. Spill the asm DP regs -> `R[]/PS` globals at the
   boundary, reload on return (the plain-global extraction makes this a memcpy).
4. Wire into machine.c: replace the C `cpu_step` inner loop with `fast_run`;
   return to C for interrupts/trap dispatch. Re-run the 68 checks THROUGH the
   asm path and boot v6, comparing icount/PC trajectory to the desktop oracle.

---

## Update 2026-07-22 (evening): integrated asm core — the whole common path is in asm

Built the real integrated core in `snes/fastcore.s`, validated by the CPU suite
run THROUGH the asm path (`make -C snes asmcputest && test-asmcputest`, Mesen).

### The seam (this is what makes it safe to build incrementally)
`cpu.c`'s `cpu_step` became `c_step` — the proven C interpreter, now the
**fallback**. `asmglue.c` provides the real `cpu_step`: call `fast_step()`; if it
returns `ST_FALLBACK` (0xFFFF), run `c_step()`. Because a fallback **never**
writes the asm's DP registers back to the C globals (`k_syncout` only runs on the
handled path), any partial asm side effects vanish and `c_step` re-runs the
instruction from pristine state. So the asm handles only clean **main-RAM,
non-trapping** instructions and bails everything exotic — **MMU on, odd/NXM/I-O
address, or an unimplemented opcode** — to C. The only guest-memory *write* is the
final store, after every operand is confirmed clean RAM (`k_wr16/k_wr8` also
refuse to write once the bail flag is set). Net: 73/73 at every step; opcode
groups moved C→asm one at a time, each proven live by injecting a deliberate bug
and watching exactly the right checks fail.

### Coverage now in asm (the entire common instruction path)
- Fetch + decode + all 8 addressing modes (`k_getea`, incl. immediate/absolute/
  index and the deferred forms) ×byte/word, with autoinc/dec side effects.
- Double-op: MOV CMP BIT BIC BIS ADD SUB (`k_subf` shares CMP/SUB flag logic).
- Single-op: CLR COM INC DEC NEG ADC SBC TST ROR ROL ASR ASL (`k_setv_nxc` =
  V=N^C for shifts).
- Branches: BR + all 14 Bxx (`k_bcond` uses the class/sense encoding trick).
- **The whole boot RAM-clear hot loop (`CLR (R0)+; CMP R0,R6; BCS; BR`) is asm.**

Registers live in the DP (`fastregs`, 32 words); `k_syncin/k_syncout` copy
`R[]/PS` to/from the C globals per step. Helpers are `jsl`/`rtl` (bank-safe).
Backend unified: `test_cpu.c` uses `guest_rd16/wr16`, and `asmcputest` uses the
SRAM backend, so the suite exercises the exact memory path boot uses.

### Still C-fallback (correct, not yet fast)
MMU-on translation, all cold ops (EIS, MFPI/MTPI, JSR/RTS/JMP/SOB, MARK, SWAB,
CLx/SEx, EMT/TRAP/RTI/RTT/HALT/WAIT/RESET), and traps. Fine for correctness.

### Remaining for the boot speedup (next)
1. **MMU-on translation in asm** — today MMU-on bails to C, and v6 runs MMU-on
   for most of the boot, so this is the gating item for real speedup.
2. **Batched `fast_run` loop** — today `fast_step` syncs DP↔globals every
   instruction (fine for the suite, ~11M syncs kills boot speed); keep regs
   DP-resident across a run, sync only at the dev_poll/fallback boundary.
3. **Wire into the boot build** (add fastcore.o+asmglue.o, `-DPDP11_ASM`) and
   measure `@`/`#` timings vs the C core. NB: wiring it in *before* step 1 would
   make boot *slower* (asm overhead on top of the C fallback for MMU-on), so do
   1+2 first.
