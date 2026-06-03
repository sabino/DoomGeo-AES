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
#define RECIP_LUT_SIZE 2048
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
    if (idx < RECIP_LUT_SIZE) return recip_lut[idx];

    /* The old 256-entry LUT saturated every distance past ~2 map cells to
     * one reciprocal, so far walls/sprites stopped shrinking and then popped
     * out instead of preserving the perspective falloff.  Keep the Neo Geo
     * fast table for the common near/mid range, but fall back to one exact
     * reciprocal for true long corridors. */
    return (fix)(0x100000000ULL / ab);
}

#define FBIG (1 << 28)            
#define FMIN (FONE >> 6)          /* clamp tiny distances                   */
#define PLAYER_RADIUS (FONE / 5)  /* Doom-ish collision body, in map cells   */
 
static fix posX, posY;           /* world position (1.0 == one map cell)    */
static fix dirX, dirY;           /* facing direction (unit)                 */
static fix planeX, planeY;       /* camera plane (sets FOV; |plane|~0.66)   */
static fix invDet;               /* inverse camera matrix determinant       */
static fix cameraXbuf[NUM_COLS]; /* constant camera x in [-1,+1] per column */

static u16 scb2buf[NUM_COLS];    /* (HSHRINK<<8)|vshrink                    */
static u16 scb3buf[NUM_COLS];    /* Y/size word                             */
static u16 curscb2[NUM_COLS];    /* control words currently in VRAM         */
static u16 curscb3[NUM_COLS];
static u8  palbuf[NUM_COLS];     /* desired palette this frame              */
static u8  curpal[NUM_COLS];     /* palette currently in VRAM (cache)       */
static u8  texbuf[NUM_COLS];     /* wall texture atlas column this frame    */
static u8  curtex[NUM_COLS];     /* atlas column currently in VRAM          */
static u8  kindbuf[NUM_COLS];    /* 0 = wall, 1..N = alt wall, N+1 = door   */
static u8  curkind[NUM_COLS];
static fix distbuf[NUM_COLS];    /* perpendicular wall distance             */
static u16 wall_tiles[TILE_WALL_ATLAS_COLS][WALL_WIN];
static u16 wall_alt_tiles[TILE_WALL_ALT_COUNT][TILE_WALL_ATLAS_COLS][WALL_WIN];
static u16 door_tiles[TILE_WALL_ATLAS_COLS][WALL_WIN];
static u8  view_dirty = 1;
static u8  wall_upload_dirty = 1;

static inline int projected_height_from_inv(fix inv_dist) {
    int h = (int)(((s32)WALLH * inv_dist) >> FBITS);
    return h < 1 ? 1 : h;
}

static inline int projected_height(fix dist) {
    return projected_height_from_inv(recip(dist));
}

static void update_projection_cache(void) {
    fix det = fmul(planeX, dirY) - fmul(dirX, planeY);
    if (det > -FMIN && det < FMIN) invDet = 0;
    else invDet = fdiv(FONE, det);
}

void rc_init(void) {
    init_recip_lut();
    for (int tx = 0; tx < TILE_WALL_ATLAS_COLS; tx++) {
        for (int row = 0; row < WALL_WIN; row++) {
            int ty = (row * TILE_WALL_ATLAS_ROWS) / WALL_WIN;
            wall_tiles[tx][row] = (u16)(TILE_WALL_ATLAS_BASE + ty * TILE_WALL_ATLAS_COLS + tx);
            for (int alt = 0; alt < TILE_WALL_ALT_COUNT; alt++) {
                wall_alt_tiles[alt][tx][row] = (u16)(TILE_WALL_ALT_ATLAS_BASE + alt * TILE_WALL_ATLAS_TILES + ty * TILE_WALL_ATLAS_COLS + tx);
            }
            door_tiles[tx][row] = (u16)(TILE_DOOR_ATLAS_BASE + ty * TILE_WALL_ATLAS_COLS + tx);
        }
    }
    for (int c = 0; c < NUM_COLS; c++) {
        cameraXbuf[c] = (fix)(((int64_t)2 * FONE * c) / (NUM_COLS - 1)) - FONE;
    }
    posX = FIX(DOOM_START_X);
    posY = FIX(DOOM_START_Y);
    dirX = FIX(DOOM_DIR_X);
    dirY = FIX(DOOM_DIR_Y);
    planeX = FIX(DOOM_PLANE_X);
    planeY = FIX(DOOM_PLANE_Y);
    update_projection_cache();
    for (int c = 0; c < NUM_COLS; c++) {
        curpal[c] = 0xFF; /* force first write */
        curtex[c] = 0xFF;
        curkind[c] = 0xFF;
        curscb2[c] = 0xFFFF;
        curscb3[c] = 0xFFFF;
    }
    rc_invalidate_view();
}

