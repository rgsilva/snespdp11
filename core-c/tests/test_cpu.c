/* Hand-assembled unit tests for the /40 core. Focus: flag semantics, EIS edge
 * cases, addressing side effects, traps, MMU — the places emulators rot.
 * Octal literals throughout, PDP-11 style.
 */
#include "pdp11.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int cpu_rtt_flag;

/* host hooks (no real console/clock in tests) */
void host_putchar(int c) { (void)c; }
int host_pollchar(void) { return -1; }
unsigned host_clock60(void) { return 0; }
void host_panic(const char *m) { fprintf(stderr, "PANIC %s\n", m); exit(9); }
void trace_instr(uint16_t pc, uint16_t i) { (void)pc; (void)i; }

static int failures, checks;

#define BASE 001000

static void load(const uint16_t *w, int n) {
    cpu_reset();
    memset(mem, 0, sizeof mem);
    for (int i = 0; i < n; i++) mem[(BASE >> 1) + i] = w[i];
    cpu.R[7] = BASE;
    cpu.R[6] = 0700;                 /* small kernel stack below the code */
    cpu.PS = 0340;
}

/* run until HALT or limit; returns 0 on HALT, trap vector if unexpected trap
 * reached an uninitialized (zero) vector, -1 on limit */
static int run(int limit) {
    int vec = setjmp(cpu.trapjmp);
    if (vec) {
        /* vectored trap: if the test set up a handler, follow it */
        if (mem[vec >> 1] == 0) return vec;
        cpu_trap_entry(vec);
    }
    while (limit-- > 0) {
        if (cpu.halted) return 0;
        int iv;
        if (dev_irq((cpu.PS >> 5) & 7, &iv)) cpu_trap_entry(iv);
        if (cpu.waiting) return -2;
        int tbit = cpu.PS & PSW_T;
        cpu_step();
        if (tbit && !cpu_rtt_flag) cpu_trap_entry(V_BPT);
        cpu_rtt_flag = 0;
    }
    return -1;
}

static void chk(const char *name, int cond) {
    checks++;
    if (!cond) { failures++; printf("FAIL: %s\n", name); }
}
static void chkeq(const char *name, uint32_t got, uint32_t want) {
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s  got=%06o want=%06o\n", name, got, want);
    }
}
#define NZVC (cpu.PS & 017)

