/* Physical bus: 128 KB RAM + I/O page. Devices: KL11 console, KW11-L clock,
 * RK11 controller with up to 4 RK05 image files. DMA bypasses the MMU (UNIBUS).
 */
#include "pdp11.h"
#include <stdio.h>
#include <string.h>

/* ---------------- guest RAM backend ----------------
 * Desktop/cputest: flat array. SNES boot build (PDP11_RAM_SRAM): the guest's
 * 128 KB lives in cart SRAM, HiROM mapping — banks $20-$2F, 8 KB windows at
 * $6000-$7FFF (256 KB verified distinct in Mesen by snes/sramprobe.c). */
#ifdef PDP11_RAM_SRAM
static volatile uint16_t *sram_ptr(uint32_t pa) {
    return (volatile uint16_t *)((((uint32_t)0x20 + (pa >> 13)) << 16) |
                                 (0x6000u | (pa & 0x1FFEu)));
}
static uint16_t ram_rd16(uint32_t pa) { return *sram_ptr(pa); }
static void ram_wr16(uint32_t pa, uint16_t v) { *sram_ptr(pa) = v; }
#else
uint16_t mem[MEMWORDS];
static uint16_t ram_rd16(uint32_t pa) { return mem[pa >> 1]; }
static void ram_wr16(uint32_t pa, uint16_t v) { mem[pa >> 1] = v; }
#endif
uint16_t guest_rd16(uint32_t pa) { return ram_rd16(pa); }   /* for boot loaders */
void guest_wr16(uint32_t pa, uint16_t v) { ram_wr16(pa, v); }

/* ---------------- KL11 console ---------------- */
static uint16_t rcsr, rbuf, xcsr;
static int tt_irq_rx, tt_irq_tx;
static int rx_delay, tx_delay;
#ifndef INQ_SIZE
#define INQ_SIZE 8192
#endif
static uint8_t inq[INQ_SIZE];
static unsigned inq_r, inq_w;
#ifndef RX_PACE
#define RX_PACE 2000                    /* dev_polls between input chars */
#endif
#ifndef TX_TIME
#define TX_TIME 64                      /* instructions until tx done */
#endif

void cons_putc_host(int c) {            /* host → guest keyboard */
    if (inq_w - inq_r < sizeof inq) inq[inq_w++ % sizeof inq] = (uint8_t)c;
}

/* ---------------- KW11-L clock ---------------- */
static uint16_t lks;
static int clk_irq;

/* ---------------- RK11 / RK05 ---------------- */
#ifndef RK_BUSY_POLLS
#define RK_BUSY_POLLS 300     /* dev_polls (256 instr each) until RDY+irq; the
                                 SNES build shrinks this — at ~1 KIPS the
                                 default is 85 wall-clock seconds per read */
#endif
typedef struct { FILE *f; } RkDrive;
static RkDrive rkdrv[4];

/* rk0 romdisk mode: read-only base + copy-on-write overlay (the SNES story:
 * base image lives in cart ROM, dirty blocks in a small RAM pool) */
#ifndef OV_CAP
#define OV_CAP 256
#endif
static int rk_ovmode, ov_max = 64;
static uint16_t ov_blk[OV_CAP];
static uint8_t  ov_data[OV_CAP][512];
static int ov_n, ov_peak, ov_dropped;

void rk_set_overlay(int maxent) {
    rk_ovmode = 1;
    ov_max = (maxent > OV_CAP) ? OV_CAP : (maxent < 1 ? 1 : maxent);
}
void rk_ovstats(int *peak, int *maxent, int *dropped) {
    *peak = ov_peak; *maxent = ov_max; *dropped = ov_dropped;
}
static uint8_t *ov_find(uint32_t blk) {
    for (int i = 0; i < ov_n; i++)
        if (ov_blk[i] == blk) return ov_data[i];
    return 0;
}
static void dsk_base_read_block(uint32_t blk, uint8_t *dst);  /* backend below */
static uint8_t *ov_alloc(uint32_t blk) {
    if (ov_n >= ov_max) return 0;
    ov_blk[ov_n] = (uint16_t)blk;
    dsk_base_read_block(blk, ov_data[ov_n]);
    if (++ov_n > ov_peak) ov_peak = ov_n;
    return ov_data[ov_n - 1];
}
uint16_t sw_reg;                        /* console switch register value */
static uint16_t rker, rkcs, rkwc, rkba, rkda;
static int rk_irq, rk_busy;             /* countdown to completion */
static int rk_pend_ide;

