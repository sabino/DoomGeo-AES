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
#define PLAYER_RADIUS ((FONE / 5) * MAP_RENDER_SCALE)  /* Doom-ish collision body */
#ifndef PORTAL_SPAN_DRAW_MIN_H
#define PORTAL_SPAN_DRAW_MIN_H 3
#endif
#ifndef PORTAL_SPAN_OCCLUDE_MIN_H
#define PORTAL_SPAN_OCCLUDE_MIN_H 5
#endif
#ifndef PORTAL_SPAN_REPLACE_MIN_H
#define PORTAL_SPAN_REPLACE_MIN_H 18
#endif
#ifndef PORTAL_SPAN_REPLACE_FAR_DIV
#define PORTAL_SPAN_REPLACE_FAR_DIV 2
#endif
 
static fix posX, posY;           /* world position (1.0 == one map cell)    */
static fix dirX, dirY;           /* facing direction (unit)                 */
static fix planeX, planeY;       /* camera plane (sets FOV; |plane|~0.66)   */
static fix invDet;               /* inverse camera matrix determinant       */
static fix cameraXbuf[NUM_COLS]; /* constant camera x in [-1,+1] per column */
static fix rayXbuf[NUM_COLS];    /* cached ray directions for current view  */
static fix rayYbuf[NUM_COLS];
static fix rayDdXbuf[NUM_COLS];  /* DDA delta distances for cached rays     */
static fix rayDdYbuf[NUM_COLS];
static signed char rayStepXbuf[NUM_COLS];
static signed char rayStepYbuf[NUM_COLS];
static u8  rays_dirty = 1;

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
static u8  closebuf[NUM_COLS];   /* reserved for emergency coarse-wall mips */
static u8  curclose[NUM_COLS];
static fix distbuf[NUM_COLS];    /* perpendicular wall distance             */
static u16 wall_tiles[TILE_WALL_ATLAS_COLS][WALL_WIN];
static u16 wall_alt_tiles[TILE_WALL_ALT_COUNT][TILE_WALL_ATLAS_COLS][WALL_WIN];
static u16 door_tiles[TILE_WALL_ATLAS_COLS][WALL_WIN];
static u8  view_dirty = 1;
static u8  wall_upload_dirty = 1;
static u8  wall_upload_scan = 0;
static u8  wall_first_upload = 1;
static u8  wall_frame_overrun = 0;
static u8  render_motion_active = 0;

static inline int projected_height_from_inv(fix inv_dist) {
    int h = (int)((((s32)WALLH * MAP_RENDER_SCALE) * inv_dist) >> FBITS);
    return h < 1 ? 1 : h;
}

static inline int projected_height(fix dist) {
    return projected_height_from_inv(recip(dist));
}

static inline int projected_span_height(fix dist, u8 span_height) {
    int full_h = projected_height(dist);
    int h = (full_h * span_height + 63) / 128;
    return h < 2 ? 2 : h;
}

static void update_projection_cache(void) {
    fix det = fmul(planeX, dirY) - fmul(dirX, planeY);
    if (det > -FMIN && det < FMIN) invDet = 0;
    else invDet = fdiv(FONE, det);
    rays_dirty = 1;
}

static void update_ray_cache(void) {
    if (!rays_dirty) return;
    for (int c = 0; c < NUM_COLS; c++) {
        fix cameraX = cameraXbuf[c];
        fix rayX = dirX + fmul(planeX, cameraX);
        fix rayY = dirY + fmul(planeY, cameraX);
        rayXbuf[c] = rayX;
        rayYbuf[c] = rayY;
        rayDdXbuf[c] = recip(rayX);
        rayDdYbuf[c] = recip(rayY);
        rayStepXbuf[c] = rayX < 0 ? -1 : 1;
        rayStepYbuf[c] = rayY < 0 ? -1 : 1;
    }
    rays_dirty = 0;
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
        curclose[c] = 0xFF;
        curscb2[c] = 0xFFFF;
        curscb3[c] = 0xFFFF;
    }
    rc_invalidate_view();
}

void rc_invalidate_view(void) {
    view_dirty = 1;
}

