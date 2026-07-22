# snespdp11 SNES-side build. Calypsi C -> 65816, same vendored toolchain as
# snespico8/snesmac. Phase A: the 68-check CPU test suite runs ON the SNES
# (mailbox at $0100 polled by Mesen Lua). Phase B: full v6 boot.
#
# -O0 for the core: the Calypsi correctness baseline (snespico8/snesmac
# precedent). M4 replaces the hot loop with WLA-DX asm for performance anyway.
CALDIR ?= ../../snespico8/tools/calypsi-65816-5.17
CC := $(CALDIR)/bin/cc65816
AS := $(CALDIR)/bin/as65816
LN := $(CALDIR)/bin/ln65816
MESEN := /Applications/Mesen.app/Contents/MacOS/Mesen
BUILD := build

MODEL := --code-model=large --data-model=large
# cputest build: 32 KB guest RAM, tiny overlay/input buffers, no file backend
CORE_DEFS := -DPDP11_SNES -DPDP11_NOFILE -DMEMWORDS=040000u -DOV_CAP=4 -DINQ_SIZE=512
CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul -O0 -I../core-c \
         $(CORE_DEFS) --list-file=$(@:.o=.lst)
AFLAGS := --core=65816 --target=SNES $(MODEL)
LNFLAGS := calypsi/linker.scm --output-format=raw --rtattr exit=simplified \
           $(CALDIR)/lib/clib-lc-ld-snes-noppu.a

.PHONY: cputest test-cputest sramprobe test-sramprobe boot test-boot bench bench-o2 benchfast test-bench test-bench-o2 test-benchfast clean

cputest: $(BUILD)/cputest.sfc

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/header.o: calypsi/header.s | $(BUILD)
	$(AS) -o $@ $< $(AFLAGS)

$(BUILD)/cpu.o: ../core-c/cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/cpu.c $(CFLAGS)

$(BUILD)/bus.o: ../core-c/bus.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/bus.c $(CFLAGS)

$(BUILD)/test_cpu.o: ../core-c/tests/test_cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/tests/test_cpu.c $(CFLAGS)

$(BUILD)/cputest.raw: $(BUILD)/header.o $(BUILD)/cpu.o $(BUILD)/bus.o $(BUILD)/test_cpu.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/cputest.map $^

$(BUILD)/cputest.sfc: $(BUILD)/cputest.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-cputest: $(BUILD)/cputest.sfc
	$(MESEN) --testrunner $(BUILD)/cputest.sfc test/cputest.lua

# ---- M4: the 68-check suite through the hand-asm core (fastcore.s) ----
# Same test_cpu.c, but cpu_step() is the asm interpreter with C fallback
# (asmglue.c). -DPDP11_ASM makes cpu.c drop its cpu_step so asmglue owns it.
ASM_DEFS := $(CORE_DEFS) -DPDP11_RAM_SRAM -DPDP11_ASM
ASM_CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul -O0 -I../core-c \
             $(ASM_DEFS) --list-file=$(@:.o=.lst)

asmcputest: $(BUILD)/asmcputest.sfc

$(BUILD)/cpu-asm.o: ../core-c/cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/cpu.c $(ASM_CFLAGS)
$(BUILD)/bus-asm.o: ../core-c/bus.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/bus.c $(ASM_CFLAGS)
$(BUILD)/test_cpu-asm.o: ../core-c/tests/test_cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/tests/test_cpu.c $(ASM_CFLAGS)
$(BUILD)/asmglue.o: asmglue.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ asmglue.c $(ASM_CFLAGS)

$(BUILD)/asmcputest.raw: $(BUILD)/header.o $(BUILD)/cpu-asm.o $(BUILD)/bus-asm.o \
                         $(BUILD)/asmglue.o $(BUILD)/fastcore.o $(BUILD)/test_cpu-asm.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/asmcputest.map $^

$(BUILD)/asmcputest.sfc: $(BUILD)/asmcputest.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-asmcputest: $(BUILD)/asmcputest.sfc
	$(MESEN) --testrunner $(BUILD)/asmcputest.sfc test/cputest.lua

# focused MMU-on debug harness
$(BUILD)/mmudbg.o: mmudbg.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ mmudbg.c $(ASM_CFLAGS)
$(BUILD)/mmudbg.raw: $(BUILD)/header.o $(BUILD)/cpu-asm.o $(BUILD)/bus-asm.o \
                     $(BUILD)/asmglue.o $(BUILD)/fastcore.o $(BUILD)/mmudbg.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/mmudbg.map $^
$(BUILD)/mmudbg.sfc: $(BUILD)/mmudbg.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@
test-mmudbg: $(BUILD)/mmudbg.sfc
	$(MESEN) --testrunner $(BUILD)/mmudbg.sfc test/mmudbg.lua

sramprobe: $(BUILD)/sramprobe.sfc

$(BUILD)/sramprobe.o: sramprobe.c | $(BUILD)
	$(CC) -o $@ $< $(CFLAGS)

$(BUILD)/sramprobe.raw: $(BUILD)/header.o $(BUILD)/sramprobe.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/sramprobe.map $^