int rk_attach(int unit, const char *path) {
#ifdef PDP11_NOFILE
    (void)unit; (void)path;
    return -1;
#else
    if (unit < 0 || unit > 3) return -1;
    rkdrv[unit].f = fopen(path, (unit == 0 && rk_ovmode) ? "rb" : "r+b");
    return rkdrv[unit].f ? 0 : -1;
#endif
}
void rk_detach_all(void) {
#ifndef PDP11_NOFILE
    for (int i = 0; i < 4; i++)
        if (rkdrv[i].f) { fclose(rkdrv[i].f); rkdrv[i].f = 0; }
#endif
}

/* ---- disk backends: FILE (desktop) / ROM+RAM (SNES boot) / none ---- */
#if defined(PDP11_DISK_ROM)
/* rk0 base = v6 image in cart ROM at PDP11_ROMDISK_ADDR (HiROM linear; see
 * raw2sfc.py --at). rk0 writes MUST go via the overlay (rk_ovmode forced by
 * the SNES glue). rk1 = RAM block device (WRAM bank $7F) — the APU-RAM
 * version replaces this buffer later without touching the guest. */
#ifndef PDP11_ROMDISK_ADDR
#define PDP11_ROMDISK_ADDR 0xD00000ul
#endif
#define RK1_BYTES (121ul * 512)
static uint8_t rk1buf[RK1_BYTES] __attribute__((section("rk1buf")));
static int dsk_ok(int drive) { return drive <= 1; }
static void dsk_base_read_block(uint32_t blk, uint8_t *dst) {
    const uint8_t *src = (const uint8_t *)(PDP11_ROMDISK_ADDR + blk * 512);
    for (int i = 0; i < 512; i++) dst[i] = src[i];
}
static int dsk_rd16(int drive, uint32_t off, uint16_t *w) {
    if (drive == 0) {
        *w = *(const uint16_t *)(PDP11_ROMDISK_ADDR + off);
        return 0;
    }
    if (off + 1 >= RK1_BYTES) return -1;
    *w = (uint16_t)(rk1buf[off] | (rk1buf[off + 1] << 8));
    return 0;
}
static int dsk_wr16(int drive, uint32_t off, uint16_t w) {
    if (drive == 0) return -1;                    /* ROM: only overlay writes */
    if (off + 1 >= RK1_BYTES) return -1;
    rk1buf[off] = (uint8_t)w;
    rk1buf[off + 1] = (uint8_t)(w >> 8);
    return 0;
}
#elif defined(PDP11_NOFILE)
static int dsk_ok(int drive) { (void)drive; return 0; }
static void dsk_base_read_block(uint32_t blk, uint8_t *dst) {
    (void)blk; memset(dst, 0, 512);
}
static int dsk_rd16(int drive, uint32_t off, uint16_t *w) {
    (void)drive; (void)off; (void)w; return -1;
}
static int dsk_wr16(int drive, uint32_t off, uint16_t w) {
    (void)drive; (void)off; (void)w; return -1;
}
#else
static int cur_base_drive;                        /* for ov_alloc's base read */
static int dsk_ok(int drive) { return drive <= 3 && rkdrv[drive].f; }
static void dsk_base_read_block(uint32_t blk, uint8_t *dst) {
    FILE *f = rkdrv[cur_base_drive].f;
    fseek(f, (long)blk * 512, SEEK_SET);
    size_t got = fread(dst, 1, 512, f);
    if (got < 512) memset(dst + got, 0, 512 - got);
}
static int dsk_rd16(int drive, uint32_t off, uint16_t *w) {
    FILE *f = rkdrv[drive].f;
    fseek(f, (long)off, SEEK_SET);
    int lo = fgetc(f), hi = fgetc(f);
    if (lo == EOF) lo = 0;
    if (hi == EOF) hi = 0;
    *w = (uint16_t)(lo | (hi << 8));
    return 0;
}
static int dsk_wr16(int drive, uint32_t off, uint16_t w) {
    FILE *f = rkdrv[drive].f;
    fseek(f, (long)off, SEEK_SET);
    fputc(w & 0377, f);
    fputc((w >> 8) & 0377, f);
    return 0;
}
#endif