int main(void) {
    /* MOV immediate + flags */
    {
        uint16_t p[] = {0012700, 000005, 0000000};      /* MOV #5,R0; HALT */
        load(p, 3); run(10);
        chkeq("mov r0", cpu.R[0], 5);
        chkeq("mov flags", NZVC, 0);
    }
    {
        uint16_t p[] = {0012700, 0100000, 0000000};     /* MOV #100000,R0 */
        load(p, 3); run(10);
        chkeq("mov N", NZVC, 010);
    }
    {
        uint16_t p[] = {0005000, 0000000};              /* CLR R0 */
        load(p, 2); cpu.PS |= 017; run(10);
        chkeq("clr flags", NZVC, 004);
    }
    /* ADD carry + overflow */
    {
        uint16_t p[] = {0012700, 0077777, 0062700, 000001, 0000000}; /* R0=77777; ADD #1 */
        load(p, 5); run(10);
        chkeq("add ovf r0", cpu.R[0], 0100000);
        chkeq("add ovf flags", NZVC, 012);              /* N V */
    }
    {
        uint16_t p[] = {0012700, 0177777, 0062700, 000001, 0000000};
        load(p, 5); run(10);
        chkeq("add carry r0", cpu.R[0], 0);
        chkeq("add carry flags", NZVC, 005);            /* Z C */
    }
    /* SUB borrow */
    {
        uint16_t p[] = {0012700, 000003, 0012701, 000005, 0160100, 0000000};
        /* R0=3 R1=5; SUB R1,R0 → 3-5 = -2 */
        load(p, 6); run(10);
        chkeq("sub r0", cpu.R[0], 0177776);
        chkeq("sub flags", NZVC, 011);                  /* N C(borrow) */
    }
    /* CMP: 1 vs 2 → borrow, N */
    {
        uint16_t p[] = {0022727, 000001, 000002, 0000000};  /* CMP #1,#2 */
        load(p, 4); run(10);
        chkeq("cmp flags", NZVC, 011);
    }
    /* CMP equal → Z */
    {
        uint16_t p[] = {0022727, 000042, 000042, 0000000};
        load(p, 4); run(10);
        chkeq("cmp eq flags", NZVC, 004);
    }
    /* MOVB sign-extends into register */
    {
        uint16_t p[] = {0112700, 000377, 0000000};      /* MOVB #377,R0 */
        load(p, 3); run(10);
        chkeq("movb sext", cpu.R[0], 0177777);
        chkeq("movb flags", NZVC, 010);
    }
    /* INC max positive → V */
    {
        uint16_t p[] = {0012700, 0077777, 0005200, 0000000};
        load(p, 4); run(10);
        chkeq("inc V", NZVC, 012);
    }
    /* COM sets C */
    {
        uint16_t p[] = {0012700, 0170000, 0005100, 0000000};
        load(p, 4); run(10);
        chkeq("com r0", cpu.R[0], 0007777);
        chkeq("com flags", NZVC, 001);
    }
    /* NEG */
    {
        uint16_t p[] = {0012700, 000001, 0005400, 0000000};
        load(p, 4); run(10);
        chkeq("neg r0", cpu.R[0], 0177777);
        chkeq("neg flags", NZVC, 011);                  /* N C */
    }
    /* ASR / ASL flags */
    {
        uint16_t p[] = {0012700, 000003, 0006200, 0000000};  /* ASR R0: 3→1, C=1 */
        load(p, 4); run(10);
        chkeq("asr r0", cpu.R[0], 1);
        chkeq("asr flags", NZVC, 003);                  /* C=1, V=N^C=1 */
    }
    {
        uint16_t p[] = {0012700, 0100000, 0006300, 0000000}; /* ASL R0: sign out */
        load(p, 4); run(10);
        chkeq("asl r0", cpu.R[0], 0);
        chkeq("asl flags", NZVC, 007);                  /* Z C V(N^C) */
    }
    /* ROR through carry */
    {
        uint16_t p[] = {0000261, 0012700, 000002, 0006000, 0000000}; /* SEC; R0=2; ROR */
        load(p, 5); run(10);
        chkeq("ror r0", cpu.R[0], 0100001);
        chkeq("ror flags", NZVC, 012);                  /* N, V=N^C, C=0 */
    }
    /* SWAB */
    {
        uint16_t p[] = {0012700, 0001402, 0000300, 0000000}; /* SWAB R0 */
        load(p, 4); run(10);
        chkeq("swab r0", cpu.R[0], 0001003);
        chkeq("swab flags", NZVC, 0);                   /* low byte 003: not zero, pos */
    }
    /* autoincrement word + byte */
    {
        uint16_t p[] = {0012702, 000100, 0012201, 0000000};  /* R2=100; MOV (R2)+,R1 */
        load(p, 4);
        mem[0100 >> 1] = 0123456;
        run(10);
        chkeq("autoinc val", cpu.R[1], 0123456);
        chkeq("autoinc r2", cpu.R[2], 0102);
    }
    {
        uint16_t p[] = {0012702, 000100, 0112201, 0000000};  /* MOVB (R2)+,R1 */
        load(p, 4);
        mem[0100 >> 1] = 0000377;
        run(10);
        chkeq("autoincb r2", cpu.R[2], 0101);
        chkeq("autoincb sext", cpu.R[1], 0177777);
    }
    /* JSR/RTS roundtrip */
    {
        uint16_t p[] = {
            0004767, 000004,       /* JSR PC,+4 (to L) */
            0000000,               /* HALT (return lands here) */
            0000000,               /* pad */
            0012700, 000042,       /* L: MOV #42,R0 */
            0000207,               /* RTS PC */
        };
        load(p, 7); run(20);
        chkeq("jsr r0", cpu.R[0], 042);
        chkeq("jsr sp", cpu.R[6], 0700);
    }
    /* SOB */
    {
        uint16_t p[] = {0012700, 000000, 0012701, 000005,
                        0005200,            /* L: INC R0 */
                        0077102,            /* SOB R1,L (offset 2 words) */
                        0000000};
        load(p, 7); run(40);
        chkeq("sob count", cpu.R[0], 5);
    }
    /* MUL: 300*300, needs 32 bits */
    {
        uint16_t p[] = {0012700, 000454, 0070027, 000454, 0000000}; /* R0=300.; MUL #300.,R0 */
        load(p, 5); run(10);
        chkeq("mul hi", cpu.R[0], (uint32_t)(90000 >> 16));
        chkeq("mul lo", cpu.R[1], (uint32_t)(90000 & 0177777));
        chkeq("mul C", NZVC, 001);
    }
    /* MUL negative, fits */
    {
        uint16_t p[] = {0012701, 0177776, 0070127, 000003, 0000000}; /* R1=-2; MUL #3,R1 (odd reg) */
        load(p, 5); run(10);
        chkeq("mul odd lo", cpu.R[1], 0177772);         /* -6, low word only */
        chkeq("mul odd flags", NZVC, 010);              /* N; fits → C=0 */
    }
    /* DIV 90000/300 */
    {
        uint16_t p[] = {0012700, 000001, 0012701, 0057620, /* R0R1 = 90000. */
                        0071027, 000454, 0000000};          /* DIV #300.,R0 */
        load(p, 7); run(10);
        chkeq("div q", cpu.R[0], 0454);
        chkeq("div r", cpu.R[1], 0);
        chkeq("div flags", NZVC, 0);
    }
    /* DIV by zero */
    {
        uint16_t p[] = {0012700, 000000, 0012701, 000064,
                        0071027, 000000, 0000000};
        load(p, 7); run(10);
        chk("div0 VC", (cpu.PS & (PSW_V | PSW_C)) == (PSW_V | PSW_C));
    }
    /* DIV overflow */
    {
        uint16_t p[] = {0012700, 000002, 0012701, 000000,   /* 2<<16 / 2 = 65536: ovf */
                        0071027, 000002, 0000000};
        load(p, 7); run(10);
        chk("divovf V", (cpu.PS & PSW_V) != 0);
        chkeq("divovf regs", cpu.R[0], 2);              /* unchanged */
    }
    /* ASH right and left */
    {
        uint16_t p[] = {0012700, 000100, 0072027, 0177775, 0000000}; /* ASH #-3,R0 */
        load(p, 5); run(10);
        chkeq("ash right", cpu.R[0], 010);
        chkeq("ash right C", NZVC, 0);                  /* last bit out = 0 */
    }
    {
        uint16_t p[] = {0012700, 0040000, 0072027, 000001, 0000000}; /* ASH #1: sign flip */
        load(p, 5); run(10);
        chkeq("ash left", cpu.R[0], 0100000);
        chkeq("ash left NV", NZVC, 012);                /* N + V(sign change) */
    }
    /* ASHC 32-bit right shift */
    {
        uint16_t p[] = {0012700, 000001, 0012701, 000000,
                        0073027, 0177770, 0000000};         /* ASHC #-8,R0 */
        load(p, 7); run(10);
        chkeq("ashc hi", cpu.R[0], 0);
        chkeq("ashc lo", cpu.R[1], 0000400);            /* 1<<16 >> 8 = 1<<8 */
    }
    /* XOR */
    {
        uint16_t p[] = {0012700, 0170360, 0012701, 0000377,
                        0074001, 0000000};                  /* XOR R0,R1 */
        load(p, 6); run(10);
        chkeq("xor", cpu.R[1], 0170017);
    }
    /* odd address word access → trap 4 */
    {
        uint16_t p[] = {0012700, 000101, 0011001, 0000000}; /* MOV (R0),R1 with R0 odd */
        load(p, 4);
        int v = run(10);
        chkeq("odd trap", (uint32_t)v, V_BUS);
    }
    /* NXM (beyond 128K) → trap 4: kernel probes memory this way */
    {
        uint16_t p[] = {0012700, 000000, 0011001, 0000000};
        load(p, 4);
        /* point R0 at top 8K (I/O page in 16-bit space, mapped to unpopulated reg) */
        cpu.R[0] = 0160010;                             /* random empty I/O address */
        uint16_t q[] = {0011001, 0000000};              /* MOV (R0),R1 */
        load(q, 2); cpu.R[0] = 0160010;
        int v = run(10);
        chkeq("nxm trap", (uint32_t)v, V_BUS);
    }
    /* JMP R0 → illegal */
    {
        uint16_t p[] = {0000100, 0000000};
        load(p, 2);
        int v = run(10);
        chkeq("jmp r0 ill", (uint32_t)v, V_ILL);
    }
    /* EMT dispatch through vector */
    {
        uint16_t p[] = {0104000, 0000000};              /* EMT 0; HALT */
        load(p, 2);
        mem[030 >> 1] = 02000;                          /* handler at 2000 */
        mem[032 >> 1] = 0340;
        mem[02000 >> 1] = 0012700;                      /* MOV #77,R0 */
        mem[02002 >> 1] = 000077;
        mem[02004 >> 1] = 0000002;                      /* RTI */
        run(20);
        chkeq("emt r0", cpu.R[0], 077);
        chk("emt returned+halted", cpu.halted);
    }
    /* MMU: map user page 0 → phys 020000, run MFPI from kernel */
    {
        uint16_t p[] = {
            0012737, 0000200, 0177640,   /* MOV #200,UPAR0  (phys 020000) */
            0012737, 0077406, 0177600,   /* MOV #77406,UPDR0 (full page RW) */
            0012737, 0000001, 0177572,   /* MOV #1,SR0 (enable MMU) */
            0012700, 0000000,            /* MOV #0,R0 */
            0012716, 0030000,            /* MOV #030000,(SP) — set prev=user in PS via... */
            0000000,
        };
        /* simpler: set PS prev-mode manually after enabling */
        load(p, 14); run(20);
        chk("mmu enabled", cpu.SR0 & 1);
        /* now do the translation directly */
        uint32_t pa = mmu_v2p(0000100, MODE_U, 0);
        chkeq("mmu xlate", pa, 020100);
    }
    /* MMU: read-only page write → abort, SR0 flags */
    {
        uint16_t p[] = {0000000};
        load(p, 1);
        cpu.par[1][0] = 0200;
        cpu.pdr[1][0] = 077402;                         /* PLF full, ACF=2 read-only */
        cpu.SR0 = 1;
        int vec = setjmp(cpu.trapjmp);
        if (!vec) {
            mmu_v2p(0000100, MODE_U, 1);
            chk("ro abort taken", 0);
        } else {
            chkeq("ro abort vec", (uint32_t)vec, V_MMU);
            chk("ro abort SR0", (cpu.SR0 & 0020000) != 0);
        }
        cpu.SR0 = 0;
    }
    /* MMU: page length abort */
    {
        uint16_t p[] = {0000000};
        load(p, 1);
        cpu.par[1][0] = 0200;
        cpu.pdr[1][0] = (003 << 8) | 6;                 /* PLF=3: blocks 0-3 only, RW */
        cpu.SR0 = 1;
        int vec = setjmp(cpu.trapjmp);
        if (!vec) {
            mmu_v2p(001000, MODE_U, 0);                 /* block 8 > PLF 3 → abort */
            chk("len abort taken", 0);
        } else {
            chkeq("len abort vec", (uint32_t)vec, V_MMU);
            chk("len abort SR0", (cpu.SR0 & 0040000) != 0);
        }
        cpu.SR0 = 0;
    }
    /* MTPI/MFPI kernel→user roundtrip */
    {
        uint16_t p[] = {
            /* map user page 0 at phys 020000, enable MMU, set prev-mode=user,
               MTPI a value into user 0, MFPI it back */
            0012737, 0000200, 0177640,
            0012737, 0077406, 0177600,
            0012737, 0000001, 0177572,
            0012737, 0030340, 0177776,   /* PS: kernel current, prev=user, pri 7 */
            0012746, 0123456,            /* MOV #123456,-(SP) */
            0006637, 0000100,            /* MTPI @#100 */
            0006537, 0000100,            /* MFPI @#100 */
            0012600,                     /* MOV (SP)+,R0 */
            0000000,
        };
        load(p, 19);
        for (int pg = 0; pg < 7; pg++) {                /* kernel identity map */
            cpu.par[0][pg] = (uint16_t)(pg * 0200);
            cpu.pdr[0][pg] = 077406;
        }
        cpu.par[0][7] = 07600; cpu.pdr[0][7] = 077406;  /* I/O page */
        run(40);
        chkeq("mtpi/mfpi", cpu.R[0], 0123456);
        chkeq("user phys", mem[(020000 + 0100) >> 1], 0123456);
    }
    /* RTI to user mode banks SP */
    {
        uint16_t p[] = {
            0012706, 0000700,            /* MOV #700,SP (kernel) */
            0012746, 0170000,            /* push PS: user mode, prev user */
            0012746, 0004000,            /* push PC: 4000 */
            0000006,                     /* RTT (behaves like RTI) */
        };
        load(p, 7);
        cpu.usp = 0500;
        mem[04000 >> 1] = 0000000;      /* HALT in user mode → trap 4 (vector 0 → stop) */
        int v = run(20);
        chkeq("rti user trap", (uint32_t)v, V_BUS);     /* user HALT traps */
        chkeq("rti user sp", cpu.R[6], 0500);
        chkeq("rti user mode", (uint32_t)(cpu.PS >> 14), 3);
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
