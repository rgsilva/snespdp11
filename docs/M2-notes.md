# M2 status — 2026-07-21 ✅

## Result: the SNES memory story is now REAL and MEASURED

Exit test (the "SNES-shaped rehearsal"): boot `/rk1unix` with **read-only root
+ CoW overlay** (`-r -o 64`) and **swap on rk1** — a full session (login,
mknod, 2× `ed`, 2× `cc`, run binaries, **3 concurrent 24 KB hogs forcing
swap**, sync) produced:

    [overlay: peak 27/64 dirty blocks (13,824 bytes), 0 dropped writes]
    base image md5 UNCHANGED  ·  rk1 swap ~20 KB written

IDEAS.md §1 *guessed* 16–32 K for the overlay; **measured worst case: 13.8 K.**
Idle boot+login session: 9 blocks (4.6 K). The APU-as-swap and ROM-as-root
designs both survive contact with reality.

## The kernel rebuild (the fun part)

`/rk1unix` was built **inside the emulator with the 1975 toolchain** —
`mkconf`, `ed`, `cc`, `as m40.s`, `ld` against dmr's prebuilt `lib1`/`lib2`,
following `/usr/sys/run` (dmr's own build script, on the image). Conf changes:
`swapdev {(0<<8)|1}` (rk1), `swplo 1`, `nswap 120` (120 blocks = 61.4 K = the
APU tier). Kernel: 27,112 bytes. Build session: ~45 M guest instructions.
SIMH boots the same kernel identically (oracle parity ✓).

Scripts: `build-rk1unix.script`, `hogtest2.script`, `ovtest.script`,
`master.script` (in scratchpad this session; the golden output is what ships).

## Golden image + mkrom

`tools/mkrom.py`: walks the v6 free-block chain, **zeroes all 888 free blocks**
(deterministic, compresses well, scrubs deleted-file remnants — release
hygiene), pads to 32 K banks. Result: **2 MB ROM, 64 banks**, fs uses 1552 KB.
Golden image: `roms/v6-golden.rom.gz` (714 K). It has `/unix` = the rk1-swap
kernel (short name for auto-boot) and `/dev/rk1` (so `ps` finds swap; without
it v6 `ps` prints "no swap device" — that confused us for a minute).

    gunzip -k roms/v6-golden.rom.gz
    dd if=/dev/zero of=rk1.dsk bs=512 count=121
    ./core-c/pdp11 -r -o 64 roms/v6-golden.rom rk1.dsk    # @unix → root

## Emulator changes

- **rk0 romdisk mode** (`-r`, pool size `-o N`): RO base + overlay pool
  (find/alloc per 512-byte block, base-block fill on first write, pool-full →
  RK error + host warning + dropped-write counter). Stats printed at exit.
  This is the exact algorithm the SNES port will use against cart ROM.
- **WAIT-state console pacing fix**: while the CPU idles in WAIT, dev_poll
  runs on wall-clock (usleep loop), so instruction-count pacing throttled
  input to ~2.5 chars/s. Now pacing is bypassed when waiting. (This was why
  long `ed` heredocs crawled.)

## Numbers worth keeping

| Thing | Value |
|---|---|
| Boot→login, rk1unix kernel | 12.29 M instr (stock rkunix: 13.09 M) |
| Overlay peak, heavy session | **27 blocks / 13.8 KB** |
| Overlay, boot+login only | 9 blocks / 4.6 KB |
| Swap actually written (3 hogs) | ~20 KB on rk1 |
| Kernel size (rk1-swap conf, rk-only devices) | 27,112 bytes |
| Root fs used | 1552 KB of 2 MB (888 free blocks scrubbed) |

## Deferred / notes for M3+

- **MAXMEM cap** (param.h, guards one-proc-bigger-than-swap): needs full
  ken/dmr recompile from source in-guest — sources are on the image
  (`/usr/sys/ken`, `/usr/sys/dmr`), `run` shows the recipe. Do during M3/M5
  hardening. Until then nothing stops a 62 K process from wedging swap.
- Overlay pool is a hard cap (no eviction possible — can't write back to ROM).
  Growth is only from *distinct* dirtied blocks; measured headroom is 2×+.
- Toolchain recon for M3: **emu8086/snes = WLA-DX** (`wla-65816` +
  `wla-spc700` via pvsneslib; SPC700 port-protocol precedent in
  `spc_midi.asm`/`spc_beep.asm`), **snesmac/snes = Calypsi C** (calypsi/ dir).
  So: M3 C-core via Calypsi (snesmac pattern), M4 asm core via WLA-DX
  (emu8086 pattern). Both in-house precedents.
