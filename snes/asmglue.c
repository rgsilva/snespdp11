/* M4 asm core glue. cpu_step() runs the hand-asm interpreter (fastcore.s) for
 * the opcodes it implements and falls back to the proven C core (c_step) for
 * everything else. Because unhandled instructions fall back, the 68-check
 * suite stays green while the asm core grows opcode-group by opcode-group.
 *
 *   fast_step() returns 0 when it fully executed the instruction, or
 *   ST_FALLBACK (0xFFFF) when it declined: MMU on, PC odd / in the I/O
 *   region, or an opcode it does not yet handle. On fallback the C register
 *   globals (R[], PS, SR0) are left untouched, so c_step re-runs the
 *   instruction from clean state.
 */
#include <stdint.h>
#include "pdp11.h"

#define ST_FALLBACK 0xFFFFu
extern uint16_t fast_step(void);

/* The asm core's PDP-11 register file, used as its direct page (D -> fastregs).
 * Bank-0 near RAM (own linker section). Layout mirrors fastcore.s:
 *   [0..7]=R0..R7  [8]=PS  [9]=SR0  [10..15]=scratch */
uint16_t fastregs[16] __attribute__((section("fastregs")));

void cpu_step(void) {
    if (fast_step() == ST_FALLBACK) c_step();
}

/* The asm core raises PDP-11 traps (odd address, NXM) through here, which
 * longjmps back to the run loop's setjmp exactly like the C core does. */
void asm_trap(int vec) { cpu_trap(vec); }