void rc_set_pose_q8(short x_q8, short y_q8, short dir_x_q8, short dir_y_q8) {
    posX = ((fix)x_q8) << (FBITS - 8);
    posY = ((fix)y_q8) << (FBITS - 8);
    dirX = ((fix)dir_x_q8) << (FBITS - 8);
    dirY = ((fix)dir_y_q8) << (FBITS - 8);
    planeX = -fmul(dirY, FIX(0.66));
    planeY =  fmul(dirX, FIX(0.66));
    update_projection_cache();
    rc_invalidate_view();
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

static inline signed char input_axis(u8 pressed, u8 neg, u8 pos) {
    u8 n = pressed & neg;
    u8 p = pressed & pos;
    if (n == p) return 0;
    return n ? -1 : 1;
}

void rc_input(u8 pressed) {
    enum { UP=1, DOWN=2, LEFT=4, RIGHT=8, A=16 };
    fix spd = FIX(MOVE_SPEED * MAP_RENDER_SCALE);
    signed char forward = input_axis(pressed, DOWN, UP);
    signed char side = 0;
    signed char turn = 0;
    if (pressed & A) {                              /* strafe with A held    */
        side = input_axis(pressed, LEFT, RIGHT);
    } else {
        turn = input_axis(pressed, LEFT, RIGHT);
    }
    render_motion_active = (u8)((forward || side || turn) ? 1 : 0);
    if (forward || side) {
        fix dx = 0;
        fix dy = 0;
        if (forward && side) spd = (fix)(((s32)spd * 181) >> 8); /* ~1/sqrt(2) */
        if (forward) {
            fix step_x = fmul(dirX, spd);
            fix step_y = fmul(dirY, spd);
            if (forward > 0) { dx += step_x; dy += step_y; }
            else             { dx -= step_x; dy -= step_y; }
        }
        if (side) {
            fix step_x = fmul(planeX, spd);
            fix step_y = fmul(planeY, spd);
            if (side > 0) { dx += step_x; dy += step_y; }
            else          { dx -= step_x; dy -= step_y; }
        }
        try_move(dx, dy);
    }
    if (turn) rotate(turn);
}

void rc_player_cell(int *cx, int *cy) {
    *cx = posX >> FBITS;
    *cy = posY >> FBITS;
}

void rc_player_q8(int *x_q8, int *y_q8) {
    *x_q8 = posX >> (FBITS - 8);
    *y_q8 = posY >> (FBITS - 8);
}

void rc_dir_q8(int *view_dir_x, int *view_dir_y) {
    *view_dir_x = dirX >> (FBITS - 8);
    *view_dir_y = dirY >> (FBITS - 8);
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
        if (col >= 0 && col < NUM_COLS) {
            u8 visible = 0;
            for (int dc = -2; dc <= 2; dc++) {
                int sample_col = col + dc;
                if (sample_col < 0 || sample_col >= NUM_COLS) continue;
                if (transformY <= distbuf[sample_col] + (FONE >> 3)) {
                    visible = 1;
                    break;
                }
            }
            if (!visible) return 0;
        }
    }

    *screen_x = sx;
    *height = h;
    *dist_q8 = (int)(transformY >> (FBITS - 8));
    return 1;
}

