 
#ifndef HW_H
#define HW_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int32_t  s32;

/* ---- LSPC / VRAM port (0x3C0000) ------------------------------------ */
#define REG_VRAMADDR (*(volatile u16 *)0x3C0000) /* VRAM word address      */
#define REG_VRAMRW   (*(volatile u16 *)0x3C0002) /* read/write data        */
#define REG_VRAMMOD  (*(volatile u16 *)0x3C0004) /* auto-increment modulo  */
#define REG_LSPCMODE (*(volatile u16 *)0x3C0006) /* mode / raster (see vbl) */

/* ---- Inputs / system (active LOW) ----------------------------------- */
#define REG_P1CNT    (*(volatile u8  *)0x300000) /* U D L R A B C D (bit0..7)*/
#define REG_WATCHDOG (*(volatile u8  *)0x300001) /* write anything to kick   */
#define REG_STATUS_B (*(volatile u8  *)0x380000) /* start/select etc.        */

/* ---- Palette RAM (directly CPU addressable) ------------------------- */
#define PALRAM       ((volatile u16 *)0x400000)  /* 256 palettes * 16 colors */
#define REG_BACKDROP (*(volatile u16 *)0x401FFE) /* last entry = backdrop    */

/* ---- VRAM map: Sprite Control Blocks -------------------------------- */
#define VRAM_SCB1 0x0000  /* 64 words/sprite: tilemap + attributes          */
#define VRAM_FIX  0x7000  /* fix layer map (8x8 text, drawn OVER sprites)    */
#define VRAM_SCB2 0x8000  /* 1 word/sprite: (hshrink<<8)|vshrink            */
#define VRAM_SCB3 0x8200  /* 1 word/sprite: (Yfield<<7)|(sticky<<6)|size    */
#define VRAM_SCB4 0x8400  /* 1 word/sprite: (X&0x1FF)<<7                    */

/* ---- VRAM access helpers -------------------------------------------- */
static inline void vram_addr(u16 a) { REG_VRAMADDR = a; }
static inline void vram_mod(u16 m)  { REG_VRAMMOD = m; }
static inline void vram_w(u16 v)    { REG_VRAMRW = v; }
static inline void vram_poke(u16 a, u16 v) { REG_VRAMADDR = a; REG_VRAMRW = v; }

/* ---- watchdog: must be kicked every frame or the board resets ------- */
static inline void watchdog_kick(void) { REG_WATCHDOG = 0; }

 
static inline u16 RGB(u8 r, u8 g, u8 b) {
    return (u16)(((r & 1) << 14) | ((g & 1) << 13) | ((b & 1) << 12) |
                 ((r >> 1) << 8) | ((g >> 1) << 4) | (b >> 1));
}

static inline void pal_set(u16 pal, u16 idx, u16 color) {
    PALRAM[pal * 16 + idx] = color;
}

 
static inline u16 fix_word(u16 pal, u16 tile) {
    return (u16)((pal << 12) | (tile & 0x0FFF));
}
static inline void fix_poke(u16 col, u16 row, u16 pal, u16 tile) {
    vram_poke((u16)(VRAM_FIX + col * 32 + row), fix_word(pal, tile));
}

/* ---- Sprite Control Block writers ----------------------------------- */
static inline void scb1_tile(u16 spr, u16 row, u16 tile, u16 pal) {
    vram_addr(VRAM_SCB1 + spr * 64 + row * 2);
    vram_mod(1);
    vram_w(tile);
    vram_w((u16)(pal << 8));
}

/* Fill a sprite's whole tilemap window with one tile/palette. */
static inline void scb1_fill(u16 spr, u16 ntiles, u16 tile, u16 pal) {
    vram_addr(VRAM_SCB1 + spr * 64);
    vram_mod(1);
    for (u16 i = 0; i < ntiles; i++) { vram_w(tile); vram_w((u16)(pal << 8)); }
}

static inline void scb2(u16 spr, u16 hshrink, u16 vshrink) {
    vram_poke(VRAM_SCB2 + spr, (u16)((hshrink << 8) | vshrink));
}

/* top: pixel row of the sprite's top edge (may be <0; caller clamps). */
static inline u16 scb3_word(int top, u16 sticky, u16 size) {
    return (u16)((((496 - top) & 0x1FF) << 7) | (sticky << 6) | (size & 0x3F));
}
static inline void scb3(u16 spr, int top, u16 sticky, u16 size) {
    vram_poke(VRAM_SCB3 + spr, scb3_word(top, sticky, size));
}

static inline void scb4(u16 spr, u16 x) {
    vram_poke(VRAM_SCB4 + spr, (u16)((x & 0x1FF) << 7));
}

 
static inline int in_vblank(void) { return (REG_LSPCMODE & 0x8000) == 0; }

static inline u8 wait_vblank_status(void) {
    u8 overrun = (u8)in_vblank();
    while (in_vblank())  { }   /* finish any current vblank   */
    while (!in_vblank()) { }   /* wait for the next one        */
    return overrun;
}

static inline void wait_vblank(void) {
    (void)wait_vblank_status();
}

#endif /* HW_H */
