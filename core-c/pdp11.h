/* snespdp11 — PDP-11/40 core, portable C.
 * Desktop-first (M1); the same core gets recompiled for the 65816 in M3.
 * Modeled hardware: 11/40 CPU + EIS, KT11-D MMU (no split I/D, no supervisor),
 * KL11 console, KW11-L line clock, RK11 with up to 4 RK05 drives. No FP11.
 */
#ifndef PDP11_H
#define PDP11_H

#include <stdint.h>
#include <setjmp.h>

#define MEMWORDS 0200000u          /* 128 KB physical RAM = 64K words */
#define MEMBYTES 0400000u          /* 128 KB in bytes (18-bit space; I/O page at top) */
#define IOPAGE   0760000u          /* 18-bit I/O page base */

/* PSW bits */
#define PSW_C 0000001
#define PSW_V 0000002
#define PSW_Z 0000004
#define PSW_N 0000010
#define PSW_T 0000020
#define PSW_PRI 0000340
#define PSW_PM 0030000             /* previous mode <13:12> */
#define PSW_CM 0140000             /* current mode <15:14> */

/* mode codes (11/40 has only these two) */
#define MODE_K 0
#define MODE_U 3

/* trap vectors */
#define V_BUS  0004                /* bus error / odd address / NXM / user HALT */
#define V_ILL  0010                /* reserved / illegal instruction */
#define V_BPT  0014
#define V_IOT  0020
#define V_PWR  0024
#define V_EMT  0030
#define V_TRAP 0034
#define V_CLK  0100
#define V_TTI  0060
#define V_TTO  0064
#define V_RK   0220
#define V_MMU  0250

typedef struct {
    uint16_t R[8];                 /* R6 = SP of *current* mode */
    uint16_t ksp, usp;             /* stashed stack pointers */
    uint16_t PS;
    uint16_t SR0, SR2;             /* KT11-D status (no SR1 on the /40 — v6 backup() copes) */
    uint16_t par[2][8], pdr[2][8]; /* [0]=kernel [1]=user */
    uint16_t slr;                  /* stack limit register (stored, not enforced) */
    int waiting;                   /* WAIT executed, idle until interrupt */
    int halted;                    /* HALT in kernel mode */
    unsigned long long icount;
    jmp_buf trapjmp;               /* longjmp(vec) target in the run loop */
} Cpu;

extern Cpu cpu;
extern uint16_t mem[MEMWORDS];

/* cpu.c */
void cpu_reset(void);
void cpu_step(void);                       /* execute one instruction (may longjmp) */
void cpu_trap(int vec);                    /* raise trap: longjmp to run loop */
void cpu_trap_entry(int vec);              /* vector push sequence (called from run loop) */
int  cpu_mode(void);                       /* current mode: MODE_K or MODE_U */
uint32_t mmu_v2p(uint16_t va, int mode, int wr);  /* translate or longjmp(V_MMU) */

/* bus.c — physical bus (RAM + I/O page). DMA uses these too. */
uint16_t bus_read16(uint32_t pa);
void     bus_write16(uint32_t pa, uint16_t v);
uint8_t  bus_read8(uint32_t pa);
void     bus_write8(uint32_t pa, uint8_t v);

/* devices (bus.c) */
void dev_reset(void);                      /* RESET instruction */
void dev_poll(void);                       /* called every few instructions */
int  dev_irq(int cpupri, int *vec);        /* highest pending irq above cpupri, or 0 */
void cons_putc_host(int c);                /* host feeds a char into the KL11 */
int  rk_attach(int unit, const char *path);
void rk_detach_all(void);

/* main.c provides these host hooks */
void host_putchar(int c);                  /* KL11 output byte */
int  host_pollchar(void);                  /* -1 or next input byte */
unsigned host_clock60(void);               /* # of 60Hz ticks elapsed since last call */
void host_panic(const char *msg);

/* tracing */
extern int trace_on;
void trace_instr(uint16_t pc, uint16_t instr);

#endif