void rc_invalidate_view(void) {
    view_dirty = 1;
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
    update_projection_cache();
    rc_invalidate_view();
}

static u8 player_can_occupy(fix x, fix y) {
    int cx = x >> FBITS;
    int cy = y >> FBITS;
    if (map_at(cx, cy)) return 0;
    if (map_at((x - PLAYER_RADIUS) >> FBITS, cy)) return 0;
    if (map_at((x + PLAYER_RADIUS) >> FBITS, cy)) return 0;
    if (map_at(cx, (y - PLAYER_RADIUS) >> FBITS)) return 0;
    if (map_at(cx, (y + PLAYER_RADIUS) >> FBITS)) return 0;
    return 1;
}

static void try_move(fix dx, fix dy) {
    fix nx = posX + dx, ny = posY + dy;
    fix ox = posX, oy = posY;
    if (player_can_occupy(nx, posY)) posX = nx;
    if (player_can_occupy(posX, ny)) posY = ny;
    if (posX != ox || posY != oy) rc_invalidate_view();
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

void rc_player_q8(int *x_q8, int *y_q8) {
    *x_q8 = posX >> (FBITS - 8);
    *y_q8 = posY >> (FBITS - 8);
}

void rc_view_q8(int *view_dir_x, int *view_dir_y, int *view_plane_x, int *view_plane_y) {
    *view_dir_x = dirX >> (FBITS - 8);
    *view_dir_y = dirY >> (FBITS - 8);
    *view_plane_x = planeX >> (FBITS - 8);
    *view_plane_y = planeY >> (FBITS - 8);
}

int rc_project_point(int world_x_q8, int world_y_q8, int *screen_x, int *height, int *dist_q8) {
    fix spriteX = ((fix)world_x_q8 << (FBITS - 8)) - posX;
    fix spriteY = ((fix)world_y_q8 << (FBITS - 8)) - posY;
    if (!invDet) return 0;

    fix transformX = fmul(invDet, fmul(dirY, spriteX) - fmul(dirX, spriteY));
    fix transformY = fmul(invDet, -fmul(planeY, spriteX) + fmul(planeX, spriteY));
    if (transformY < (FONE >> 3)) return 0;

    fix invY = recip(transformY);
    fix scaleX = fmul(transformX, invY);
    int sx = (SCRW / 2) + (int)(((s32)(SCRW / 2) * scaleX) >> FBITS);
    int h = projected_height_from_inv(invY);
    if (h < 1) return 0;
    if (h > GAME_H) h = GAME_H;

    if (sx >= 0 && sx < SCRW) {
        int col = sx / COLW;
        if (col >= 0 && col < NUM_COLS && transformY > distbuf[col] + (FONE >> 3)) return 0;
    }

    *screen_x = sx;
    *height = h;
    *dist_q8 = (int)(transformY >> (FBITS - 8));
    return 1;
}

void rc_render(void) {
    if (!view_dirty) return;
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
        unsigned char hit_cell = 1;
        for (;;) {
            if (sideX < sideY) { sideX += ddX; mapX += stepX; side = 0; }
            else               { sideY += ddY; mapY += stepY; side = 1; }
            if (map_at(mapX, mapY)) {
                hit_cell = map_cell_value(mapX, mapY);
                break;
            }
        }
        kindbuf[x] = (hit_cell >= 2) ? (TILE_WALL_ALT_COUNT + 1) : map_cell_texture(mapX, mapY);

        fix perp = (side == 0) ? (sideX - ddX) : (sideY - ddY);
        if (perp < FMIN) perp = FMIN;
        distbuf[x] = perp;

        {
            fix wall = (side == 0) ? posY + fmul(perp, rayY) : posX + fmul(perp, rayX);
            int tex_x = (int)(((wall & (FONE - 1)) * TILE_WALL_ATLAS_COLS) >> FBITS);
            if (tex_x < 0) tex_x = 0;
            if (tex_x >= TILE_WALL_ATLAS_COLS) tex_x = TILE_WALL_ATLAS_COLS - 1;
            tex_x = (tex_x + map_cell_texture_phase(mapX, mapY)) & (TILE_WALL_ATLAS_COLS - 1);
            texbuf[x] = (u8)tex_x;
        }

        fix inv_perp = recip(perp);
        int h = projected_height_from_inv(inv_perp);     /* slice height px */
        if (h < 1)     h = 1;
        if (h > MAX_H) h = MAX_H;

        int top = (GAME_H - h) / 2;         /* >=0 because h<=GAME_H         */
        int vsh = h - 1;                    /* on-screen px = vshrink+1      */
        if (vsh < 0)   vsh = 0;
        if (vsh > 255) vsh = 255;

        scb2buf[x] = (u16)((HSHRINK << 8) | (vsh & 0xFF));
		
        scb3buf[x] = scb3_word(top, 0, WALL_WIN);

        /* distance shading */
        int band = ((MAX_H - h) * DEPTH_BANDS) / MAX_H;
        if (band < 0) band = 0;
        if (band >= DEPTH_BANDS) band = DEPTH_BANDS - 1;
        palbuf[x] = (u8)(((kindbuf[x] > TILE_WALL_ALT_COUNT) ? PAL_DOOR_DEPTH_BASE : (kindbuf[x] ? PAL_WALL_ALT_DEPTH_BASE + (kindbuf[x] - 1) * PAL_WALL_ALT_DEPTH_STRIDE : PAL_DEPTH_BASE)) + (side ? DEPTH_BANDS : 0) + band);
    }
    view_dirty = 0;
    wall_upload_dirty = 1;
}

