/* M4 ceiling probe driver: set up the boot RAM-clear hot loop in guest SRAM,
 * then hand off to the hand-asm interpreter (fastcore.s). Same program as
 * bench.c, so the KIPS numbers are directly comparable. */
#include <stdint.h>
#include "pdp11.h"

#define MB ((volatile uint16_t *)0x100)

/* PDP-11 register file the asm loop uses as its direct page (near = bank 0). */
uint16_t fastregs[16] __attribute__((section("fastregs")));
extern void fast_run(void);

static const uint16_t prog[] = {
    0012700, 0004000,     /* MOV #04000,R0  ($800) */
    0012706, 0020000,     /* MOV #020000,R6 ($2000) */
    0005020,              /* CLR (R0)+ */
    0020006,              /* CMP R0,R6 */
    0103775,              /* BCS loop (offset -3) */
    0000770,              /* BR 0 (offset -8) */
};

void host_putchar(int c) { (void)c; }
int host_pollchar(void) { return -1; }
unsigned host_clock60(void) { return 0; }
void host_panic(const char *m) { (void)m; MB[0] = 0xDEAD; for (;;) ; }
void trace_instr(uint16_t pc, uint16_t i) { (void)pc; (void)i; }

int main(void) {
    int i;
    for (i = 0; i < (int)(sizeof prog / 2); i++) guest_wr16(i * 2, prog[i]);
    for (i = 0; i < 16; i++) fastregs[i] = 0;
    fastregs[7] = 0;                      /* PC (DP $0E) */
    fastregs[8] = 0340;                   /* PS (DP $10): kernel, pri 7 */
    MB[0] = 0xB0B0;
    fast_run();                           /* never returns */
    return 0;
}
