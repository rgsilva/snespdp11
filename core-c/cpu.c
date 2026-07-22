/* PDP-11/40 CPU + EIS + KT11-D MMU. See pdp11.h for the modeled config.
 * Instruction semantics follow the PDP-11/40 Processor Handbook; structure
 * cross-checked against pdp11.js/avr11 (WTFPL) which boot the same v6 images.
 */
#include "pdp11.h"
#include <string.h>

Cpu cpu;
uint16_t mem[MEMWORDS];
int trace_on;

int cpu_mode(void) { return (cpu.PS >> 14) & 3; }
static int prev_mode(void) { return (cpu.PS >> 12) & 3; }

/* ---- flags ---- */
#define FN PSW_N
#define FZ PSW_Z
#define FV PSW_V
#define FC PSW_C
static void fl_set(int f, int on) { if (on) cpu.PS |= f; else cpu.PS &= ~f; }
static void fl_nz(uint32_t v, int byte) {
    uint32_t sign = byte ? 0200 : 0100000, mask = byte ? 0377 : 0177777;
    fl_set(FN, v & sign);
    fl_set(FZ, (v & mask) == 0);
}

/* ---- MMU ---- */
static void mmu_abort(int bits, int m, int pg) {
    if (!(cpu.SR0 & 0160000))                     /* not already frozen */
        cpu.SR0 = bits | (m ? 0100 : 0) | (pg << 1) | (cpu.SR0 & 1);
    cpu_trap(V_MMU);
}
uint32_t mmu_v2p(uint16_t va, int mode, int wr) {
    if (!(cpu.SR0 & 1)) {
        uint32_t pa = va;
        if (pa >= 0160000) pa += 0600000;         /* 16-bit top 8K → I/O page */
        return pa;
    }
    int m = (mode == MODE_U) ? 1 : 0;
    int pg = va >> 13;
    uint16_t pdr = cpu.pdr[m][pg];
    int acf = pdr & 7;
    if (acf == 0 || acf == 3 || acf == 7) mmu_abort(0100000, m, pg);
    int bn = (va >> 6) & 0177, plf = (pdr >> 8) & 0177;
    if ((pdr & 010) ? (bn < plf) : (bn > plf)) mmu_abort(0040000, m, pg);
    if (wr) {
        if (acf < 4) mmu_abort(0020000, m, pg);   /* 1,2 = read-only */
        cpu.pdr[m][pg] |= 0100;                   /* W bit */
    }
    return (((uint32_t)(cpu.par[m][pg] & 07777) << 6) + (va & 017777)) & 0777777;
}

/* ---- virtual access, current mode ---- */
static uint16_t ldw_m(uint16_t va, int mode) {
    if (va & 1) cpu_trap(V_BUS);
    return bus_read16(mmu_v2p(va, mode, 0));
}
static void stw_m(uint16_t va, uint16_t v, int mode) {
    if (va & 1) cpu_trap(V_BUS);
    bus_write16(mmu_v2p(va, mode, 1), v);
}
static uint16_t ldw(uint16_t va) { return ldw_m(va, cpu_mode()); }
static void stw(uint16_t va, uint16_t v) { stw_m(va, v, cpu_mode()); }
static uint8_t ldb(uint16_t va) { return bus_read8(mmu_v2p(va, cpu_mode(), 0)); }
static void stb(uint16_t va, uint8_t v) { bus_write8(mmu_v2p(va, cpu_mode(), 1), v); }

static uint16_t fetch16(void) {
    uint16_t va = cpu.R[7];
    if (va & 1) cpu_trap(V_BUS);
    uint16_t v = bus_read16(mmu_v2p(va, cpu_mode(), 0));
    cpu.R[7] = va + 2;
    return v;
}

static void push(uint16_t v) { cpu.R[6] -= 2; stw(cpu.R[6], v); }
static uint16_t pop(void) { uint16_t v = ldw(cpu.R[6]); cpu.R[6] += 2; return v; }

