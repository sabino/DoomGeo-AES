/* main.c - boot setup and the game loop.
 
 */
#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"
#include "raycast.h"
#include "map.h"

/* ---- palette setup --------------------------------------------------- */
static void init_palettes(void) {
    /* index 0 of every palette is transparent for sprites; we keep walls
     * opaque by only using indices 1..3. */

    for (int i = 0; i < WALL_PALETTE_COLORS; i++) {
        u8 r = g_wall_palette_rgb[i][0];
        u8 g = g_wall_palette_rgb[i][1];
        u8 b = g_wall_palette_rgb[i][2];
        pal_set(PAL_WALL_LIT, (u16)(i + 1), RGB(r, g, b));
        pal_set(PAL_WALL_DARK, (u16)(i + 1), RGB((u8)(r * 140 / 256), (u8)(g * 140 / 256), (u8)(b * 140 / 256)));
    }

    /* distance-shaded wall palettes. */
    {
        for (int b = 0; b < DEPTH_BANDS; b++) {
            int fn = 256 - (b * 200) / (DEPTH_BANDS - 1);   /* 256 near .. 56 far */
            for (int s = 0; s < 2; s++) {
                int sf = s ? 140 : 256;                     /* dark side ~55%     */
                u16 pal = PAL_DEPTH_BASE + s * DEPTH_BANDS + b;
                for (int i = 0; i < WALL_PALETTE_COLORS; i++) {
                    int r = g_wall_palette_rgb[i][0] * fn / 256 * sf / 256;
                    int g = g_wall_palette_rgb[i][1] * fn / 256 * sf / 256;
                    int bl = g_wall_palette_rgb[i][2] * fn / 256 * sf / 256;
                    pal_set(pal, (u16)(i + 1), RGB((u8)r, (u8)g, (u8)bl));
                }
            }
        }
    }

    for (int i = 0; i < CEILING_PALETTE_COLORS; i++) {
        u8 r = g_ceiling_palette_rgb[i][0];
        u8 g = g_ceiling_palette_rgb[i][1];
        u8 b = g_ceiling_palette_rgb[i][2];
        pal_set(PAL_CEILING, (u16)(i + 1), RGB(r, g, b));
    }
    for (int i = 0; i < FLOOR_PALETTE_COLORS; i++) {
        u8 r = g_floor_palette_rgb[i][0];
        u8 g = g_floor_palette_rgb[i][1];
        u8 b = g_floor_palette_rgb[i][2];
        pal_set(PAL_FLOOR, (u16)(i + 1), RGB(r, g, b));
    }

    /* minimap */
    pal_set(PAL_MAP_WALL,   15, RGB(20, 20, 22)); /* walls */
    pal_set(PAL_MAP_PLAYER, 15, RGB( 4, 31,  8)); /* player */

    for (int i = 0; i < HUD_PALETTE_COLORS; i++) {
        u8 r = g_hud_palette_rgb[i][0];
        u8 g = g_hud_palette_rgb[i][1];
        u8 b = g_hud_palette_rgb[i][2];
        pal_set(PAL_HUD, (u16)(i + 1), RGB(r, g, b));
    }
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        u8 r = g_weapon_palette_rgb[i][0];
        u8 g = g_weapon_palette_rgb[i][1];
        u8 b = g_weapon_palette_rgb[i][2];
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(r, g, b));
    }

    REG_BACKDROP = RGB(0, 0, 0);
}

/* ---- clear the fix layer */
static void clear_fix(void) {
    vram_addr(VRAM_FIX);
    vram_mod(1);
    for (int i = 0; i < 40 * 32; i++) vram_w(0x0000);
}

static int prev_px = -1, prev_py = -1;
static u8  map_on = 0;              /* minimap visible?                       */
static u8  bg_phase = 0xFF;
static u8  weapon_frame = 0xFF;
static u8  fire_timer = 0;

static void map_cell(int mx, int my, u16 pal, u16 tile) {
    fix_poke((u16)(MAP_FIX_COL + mx), (u16)(MAP_FIX_ROW + my), pal, tile);
}

static void draw_minimap(void) {
    for (int my = 0; my < MAP_H; my++)
        for (int mx = 0; mx < MAP_W; mx++) {
            if (map_at(mx, my))
                map_cell(mx, my, PAL_MAP_WALL, FIX_SOLID);
            else
                map_cell(mx, my, 0, FIX_BLANK);
        }
}

/* blank just the minimap's fix region */
static void clear_minimap(void) {
    for (int my = 0; my < MAP_H; my++)
        for (int mx = 0; mx < MAP_W; mx++)
            map_cell(mx, my, 0, FIX_BLANK);
}

/* restore the cell the marker was on, then mark the new one */
static void update_marker(void) {
    int px, py;
    if (!map_on) return;
    rc_player_cell(&px, &py);
    if (px == prev_px && py == prev_py) return;
    if (prev_px >= 0) {                 /* repaint old cell as its map content */
        if (map_at(prev_px, prev_py)) map_cell(prev_px, prev_py, PAL_MAP_WALL, FIX_SOLID);
        else                          map_cell(prev_px, prev_py, 0, FIX_BLANK);
    }
    map_cell(px, py, PAL_MAP_PLAYER, FIX_SOLID);
    prev_px = px; prev_py = py;
}

 
static void disable_sprites(void) {
    for (u16 s = 1; s < SPR_TOTAL; s++) {
        scb2(s, 0x0F, 0x00);          /* full width, zero height            */
        scb3(s, SCRH + 32, 0, 1);     /* below the visible area             */
        scb4(s, 0);
    }
}

