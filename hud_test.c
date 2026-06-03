#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"

#define HUD_TEST_KEY_BASE 300
#define HUD_TEST_BAR_BASE 120
#define HUD_TEST_KEY_COUNT 3

enum {
    HUD_FIX_TOP_ROW = (GAME_H / 8) + 1,
    HUD_FIX_BOTTOM_ROW = HUD_FIX_TOP_ROW + 1,
    HUD_NUM_ROW = 25,
    HUD_AMMO_COL = 2,
    HUD_HEALTH_COL = 9,
    HUD_FRAG_COL = 17,
    HUD_ARMOR_COL = 24
};

enum {
    HUD_ITEM_BAR = 0,
    HUD_ITEM_AMMO,
    HUD_ITEM_HEALTH,
    HUD_ITEM_FRAG,
    HUD_ITEM_ARMOR,
    HUD_ITEM_KEYS,
    HUD_ITEM_COUNT
};

typedef struct HudOffset {
    signed char x;
    signed char y;
} HudOffset;

static HudOffset offsets[HUD_ITEM_COUNT];
static u8 selected_item = HUD_ITEM_BAR;
static u8 bar_variant = 0;
static u8 prev_input = 0;
static u8 repeat_tick = 0;

static const HudOffset default_offsets[HUD_ITEM_COUNT] = {
    { 0, 0 }, /* BAR: render variant only */
    { 0, 0 }, /* AMMO: fix-cell offset */
    { 0, 0 }, /* HEALTH: fix-cell offset */
    { 0, 0 }, /* FRAG: fix-cell offset */
    { 0, 0 }, /* ARMOR: fix-cell offset */
    { 0, 0 }, /* KEYS: sprite pixel offset */
};

static const u16 key_thing_types[HUD_TEST_KEY_COUNT] = {
    5,  /* blue keycard */
    13, /* red keycard */
    6   /* yellow keycard */
};

static int key_sprite_def_for_type(u16 thing_type) {
    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type == thing_type) return i;
    }
    return -1;
}

static void load_key_palette(u16 key, int def_idx) {
    for (int i = 0; i < ENEMY_PALETTE_COLORS; i++) {
        u8 r = g_enemy_palette_rgb[def_idx][i][0];
        u8 g = g_enemy_palette_rgb[def_idx][i][1];
        u8 b = g_enemy_palette_rgb[def_idx][i][2];
        pal_set((u16)(PAL_ENEMY_BASE + key), (u16)(i + 1), RGB(r, g, b));
    }
}

static void set_key_sprite_tiles(u16 key, const DoomSpriteScale *meta) {
    u16 spr = (u16)(HUD_TEST_KEY_BASE + key);
    u16 pal = (u16)(PAL_ENEMY_BASE + key);
    for (u16 row = 0; row < HUD_WIN; row++) {
        u16 tile = (row < meta->rows) ? (u16)(meta->tile_base + row * meta->strips) : TILE_BLANK;
        scb1_tile(spr, row, tile, pal);
    }
}

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
        pal_set(PAL_MAP_WALL, (u16)i, RGB(18, 18, 18));
    }
    REG_BACKDROP = RGB(0, 0, 0);
}

static void draw_compact_digit(u16 col, u16 row, u16 value, u16 pal) {
    fix_poke(col, row, pal, (u16)(FIX_DIGIT_BASE + (value % 10)));
}

static void draw_compact_two(u16 col, u16 row, int value, u16 pal) {
    if (value < 0) value = -value;
    if (value > 99) value = 99;
    draw_compact_digit(col, row, (u16)(value / 10), pal);
    draw_compact_digit((u16)(col + 1), row, (u16)value, pal);
}

static void draw_big_number3(u16 col, u16 row, u16 value, u16 pal) {
    u8 digits[3];
    if (value > 999) value = 999;
    digits[0] = (u8)(value / 100);
    digits[1] = (u8)((value / 10) % 10);
    digits[2] = (u8)(value % 10);
    for (u16 i = 0; i < 3; i++) {
        u16 tile = (u16)(FIX_BIG_DIGIT_BASE + digits[i] * 4);
        u16 x = (u16)(col + i * 2);
        fix_poke(x, row, pal, tile);
        fix_poke((u16)(x + 1), row, pal, (u16)(tile + 1));
        fix_poke(x, (u16)(row + 1), pal, (u16)(tile + 2));
        fix_poke((u16)(x + 1), (u16)(row + 1), pal, (u16)(tile + 3));
    }
}