/* PS writes that may switch mode must bank the stack pointers */
static void ps_write(uint16_t nps) {
    int om = cpu_mode(), nm = (nps >> 14) & 3;
    if (om != nm) {
        if (om == MODE_U) cpu.usp = cpu.R[6]; else cpu.ksp = cpu.R[6];
        cpu.R[6] = (nm == MODE_U) ? cpu.usp : cpu.ksp;
    }
    cpu.PS = nps;
}
uint16_t cpu_ps_read(void) { return cpu.PS; }
void cpu_ps_iowrite(uint16_t v) {          /* write via address 777776: T unchanged */
    ps_write((v & ~PSW_T) | (cpu.PS & PSW_T));
}

/* ---- operands ---- */
typedef struct { int reg; uint16_t va; } Opd;   /* reg >= 0 → register direct */

static uint16_t calc_ea(int m, int r, int byte) {
    uint16_t va, x;
    uint16_t inc = (byte && r < 6) ? 1 : 2;
    switch (m) {
    default:
    case 1: return cpu.R[r];
    case 2: va = cpu.R[r]; cpu.R[r] += inc; return va;
    case 3: va = ldw(cpu.R[r]); cpu.R[r] += 2; return va;
    case 4: cpu.R[r] -= inc; return cpu.R[r];
    case 5: cpu.R[r] -= 2; return ldw(cpu.R[r]);
    case 6: x = fetch16(); return (uint16_t)(cpu.R[r] + x);
    case 7: x = fetch16(); return ldw((uint16_t)(cpu.R[r] + x));
    }
}
static Opd opd_get(uint16_t spec, int byte) {
    Opd d;
    int m = (spec >> 3) & 7, r = spec & 7;
    if (m == 0) { d.reg = r; d.va = 0; return d; }
    d.reg = -1; d.va = calc_ea(m, r, byte);
    return d;
}
static uint16_t opd_ld(Opd d, int byte) {
    if (d.reg >= 0) return byte ? (cpu.R[d.reg] & 0377) : cpu.R[d.reg];
    return byte ? ldb(d.va) : ldw(d.va);
}
static void opd_st(Opd d, int byte, uint16_t v) {
    if (d.reg >= 0) {
        if (byte) cpu.R[d.reg] = (cpu.R[d.reg] & 0177400) | (v & 0377);
        else cpu.R[d.reg] = v;
    } else if (byte) stb(d.va, (uint8_t)v);
    else stw(d.va, v);
}
static uint16_t src_val(uint16_t spec, int byte) { return opd_ld(opd_get(spec, byte), byte); }

/* ---- trap machinery ---- */
void cpu_trap(int vec) { longjmp(cpu.trapjmp, vec); }

void cpu_trap_entry(int vec) {
    uint16_t opc = cpu.R[7], ops = cpu.PS;
    uint16_t npc = ldw_m((uint16_t)vec, MODE_K);
    uint16_t nps = ldw_m((uint16_t)(vec + 2), MODE_K);
    ps_write((uint16_t)((nps & ~PSW_PM) | ((ops >> 2) & PSW_PM)));
    push(ops);
    push(opc);
    cpu.R[7] = npc;
    cpu.waiting = 0;
}