static u8 rc_refine_render_line_hit(fix rayX, fix rayY, int cell_x, int cell_y, u8 accept_mode, fix *dist, u8 *kind, u8 *tex, int *side, u8 *span, u8 *span_height) {
#if DOOM_RENDER_LINES
    unsigned char cell_count = g_render_cell_count[cell_y][cell_x];
    unsigned short cell_start;
    if (!cell_count) return 0;

    int pos_x_q4 = (int)(posX >> (FBITS - 4));
    int pos_y_q4 = (int)(posY >> (FBITS - 4));
    int ray_x_q4 = (int)(rayX >> (FBITS - 4));
    int ray_y_q4 = (int)(rayY >> (FBITS - 4));
    int best_t_q8 = 0x7FFFFFFF;
    int best_u_q8 = 0;
    int best_line = -1;

    cell_start = g_render_cell_start[cell_y][cell_x];
    for (unsigned char n = 0; n < cell_count; n++) {
        int i = g_render_cell_lines[cell_start + n];
        u8 line_span = g_render_lines[i].span;
        if (accept_mode == 1) {
            if (!line_span) continue;
        } else if (accept_mode == 0 && line_span) {
            continue;
        }
        int x1 = g_render_lines[i].x1_q8 >> 4;
        int y1 = g_render_lines[i].y1_q8 >> 4;
        int x2 = g_render_lines[i].x2_q8 >> 4;
        int y2 = g_render_lines[i].y2_q8 >> 4;
        int seg_x = x2 - x1;
        int seg_y = y2 - y1;
        u8 flags = g_render_lines[i].flags;
        int denom = ray_x_q4 * seg_y - ray_y_q4 * seg_x;
        int rel_x;
        int rel_y;
        int num_t;
        int num_u;
        int t_q8;
        int u_q8;

        if (denom == 0) continue;
        if (flags & (NG_RENDER_SIDE_POS | NG_RENDER_SIDE_NEG)) {
            int side_cross = seg_x * (pos_y_q4 - y1) - seg_y * (pos_x_q4 - x1);
            if (side_cross > 0) {
                if (!(flags & NG_RENDER_SIDE_POS)) continue;
            } else if (side_cross < 0) {
                if (!(flags & NG_RENDER_SIDE_NEG)) continue;
            }
        }
        rel_x = x1 - pos_x_q4;
        rel_y = y1 - pos_y_q4;
        num_t = rel_x * seg_y - rel_y * seg_x;
        num_u = rel_x * ray_y_q4 - rel_y * ray_x_q4;
        if (denom < 0) {
            denom = -denom;
            num_t = -num_t;
            num_u = -num_u;
        }
        if (num_t <= 0 || num_u < 0 || num_u > denom) continue;
        t_q8 = (num_t << 8) / denom;
        if (t_q8 < 24 || t_q8 >= best_t_q8) continue;
        u_q8 = (num_u << 8) / denom;
        best_t_q8 = t_q8;
        best_u_q8 = u_q8;
        best_line = i;
    }

    if (best_line >= 0) {
        int seg_x = (g_render_lines[best_line].x2_q8 - g_render_lines[best_line].x1_q8);
        int seg_y = (g_render_lines[best_line].y2_q8 - g_render_lines[best_line].y1_q8);
        int abs_x = seg_x < 0 ? -seg_x : seg_x;
        int abs_y = seg_y < 0 ? -seg_y : seg_y;
        int tex_x = ((best_u_q8 * TILE_WALL_ATLAS_COLS) >> 8) & (TILE_WALL_ATLAS_COLS - 1);
        tex_x = (tex_x + g_render_lines[best_line].phase) & (TILE_WALL_ATLAS_COLS - 1);
        *dist = ((fix)best_t_q8) << (FBITS - 8);
        *kind = g_render_lines[best_line].texture;
        *tex = (u8)tex_x;
        *side = (abs_x > abs_y) ? 1 : 0;
        *span = g_render_lines[best_line].span;
        *span_height = g_render_lines[best_line].height;
        return 1;
    }
#else
    (void)rayX;
    (void)rayY;
    (void)cell_x;
    (void)cell_y;
    (void)accept_mode;
#endif
    (void)dist;
    (void)kind;
    (void)tex;
    (void)side;
    (void)span;
    (void)span_height;
    return 0;
}