static void draw_big_number2(u16 col, u16 row, u16 value, u16 pal) {
    u8 digits[2];
    if (value > 99) value = 99;
    digits[0] = (u8)(value / 10);
    digits[1] = (u8)(value % 10);
    for (u16 i = 0; i < 2; i++) {
        u16 tile = (u16)(FIX_BIG_DIGIT_BASE + digits[i] * 4);
        u16 x = (u16)(col + i * 2);
        fix_poke(x, row, pal, tile);
        fix_poke((u16)(x + 1), row, pal, (u16)(tile + 1));
        fix_poke(x, (u16)(row + 1), pal, (u16)(tile + 2));
        fix_poke((u16)(x + 1), (u16)(row + 1), pal, (u16)(tile + 3));
    }
}

static u16 clamp_fix_col(int col) {
    if (col < 0) return 0;
    if (col > 39) return 39;
    return (u16)col;
}

static u16 clamp_fix_row(int row) {
    if (row < 0) return 0;
    if (row > 27) return 27;
    return (u16)row;
}

static void draw_marker_for_item(u8 item) {
    int col = 0;
    int row = 24;
    switch (item) {
    case HUD_ITEM_AMMO:
        col = HUD_AMMO_COL + offsets[HUD_ITEM_AMMO].x;
        row = HUD_NUM_ROW + offsets[HUD_ITEM_AMMO].y - 1;
        break;
    case HUD_ITEM_HEALTH:
        col = HUD_HEALTH_COL + offsets[HUD_ITEM_HEALTH].x;
        row = HUD_NUM_ROW + offsets[HUD_ITEM_HEALTH].y - 1;
        break;
    case HUD_ITEM_FRAG:
        col = HUD_FRAG_COL + offsets[HUD_ITEM_FRAG].x;
        row = HUD_NUM_ROW + offsets[HUD_ITEM_FRAG].y - 1;
        break;
    case HUD_ITEM_ARMOR:
        col = HUD_ARMOR_COL + offsets[HUD_ITEM_ARMOR].x;
        row = HUD_NUM_ROW + offsets[HUD_ITEM_ARMOR].y - 1;
        break;
    case HUD_ITEM_KEYS:
        col = 35 + (offsets[HUD_ITEM_KEYS].x / 8);
        row = HUD_FIX_BOTTOM_ROW + (offsets[HUD_ITEM_KEYS].y / 8) - 1;
        break;
    default:
        col = 0;
        row = 24;
        break;
    }
    fix_poke(clamp_fix_col(col), clamp_fix_row(row), PAL_MAP_PLAYER, FIX_SOLID);
}

static void draw_top_indicator(void) {
    for (u16 i = 0; i < HUD_ITEM_COUNT; i++) {
        u16 pal = (i == selected_item) ? PAL_MAP_PLAYER : PAL_MAP_WALL;
        draw_compact_digit((u16)(14 + i * 2), 0, i, pal);
    }
    draw_compact_digit(18, 2, selected_item, PAL_MAP_PLAYER);
    if (selected_item == HUD_ITEM_BAR) {
        draw_compact_digit(21, 2, bar_variant, PAL_MAP_PLAYER);
    } else {
        draw_compact_two(21, 2, offsets[selected_item].x, PAL_MAP_PLAYER);
        draw_compact_two(24, 2, offsets[selected_item].y, PAL_MAP_PLAYER);
    }
}

static void render_hud_bar(void) {
    int base_y = GAME_H + HUD_Y_OFFSET;
    int base_x = 0;
    u8 swap_rows = bar_variant & 1;
    u8 swap_cols = bar_variant & 2;
    u8 solid_top = bar_variant & 4;
    for (u16 i = 0; i < HUD_COUNT; i++) {
        u16 spr = HUD_TEST_BAR_BASE + i;
        u16 src_col = swap_cols ? (u16)(HUD_COUNT - 1 - i) : i;
        for (u16 row = 0; row < HUD_WIN; row++) {
            u16 src_row;
            if (solid_top) {
                src_row = swap_rows ? 1 : 0;
            } else {
                src_row = swap_rows ? (u16)(HUD_WIN - 1 - row) : row;
            }
            u16 tile = (u16)(TILE_HUD_BASE + src_row * HUD_COUNT + src_col);
            scb1_tile(spr, row, tile, PAL_HUD);
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, base_y, 0, HUD_WIN);
        scb4(spr, (u16)(base_x + (int)i * 16));
    }
}

