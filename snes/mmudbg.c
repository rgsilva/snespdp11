/* Focused MMU-on debug: run MOV #123456,@#020000 with kernel identity map
 * (page 1 -> phys 040000), MMU on, then report what happened. */
#include <stdint.h>
#include <setjmp.h>
#include "pdp11.h"
#define MB ((volatile uint16_t *)0x100)
void host_putchar(int c) { (void)c; }
int host_pollchar(void) { return -1; }
unsigned host_clock60(void) { return 0; }
void host_panic(const char *m) { (void)m; MB[0] = 0xDEAD; for (;;) ; }
void trace_instr(uint16_t p, uint16_t i) { (void)p; (void)i; }

int main(void) {
    int pg, i, vec;
    cpu_reset();
    { uint32_t a; for (a = 0; a < 0100000; a += 2) guest_wr16(a, 0); }  /* zero RAM */
    guest_wr16(001000, 0012737);   /* MOV #123456,@#020000 */
    guest_wr16(001002, 0123456);
    guest_wr16(001004, 0020000);
    guest_wr16(001006, 0000000);   /* HALT */
    for (pg = 0; pg < 8; pg++) { cpu.par[0][pg] = (uint16_t)(pg * 0200); cpu.pdr[0][pg] = 077406; }
    cpu.par[0][1] = 0400;          /* page 1 -> phys 040000 */
    cpu.par[0][7] = 07600;
    R[7] = 001000; R[6] = 0700; PS = 0340; SR0 = 1;

    for (i = 0x20; i < 0x40; i++) MB[i] = 0;   /* clear debug slots */
    MB[0] = 0xB0B0;
    vec = setjmp(cpu.trapjmp);
    if (vec) { MB[10] = (uint16_t)vec; MB[0] = 0xCAFE; for (;;) ; }  /* trapped */
    for (i = 0; i < 6; i++) { if (cpu.halted) break; cpu_step(); }
    MB[8] = guest_rd16(040000);    /* expect 0123456 */
    MB[9] = guest_rd16(020000);    /* expect 0 */
    MB[11] = R[7];
    MB[12] = SR0;
    MB[0] = 0xCAFE;
    for (;;) ;
}