/* ---- instruction groups ---- */
static void op_dbl(uint16_t op) {
    int code = (op >> 12) & 07;
    int byte = (op >> 15) & 1;
    if (code == 6) byte = 0;                      /* ADD/SUB are word ops */
    uint32_t sign = byte ? 0200 : 0100000, mask = byte ? 0377 : 0177777;
    uint32_t s = src_val((op >> 6) & 077, byte) & mask;
    Opd d = opd_get(op & 077, byte);
    uint32_t dd, r;

    switch (code) {
    case 1:                                       /* MOV(B) */
        if (byte && d.reg >= 0) cpu.R[d.reg] = (uint16_t)(int16_t)(int8_t)s;
        else opd_st(d, byte, (uint16_t)s);
        fl_nz(s, byte); fl_set(FV, 0);
        return;
    case 2:                                       /* CMP(B): src - dst */
        dd = opd_ld(d, byte) & mask;
        r = (s - dd) & mask;
        fl_nz(r, byte);
        fl_set(FV, (s ^ dd) & ~(r ^ dd) & sign);
        fl_set(FC, s < dd);
        return;
    case 3:                                       /* BIT(B) */
        dd = opd_ld(d, byte) & mask;
        r = s & dd;
        fl_nz(r, byte); fl_set(FV, 0);
        return;
    case 4:                                       /* BIC(B) */
        dd = opd_ld(d, byte) & mask;
        r = ~s & dd & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, 0);
        return;
    case 5:                                       /* BIS(B) */
        dd = opd_ld(d, byte) & mask;
        r = (s | dd) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, 0);
        return;
    case 6:
        dd = opd_ld(d, 0);
        if (op & 0100000) {                       /* SUB: dst - src */
            r = (dd - s) & 0177777;
            fl_set(FV, (dd ^ s) & ~(r ^ s) & 0100000);
            fl_set(FC, dd < s);
        } else {                                  /* ADD */
            r = dd + s;
            fl_set(FV, ~(dd ^ s) & (dd ^ r) & 0100000);
            fl_set(FC, r > 0177777);
            r &= 0177777;
        }
        opd_st(d, 0, (uint16_t)r);
        fl_nz(r, 0);
        return;
    }
}

static void op_single(uint16_t op, int byte) {
    int code = (op >> 6) & 077;                   /* 050..063, 067 */
    uint32_t sign = byte ? 0200 : 0100000, mask = byte ? 0377 : 0177777;
    Opd d = opd_get(op & 077, byte);
    uint32_t v, r;
    int c = (cpu.PS & FC) != 0;

    switch (code) {
    case 050:                                     /* CLR */
        opd_st(d, byte, 0);
        cpu.PS = (cpu.PS & ~(FN | FV | FC)) | FZ;
        return;
    case 051:                                     /* COM */
        r = ~opd_ld(d, byte) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, 0); fl_set(FC, 1);
        return;
    case 052:                                     /* INC */
        v = opd_ld(d, byte) & mask;
        r = (v + 1) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, v == sign - 1);
        return;
    case 053:                                     /* DEC */
        v = opd_ld(d, byte) & mask;
        r = (v - 1) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, v == sign);
        return;
    case 054:                                     /* NEG */
        r = (-(int32_t)(opd_ld(d, byte) & mask)) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte); fl_set(FV, r == sign); fl_set(FC, r != 0);
        return;
    case 055:                                     /* ADC */
        v = opd_ld(d, byte) & mask;
        r = (v + c) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte);
        fl_set(FV, c && v == sign - 1);
        fl_set(FC, c && v == mask);
        return;
    case 056:                                     /* SBC */
        v = opd_ld(d, byte) & mask;
        r = (v - c) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_nz(r, byte);
        fl_set(FV, v == sign);
        fl_set(FC, c && v == 0);
        return;
    case 057:                                     /* TST */
        r = opd_ld(d, byte) & mask;
        fl_nz(r, byte); fl_set(FV, 0); fl_set(FC, 0);
        return;
    case 060:                                     /* ROR */
        v = opd_ld(d, byte) & mask;
        r = (v >> 1) | (c ? sign : 0);
        opd_st(d, byte, (uint16_t)r);
        fl_set(FC, v & 1);
        fl_nz(r, byte);
        fl_set(FV, ((cpu.PS & FN) != 0) ^ ((cpu.PS & FC) != 0));
        return;
    case 061:                                     /* ROL */
        v = opd_ld(d, byte) & mask;
        r = ((v << 1) | c) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_set(FC, v & sign);
        fl_nz(r, byte);
        fl_set(FV, ((cpu.PS & FN) != 0) ^ ((cpu.PS & FC) != 0));
        return;
    case 062:                                     /* ASR */
        v = opd_ld(d, byte) & mask;
        r = (v >> 1) | (v & sign);
        opd_st(d, byte, (uint16_t)r);
        fl_set(FC, v & 1);
        fl_nz(r, byte);
        fl_set(FV, ((cpu.PS & FN) != 0) ^ ((cpu.PS & FC) != 0));
        return;
    case 063:                                     /* ASL */
        v = opd_ld(d, byte) & mask;
        r = (v << 1) & mask;
        opd_st(d, byte, (uint16_t)r);
        fl_set(FC, v & sign);
        fl_nz(r, byte);
        fl_set(FV, ((cpu.PS & FN) != 0) ^ ((cpu.PS & FC) != 0));
        return;
    case 067:                                     /* SXT (word only) */
        r = (cpu.PS & FN) ? 0177777 : 0;
        opd_st(d, 0, (uint16_t)r);
        fl_set(FZ, !(cpu.PS & FN)); fl_set(FV, 0);
        return;
    default:
        cpu_trap(V_ILL);
    }
}