static void rk_xfer(void) {
    int drive = (rkda >> 13) & 7;
    int cyl = (rkda >> 5) & 0377, surf = (rkda >> 4) & 1, sect = rkda & 017;
    int func = (rkcs >> 1) & 7;
    uint32_t lba = ((uint32_t)cyl * 2 + surf) * 12 + sect;
    uint32_t words = (0200000 - rkwc) & 0177777;
    uint32_t ba = (((uint32_t)rkcs & 060) << 12) | rkba;

    if (drive > 3 || !dsk_ok(drive) || cyl > 202 || sect > 11) {
        rker |= 0100000; rkcs |= 0100000;         /* DRE/ERR */
        return;
    }
#if !defined(PDP11_DISK_ROM) && !defined(PDP11_NOFILE)
    cur_base_drive = 0;                           /* overlay base is rk0 */
#endif
    int use_ov = (drive == 0 && rk_ovmode);
    for (uint32_t i = 0; i < words; i++) {
        uint32_t pa = (ba + i * 2) & 0777777;
        uint32_t off = lba * 512 + i * 2;
        uint32_t blk = off / 512;
        if ((pa >> 1) >= MEMWORDS) { rker |= 0020000; rkcs |= 0100000; break; }  /* NXM */
        if (func == 2) {                          /* READ */
            uint8_t *ov = use_ov ? ov_find(blk) : 0;
            uint16_t w;
            if (ov) {
                w = (uint16_t)(ov[off % 512] | (ov[off % 512 + 1] << 8));
            } else if (dsk_rd16(drive, off, &w) < 0) {
                rker |= 0100000; rkcs |= 0100000;
                break;
            }
            ram_wr16(pa, w);
        } else if (func == 1) {                   /* WRITE */
            uint16_t w = ram_rd16(pa);
            if (use_ov) {
                uint8_t *ov = ov_find(blk);
                if (!ov) ov = ov_alloc(blk);
                if (!ov) {                        /* pool full: I/O error */
                    ov_dropped++;
                    rker |= 0100000; rkcs |= 0100000;
                    break;
                }
                ov[off % 512] = (uint8_t)w;
                ov[off % 512 + 1] = (uint8_t)(w >> 8);
            } else if (dsk_wr16(drive, off, w) < 0) {
                rker |= 0100000; rkcs |= 0100000;
                break;
            }
        }                                          /* WCHK/RCHK: pretend OK */
    }
#if !defined(PDP11_DISK_ROM) && !defined(PDP11_NOFILE)
    if (func == 1 && !use_ov && rkdrv[drive].f) fflush(rkdrv[drive].f);
#endif
    /* advance registers the way the hardware does */
    uint32_t nba = ba + words * 2;
    rkba = (uint16_t)nba;
    rkcs = (uint16_t)((rkcs & ~060) | ((nba >> 12) & 060));
    rkwc = 0;
    uint32_t nlba = lba + (words * 2 + 511) / 512;
    uint32_t t = nlba / 12, ns = nlba % 12;
    rkda = (uint16_t)((rkda & 0160000) | ((t >> 1) << 5) | ((t & 1) << 4) | ns);
}

