/* SRAM probe: how much distinct cart SRAM does the emulator actually back?
 * (snespico8 got burned assuming the header request was honored — verify.)
 * HiROM SRAM: banks $20-$3F, window $6000-$7FFF = 8 KB each, 256 KB max.
 * Writes a distinct tag per window, then verifies — mirrors collapse to the
 * last-written tag and fail the check, so n = genuinely distinct windows.
 * Mailbox: MB[0]=0xCAFE done, MB[1]=live windows, MB[2]=KB.
 */
#include <stdint.h>

#define MB ((volatile uint16_t *)0x100)

int main(void) {
    int i, n = 0;
    for (i = 0; i < 32; i++) {
        volatile uint16_t *p = (volatile uint16_t *)(((uint32_t)(0x20 + i) << 16) | 0x6000);
        p[0] = (uint16_t)(0xA500 | i);
        p[1] = (uint16_t)~(0xA500 | i);
        p[0x0FFE] = (uint16_t)(0x5A00 | i);       /* window end too */
    }
    for (i = 0; i < 32; i++) {
        volatile uint16_t *p = (volatile uint16_t *)(((uint32_t)(0x20 + i) << 16) | 0x6000);
        if (p[0] == (uint16_t)(0xA500 | i) &&
            p[1] == (uint16_t)~(0xA500 | i) &&
            p[0x0FFE] == (uint16_t)(0x5A00 | i))
            n++;
    }
    MB[1] = (uint16_t)n;
    MB[2] = (uint16_t)(n * 8);
    MB[0] = 0xCAFE;
    for (;;) ;
}