/* MFPI/MTPI (and their D-space aliases — the /40 has no split I/D) */
static void op_mfpi(uint16_t op) {
    int m = (op >> 3) & 7, r = op & 7;
    int pm = prev_mode();
    uint16_t v;
    if (m == 0) {
        if (r == 6 && pm != cpu_mode()) v = (pm == MODE_U) ? cpu.usp : cpu.ksp;
        else v = cpu.R[r];
    } else {
        uint16_t va = calc_ea(m, r, 0);
        v = ldw_m(va, pm);
    }
    push(v);
    fl_nz(v, 0); fl_set(FV, 0);
}
static void op_mtpi(uint16_t op) {
    int m = (op >> 3) & 7, r = op & 7;
    int pm = prev_mode();
    uint16_t v = pop();
    if (m == 0) {
        if (r == 6 && pm != cpu_mode()) { if (pm == MODE_U) cpu.usp = v; else cpu.ksp = v; }
        else cpu.R[r] = v;
    } else {
        uint16_t va = calc_ea(m, r, 0);
        stw_m(va, v, pm);
    }
    fl_nz(v, 0); fl_set(FV, 0);
}

static void op_eis(uint16_t op) {
    int r = (op >> 6) & 7;
    int sub = (op >> 9) & 7;
    uint32_t s;
    int32_t prod, num, quo, rem;
    int n, i, cc, vv;

    switch (sub) {
    case 0:                                       /* MUL */
        s = src_val(op & 077, 0);
        prod = (int32_t)(int16_t)cpu.R[r] * (int16_t)s;
        cpu.R[r] = (uint16_t)((uint32_t)prod >> 16);
        cpu.R[r | 1] = (uint16_t)prod;
        fl_set(FN, prod < 0); fl_set(FZ, prod == 0); fl_set(FV, 0);
        fl_set(FC, prod < -32768 || prod > 32767);
        return;
    case 1:                                       /* DIV */
        s = src_val(op & 077, 0);
        num = (int32_t)(((uint32_t)cpu.R[r] << 16) | cpu.R[r | 1]);
        if ((int16_t)s == 0) {
            fl_set(FC, 1); fl_set(FV, 1); fl_set(FN, 0); fl_set(FZ, 0);
            return;
        }
        quo = num / (int16_t)s;
        rem = num % (int16_t)s;
        if (quo > 32767 || quo < -32768) {
            fl_set(FV, 1); fl_set(FC, 0);
            fl_set(FN, quo < 0); fl_set(FZ, 0);
            return;
        }
        cpu.R[r] = (uint16_t)quo;
        cpu.R[r | 1] = (uint16_t)rem;
        fl_set(FN, quo < 0); fl_set(FZ, quo == 0); fl_set(FV, 0); fl_set(FC, 0);
        return;
    case 2: {                                     /* ASH */
        s = src_val(op & 077, 0);
        n = s & 077; if (n & 040) n -= 0100;
        uint32_t x = cpu.R[r];
        cc = 0; vv = 0;
        if (n > 0) for (i = 0; i < n; i++) {
            cc = (x >> 15) & 1;
            uint32_t nx = (x << 1) & 0177777;
            if (((nx ^ x) >> 15) & 1) vv = 1;
            x = nx;
        } else for (i = 0; i < -n; i++) {
            cc = x & 1;
            x = (x >> 1) | (x & 0100000);
        }
        cpu.R[r] = (uint16_t)x;
        fl_nz(x, 0); fl_set(FV, vv); fl_set(FC, n ? cc : 0);
        return;
    }
    case 3: {                                     /* ASHC */
        s = src_val(op & 077, 0);
        n = s & 077; if (n & 040) n -= 0100;
        uint32_t x = ((uint32_t)cpu.R[r] << 16) | cpu.R[r | 1];
        cc = 0; vv = 0;
        if (n > 0) for (i = 0; i < n; i++) {
            cc = (x >> 31) & 1;
            uint32_t nx = x << 1;
            if (((nx ^ x) >> 31) & 1) vv = 1;
            x = nx;
        } else for (i = 0; i < -n; i++) {
            cc = x & 1;
            x = (x >> 1) | (x & 0x80000000u);
        }
        cpu.R[r] = (uint16_t)(x >> 16);
        cpu.R[r | 1] = (uint16_t)x;
        fl_set(FN, x & 0x80000000u); fl_set(FZ, x == 0);
        fl_set(FV, vv); fl_set(FC, n ? cc : 0);
        return;
    }
    case 4: {                                     /* XOR */
        Opd d = opd_get(op & 077, 0);
        uint16_t rr = opd_ld(d, 0) ^ cpu.R[r];
        opd_st(d, 0, rr);
        fl_nz(rr, 0); fl_set(FV, 0);
        return;
    }
    case 7:                                       /* SOB */
        cpu.R[r]--;
        if (cpu.R[r] != 0) cpu.R[7] -= 2 * (op & 077);
        return;
    default:                                      /* FIS / reserved */
        cpu_trap(V_ILL);
    }
}

