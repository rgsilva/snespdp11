# M0 + M1 status — 2026-07-21

## Result: ✅ M1 exit criteria met, same session as M0

The portable C core **boots real UNIX v6 to `login:`**, and a full scripted
session passes: `root` login (no password on this image), `ls -l`,
`ed hello.c` (write a C file), **`cc hello.c && a.out` → "hello from 1975"**,
`/usr/games` present (bj, chess, cubic, moo, ttt, wump). This clears the
bar we set *above* avr11 — their documented MMU bugs break large programs;
our `cc` (the real 1975 Ritchie compiler) runs clean.

**Measured: boot → `login:` = 13.09 M instructions.** IDEAS.md §1 estimated
5–15 M (→ 5–30 min at the SNES asm core's calibrated 15–35 KIPS). The estimate
holds: **~6–15 min SNES boot** projected.

## What was built (core-c/, tools/)

- `pdp11.h/cpu.c/bus.c/main.c` — PDP-11/40 + EIS, KT11-D MMU (no split I/D,
  no SR1 — v6's `backup()` copes, as on real /40 hardware), KL11 console,
  KW11-L clock, RK11 ×4 drives. ~1200 lines portable C99.
- `main.c` runner: raw-tty console (`^]` quits, prints icount), `-t`/`-l log`
  trace, `-s N` trace-from-icount, `-i N` instruction limit.
- `tests/test_cpu.c` — 68 hand-assembled checks: flag semantics (incl. the
  nasty ones: SBC carry, ROR V=N^C, DIV overflow/zero, ASH sign tracking),
  addressing side effects, MMU aborts (SR0 bits), MFPI/MTPI, RTI mode banking.
- `tools/v6fs.py` — read-only v6 filesystem walker (ls/cat/stat). This is the
  seed of M2's `mkrom`.
- `tools/mkboot.py` — installs `/usr/mdec/rkuboot` at block 0 (see below).
- `tools/ptydrive.py` — expect-style pty driver for scripted boots (works on
  our emulator and SIMH alike).

## The image story (important for M2)

TUHS `Dennis_v6/v6root.gz` has **no boot block** — block 0 is junk. Verified
by booting under SIMH (open-simh, built in scratchpad): identical double
fault. Fix: the real boot block is *inside* the fs at `/usr/mdec/rkuboot`
(a.out, strip 16-byte header, pad to 512). After installing it, **SIMH boots
the image to `login:` and so do we.** Pipeline:

    gunzip -k roms/v6root.gz
    python3 tools/mkboot.py v6root v6boot.dsk
    core-c/pdp11 v6boot.dsk        # type: rkunix ↵ … root

Boot dialogue: `@` prompt wants **CR** (`\r`) as terminator, kernel name is
`rkunix`. The image also carries `unix`/`rpunix`/`hpunix` kernels and full
`/usr/source` is on the separate v6src image.

## Bugs found on the way (all fixed, all now covered by tests or boot)

1. **High-branch decode**: BPL..BCS dispatch used case values 0400..0407 where
   `(op>>8)` actually yields 0200..0207 → BCS/BHI never branched → boot
   loader's copy loop fell through. (Caught by tracing the boot block.)
2. **MEMWORDS off by 2×** (0100000 = 32 K words, not 64 K) → host buffer
   overflow (SIGBUS) when the kernel loaded above 64 KB. Caught with ASan.
3. Unit-test vector errors of mine (octal arithmetic), fixed after the core
   proved right: ROR's V=N^C, 90000 in octal, XOR operand, PDR ACF encodings.

## Honest gaps (M1-hardening backlog, do before/during M3)

- **SIMH-lockstep JSON vectors NOT built yet.** Current confidence comes from
  68 unit checks + booting the same image SIMH boots + cc/ed/games running.
  The lockstep differ (PLAN §M1) is still the right hardening step.
- MAINDEC diagnostics not run.
- Console input quirk: the first char sent immediately after `login:` appears
  gets eaten (fine with ≥0.3 s settle). Suspect guest-side tty open/flush
  timing, not proven — investigate during M3 console work.
- KW11 ticks at host-real 60 Hz; dilation knob (PLAN M3) not yet implemented.
- `ttt` (tic-tac-toe) starts; not fully play-tested. chess/bj untested.

## Perf note (desktop, M4 Max, -O2)

Boot's 13.09 M instructions complete in well under a second of CPU time —
the C core is comfortably >50 M instr/s on the host, so all desktop-side
work (M2 image prep, lockstep runs) is free. The SNES port is where the
15–35 KIPS reality lives.
