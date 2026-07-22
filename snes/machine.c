/* SNES machine glue: host hooks + run loop. Boots UNIX v6 single-user.
 *
 * Memory map (measured, see docs/M3-notes.md):
 *   guest 128 KB  -> cart SRAM banks $20-$2F (bus.c PDP11_RAM_SRAM)
 *   v6 root image -> cart ROM at $D00000 (raw2sfc --at 0x100000), RO + overlay
 *   rk1 swap      -> 62 KB WRAM buffer, bank $7F (bus.c PDP11_DISK_ROM;
 *                    the SPC700/APU-RAM version replaces that buffer later)
 *   console       -> Mode 5 64x28 (console.c)
 *
 * Mailbox for the Mesen Lua harness (low WRAM, reserved in linker.scm):
 *   $0100 magic: 0xB007 running, 0xDEAD host panic
 *   $0102/$0104 guest icount lo/hi   $0106 output count
 *   $0108/$010A 24-bit address of the 1 KB output transcript ring
 *
 * Boot input is canned: "unix\r" at the '@' prompt; switch register 0173030
 * makes v6 init go single-user (no login). When the first '#' prompt appears
 * we type a small demo. Real keyboard input is a later milestone.
 */
#include <stdint.h>
#include <setjmp.h>
#include "pdp11.h"
#include "console.h"

#define MB ((volatile uint16_t *)0x100)

extern int cpu_rtt_flag;

static uint8_t outring[1024];
static uint16_t outidx;
static const char *feed = "unix\r";
static uint8_t seen_hash;

void host_putchar(int c) {
    con_putc(c);
    outring[outidx & 1023] = (uint8_t)c;
    outidx++;
    MB[3] = outidx;
    if (c == '#' && !seen_hash) {         /* first shell prompt: run the demo */
        seen_hash = 1;
        feed = "stty -lcase\rls -l /usr/games\r";
    }
}

int host_pollchar(void) {
    if (feed && *feed) return *feed++;
    return -1;
}

unsigned host_clock60(void) {
    /* KW11 dilation: dev_poll runs every 256 guest instructions; tick every
     * 8th call -> one guest clock tick per ~2048 instructions. Guest wall
     * clock runs slow; v6 doesn't mind. */
    static uint8_t n;
    return (++n & 7) ? 0 : 1;
}

void host_panic(const char *msg) {
    MB[0] = 0xDEAD;
    con_puts("\nHOST PANIC: ");
    con_puts(msg);
    for (;;) con_flush();
}

void trace_instr(uint16_t pc, uint16_t instr) { (void)pc; (void)instr; }

int main(void) {
    uint32_t i;
    int vec, trapdepth = 0;

    con_init();
    con_puts("snespdp11 - PDP-11/40 running UNIX V6 (1975)\r\n");
    con_puts("guest RAM 128K in cart SRAM, root filesystem in cart ROM\r\n\r\n");

    MB[4] = (uint16_t)(uint32_t)outring;
    MB[5] = (uint16_t)((uint32_t)outring >> 16);
    MB[0] = 0xB007;

    sw_reg = 0173030;                     /* v6 init: single-user */
    rk_set_overlay(48);                   /* 24 KB CoW pool (13.8 K measured peak) */
    cpu_reset();

    for (i = 0; i < 0200000; i += 2)      /* stale-SRAM insurance: zero guest RAM */
        guest_wr16(i, 0);
    for (i = 0; i < 256; i++)             /* boot: rk0 block 0 -> guest 0 */
        guest_wr16(i * 2, *(const uint16_t *)(0xD00000ul + i * 2));
    cpu.R[7] = 0;
    cpu.PS = 0340;

    for (;;) {
        vec = setjmp(cpu.trapjmp);
        if (vec) {
            if (++trapdepth > 3) host_panic("trap loop");
            cpu_trap_entry(vec);
        }
        for (;;) {
            int iv;
            if (cpu.halted) host_panic("HALT");
            if (dev_irq((cpu.PS >> 5) & 7, &iv)) cpu_trap_entry(iv);
            if (cpu.waiting) {
                dev_poll();
                con_flush();
                continue;
            }
            {
                int tbit = cpu.PS & PSW_T;
                cpu_step();
                trapdepth = 0;
                if (tbit && !cpu_rtt_flag) cpu_trap_entry(V_BPT);
                cpu_rtt_flag = 0;
            }
            if ((cpu.icount & 0377) == 0) {
                dev_poll();
                con_flush();
                MB[1] = (uint16_t)cpu.icount;
                MB[2] = (uint16_t)(cpu.icount >> 16);
                MB[6] = cpu.R[7];
                MB[7] = cpu.PS;
            }
        }
    }
}
