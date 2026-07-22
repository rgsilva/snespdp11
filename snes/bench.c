/* KIPS benchmark: run the interpreter flat-out on the RAM-clear hot loop that
 * dominates early boot (CLR (R0)+; CMP R0,R6; BCS), no console, no dev_poll.
 * Mesen Lua reads icount at a fixed frame and computes instr/sec.
 *
 * Mailbox: $0100 magic 0xB0B0, $0102/4 icount lo/hi.
 */
#include <stdint.h>
#include <setjmp.h>
#include "pdp11.h"

#define MB ((volatile uint16_t *)0x100)

/* guest program at address 0: clear 0..020000, then restart forever */
static const uint16_t prog[] = {
    0012700, 0004000,     /* 0000: MOV #04000,R0  ($800, above this program) */
    0012706, 0020000,     /* 0004: MOV #020000,R6 ($2000) */
    0005020,              /* 0010: CLR (R0)+      */
    0020006,              /* 0012: CMP R0,R6      */
    0103775,              /* 0014: BCS 0010 (offset -3) */
    0000770,              /* 0016: BR 0000 (offset -8) */
};

void host_putchar(int c) { (void)c; }
int host_pollchar(void) { return -1; }
unsigned host_clock60(void) { return 0; }
void host_panic(const char *m) { (void)m; MB[0] = 0xDEAD; for (;;) ; }
void trace_instr(uint16_t pc, uint16_t i) { (void)pc; (void)i; }

int main(void) {
    int i, vec;
    cpu_reset();
    for (i = 0; i < (int)(sizeof prog / 2); i++) guest_wr16(i * 2, prog[i]);
    R[7] = 0;
    PS = 0340;

    MB[0] = 0xB0B0;
    vec = setjmp(cpu.trapjmp);
    if (vec) cpu_trap_entry(vec);
    for (;;) {
        cpu_step();
        if ((cpu.icount & 0x3FF) == 0) {
            MB[1] = (uint16_t)cpu.icount;
            MB[2] = (uint16_t)(cpu.icount >> 16);
        }
    }
}
