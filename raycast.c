/* raycast.c - fixed-point DDA raycaster mapping wall slices onto the Neo
 * Geo's hardware sprite shrinker */
#include "raycast.h"
#include "config.h"
#include "map.h"

/* ---- 16.16 fixed point ---------------------------------------------- */
typedef s32 fix;
#define FBITS 16
#define FONE  (1 << FBITS)
#define FIX(x) ((fix)((x) * (double)FONE))   /* constant initializers only  */

static inline fix fmul(fix a, fix b) { return (fix)(((int64_t)a * b) >> FBITS); }
static inline fix fdiv(fix a, fix b) { return (fix)(((int64_t)a << FBITS) / b); }
static inline fix fabsx(fix a)       { return a < 0 ? -a : a; }
 
#define RECIP_SHIFT    9
#define RECIP_LUT_SIZE 256
#define RECIP_MIN      (FONE >> 8)

static fix recip_lut[RECIP_LUT_SIZE];

static void init_recip_lut(void) {
    for (int i = 0; i < RECIP_LUT_SIZE; i++) {
        u32 ab = (u32)(i << RECIP_SHIFT);
        if (ab < (u32)RECIP_MIN) ab = RECIP_MIN;
        recip_lut[i] = (fix)(0x100000000ULL / ab);
    }
}

static inline fix recip(fix b) {
    u32 ab = (b < 0) ? (u32)(-b) : (u32)b;
    u32 idx;
    if (ab < (u32)RECIP_MIN) return recip_lut[0];
    idx = ab >> RECIP_SHIFT;
    if (idx >= RECIP_LUT_SIZE) idx = RECIP_LUT_SIZE - 1;
    return recip_lut[idx];
}

#define FBIG (1 << 28)            
#define FMIN (FONE >> 6)          /* clamp tiny distances                   */
 
static fix posX, posY;           /* world position (1.0 == one map cell)    */
static fix dirX, dirY;           /* facing direction (unit)                 */
static fix planeX, planeY;       /* camera plane (sets FOV; |plane|~0.66)   */
static fix cameraXbuf[NUM_COLS]; /* constant camera x in [-1,+1] per column */

static u16 scb2buf[NUM_COLS];    /* (HSHRINK<<8)|vshrink                    */
static u16 scb3buf[NUM_COLS];    /* Y/size word                             */
static u8  palbuf[NUM_COLS];     /* desired palette this frame              */
static u8  curpal[NUM_COLS];     /* palette currently in VRAM (cache)       */

void rc_init(void) {
    init_recip_lut();
    for (int c = 0; c < NUM_COLS; c++) {
        cameraXbuf[c] = (fix)(((int64_t)2 * FONE * c) / (NUM_COLS - 1)) - FONE;
    }
    posX = FIX(DOOM_START_X);
    posY = FIX(DOOM_START_Y);
    dirX = FIX(DOOM_DIR_X);
    dirY = FIX(DOOM_DIR_Y);
    planeX = FIX(DOOM_PLANE_X);
    planeY = FIX(DOOM_PLANE_Y);
    for (int c = 0; c < NUM_COLS; c++) curpal[c] = 0xFF; /* force first write */
}

static void rotate(int sign) {
    fix cs = FIX(ROT_COS);
    fix sn = sign < 0 ? -FIX(ROT_SIN) : FIX(ROT_SIN);
    fix ox = dirX;
    dirX = fmul(dirX, cs) - fmul(dirY, sn);
    dirY = fmul(ox,   sn) + fmul(dirY, cs);
    fix opx = planeX;
    planeX = fmul(planeX, cs) - fmul(planeY, sn);
    planeY = fmul(opx,    sn) + fmul(planeY, cs);
}

static void try_move(fix dx, fix dy) {
    fix nx = posX + dx, ny = posY + dy;
    if (!map_at(nx >> FBITS, posY >> FBITS)) posX = nx;
    if (!map_at(posX >> FBITS, ny >> FBITS)) posY = ny;
}