static void rk_go(uint16_t v) {
    rkcs = (uint16_t)((rkcs & 0100200) | (v & 077577));  /* keep ERR+RDY, load rest */
    if (!(v & 1)) { rk_pend_ide = (v & 0100) != 0; return; }
    int func = (v >> 1) & 7;
    rkcs &= ~0200;                                /* busy */
    rk_pend_ide = (v & 0100) != 0;
    switch (func) {
    case 0:                                       /* control reset */
        rker = 0; rkcs = 0200; rkwc = rkba = 0;
        rk_irq = 0; rk_busy = 0;
        return;
    case 1: case 2: case 3: case 5:               /* write/read/wchk/rchk */
        rk_xfer();
        rk_busy = RK_BUSY_POLLS;                  /* completion delay (dev_polls!) */
        return;
    default:                                      /* seek, dreset, wlock */
        rk_busy = RK_BUSY_POLLS;
        return;
    }
}

/* ---------------- irq + poll ---------------- */
void dev_reset(void) {
    rcsr = 0; rbuf = 0; xcsr = 0200;
    tt_irq_rx = tt_irq_tx = 0;
    rx_delay = tx_delay = 0;
    lks = 0; clk_irq = 0;
    rker = 0; rkcs = 0200; rkwc = rkba = rkda = 0;
    rk_irq = 0; rk_busy = 0; rk_pend_ide = 0;
}

void dev_poll(void) {
    int c;
    /* console input pacing (skip pacing while the CPU idles in WAIT —
     * the poll loop runs on wall-clock then, not instruction count) */
    if (cpu.waiting) { rx_delay = 0; if (tx_delay > 1) tx_delay = 1; }
    if (rx_delay > 0) rx_delay--;
    if (!(rcsr & 0200) && rx_delay <= 0 && inq_r != inq_w) {
        rbuf = inq[inq_r++ % sizeof inq];
        rcsr |= 0200;
        if (rcsr & 0100) tt_irq_rx = 1;
        rx_delay = RX_PACE;
    }
    while ((c = host_pollchar()) >= 0) cons_putc_host(c);
    /* console output completion */
    if (tx_delay > 0 && --tx_delay == 0) {
        xcsr |= 0200;
        if (xcsr & 0100) tt_irq_tx = 1;
    }
    /* clock */
    unsigned t = host_clock60();
    if (t) {
        lks |= 0200;
        if (lks & 0100) clk_irq = 1;
    }
    /* disk completion */
    if (rk_busy > 0 && --rk_busy == 0) {
        rkcs |= 0200;
        if (rk_pend_ide) rk_irq = 1;
    }
}

int dev_irq(int cpupri, int *vec) {
    if (clk_irq && cpupri < 6) { clk_irq = 0; *vec = V_CLK; return 1; }
    if (rk_irq && cpupri < 5) { rk_irq = 0; *vec = V_RK; return 1; }
    if (tt_irq_rx && cpupri < 4) { tt_irq_rx = 0; *vec = V_TTI; return 1; }
    if (tt_irq_tx && cpupri < 4) { tt_irq_tx = 0; *vec = V_TTO; return 1; }
    return 0;
}

/* ---------------- I/O page ---------------- */
uint16_t cpu_ps_read(void);
void cpu_ps_iowrite(uint16_t v);

static uint16_t io_read16(uint32_t pa) {
    switch (pa & 0777776) {
    case 0777546: return lks;
    case 0777560: return rcsr;
    case 0777562: rcsr &= ~0200; return rbuf;
    case 0777564: return xcsr;
    case 0777566: return 0;
    case 0777400: return 0700;                    /* RKDS: SOK|RDY|DRY */
    case 0777402: return rker;
    case 0777404: return rkcs;
    case 0777406: return rkwc;
    case 0777410: return rkba;
    case 0777412: return rkda;
    case 0777414: case 0777416: return 0;
    case 0777570: return sw_reg;                  /* console switches (173030 = single-user) */
    case 0777572: return SR0;
    case 0777574: return 0;                       /* SR1 absent on /40 */
    case 0777576: return cpu.SR2;
    case 0777774: return cpu.slr;
    case 0777776: return cpu_ps_read();
    default:
        if ((pa & 0777760) == 0772300) return cpu.pdr[0][(pa >> 1) & 7];
        if ((pa & 0777760) == 0772340) return cpu.par[0][(pa >> 1) & 7];
        if ((pa & 0777760) == 0777600) return cpu.pdr[1][(pa >> 1) & 7];
        if ((pa & 0777760) == 0777640) return cpu.par[1][(pa >> 1) & 7];
        cpu_trap(V_BUS);
        return 0;
    }
}

