/* Mode 5 hires text console (512px -> 64 cols of 8px chars, 28 visible rows).
 *
 * BG1 in Mode 5 fetches tilemap entries at double rate: a 64x32 tilemap
 * (BG1SC size bit 0) gives 64 columns of 8x8 chars across 512 hires pixels.
 * The 32-row tilemap is used as a ring; scrolling = bump BG1VOFS by 8 and
 * clear the wrapped row, so a scroll dirties one row, not 28.
 *
 * VRAM: font tiles at word $0000 (tile# == ASCII code, 4bpp, plane0 only),
 * tilemap at word $4000. VRAM writes only happen in vblank (con_flush spins
 * for the next vblank when there are dirty rows; <=2 rows per vblank).
 */
#include <stdint.h>
#include "console.h"
#include "font8x8_basic.h"

#define R8(a) (*(volatile uint8_t *)(a))

#define COLS 64
#define ROWS 28                 /* visible; ring is 32 */
#define RING 32
#define MAP_VRAM 0x4000         /* word address */

static uint8_t shadow[RING][COLS];
static uint8_t dirty[RING];
static uint8_t top;             /* ring row shown at screen top */
static uint8_t row, col;        /* cursor, 0..ROWS-1 / 0..COLS-1 */
static uint8_t vofs_dirty;

static void vaddr(uint16_t a) { R8(0x2116) = (uint8_t)a; R8(0x2117) = (uint8_t)(a >> 8); }
static void vdata(uint16_t v) { R8(0x2118) = (uint8_t)v; R8(0x2119) = (uint8_t)(v >> 8); }

static uint8_t rev(uint8_t b) { /* font8x8 is LSB-left; SNES wants MSB-left */
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

static void set_vofs(void) {
    uint16_t v = (uint16_t)(((top * 8) - 1) & 0xFF);
    R8(0x210E) = (uint8_t)v;
    R8(0x210E) = (uint8_t)(v >> 8);
}

void con_init(void) {
    int ch, y, i;
    R8(0x2100) = 0x8F;                    /* force blank */
    R8(0x2105) = 0x05;                    /* Mode 5 */
    R8(0x2107) = (uint8_t)(((MAP_VRAM >> 10) << 2) | 1);   /* map, 64x32 */
    R8(0x210B) = 0x00;                    /* BG1 chars at $0000 */
    R8(0x212C) = 0x01;                    /* main screen: BG1 */
    R8(0x212D) = 0x00;
    R8(0x2130) = 0x00; R8(0x2131) = 0x00; /* no color math */
    R8(0x2133) = 0x00;
    R8(0x2121) = 0;                       /* palette: 0=black, 1=white */
    R8(0x2122) = 0x00; R8(0x2122) = 0x00;
    R8(0x2122) = 0xFF; R8(0x2122) = 0x7F;
    R8(0x2115) = 0x80;                    /* VRAM inc after high byte */

    vaddr(0);                             /* font: 128 chars, 4bpp, plane 0 only */
    for (ch = 0; ch < 128; ch++) {
        for (y = 0; y < 8; y++) vdata(rev((uint8_t)font8x8_basic[ch][y]));
        for (y = 0; y < 8; y++) vdata(0);
    }
    vaddr(MAP_VRAM);                      /* clear map to spaces */
    for (i = 0; i < 64 * 32; i++) vdata(0x20);

    for (y = 0; y < RING; y++) {
        for (i = 0; i < COLS; i++) shadow[y][i] = ' ';
        dirty[y] = 0;
    }
    top = 0; row = 0; col = 0;
    set_vofs();
    vofs_dirty = 0;
    R8(0x2100) = 0x0F;                    /* display on, full brightness */
}

static void newline(void) {
    int i;
    col = 0;
    if (row < ROWS - 1) { row++; return; }
    top = (uint8_t)((top + 1) % RING);    /* hardware scroll */
    {
        uint8_t r = (uint8_t)((top + ROWS - 1) % RING);
        for (i = 0; i < COLS; i++) shadow[r][i] = ' ';
        dirty[r] = 1;
    }
    vofs_dirty = 1;
}

void con_putc(int c) {
    uint8_t r;
    c &= 0x7F;
    if (c == '\n') { newline(); return; }
    if (c == '\r') { col = 0; return; }
    if (c == '\b') { if (col) col--; return; }
    if (c == 7) return;                   /* bell */
    if (c < 0x20) return;
    r = (uint8_t)((top + row) % RING);
    shadow[r][col] = (uint8_t)c;
    dirty[r] = 1;
    if (++col >= COLS) newline();
}

void con_puts(const char *s) { while (*s) con_putc(*s++); }

void con_flush(void) {
    int any = vofs_dirty, i, n = 0;
    uint8_t r;
    if (!any) for (r = 0; r < RING; r++) if (dirty[r]) { any = 1; break; }
    if (!any) return;
    while (R8(0x4212) & 0x80) ;           /* leave current vblank... */
    while (!(R8(0x4212) & 0x80)) ;        /* ...catch the start of the next */
    if (vofs_dirty) { set_vofs(); vofs_dirty = 0; }
    for (r = 0; r < RING && n < 2; r++) {
        if (!dirty[r]) continue;
        dirty[r] = 0; n++;
        vaddr((uint16_t)(MAP_VRAM + r * 64));
        for (i = 0; i < COLS; i++) vdata(shadow[r][i]);
    }
}