static void render_values(void) {
    draw_big_number3(
        clamp_fix_col(HUD_AMMO_COL + offsets[HUD_ITEM_AMMO].x),
        clamp_fix_row(HUD_NUM_ROW + offsets[HUD_ITEM_AMMO].y),
        50,
        PAL_HUD
    );
    draw_big_number3(
        clamp_fix_col(HUD_HEALTH_COL + offsets[HUD_ITEM_HEALTH].x),
        clamp_fix_row(HUD_NUM_ROW + offsets[HUD_ITEM_HEALTH].y),
        100,
        PAL_HUD
    );
    draw_big_number2(
        clamp_fix_col(HUD_FRAG_COL + offsets[HUD_ITEM_FRAG].x),
        clamp_fix_row(HUD_NUM_ROW + offsets[HUD_ITEM_FRAG].y),
        0,
        PAL_HUD
    );
    draw_big_number3(
        clamp_fix_col(HUD_ARMOR_COL + offsets[HUD_ITEM_ARMOR].x),
        clamp_fix_row(HUD_NUM_ROW + offsets[HUD_ITEM_ARMOR].y),
        0,
        PAL_HUD
    );
}

static void render_keycards(void) {
    int base_x = 278 + offsets[HUD_ITEM_KEYS].x;
    int base_y = GAME_H + 7 + offsets[HUD_ITEM_KEYS].y;

    for (u16 key = 0; key < HUD_TEST_KEY_COUNT; key++) {
        u16 spr = (u16)(HUD_TEST_KEY_BASE + key);
        int def_idx = key_sprite_def_for_type(key_thing_types[key]);
        if (def_idx < 0) {
            scb2(spr, 0x0F, 0x00);
            continue;
        }

        load_key_palette(key, def_idx);
        scb1_tile(spr, 0, (u16)(TILE_HUD_KEYCARD_BASE + key), (u16)(PAL_ENEMY_BASE + key));
        scb1_tile(spr, 1, TILE_BLANK, 0);
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, (u16)base_y, 0, 1);
        scb4(spr, (u16)(base_x + key * 11));
    }
}

static void render_all(void) {
    clear_fix_layer();
    render_hud_bar();
    render_keycards();
    render_values();
    draw_marker_for_item(selected_item);
    draw_top_indicator();
}

static void reset_selected(void) {
    if (selected_item == HUD_ITEM_BAR) {
        bar_variant = 0;
        render_all();
        return;
    }
    offsets[selected_item] = default_offsets[selected_item];
    render_all();
}

static void reset_all(void) {
    for (u8 i = 0; i < HUD_ITEM_COUNT; i++) offsets[i] = default_offsets[i];
    bar_variant = 0;
    render_all();
}

static void move_selected(signed char dx, signed char dy) {
    HudOffset *o = &offsets[selected_item];
    if (selected_item == HUD_ITEM_BAR) {
        if (dx < 0 || dy < 0) bar_variant = (u8)((bar_variant + 7) & 7);
        if (dx > 0 || dy > 0) bar_variant = (u8)((bar_variant + 1) & 7);
        render_all();
        return;
    }
    o->x = (signed char)(o->x + dx);
    o->y = (signed char)(o->y + dy);
    if (selected_item == HUD_ITEM_KEYS) {
        if (o->x < -64) o->x = -64;
        if (o->x > 40) o->x = 40;
        if (o->y < -24) o->y = -24;
        if (o->y > 24) o->y = 24;
    } else {
        if (o->x < -16) o->x = -16;
        if (o->x > 16) o->x = 16;
        if (o->y < -6) o->y = -6;
        if (o->y > 4) o->y = 4;
    }
    render_all();
}

static void update_input(void) {
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FACE = 0xF0 };
    u8 pressed = (u8)~REG_P1CNT;
    u8 edge = (u8)(pressed & ~prev_input);
    u8 arrows = (u8)(pressed & (UP | DOWN | LEFT | RIGHT));
    u8 step = (u8)(edge & (UP | DOWN | LEFT | RIGHT));

    if (arrows) {
        if ((repeat_tick++ & 7) == 0) step |= arrows;
    } else {
        repeat_tick = 0;
    }

    if (edge & FACE) {
        selected_item = (u8)((selected_item + 1) % HUD_ITEM_COUNT);
        render_all();
    }
    if (step & LEFT) move_selected(-1, 0);
    if (step & RIGHT) move_selected(1, 0);
    if (step & UP) move_selected(0, -1);
    if (step & DOWN) move_selected(0, 1);

    prev_input = pressed;
}

int main(void) {
    watchdog_kick();
    init_test_palettes();
    clear_fix_layer();
    disable_all_sprites();
    reset_all();

    for (;;) {
        wait_vblank();
        watchdog_kick();
        update_input();
    }
}