$(BUILD)/sramprobe.sfc: $(BUILD)/sramprobe.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-sramprobe: $(BUILD)/sramprobe.sfc
	$(MESEN) --testrunner $(BUILD)/sramprobe.sfc test/sramprobe.lua

# ---- Phase B: the real thing — boot UNIX v6 ----
BOOT_DEFS := -DPDP11_SNES -DPDP11_RAM_SRAM -DPDP11_DISK_ROM -DOV_CAP=48 -DINQ_SIZE=512 \
             -DRK_BUSY_POLLS=2 -DRX_PACE=2
BOOT_CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul -O0 -I../core-c               $(BOOT_DEFS) --list-file=$(@:.o=.lst)

boot: $(BUILD)/snespdp11.sfc

$(BUILD)/cpu-boot.o: ../core-c/cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/cpu.c $(BOOT_CFLAGS)

$(BUILD)/bus-boot.o: ../core-c/bus.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/bus.c $(BOOT_CFLAGS)

$(BUILD)/console.o: console.c console.h font8x8_basic.h | $(BUILD)
	$(CC) -o $@ console.c $(BOOT_CFLAGS)

$(BUILD)/machine.o: machine.c console.h ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ machine.c $(BOOT_CFLAGS)

$(BUILD)/boot.raw: $(BUILD)/header.o $(BUILD)/cpu-boot.o $(BUILD)/bus-boot.o $(BUILD)/console.o $(BUILD)/machine.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/boot.map $^

$(BUILD)/v6rom.bin: ../roms/v6-golden.rom.gz | $(BUILD)
	gunzip -c $< > $@

$(BUILD)/snespdp11.sfc: $(BUILD)/boot.raw $(BUILD)/v6rom.bin tools/raw2sfc.py
	python3 tools/raw2sfc.py $(BUILD)/boot.raw $@ --at 0x100000 $(BUILD)/v6rom.bin

test-boot: $(BUILD)/snespdp11.sfc
	$(MESEN) --testrunner $(BUILD)/snespdp11.sfc test/boot.lua

# ---- M4: KIPS benchmark (flat-out interpreter on the boot hot loop) ----
BENCH_DEFS := -DPDP11_SNES -DPDP11_RAM_SRAM -DPDP11_DISK_ROM -DOV_CAP=4 -DINQ_SIZE=64
define BENCH_CFLAGS
--core=65816 --target=snes $(MODEL) --no-ppu-mul $(1) -I../core-c $(BENCH_DEFS)
endef

bench: $(BUILD)/bench-o0.sfc
bench-o2: $(BUILD)/bench-o2.sfc

$(BUILD)/cpu-b%.o: ../core-c/cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/cpu.c --core=65816 --target=snes $(MODEL) --no-ppu-mul -$* -I../core-c $(BENCH_DEFS)
$(BUILD)/bus-b%.o: ../core-c/bus.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/bus.c --core=65816 --target=snes $(MODEL) --no-ppu-mul -$* -I../core-c $(BENCH_DEFS)
$(BUILD)/bench-b%.o: bench.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ bench.c --core=65816 --target=snes $(MODEL) --no-ppu-mul -$* -I../core-c $(BENCH_DEFS)

$(BUILD)/bench-o0.raw: $(BUILD)/cpu-bO0.o $(BUILD)/bus-bO0.o $(BUILD)/bench-bO0.o $(BUILD)/header.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/bench-o0.map $^
$(BUILD)/bench-o2.raw: $(BUILD)/cpu-bO2.o $(BUILD)/bus-bO2.o $(BUILD)/bench-bO2.o $(BUILD)/header.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/bench-o2.map $^

$(BUILD)/bench-o0.sfc $(BUILD)/bench-o2.sfc: %.sfc: %.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-bench: bench
	$(MESEN) --testrunner $(BUILD)/bench-o0.sfc test/bench.lua

# ---- M4: asm ceiling probe (fastcore.s, regs in direct page) ----
BF_CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul -O2 -I../core-c $(BENCH_DEFS)
benchfast: $(BUILD)/benchfast.sfc
$(BUILD)/fastcore.o: fastcore.s | $(BUILD)
	$(AS) -o $@ fastcore.s $(AFLAGS)
$(BUILD)/bus-bf.o: ../core-c/bus.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/bus.c $(BF_CFLAGS)
$(BUILD)/cpu-bf.o: ../core-c/cpu.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ ../core-c/cpu.c $(BF_CFLAGS)
$(BUILD)/benchfast.o: benchfast.c ../core-c/pdp11.h | $(BUILD)
	$(CC) -o $@ benchfast.c $(BF_CFLAGS)
$(BUILD)/benchfast.raw: $(BUILD)/header.o $(BUILD)/fastcore.o $(BUILD)/bus-bf.o $(BUILD)/cpu-bf.o $(BUILD)/benchfast.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/benchfast.map $^
$(BUILD)/benchfast.sfc: $(BUILD)/benchfast.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@
test-benchfast: benchfast
	$(MESEN) --testrunner $(BUILD)/benchfast.sfc test/bench.lua
test-bench-o2: bench-o2
	$(MESEN) --testrunner $(BUILD)/bench-o2.sfc test/bench.lua

clean:
	rm -rf $(BUILD)
