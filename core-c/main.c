/* Desktop runner: boots an RK05 image (block 0 → address 0) and wires the
 * console to the host terminal. Usage:
 *   pdp11 [-t] [-l trace.log] [-i maxinstr] rk0.dsk [rk1.dsk ...]
 * Interactive quit: ^] (like telnet). Non-tty stdin (pipes/pty scripts) works.
 */
#include "pdp11.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/select.h>

extern int cpu_rtt_flag;

static struct termios tio_saved;
static int tty_raw;
static FILE *tracef;
static unsigned long long maxinstr;
static unsigned long long tracestart;

static void tty_restore(void) {
    if (tty_raw) tcsetattr(0, TCSANOW, &tio_saved);
}

void host_putchar(int c) {
    if (c == 0) return;
    fputc(c, stdout);
    fflush(stdout);
}

int host_pollchar(void) {
    static int gate;
    if (++gate < 32 && !cpu.waiting) return -1;   /* don't select() every call */
    gate = 0;
    fd_set fds;
    struct timeval tv = {0, 0};
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    if (select(1, &fds, 0, 0, &tv) <= 0) return -1;
    unsigned char c;
    ssize_t n = read(0, &c, 1);
    if (n <= 0) return -1;                        /* EOF: stop feeding */
    if (tty_raw && c == 0x1d) {                   /* ^] quit */
        tty_restore();
        fprintf(stderr, "\n[pdp11: quit after %llu instructions]\n", cpu.icount);
        exit(0);
    }
    return c;
}

unsigned host_clock60(void) {
    static long long last;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = (long long)ts.tv_sec * 60 + (ts.tv_nsec * 60LL) / 1000000000LL;
    if (!last) last = now;
    unsigned d = (unsigned)(now - last);
    last = now;
    return d;
}

void host_panic(const char *msg) {
    tty_restore();
    fprintf(stderr, "\n[pdp11 PANIC: %s] PC=%06o PS=%06o icount=%llu\n",
            msg, cpu.R[7], cpu.PS, cpu.icount);
    exit(1);
}

void trace_instr(uint16_t pc, uint16_t instr) {
    if (!tracef || cpu.icount < tracestart) return;
    fprintf(tracef,
        "%9llu %06o %06o  R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
        cpu.icount, pc, instr, cpu.R[0], cpu.R[1], cpu.R[2], cpu.R[3],
        cpu.R[4], cpu.R[5], cpu.R[6], cpu.PS);
}

static void run(void) {
    static int trapdepth;
    int vec = setjmp(cpu.trapjmp);
    if (vec) {
        if (++trapdepth > 3) host_panic("trap loop (double fault)");
        cpu_trap_entry(vec);
    }
    for (;;) {
        if (cpu.halted) return;
        int iv;
        if (dev_irq((cpu.PS >> 5) & 7, &iv)) cpu_trap_entry(iv);
        if (cpu.waiting) {
            dev_poll();
            if (cpu.waiting) usleep(200);
            continue;
        }
        int tbit = cpu.PS & PSW_T;
        cpu_step();
        trapdepth = 0;
        if (tbit && !cpu_rtt_flag) cpu_trap_entry(V_BPT);
        cpu_rtt_flag = 0;
        if ((cpu.icount & 0377) == 0) dev_poll();
        if (maxinstr && cpu.icount >= maxinstr) {
            tty_restore();
            fprintf(stderr, "\n[pdp11: instruction limit %llu reached] PC=%06o\n",
                    maxinstr, cpu.R[7]);
            exit(2);
        }
    }
}

int main(int argc, char **argv) {
    int i, ndisk = 0;
    const char *disk0 = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) { tracef = stderr; trace_on = 1; }
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) {
            tracef = fopen(argv[++i], "w");
            trace_on = 1;
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            maxinstr = strtoull(argv[++i], 0, 0);
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            tracestart = strtoull(argv[++i], 0, 0);
            if (!tracef) { tracef = stderr; trace_on = 1; }
        } else {
            if (rk_attach(ndisk, argv[i]) < 0) {
                fprintf(stderr, "pdp11: cannot open %s\n", argv[i]);
                return 1;
            }
            if (!ndisk) disk0 = argv[i];
            ndisk++;
        }
    }
    if (!ndisk) {
        fprintf(stderr, "usage: pdp11 [-t] [-l log] [-i maxinstr] rk0.dsk [rk1 ...]\n");
        return 1;
    }

    cpu_reset();

    /* boot: block 0 of rk0 → memory 0, start at 0 (what the boot ROM did) */
    {
        uint8_t blk[512];
        FILE *f = fopen(disk0, "rb");
        if (!f || fread(blk, 1, 512, f) != 512) {
            fprintf(stderr, "pdp11: cannot read boot block\n");
            return 1;
        }
        fclose(f);
        for (i = 0; i < 256; i++)
            mem[i] = (uint16_t)(blk[i * 2] | (blk[i * 2 + 1] << 8));
    }
    cpu.R[7] = 0;
    cpu.PS = 0340;

    if (isatty(0)) {
        tcgetattr(0, &tio_saved);
        struct termios t = tio_saved;
        cfmakeraw(&t);
        tcsetattr(0, TCSANOW, &t);
        tty_raw = 1;
        atexit(tty_restore);
        fprintf(stderr, "[pdp11: ^] to quit]\r\n");
    }

    run();

    tty_restore();
    fprintf(stderr, "\n[pdp11: HALT at PC=%06o after %llu instructions]\n",
            cpu.R[7], cpu.icount);
    return 0;
}
