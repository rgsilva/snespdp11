CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c99

all: pdp11 test_cpu

pdp11: main.c cpu.c bus.c pdp11.h
	$(CC) $(CFLAGS) -o $@ main.c cpu.c bus.c

test_cpu: tests/test_cpu.c cpu.c bus.c pdp11.h
	$(CC) $(CFLAGS) -I. -o $@ tests/test_cpu.c cpu.c bus.c

test: test_cpu
	./test_cpu

clean:
	rm -f pdp11 test_cpu

.PHONY: all test clean