static void set_background_phase(u8 phase) {
    u16 phase_base = (u16)(phase * TILE_BG_PHASE_TILES);
    for (u16 i = 0; i < BG_COUNT; i++) {
        u16 spr = BG_BASE + i;
        vram_addr(VRAM_SCB1 + spr * 64);
        vram_mod(1);
        for (u16 t = 0; t < BG_WIN; t++) {
            if (t < BG_SPLIT) {
                vram_w((u16)(TILE_CEILING_BASE + phase_base + t * BG_COUNT + i));
                vram_w((u16)(PAL_CEILING << 8));
            } else {
                u16 row = (u16)(t - BG_SPLIT);
                vram_w((u16)(TILE_FLOOR_BASE + phase_base + row * BG_COUNT + i));
                vram_w((u16)(PAL_FLOOR << 8));
            }
        }
    }
    bg_phase = phase;
}

static void update_background_phase(u8 phase) {
    if (phase == bg_phase) return;
    set_background_phase(phase);
}

/* ---- floor/ceiling backdrop: BG_COUNT full-width columns -------------- */
static void init_background(void) {
    for (u16 i = 0; i < BG_COUNT; i++) {
        u16 spr = BG_BASE + i;
        scb2(spr, 0x0F, 0xFF);        /* full size, no shrink (16-tile ref)  */
        scb3(spr, 0, 0, BG_WIN);      /* top of screen                       */
        scb4(spr, i * 16);
    }
    set_background_phase(0);
}

/* ---- wall-slice sprites: fixed X + brick tilemap set once; SCB2/SCB3
 * (and palette when shading flips) are updated every frame in rc_blit. --- */
static void init_walls(void) {
    for (u16 c = 0; c < NUM_COLS; c++) {
        u16 spr = WALL_BASE + c;
        scb1_fill(spr, WALL_WIN, TILE_BRICK, PAL_WALL_LIT);
        scb4(spr, c * COLW);
        scb2(spr, HSHRINK, 0x00);     
        scb3(spr, 0, 0, WALL_WIN);
    }
}

static void init_hud(void) {
    for (u16 i = 0; i < HUD_COUNT; i++) {
        u16 spr = HUD_BASE + i;
        for (u16 row = 0; row < HUD_WIN; row++) {
            u16 tile = (u16)(TILE_HUD_BASE + row * HUD_COUNT + i);
            scb1_tile(spr, row, tile, PAL_HUD);
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, GAME_H, 0, HUD_WIN);
        scb4(spr, i * 16);
    }
}

static void set_weapon_frame(u8 frame) {
    u16 frame_base = (u16)(TILE_WEAPON_BASE + (frame % TILE_WEAPON_FRAMES) * TILE_WEAPON_FRAME_TILES);
    for (u16 i = 0; i < WEAPON_COUNT; i++) {
        u16 spr = WEAPON_BASE + i;
        vram_addr(VRAM_SCB1 + spr * 64);
        vram_mod(1);
        for (u16 row = 0; row < WEAPON_WIN; row++) {
            vram_w((u16)(frame_base + row * WEAPON_COUNT + i));
            vram_w((u16)(PAL_WEAPON << 8));
        }
    }
    weapon_frame = frame;
}

static void update_weapon(u8 pressed) {
    enum { B = 0x20 };
    static u8 b_prev = 0;
    u8 b_now = pressed & B;
    if (b_now && !b_prev && fire_timer == 0) fire_timer = 12;
    b_prev = b_now;

    u8 frame = 0;
    if (fire_timer) {
        if (fire_timer > 8) frame = 1;
        else if (fire_timer > 4) frame = 2;
        else frame = 3;
        fire_timer--;
    }
    if (frame != weapon_frame) set_weapon_frame(frame);
}

static void init_weapon(void) {
    u16 start_x = (u16)((SCRW - WEAPON_COUNT * 16) / 2);
    int top = GAME_H - WEAPON_WIN * 16;
    for (u16 i = 0; i < WEAPON_COUNT; i++) {
        u16 spr = WEAPON_BASE + i;
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, top, 0, WEAPON_WIN);
        scb4(spr, (u16)(start_x + i * 16));
    }
    set_weapon_frame(0);
}

int main(void) {
    watchdog_kick();
    clear_fix();
    init_palettes();
    disable_sprites();
    init_background();
    init_walls();
    init_hud();
    init_weapon();
    rc_init();

    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;    
        rc_input(pressed);
        rc_render();                    /* DDA during active display          */
        wait_vblank();
        watchdog_kick();
        rc_blit();                      /* push to VRAM during vblank         */
        update_background_phase(rc_bg_phase());
        update_weapon(pressed);

        /* button C toggles the minimap  */
        {
            enum { C = 0x40 };          /* P1 bit 6                            */
            static u8 c_prev = 0;
            u8 c_now = pressed & C;
            if (c_now && !c_prev) {
                map_on = !map_on;
                if (map_on) { draw_minimap(); prev_px = -1; }  /* -1 forces marker repaint */
                else          clear_minimap();
            }
            c_prev = c_now;
        }

        update_marker();                /* 2 fix writes when the cell changes */
    }
    return 0;
}
