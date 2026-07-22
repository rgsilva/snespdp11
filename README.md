# snespdp11 — UNIX v6 on a Super Nintendo (PDP-11/40 emulator)

Third entry in the "what else can a SNES run" series (emu8086, snespico8,
snesmac). Goal: boot **real 1975 UNIX v6** on a stock SNES, root filesystem in
cart ROM, swap in the audio coprocessor's RAM. Yes, really. See `PLAN.md`.

## Status

| Milestone | Status |
|---|---|
| M0 scaffold + v6 images + references | ✅ 2026-07-21 |
| M1 portable C core boots v6 on desktop | ✅ 2026-07-21 — login, `ed`, **`cc`**, games all work (`docs/M1-notes.md`) |
| M2 guest image pipeline (rk1-swap conf, mkrom) | — |
| M3 SNES port (C core, Mode 5 console, APU-swap) | — |
| M4 65816 asm dispatch loop (15–35 KIPS target) | — |
| M5 demo + free-ROM release | — |

Measured: boot → `login:` = **13.09 M instructions** → projected **6–15 min**
on the SNES asm core. `docs/first-boot-transcript.txt` is the receipt.

## Quickstart (desktop core)

```sh
cd core-c && make && make test
gunzip -k roms/v6root.gz && mv roms/v6root v6root.dsk
python3 tools/mkboot.py v6root.dsk v6boot.dsk   # TUHS image lacks a boot block
./core-c/pdp11 v6boot.dsk
# at '@' type: rkunix ⏎   (CR matters) — login: root — ^] quits
```

## Layout

- `core-c/` — portable C99 PDP-11/40 + EIS + KT11-D MMU, KL11/KW11/RK11;
  `tests/` 68 hand-assembled instruction checks
- `tools/` — `v6fs.py` (v6 fs walker → future mkrom), `mkboot.py`,
  `ptydrive.py` (scripted boots)
- `roms/` — TUHS images (Dennis v6root/v6src, Wellsch tape), Caldera-licensed
- `snes/` — (M3+) 65816 port; console UI will be its own module
- `docs/` — milestone notes

UNIX v6 is distributed under the 2002 Caldera "Ancient UNIX" license — it can
ship in the free public ROM.
