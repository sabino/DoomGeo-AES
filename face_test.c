#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"

#define FACE_TEST_BASE 120
#define FACE_TEST_START_X (TILE_HUD_FACE_COL * 16)
#define FACE_TEST_START_Y GAME_H
#define FACE_TEST_TOP_DIGIT_COL 19
#define FACE_TEST_TOP_DIGIT_ROW 21

static u8 face_variant = 0;
static u8 prev_input = 0;
static u8 repeat_tick = 0;

static void clear_fix_layer(void) {
    for (u16 col = 0; col < SCRW / 8; col++) {
        for (u16 row = 0; row < SCRH / 8; row++) {
            fix_poke(col, row, 0, FIX_BLANK);
        }
    }
}

static void disable_all_sprites(void) {
    for (u16 s = 1; s < SPR_TOTAL; s++) {
        scb2(s, 0x0F, 0x00);
        scb3(s, SCRH + 32, 0, 1);
        scb4(s, 0);
    }
}

static void init_test_palettes(void) {
    for (int i = 0; i < HUD_PALETTE_COLORS; i++) {
        pal_set(PAL_HUD, (u16)(i + 1), RGB(g_hud_palette_rgb[i][0], g_hud_palette_rgb[i][1], g_hud_palette_rgb[i][2]));
    }
    for (int i = 1; i < 16; i++) {
        pal_set(PAL_MAP_PLAYER, (u16)i, RGB(4, 31, 8));
    }
    REG_BACKDROP = RGB(0, 0, 0);
}

static void draw_digit(u16 col, u16 row, u16 value) {
    fix_poke(col, row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (value % 10)));
}

static void draw_two_digits(u16 col, u16 row, u16 value) {
    draw_digit(col, row, value / 10);
    draw_digit((u16)(col + 1), row, value);
}

static void draw_debug_fix(void) {
    clear_fix_layer();
    draw_two_digits(FACE_TEST_TOP_DIGIT_COL, FACE_TEST_TOP_DIGIT_ROW, face_variant);
}

static void write_face_tiles(void) {
    u16 frame_base = (u16)(TILE_HUD_FACE_BASE + face_variant * TILE_HUD_FACE_FRAME_TILES);
    for (u16 col = 0; col < TILE_HUD_FACE_COLS; col++) {
        u16 spr = (u16)(FACE_TEST_BASE + col);
        for (u16 row = 0; row < TILE_HUD_FACE_ROWS; row++) {
            u16 tile = (u16)(frame_base + row * TILE_HUD_FACE_COLS + col);
            scb1_tile(spr, row, tile, PAL_HUD);
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, FACE_TEST_START_Y, 0, TILE_HUD_FACE_ROWS);
        scb4(spr, (u16)(FACE_TEST_START_X + col * 16));
    }
}

static void set_face_variant(u8 next) {
    face_variant = (u8)(next % TILE_HUD_FACE_FRAMES);
    write_face_tiles();
    draw_debug_fix();
}

static void update_input(void) {
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, B = 0x20 };
    u8 pressed = (u8)~REG_P1CNT;
    u8 edge = (u8)(pressed & ~prev_input);
    u8 arrows = (u8)(pressed & (UP | DOWN | LEFT | RIGHT));
    u8 step = (u8)(edge & (UP | DOWN | LEFT | RIGHT));

    if (arrows) {
        if ((repeat_tick++ & 7) == 0) step |= arrows;
    } else {
        repeat_tick = 0;
    }

    if (step & LEFT) set_face_variant((u8)((face_variant + TILE_HUD_FACE_FRAMES - 1) % TILE_HUD_FACE_FRAMES));
    if (step & RIGHT) set_face_variant((u8)((face_variant + 1) % TILE_HUD_FACE_FRAMES));
    if (step & UP) set_face_variant((u8)((face_variant + TILE_HUD_FACE_FRAMES - 4) % TILE_HUD_FACE_FRAMES));
    if (step & DOWN) set_face_variant((u8)((face_variant + 4) % TILE_HUD_FACE_FRAMES));
    if (edge & B) set_face_variant(0);

    prev_input = pressed;
}

int main(void) {
    watchdog_kick();
    init_test_palettes();
    clear_fix_layer();
    disable_all_sprites();
    set_face_variant(0);

    for (;;) {
        wait_vblank();
        watchdog_kick();
        update_input();
    }
}