static void op_branch(uint16_t op) {
    int n = (cpu.PS & FN) != 0, z = (cpu.PS & FZ) != 0;
    int v = (cpu.PS & FV) != 0, c = (cpu.PS & FC) != 0;
    int taken;
    switch ((op >> 8) & 0377) {
    case 0001: taken = 1; break;                  /* BR   */
    case 0002: taken = !z; break;                 /* BNE  */
    case 0003: taken = z; break;                  /* BEQ  */
    case 0004: taken = !(n ^ v); break;           /* BGE  */
    case 0005: taken = n ^ v; break;              /* BLT  */
    case 0006: taken = !(z | (n ^ v)); break;     /* BGT  */
    case 0007: taken = z | (n ^ v); break;        /* BLE  */
    case 0200: taken = !n; break;                 /* BPL  */
    case 0201: taken = n; break;                  /* BMI  */
    case 0202: taken = !c && !z; break;           /* BHI  */
    case 0203: taken = c || z; break;             /* BLOS */
    case 0204: taken = !v; break;                 /* BVC  */
    case 0205: taken = v; break;                  /* BVS  */
    case 0206: taken = !c; break;                 /* BCC  */
    case 0207: taken = c; break;                  /* BCS  */
    default: taken = 0; break;
    }
    if (taken) cpu.R[7] += 2 * (int16_t)(int8_t)(op & 0377);
}

int cpu_rtt_flag;                                 /* set by RTT: skip one T-trap */