static void io_write16(uint32_t pa, uint16_t v) {
    switch (pa & 0777776) {
    case 0777546:
        lks = v & 0377;
        if ((lks & 0300) == 0300) clk_irq = 1;
        return;
    case 0777560:
        if ((v & 0100) && !(rcsr & 0100) && (rcsr & 0200)) tt_irq_rx = 1;
        rcsr = (uint16_t)((rcsr & ~0100) | (v & 0100));
        return;
    case 0777562: return;
    case 0777564:
        if ((v & 0100) && !(xcsr & 0100) && (xcsr & 0200)) tt_irq_tx = 1;
        xcsr = (uint16_t)((xcsr & ~0100) | (v & 0100));
        return;
    case 0777566:
        host_putchar(v & 0177);
        xcsr &= ~0200;
        tx_delay = TX_TIME;
        return;
    case 0777400: return;
    case 0777402: return;
    case 0777404: rk_go(v); return;
    case 0777406: rkwc = v; return;
    case 0777410: rkba = v; return;
    case 0777412: rkda = v; return;
    case 0777414: case 0777416: return;
    case 0777570: return;
    case 0777572: SR0 = v & 0160001; return;
    case 0777574: return;
    case 0777576: return;                         /* SR2 read-only */
    case 0777774: cpu.slr = v; return;
    case 0777776: cpu_ps_iowrite(v); return;
    default:
        if ((pa & 0777760) == 0772300) { cpu.pdr[0][(pa >> 1) & 7] = v & 077416; return; }
        if ((pa & 0777760) == 0772340) { cpu.par[0][(pa >> 1) & 7] = v & 07777; return; }
        if ((pa & 0777760) == 0777600) { cpu.pdr[1][(pa >> 1) & 7] = v & 077416; return; }
        if ((pa & 0777760) == 0777640) { cpu.par[1][(pa >> 1) & 7] = v & 07777; return; }
        cpu_trap(V_BUS);
    }
}

/* ---------------- physical access ---------------- */
uint16_t bus_read16(uint32_t pa) {
    pa &= 0777776;
    if ((pa >> 1) < MEMWORDS) return ram_rd16(pa);
    if (pa >= IOPAGE) return io_read16(pa);
    cpu_trap(V_BUS);                              /* NXM — v6 sizes memory with this */
    return 0;
}
void bus_write16(uint32_t pa, uint16_t v) {
    pa &= 0777776;
    if ((pa >> 1) < MEMWORDS) { ram_wr16(pa & ~1UL, v); return; }
    if (pa >= IOPAGE) { io_write16(pa, v); return; }
    cpu_trap(V_BUS);
}
uint8_t bus_read8(uint32_t pa) {
    uint16_t w = bus_read16(pa & ~1UL);
    return (pa & 1) ? (uint8_t)(w >> 8) : (uint8_t)w;
}
void bus_write8(uint32_t pa, uint8_t v) {
    if ((pa >> 1) < MEMWORDS) {
        uint16_t w = ram_rd16(pa & ~1UL);
        w = (pa & 1) ? (uint16_t)((w & 0377) | (v << 8))
                     : (uint16_t)((w & 0177400) | v);
        ram_wr16(pa & ~1UL, w);
        return;
    }
    uint16_t w = io_read16(pa & ~1UL);
    w = (pa & 1) ? (uint16_t)((w & 0377) | (v << 8))
                 : (uint16_t)((w & 0177400) | v);
    io_write16(pa & ~1UL, w);
}
