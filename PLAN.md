# snespdp11 — UNIX v6 on the SNES (PDP-11/40) — Build Plan

*Authored 2026-07-21. Evaluation basis: `../IDEAS.md` §0 (constraint physics), §1 (PDP-11
entry), §5 (calibrated re-check). This file is the handoff doc for the build session(s).*

---

## 0. Why this is feasible (recap + new evidence)

- **Calibrated speed:** emu8086's asm core measures **0.05–0.1× an IBM XT ≈ 15–35 KIPS**
  (`emu8086/docs/FUTURE.md`). v6 boot ≈ 5–15 M instructions → **~5–30 min boot**, then an
  interactive-if-leisurely shell. Asm dispatch loop is **mandatory** (plain C several× slower).
- **Calibrated RAM:** guest core ≈ **150–160 K** of the 192 K fast pool after ~20–40 K host
  state + 16–32 K CoW overlay (emu8086 precedent: guest sees 108/172 K of 192 K). v6 minimum
  config ≈ 128 K → clears the bar, tight but real.
- **External proof (new):** [avr11](https://github.com/davecheney/avr11) boots real v6 on a
  **16 MHz 8-bit Atmega2560** — "at least 10× slower than an original 11/40" and still usable
  ([write-up](https://dave.cheney.net/2014/01/23/avr11-simulating-minicomputers-on-microcontrollers)).
  It derives from Julius Schmidt's pdp11.js and is **WTFPL-licensed** (vendorable, zero friction).
  A [Cardputer port](https://github.com/alvaroramosf/cardputer-PDP11) boots v6 *and* RT-11.
  The SNES is squarely in this device class.
- **Architecture synergy:** the PDP-11 is a 16-bit **little-endian** machine — exactly the
  65816's native word format. No segmentation, no prefixes, tiny orthogonal ISA (vs the 8086
  we already emulate). The 15–35 KIPS calibration is closer to a floor than a ceiling here.
  (Cost to watch: PDP-11 sets NZVC flags on nearly every instruction, including MOV.)
- **Licensing:** v6 is under the 2002 Caldera "Ancient UNIX" license → redistributable in the
  **free public ROM** (unlike PC-DOS; note in the `make release` variant matrix).

## 1. Locked design decisions

| Decision | Choice | Why |
|---|---|---|
| Machine | **PDP-11/40** + EIS + KT11-D MMU | Simpler MMU: no split I/D, no supervisor mode; historically ran v6 + `cc`; 64 K/proc cap pairs with the ~62 K swap tier (IDEAS §5.4) |
| EIS | In from day 1 (MUL/DIV/ASH/ASHC + SOB/XOR/SXT/RTT/MARK) | v6 `cc` output uses mul/div |
| FP11 | **Omitted** | Core utilities don't use floating point; float-using programs relied on software simulation on real /40s anyway. Deal with it if something traps. |
| Guest physical RAM | **128 K** to start (classic /40 v6 config), stretch to ~152 K if measured host state allows | Budget is 150–160 K; more guest RAM = colder swap |
| Devices | KL11 console, KW11-L line clock, **RK11 with two drives** | Minimal stock v6 boot set — no custom guest drivers at all |
| Root FS | **rk0** = ~2.5 MB RK05 image in cart **ROM** + host-side **CoW overlay** (16–32 K of the RW pool) for dirty blocks | ROM is read-only; a text session dirties few blocks |
| Swap | **rk1** = fake ~61 K RK05 backed by **APU RAM** (SPC700 stub + 4-port block protocol) | Guest uses the *stock* RK driver; host maps rk1 I/O to APU transfers. Volatile + slow (~30–60 KB/s → 1–2 s per swap-out) is fine for scratch. Swap-to-ROM is impossible. |
| Kernel conf | Rebuild v6 conf: `swapdev`=rk1, `swplo`=0, `nswap`≈120 blocks; cap per-process user core just below nswap | One swapped image must always fit (IDEAS §5.4 caveat) |
| Console | **Mode 5, 64 cols × 28 rows @ 8 px font**, own new module | 512 px ÷ 8 = 64 natural fit; **do not touch `keyboard/` or `basic-rom/` dirs** (reuse the approach, not the code) |
| Clock | KW11 tick tied to vblank, **dilated** (start ~10–15 Hz guest-side) | At 20 KIPS a real 60 Hz tick could eat ~30% of the instruction budget; v6 doesn't mind slow wall-clock |
| Test oracle | **SIMH lockstep** + DEC MAINDEC /40 diagnostics | No prefab vector suite exists like the 8088 v2 JSONs — so generate our own JSON vectors *from SIMH once*, then the workflow feels identical to snes86/snesmac M1 |
| **Toolchain (locked, Ricardo 2026-07-21)** | **M3 = Calypsi C at -O0** (vendored 5.17 from snespico8, the correctness baseline) → **M4 = rewrite the hot dispatch loop in WLA-DX 65816 asm for performance** | Correct first, fast second. Same two-step as snesmac (Calypsi) and emu8086 (WLA asm core + SPC700 port-protocol precedent) |

## 2. Milestones

### M0 — Scaffold ✅ (done 2026-07-21)
- Create `snespdp11/` layout mirroring snesmac: `core-c/`, `snes/`, `tools/`, `docs/`, `roms/`.
- Fetch v6 RK05 images (TUHS dist tape or [rsc/unix](https://github.com/rsc/unix)); verify they
  boot under SIMH (that's our oracle baseline).
- Skim avr11 + pdp11.js + cardputer sources. **Recommendation: write our own core in portable
  C** (the proven snes86 workflow; avr11 has known MMU bugs with larger programs) and keep
  those three as reference implementations, license-clean to crib from (WTFPL).

### M1 — CPU core in portable C, desktop-first *(long pole #1)* ✅ (done 2026-07-21)
*Result: boots v6 to `login:`; ed/cc/games verified; **13.09 M instr to login** —
see `docs/M1-notes.md`. Deferred to hardening: SIMH-lockstep vectors, MAINDEC.
Image gotcha: TUHS Dennis image ships without a boot block — `tools/mkboot.py`
installs `/usr/mdec/rkuboot` at block 0 (verified against SIMH).*
- Base ISA + EIS + traps/PSW/interrupt priorities + KT11-D MMU (8 kernel + 8 user PAR/PDR).
- Vector harness: generate JSON instruction vectors from SIMH; lockstep-diff regs/flags/memory.
  MAINDEC CPU exerciser as acceptance — condition codes are where emulator bugs hide.
- C-side devices: KL11 on stdio, RK11 on image files, KW11 on a timer.
- **Exit: boots stock v6 to `login:` on desktop; `ed`, `cc hello.c`, a /usr/games title all work.**
  (Bar is *above* avr11: MMU correct under big programs, `cc` completes.)

### M2 — Guest image pipeline ✅ (done 2026-07-21)
*Result: `/rk1unix` (swapdev=rk1, nswap=120) rebuilt in-guest with the 1975
toolchain; swap-on-rk1 proven (3-hog stress, ~20 K written); overlay measured
**peak 27 blocks / 13.8 K, 0 drops** on a heavy session; `mkrom.py` scrubs
free blocks → 2 MB/64-bank golden ROM (`roms/v6-golden.rom.gz`); SIMH parity ✓.
Deferred: MAXMEM cap (needs full in-guest kernel recompile). `docs/M2-notes.md`.*
- Under SIMH: `mkconf` + edit conf for the rk1-swap layout; rebuild kernel; trim root FS;
  single-user-friendly `/etc/rc`.
- `tools/mkrom`: pack the RK05 image into ROM banks + block index; define the CoW overlay
  format (fixed pool of 512-byte dirty blocks, ~32–64 entries; overflow → console warning).
- **Exit: custom image boots under SIMH and under the M1 core identically.**

### M3 — SNES port, C core first (slow but true) — *Phase A ✅ 2026-07-21*
*Phase A (core-on-console proof): Calypsi build of the untouched core compiles
first try; **all 68 CPU checks pass under Mesen** (mailbox harness at $0100,
`make -C snes test-cputest`). Portability kill: `(int8_t)` casts differ between
clang and Calypsi — would have broken every backward branch; replaced with bit
math. Phase B = guest-RAM routing (WRAM/SRAM), ROM-rk0 via `raw2sfc --at`,
Mode 5 console, SPC700 rk1 stub, canned boot input → `#` in Mesen.*
- Compile core-c for the 65816 (match whichever toolchain emu8086's snes/ build uses — check
  in-session). Map guest 128 K across WRAM + cart SRAM; **verify the ~64 K Mesen SRAM cap
  first thing** (the snespico8 trap).
- SPC700 stub exposing APU RAM as rk1's backing store over the 4-port handshake.
- New Mode 5 console module + keyboard input (own module).
- Clock dilation knob wired from day 1.
- **Exit: boots to single-user `#` in Mesen.** Expect awful speed (C interp maybe 3–10 KIPS →
  boot could take an hour+); that's fine, it's a correctness milestone.

### M4 — 65816 asm dispatch loop *(long pole #2, mandatory)*
- Port emu8086's threaded-jump-table pattern (DBR=$7E etc.) to PDP-11 decode: top-nibble
  primary dispatch + shared addressing-mode helpers — the PDP-11's regular
  `op|mode/reg|mode/reg` encoding suits table-driven asm even better than x86 did.
- MMU fast path: cache per-page PAR translation; invalidate on PAR/PDR write or mode switch.
- **Exit: ≥15–35 KIPS class → boot in 5–30 min, keystroke echo instant, `ls` in seconds.**

### M5 — Polish, demo, release
- Boot UX: auto-type `rkunix` at the `@` prompt, motd, optional login bypass.
- Demo script: login → `ls -l` → `ed` a file → play wump/moo/ttt → `who`/`date` →
  `cc hello.c && a.out` as the endurance stunt.
- Add to the `make release` free-ROM matrix (v6 = Caldera-licensed, ships in the public ROM).
- **Stretch trophy: boot RT-11 on the same emulated hardware** (different disk image, same core).

## 3. Risks → mitigations

- **MMU correctness** (avr11's documented weak spot) → SIMH lockstep + MAINDEC *early*, in M1,
  not discovered on-console in M3.
- **Interrupt overhead at low KIPS** → clock dilation knob from day 1; measure ISR cost.
- **Swap tier only ~61 K vs 64 K/proc worst case** → conf caps user core below nswap (locked above).
- **Overlay pool overflow** (write-heavy session) → fixed pool + loud console warning; revisit
  only if it actually bites.
- **SRAM cap / mapper variance in Mesen** → verified first thing in M3, before layout hardens.

## 4. Open questions for the build session
1. Own core vs adapt avr11/cardputer — recommendation above says *own*, but skim their MMU
   and trap code first; steal tests and tricks (WTFPL allows it; check cardputer's license).
2. Exact WRAM/SRAM split for the guest 128 K + where host state lives (measure, don't guess).
3. Toolchain: match emu8086's snes/ build (whatever hosts its asm core pattern).
4. Guest clock rate: how slow can KW11 tick before v6 scheduling feels broken?

## 5. References
- [avr11 (GitHub, WTFPL)](https://github.com/davecheney/avr11) ·
  [avr11 write-up](https://dave.cheney.net/2014/01/23/avr11-simulating-minicomputers-on-microcontrollers) ·
  [cardputer-PDP11](https://github.com/alvaroramosf/cardputer-PDP11) ·
  [rsc/unix (v6 images + tools)](https://github.com/rsc/unix)
- SIMH (oracle), TUHS archive (v6 dist tapes), PDP-11/40 Processor Handbook (ISA/MMU truth),
  **Lions' Commentary on UNIX v6** — the map of the kernel; invaluable when boot hangs.
