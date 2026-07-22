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

.PHONY: cputest test-cputest clean

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

clean:
	rm -rf $(BUILD)