void cpu_step(void) {
    uint16_t pc = cpu.R[7];
    if (!(cpu.SR0 & 0160000)) cpu.SR2 = pc;
    uint16_t op = fetch16();
    if (trace_on) trace_instr(pc, op);
    cpu.icount++;

    int hi3 = (op >> 12) & 07;
    if (hi3 >= 1 && hi3 <= 6) { op_dbl(op); return; }
    if (hi3 == 7) {
        if (op & 0100000) cpu_trap(V_ILL);        /* FP11 absent */
        op_eis(op);
        return;
    }

    if (!(op & 0100000)) {                        /* 00xxxx */
        if (op < 0000400) {
            switch (op) {
            case 0: /* HALT */
                if (cpu_mode() == MODE_U) cpu_trap(V_BUS);
                cpu.halted = 1;
                return;
            case 1: cpu.waiting = 1; return;      /* WAIT */
            case 2: case 6: {                     /* RTI / RTT */
                uint16_t npc = pop(), nps = pop();
                if (cpu_mode() == MODE_U)
                    nps = (uint16_t)((nps & ~(PSW_CM | PSW_PM | PSW_PRI)) |
                                     (cpu.PS & (PSW_CM | PSW_PM | PSW_PRI)));
                cpu.R[7] = npc;
                ps_write(nps);
                if (op == 6) cpu_rtt_flag = 1;
                return;
            }
            case 3: cpu_trap(V_BPT); return;
            case 4: cpu_trap(V_IOT); return;
            case 5: /* RESET */
                if (cpu_mode() == MODE_K) dev_reset();
                return;
            default: break;
            }
            if ((op & 0177700) == 0000100) {      /* JMP */
                int m = (op >> 3) & 7;
                if (m == 0) cpu_trap(V_ILL);
                cpu.R[7] = calc_ea(m, op & 7, 0);
                return;
            }
            if ((op & 0177770) == 0000200) {      /* RTS */
                int r = op & 7;
                cpu.R[7] = cpu.R[r];
                cpu.R[r] = pop();
                return;
            }
            if ((op & 0177740) == 0000240) {      /* CLx/SEx */
                if (op & 020) cpu.PS |= op & 017;
                else cpu.PS &= ~(op & 017);
                return;
            }
            if ((op & 0177700) == 0000300) {      /* SWAB */
                Opd d = opd_get(op & 077, 0);
                uint16_t v = opd_ld(d, 0);
                uint16_t r = (uint16_t)((v >> 8) | (v << 8));
                opd_st(d, 0, r);
                fl_nz(r & 0377, 1);
                fl_set(FV, 0); fl_set(FC, 0);
                return;
            }
            cpu_trap(V_ILL);
        }
        if (op < 0004000) { op_branch(op); return; }
        if ((op & 0177000) == 0004000) {          /* JSR */
            int m = (op >> 3) & 7, r = (op >> 6) & 7;
            if (m == 0) cpu_trap(V_ILL);
            uint16_t va = calc_ea(m, op & 7, 0);
            push(cpu.R[r]);
            cpu.R[r] = cpu.R[7];
            cpu.R[7] = va;
            return;
        }
        switch ((op >> 6) & 0777) {
        case 0064: {                              /* MARK */
            cpu.R[6] = (uint16_t)(cpu.R[7] + 2 * (op & 077));
            cpu.R[7] = cpu.R[5];
            cpu.R[5] = pop();
            return;
        }
        case 0065: op_mfpi(op); return;
        case 0066: op_mtpi(op); return;
        default:
            op_single(op, 0);                     /* 0050dd..0063dd, 0067dd, else ILL */
            return;
        }
    } else {                                      /* 10xxxx */
        if (op < 0104000) { op_branch(op); return; }
        if (op < 0104400) cpu_trap(V_EMT);
        if (op < 0105000) cpu_trap(V_TRAP);
        switch ((op >> 6) & 077) {
        case 065: op_mfpi(op); return;            /* MFPD ≡ MFPI on the /40 */
        case 066: op_mtpi(op); return;            /* MTPD ≡ MTPI */
        default:
            op_single(op, 1);                     /* byte group, else ILL */
            return;
        }
    }
}

void cpu_reset(void) {
    memset(&cpu, 0, sizeof cpu);
    cpu.PS = 0340;                                /* kernel, priority 7 */
    cpu_rtt_flag = 0;
    dev_reset();
}
