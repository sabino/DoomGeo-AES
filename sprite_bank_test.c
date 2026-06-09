#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"

#define SPRITE_TEST_BASE 1
#define SPRITE_TEST_PER_PAGE 8
#define SPRITE_TEST_STRIPS ENEMY_STRIPS
#define SPRITE_TEST_PAGE_TICKS 90

static u16 sprite_page = 0;
static u8 prev_input = 0;
static u8 repeat_tick = 0;
static u8 auto_tick = 0;

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

static void init_palettes(void) {
    for (int slot = 0; slot < SPRITE_TEST_PER_PAGE; slot++) {
        for (int i = 1; i < 16; i++) pal_set((u16)(PAL_ENEMY_BASE + slot), (u16)i, RGB(31, 31, 31));
    }
    for (int i = 1; i < 16; i++) pal_set(PAL_MAP_PLAYER, (u16)i, RGB(4, 31, 8));
    REG_BACKDROP = RGB(0, 0, 0);
}

static void load_sprite_palette(u16 slot, u16 def_idx) {
    if (def_idx >= ENEMY_SPRITE_COUNT) return;
    for (int i = 0; i < ENEMY_PALETTE_COLORS; i++) {
        pal_set((u16)(PAL_ENEMY_BASE + slot), (u16)(i + 1),
                RGB(g_enemy_palette_rgb[def_idx][i][0],
                    g_enemy_palette_rgb[def_idx][i][1],
                    g_enemy_palette_rgb[def_idx][i][2]));
    }
}

static void draw_digit(u16 col, u16 row, u16 value) {
    fix_poke(col, row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (value % 10)));
}

static void draw_number4(u16 col, u16 row, u16 value) {
    draw_digit(col, row, value / 1000);
    draw_digit((u16)(col + 1), row, value / 100);
    draw_digit((u16)(col + 2), row, value / 10);
    draw_digit((u16)(col + 3), row, value);
}

static void draw_labels(void) {
    clear_fix_layer();
    draw_number4(0, 0, sprite_page);
    draw_number4(6, 0, ENEMY_SPRITE_COUNT);
    for (u16 slot = 0; slot < SPRITE_TEST_PER_PAGE; slot++) {
        u16 def_idx = (u16)(sprite_page + slot);
        u16 x = (u16)(slot * 5);
        if (def_idx >= ENEMY_SPRITE_COUNT) break;
        draw_number4(x, 2, def_idx);
        draw_number4(x, 3, g_enemy_sprite_defs[def_idx].thing_type);
    }
}

static void draw_sprite_slot(u16 slot, u16 def_idx) {
    u16 base_spr = (u16)(SPRITE_TEST_BASE + slot * SPRITE_TEST_STRIPS);
    u16 x = (u16)(slot * 40 + 2);
    u16 y = (u16)(slot < 4 ? 56 : 132);
    u16 local_x = (u16)((slot % 4) * 78 + 8);
    const DoomEnemySpriteDef *def;
    const DoomSpriteScale *meta;

    if (def_idx >= ENEMY_SPRITE_COUNT) {
        for (u16 strip = 0; strip < SPRITE_TEST_STRIPS; strip++) {
            u16 spr = (u16)(base_spr + strip);
            scb2(spr, 0x0F, 0x00);
            scb3(spr, SCRH + 32, 0, 1);
            scb4(spr, 0);
        }
        return;
    }

    def = &g_enemy_sprite_defs[def_idx];
    meta = &g_enemy_scales[def->first_scale];
    load_sprite_palette(slot, def_idx);

    for (u16 strip = 0; strip < SPRITE_TEST_STRIPS; strip++) {
        u16 spr = (u16)(base_spr + strip);
        vram_addr(VRAM_SCB1 + spr * 64);
        vram_mod(1);
        for (u16 row = 0; row < ENEMY_WIN; row++) {
            if (strip < meta->strips && row < meta->rows) {
                vram_w((u16)(meta->tile_base + row * meta->strips + strip));
                vram_w((u16)((PAL_ENEMY_BASE + slot) << 8));
            } else {
                vram_w(TILE_BLANK);
                vram_w(0);
            }
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, y, 0, ENEMY_WIN);
        scb4(spr, (u16)(local_x + strip * 16));
    }
    (void)x;
}

static void draw_page(void) {
    for (u16 slot = 0; slot < SPRITE_TEST_PER_PAGE; slot++) {
        draw_sprite_slot(slot, (u16)(sprite_page + slot));
    }
    draw_labels();
}

static void set_page(u16 page) {
    if (page >= ENEMY_SPRITE_COUNT) page = 0;
    sprite_page = (u16)(page - (page % SPRITE_TEST_PER_PAGE));
    draw_page();
}

static void update_input(void) {
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, B = 0x20 };
    u8 pressed = (u8)~REG_P1CNT;
    u8 edge = (u8)(pressed & ~prev_input);
    u8 arrows = (u8)(pressed & (UP | DOWN | LEFT | RIGHT));
    u8 step = (u8)(edge & (UP | DOWN | LEFT | RIGHT));

    if (arrows) {
        if ((repeat_tick++ & 7) == 0) step |= arrows;
        auto_tick = 0;
    } else {
        repeat_tick = 0;
    }

    if (step & LEFT) {
        set_page(sprite_page >= SPRITE_TEST_PER_PAGE
            ? (u16)(sprite_page - SPRITE_TEST_PER_PAGE)
            : (u16)(ENEMY_SPRITE_COUNT - 1));
    }
    if (step & RIGHT) set_page((u16)(sprite_page + SPRITE_TEST_PER_PAGE));
    if (step & UP) {
        set_page(sprite_page >= SPRITE_TEST_PER_PAGE * 4
            ? (u16)(sprite_page - SPRITE_TEST_PER_PAGE * 4)
            : (u16)(ENEMY_SPRITE_COUNT - 1));
    }
    if (step & DOWN) set_page((u16)(sprite_page + SPRITE_TEST_PER_PAGE * 4));
    if (edge & B) set_page(0);

    if (!arrows && ++auto_tick >= SPRITE_TEST_PAGE_TICKS) {
        auto_tick = 0;
        set_page((u16)(sprite_page + SPRITE_TEST_PER_PAGE));
    }

    prev_input = pressed;
}

int main(void) {
    watchdog_kick();
    init_palettes();
    clear_fix_layer();
    disable_all_sprites();
    set_page(0);

    for (;;) {
        wait_vblank();
        watchdog_kick();
        update_input();
    }
}