void rc_blit(void) {
    int scb2_changes = 0;
    int scb3_changes = 0;
    if (!wall_upload_dirty) return;

    for (int c = 0; c < NUM_COLS; c++) {
        if (scb2buf[c] != curscb2[c]) scb2_changes++;
        if (scb3buf[c] != curscb3[c]) scb3_changes++;
    }

    if (scb2_changes > NUM_COLS / 2) {
        vram_addr(VRAM_SCB2 + WALL_BASE);
        vram_mod(1);
        for (int c = 0; c < NUM_COLS; c++) {
            vram_w(scb2buf[c]);
            curscb2[c] = scb2buf[c];
        }
    } else {
        for (int c = 0; c < NUM_COLS; c++) {
            if (scb2buf[c] == curscb2[c]) continue;
            vram_poke((u16)(VRAM_SCB2 + WALL_BASE + c), scb2buf[c]);
            curscb2[c] = scb2buf[c];
        }
    }

    if (scb3_changes > NUM_COLS / 2) {
        vram_addr(VRAM_SCB3 + WALL_BASE);
        vram_mod(1);
        for (int c = 0; c < NUM_COLS; c++) {
            vram_w(scb3buf[c]);
            curscb3[c] = scb3buf[c];
        }
    } else {
        for (int c = 0; c < NUM_COLS; c++) {
            if (scb3buf[c] == curscb3[c]) continue;
            vram_poke((u16)(VRAM_SCB3 + WALL_BASE + c), scb3buf[c]);
            curscb3[c] = scb3buf[c];
        }
    }

    /* directional shading */
    for (int c = 0; c < NUM_COLS; c++) {
        if (texbuf[c] != curtex[c] || kindbuf[c] != curkind[c]) {
            u16 spr = WALL_BASE + c;
            u16 *tiles = (kindbuf[c] > TILE_WALL_ALT_COUNT) ? door_tiles[texbuf[c]] : (kindbuf[c] ? wall_alt_tiles[kindbuf[c] - 1][texbuf[c]] : wall_tiles[texbuf[c]]);
            if (palbuf[c] != curpal[c]) {
                u16 attr = (u16)(palbuf[c] << 8);
                vram_addr(VRAM_SCB1 + spr * 64);
                vram_mod(1);
                for (int t = 0; t < WALL_WIN; t++) {
                    vram_w(tiles[t]);
                    vram_w(attr);
                }
                curpal[c] = palbuf[c];
            } else {
                vram_addr(VRAM_SCB1 + spr * 64);
                vram_mod(2);
                for (int t = 0; t < WALL_WIN; t++) vram_w(tiles[t]);
            }
            curtex[c] = texbuf[c];
            curkind[c] = kindbuf[c];
            continue;
        }
        if (palbuf[c] == curpal[c]) continue;
        u16 spr = WALL_BASE + c;
        vram_addr(VRAM_SCB1 + spr * 64 + 1);
        vram_mod(2);                            
        u16 attr = (u16)(palbuf[c] << 8);
        for (int t = 0; t < WALL_WIN; t++) vram_w(attr);
        curpal[c] = palbuf[c];
    }
    wall_upload_dirty = 0;
}