void rc_render(void) {
    if (!view_dirty) return;
    update_ray_cache();
    int baseMapX = posX >> FBITS;
    int baseMapY = posY >> FBITS;
    u8 allow_span_refinement = 1;
    u8 near_refinement_cells = DOOM_NEAR_LINE_REFINEMENT_CELLS;
#if DOOM_ADAPTIVE_LINE_REFINEMENT
    if (wall_frame_overrun) {
        allow_span_refinement = 0;
        near_refinement_cells = DOOM_OVERRUN_LINE_REFINEMENT_CELLS;
    } else if (render_motion_active) {
#if !DOOM_MOVING_SPAN_REFINEMENT
        allow_span_refinement = 0;
#endif
        near_refinement_cells = DOOM_MOVING_LINE_REFINEMENT_CELLS;
    }
#endif
    for (int x = 0; x < NUM_COLS; x++) {
        fix rayX = rayXbuf[x];
        fix rayY = rayYbuf[x];

        int mapX = baseMapX;
        int mapY = baseMapY;

        fix ddX = rayDdXbuf[x];
        fix ddY = rayDdYbuf[x];

        int stepX = rayStepXbuf[x];
        int stepY = rayStepYbuf[x];
        fix sideX, sideY;
        fix span_perp = FBIG;
        u8 span = 0;
        u8 span_height = 0;
        u8 span_kind = 0;
        u8 span_tex = 0;
        int span_side = 0;
        u8 visual_line = 0;
        if (stepX < 0) sideX = fmul(posX - (mapX << FBITS), ddX);
        else           sideX = fmul(((mapX + 1) << FBITS) - posX, ddX);
        if (stepY < 0) sideY = fmul(posY - (mapY << FBITS), ddY);
        else           sideY = fmul(((mapY + 1) << FBITS) - posY, ddY);

        int side = 0;                       /* 0 = hit on X grid line (N/S)  */
        unsigned char hit_cell = 1;
        for (;;) {
            if (sideX < sideY) { sideX += ddX; mapX += stepX; side = 0; }
            else               { sideY += ddY; mapY += stepY; side = 1; }
            if (map_at(mapX, mapY)) {
                hit_cell = map_cell_value(mapX, mapY);
                break;
            } else {
                fix line_perp = FBIG;
                u8 line_kind = 0;
                u8 line_tex = 0;
                int line_side = side;
                u8 line_span = 0;
                u8 line_height = 0;
                if (allow_span_refinement && g_render_cell_count[mapY][mapX] &&
                    rc_refine_render_line_hit(
                        rayX, rayY, mapX, mapY,
                        DOOM_VISUAL_SOLID_LINE_OCCLUSION ? 2 : 1,
                        &line_perp, &line_kind, &line_tex, &line_side, &line_span, &line_height)) {
                    int occlude_h = line_span ? projected_span_height(line_perp, line_height) : projected_height(line_perp);
                    int min_h = line_span ? PORTAL_SPAN_OCCLUDE_MIN_H : PORTAL_SPAN_REPLACE_MIN_H;
                    if (occlude_h < min_h) continue;
                    if (line_perp < span_perp) {
                        span_kind = line_kind;
                        span_tex = line_tex;
                        span_side = line_side;
                        span_perp = line_perp;
                        span = line_span;
                        span_height = line_height;
                        visual_line = 1;
                    }
                }
                continue;
            }
        }
        kindbuf[x] = (hit_cell >= 2) ? (TILE_WALL_ALT_COUNT + 1) : map_cell_texture(mapX, mapY);

        fix perp = (side == 0) ? (sideX - ddX) : (sideY - ddY);
        if (perp < FMIN) perp = FMIN;

        fix wall = (side == 0) ? posY + fmul(perp, rayY) : posX + fmul(perp, rayX);
        int tex_x = (int)(((wall & (FONE - 1)) * TILE_WALL_ATLAS_COLS) >> FBITS);
        if (tex_x < 0) tex_x = 0;
        if (tex_x >= TILE_WALL_ATLAS_COLS) tex_x = TILE_WALL_ATLAS_COLS - 1;
        tex_x = (tex_x + map_cell_texture_phase(mapX, mapY)) & (TILE_WALL_ATLAS_COLS - 1);
        texbuf[x] = (u8)tex_x;
        u8 solid_height = 0;
#if DOOM_SOLID_LINE_REFINEMENT || DOOM_NEAR_LINE_REFINEMENT
        if (g_render_cell_count[mapY][mapX]) {
            u8 solid_span = 0;
            u8 solid_span_height = 0;
#if DOOM_SOLID_LINE_REFINEMENT
            if (rc_refine_render_line_hit(rayX, rayY, mapX, mapY, 0, &perp, &kindbuf[x], &texbuf[x], &side, &solid_span, &solid_span_height)) {
                solid_height = solid_span_height;
            }
#else
            if (near_refinement_cells && perp <= ((fix)near_refinement_cells << FBITS)) {
                if (rc_refine_render_line_hit(rayX, rayY, mapX, mapY, 0, &perp, &kindbuf[x], &texbuf[x], &side, &solid_span, &solid_span_height)) {
                    solid_height = solid_span_height;
                }
            }
#endif
        }
#endif
        fix inv_perp = recip(perp);
        int full_h = projected_height_from_inv(inv_perp);
        int h = full_h;                                  /* slice height px */
        if (solid_height && solid_height < 128) {
            h = (full_h * solid_height + 63) / 128;
            if (h < 2) h = 2;
        }
        if (visual_line && span_perp < perp) {
            int candidate_h = span ? projected_span_height(span_perp, span_height) : projected_height(span_perp);
            int far_floor = full_h / PORTAL_SPAN_REPLACE_FAR_DIV;
            if (candidate_h >= PORTAL_SPAN_REPLACE_MIN_H && candidate_h >= far_floor) {
                kindbuf[x] = span_kind;
                texbuf[x] = span_tex;
                side = span_side;
                perp = span_perp;
                inv_perp = recip(perp);
                full_h = projected_height_from_inv(inv_perp);
                if (!span) h = full_h;
            } else {
                visual_line = 0;
                span = 0;
                span_height = 0;
            }
        }
        distbuf[x] = perp;

        if (span && span_height) {
            h = (full_h * span_height + 63) / 128;
            if (h < 2) h = 2;
            if (h < PORTAL_SPAN_DRAW_MIN_H) {
                span = 0;
                span_height = 0;
                h = full_h;
            }
        }
        if (h < 1)     h = 1;
        if (h > MAX_H) h = MAX_H;
        /* Keep close walls on the baked Doom texture atlas.  The old coarse
         * TILE_BRICK fallback avoided shimmer, but it turned nearby E1M1/E1M2
         * walls into unreadable flat slabs and hid the converted texture cues. */
        closebuf[x] = 0;

        int top = (GAME_H - h) / 2;         /* >=0 because h<=GAME_H         */
        if (span == 1) {
            int bottom = (GAME_H + full_h) / 2;
            if (bottom > GAME_H) bottom = GAME_H;
            top = bottom - h;
        } else if (span == 2) {
            top = (GAME_H - full_h) / 2;
        }
        if (top < 0) top = 0;
        if (top > GAME_H - 1) top = GAME_H - 1;
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

void rc_set_frame_overrun(u8 overrun) {
    wall_frame_overrun = (u8)(overrun ? 1 : 0);
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

    /* Texture/palette strip writes are much more expensive than SCB2/SCB3
     * geometry. Spread them across frames so input response is not held
     * hostage by rewriting every 15-tile wall column while turning. */
    {
        u8 budget = wall_first_upload ? NUM_COLS : WALL_TILE_UPLOAD_COLUMNS_PER_FRAME;
        u8 remaining = 0;
        u8 start = wall_upload_scan;
        if (wall_frame_overrun && !wall_first_upload && budget > WALL_TILE_UPLOAD_COLUMNS_OVERRUN) {
            budget = WALL_TILE_UPLOAD_COLUMNS_OVERRUN;
        }
        for (int i = 0; i < NUM_COLS; i++) {
            int c = start + i;
            u8 texture_changed;
            u16 spr;
            if (c >= NUM_COLS) c -= NUM_COLS;
            spr = WALL_BASE + c;
            texture_changed = (u8)(texbuf[c] != curtex[c] || kindbuf[c] != curkind[c] || closebuf[c] != curclose[c]);
            if (!texture_changed && palbuf[c] == curpal[c]) continue;
            if (!budget) {
                remaining = 1;
                break;
            }
            budget--;
            wall_upload_scan = (u8)(c + 1);
            if (wall_upload_scan >= NUM_COLS) wall_upload_scan = 0;
            if (texture_changed) {
                u16 *tiles = (kindbuf[c] > TILE_WALL_ALT_COUNT) ? door_tiles[texbuf[c]] : (kindbuf[c] ? wall_alt_tiles[kindbuf[c] - 1][texbuf[c]] : wall_tiles[texbuf[c]]);
                if (palbuf[c] != curpal[c]) {
                    u16 attr = (u16)(palbuf[c] << 8);
                    vram_addr(VRAM_SCB1 + spr * 64);
                    vram_mod(1);
                    for (int t = 0; t < WALL_WIN; t++) {
                        vram_w(closebuf[c] ? TILE_BRICK : tiles[t]);
                        vram_w(attr);
                    }
                    curpal[c] = palbuf[c];
                } else {
                    vram_addr(VRAM_SCB1 + spr * 64);
                    vram_mod(2);
                    for (int t = 0; t < WALL_WIN; t++) vram_w(closebuf[c] ? TILE_BRICK : tiles[t]);
                }
                curtex[c] = texbuf[c];
                curkind[c] = kindbuf[c];
                curclose[c] = closebuf[c];
                continue;
            }
            vram_addr(VRAM_SCB1 + spr * 64 + 1);
            vram_mod(2);
            u16 attr = (u16)(palbuf[c] << 8);
            for (int t = 0; t < WALL_WIN; t++) vram_w(attr);
            curpal[c] = palbuf[c];
        }
        wall_upload_dirty = remaining;
        if (!remaining) wall_first_upload = 0;
    }
}