void rc_input(u8 pressed) {
    enum { UP=1, DOWN=2, LEFT=4, RIGHT=8, A=16 };
    fix spd = FIX(MOVE_SPEED);
    if (pressed & UP)   try_move(fmul(dirX, spd), fmul(dirY, spd));
    if (pressed & DOWN) try_move(-fmul(dirX, spd), -fmul(dirY, spd));
    if (pressed & A) {                              /* strafe with A held    */
        if (pressed & LEFT)  try_move(-fmul(planeX, spd), -fmul(planeY, spd));
        if (pressed & RIGHT) try_move( fmul(planeX, spd),  fmul(planeY, spd));
    } else {
        if (pressed & LEFT)  rotate(-1);
        if (pressed & RIGHT) rotate(+1);
    }
}

void rc_player_cell(int *cx, int *cy) {
    *cx = posX >> FBITS;
    *cy = posY >> FBITS;
}

void rc_render(void) {
    for (int x = 0; x < NUM_COLS; x++) {
        fix cameraX = cameraXbuf[x];
        fix rayX = dirX + fmul(planeX, cameraX);
        fix rayY = dirY + fmul(planeY, cameraX);

        int mapX = posX >> FBITS;
        int mapY = posY >> FBITS;

        fix ddX = recip(rayX);
        fix ddY = recip(rayY);

        int stepX, stepY;
        fix sideX, sideY;
        if (rayX < 0) { stepX = -1; sideX = fmul(posX - (mapX << FBITS), ddX); }
        else          { stepX =  1; sideX = fmul(((mapX + 1) << FBITS) - posX, ddX); }
        if (rayY < 0) { stepY = -1; sideY = fmul(posY - (mapY << FBITS), ddY); }
        else          { stepY =  1; sideY = fmul(((mapY + 1) << FBITS) - posY, ddY); }

        int side = 0;                       /* 0 = hit on X grid line (N/S)  */
        for (;;) {
            if (sideX < sideY) { sideX += ddX; mapX += stepX; side = 0; }
            else               { sideY += ddY; mapY += stepY; side = 1; }
            if (map_at(mapX, mapY)) break;
        }

        fix perp = (side == 0) ? (sideX - ddX) : (sideY - ddY);
        if (perp < FMIN) perp = FMIN;

        int h =  (int)(((int64_t)WALLH << FBITS) / perp);  /* slice height px */
        if (h < 1)     h = 1;
        if (h > MAX_H) h = MAX_H;

        int top = (SCRH - h) / 2;           /* >=0 because h<=SCRH           */
        int vsh = h - 1;                    /* on-screen px = vshrink+1      */
        if (vsh < 0)   vsh = 0;
        if (vsh > 255) vsh = 255;

        scb2buf[x] = (u16)((HSHRINK << 8) | (vsh & 0xFF));
		
        scb3buf[x] = scb3_word(top, 0, WALL_WIN);

        /* distance shading */
        int band = ((MAX_H - h) * DEPTH_BANDS) / MAX_H;
        if (band < 0) band = 0;
        if (band >= DEPTH_BANDS) band = DEPTH_BANDS - 1;
        palbuf[x] = (u8)(PAL_DEPTH_BASE + (side ? DEPTH_BANDS : 0) + band);
    }
}

void rc_blit(void) {
    /* stream vertical shrink for every wall slice */
    vram_addr(VRAM_SCB2 + WALL_BASE);
    vram_mod(1);
    for (int c = 0; c < NUM_COLS; c++) vram_w(scb2buf[c]);

    /* stream Y/size */
    vram_addr(VRAM_SCB3 + WALL_BASE);
    vram_mod(1);
    for (int c = 0; c < NUM_COLS; c++) vram_w(scb3buf[c]);

    /* directional shading */
    for (int c = 0; c < NUM_COLS; c++) {
        if (palbuf[c] == curpal[c]) continue;
        u16 spr = WALL_BASE + c;
        vram_addr(VRAM_SCB1 + spr * 64 + 1);
        vram_mod(2);                            
        u16 attr = (u16)(palbuf[c] << 8);
        for (int t = 0; t < WALL_WIN; t++) vram_w(attr);
        curpal[c] = palbuf[c];
    }
}
