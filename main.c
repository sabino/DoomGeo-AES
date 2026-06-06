/* main.c - boot setup and the game loop.
 
 */
#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"
#include "raycast.h"
#include "map.h"
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
#include "chunk_stream.h"
#endif

#if ACTIVE_MAP_W > 255 || ACTIVE_MAP_H > 255
#error "monster path queue packs x/y into one word; ACTIVE_MAP_W and ACTIVE_MAP_H must stay <= 255"
#endif

#if defined(DOOM_COMBAT_TEST) || defined(DOOM_E1M1_ENCOUNTER_TEST) || defined(DOOM_E1M1_SCOUT_TEST) \
    || defined(DOOM_E1M1_EXIT_TEST) || defined(DOOM_HIDDEN_ATTACK_TEST) || defined(DOOM_MELEE_TEST) \
    || defined(DOOM_MONSTER_GALLERY_TEST) || defined(DOOM_ARSENAL_TEST) || defined(DOOM_DEATH_TEST) \
    || defined(DOOM_POWERUP_TEST) || defined(DOOM_KEY_DOOR_TEST) || defined(DOOM_E1M8_BOSS_TEST) \
    || defined(DOOM_CHUNK_MOVEMENT_TEST)
#define DOOM_FOCUSED_TEST 1
#define DOOM_SKIP_INTRO 1
#endif

enum {
    THING_CLASS_NONE = 0,
    THING_CLASS_MONSTER = 1,
    THING_CLASS_THREAT = 2,
    THING_CLASS_PICKUP = 3,
    THING_CLASS_CORPSE = 4
};

unsigned char g_runtime_door_open[NG_RUNTIME_DOOR_COUNT ? NG_RUNTIME_DOOR_COUNT : 1];
unsigned char g_runtime_lift_open[NG_RUNTIME_LIFT_COUNT ? NG_RUNTIME_LIFT_COUNT : 1];
unsigned char g_runtime_cell_open[MAP_RUNTIME_OPEN_BYTES ? MAP_RUNTIME_OPEN_BYTES : 1];
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
enum { CHUNK_DYNAMIC_DROP_SLOTS = 8 };
unsigned short g_simple_active_chunk = DOOM_CHUNK_START_CHUNK;
unsigned char g_chunk_door_open[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
unsigned char g_chunk_lift_open[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];
static u16 chunk_thing_state_type[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];
static short chunk_thing_state_x_q8[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];
static short chunk_thing_state_y_q8[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];
static u8 chunk_thing_state_dead[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];
static u8 chunk_thing_state_hp[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];
static u8 chunk_drop_active[DOOM_CHUNK_COUNT][CHUNK_DYNAMIC_DROP_SLOTS];
static u16 chunk_drop_type[DOOM_CHUNK_COUNT][CHUNK_DYNAMIC_DROP_SLOTS];
static short chunk_drop_x_q8[DOOM_CHUNK_COUNT][CHUNK_DYNAMIC_DROP_SLOTS];
static short chunk_drop_y_q8[DOOM_CHUNK_COUNT][CHUNK_DYNAMIC_DROP_SLOTS];
static unsigned short thing_chunk_index[NG_RUNTIME_THING_COUNT];
static short thing_chunk_offset_x_q8[NG_RUNTIME_THING_COUNT];
static short thing_chunk_offset_y_q8[NG_RUNTIME_THING_COUNT];
#endif
static u8 hurt_flash = 0;
static u8 muzzle_flash = 0;
static u8 bonus_flash = 0;
static u8 palette_effect = 0;
static u8 power_palette_kind = 0;

#ifndef DOOM_FAST_DAMAGE_FLASH
#define DOOM_FAST_DAMAGE_FLASH 1
#endif
#ifndef DOOM_FAST_BONUS_FLASH
#define DOOM_FAST_BONUS_FLASH 1
#endif
#ifndef DOOM_CHECKER_FLASH_OVERLAY
#define DOOM_CHECKER_FLASH_OVERLAY 1
#endif
static u8 sector_floor_visual_kind = 0xFF;
static u8 sector_light_band = 0xFF;
static u8 sector_liquid_phase = 0;
static u8 sector_liquid_tick = 0;
static u8 flash_overlay_active = 0;
static int sector_palette_px_key = 0x7FFFFFFF;
static int sector_palette_py_key = 0x7FFFFFFF;
static int sector_palette_dir_x = 0x7FFFFFFF;
static int sector_palette_dir_y = 0x7FFFFFFF;
static u8 face_pain_timer = 0;
static u8 face_evil_timer = 0;
static u8 face_turn_timer = 0;
static u8 face_turn_frame = 0;
static u8 face_idle_tick = 0;
static u8 face_idle_variant = 0;
static u16 power_invuln_timer = 0;
static u16 power_invis_timer = 0;
static u16 power_radsuit_timer = 0;
static u16 power_lightamp_timer = 0;
static u8 power_computer_map = 0;

/* ---- palette setup --------------------------------------------------- */
static u8 shade_channel(u8 value, u16 scale) {
    u16 shaded = (u16)value * scale / 128;
    return shaded > 31 ? 31 : (u8)shaded;
}

static void set_shaded_palette(u16 pal, const u8 rgb[][3], u16 colors, u16 scale) {
    for (u16 i = 0; i < colors; i++) {
        u8 r = shade_channel(rgb[i][0], scale);
        u8 g = shade_channel(rgb[i][1], scale);
        u8 b = shade_channel(rgb[i][2], scale);
        pal_set(pal, (u16)(i + 1), RGB(r, g, b));
    }
}

static u8 clamp31(int value);
static u8 sector_floor_visual_is_liquid(u8 kind);

static u8 sector_light_scale(u8 light) {
    switch (light) {
    case 0: return 88;
    case 1: return 104;
    case 2: return 116;
    default: return 128;
    }
}

static u8 sector_floor_pulse(u8 kind) {
    static const u8 pulse[] = {0, 2, 4, 2};
    if (kind == 0 || kind == 0xFF) return 0;
    return (kind >= 4) ? (u8)(pulse[sector_liquid_phase & 3] >> 1) : pulse[sector_liquid_phase & 3];
}

static u8 sector_floor_tint(u8 value, u8 kind, u8 component, u8 pulse) {
    int out = value;
    switch (kind) {
    case 1: /* water */
        out = (component == 2) ? value + 8 + pulse : (component == 1 ? value + 2 : value * 2 / 3);
        break;
    case 2: /* nukage/slime/lava/hazard */
        if (component == 1) out = value + 10 + pulse;
        else if (component == 2) out = value + 4 + (pulse >> 1);
        else out = value * 2 / 3;
        break;
    case 3: /* blood */
        out = (component == 0) ? value + 8 + pulse : value * 2 / 3;
        break;
    case 4: /* water visible ahead */
        if (component == 2) out = value + 7 + pulse;
        else if (component == 1) out = value + 2;
        else out = value * 3 / 4;
        break;
    case 5: /* hazard visible ahead */
        if (component == 1) out = value + 8 + pulse;
        else if (component == 2) out = value + 4 + (pulse >> 1);
        else out = value * 3 / 4;
        break;
    case 6: /* blood visible ahead */
        out = (component == 0) ? value + 2 + pulse : value;
        break;
    default:
        break;
    }
    return clamp31(out);
}

static void set_sector_flat_palette(u8 kind, u8 light) {
    u16 light_scale = sector_light_scale(light);
    u8 pulse = sector_floor_pulse(kind);
    u8 base_kind = (kind <= 3) ? kind : 0;
    for (int i = 0; i < CEILING_PALETTE_COLORS; i++) {
        u8 r = shade_channel(g_ceiling_palette_rgb[i][0], light_scale);
        u8 g = shade_channel(g_ceiling_palette_rgb[i][1], light_scale);
        u8 b = shade_channel(g_ceiling_palette_rgb[i][2], light_scale);
        pal_set(PAL_CEILING, (u16)(i + 1), RGB(r, g, b));
    }
    for (int i = 0; i < FLOOR_PALETTE_COLORS; i++) {
        u8 r = shade_channel(g_floor_palette_rgb[i][0], light_scale);
        u8 g = shade_channel(g_floor_palette_rgb[i][1], light_scale);
        u8 b = shade_channel(g_floor_palette_rgb[i][2], light_scale);
        pal_set(PAL_FLOOR, (u16)(i + 1), RGB(sector_floor_tint(r, base_kind, 0, pulse),
                                             sector_floor_tint(g, base_kind, 1, pulse),
                                             sector_floor_tint(b, base_kind, 2, pulse)));
    }

    for (u16 row = 0; row < BG_SPLIT; row++) {
        u16 ceiling_scale = (u16)((90 + row * 24) * light_scale / 128);
        u16 floor_scale = (u16)((150 + row * 42) * light_scale / 128);
        u8 row_base_kind = base_kind;
        if (kind >= 4 && row < (BG_SPLIT / 2)) row_base_kind = kind;
        for (u16 i = 0; i < CEILING_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_ceiling_palette_rgb[i][0], ceiling_scale);
            u8 g = shade_channel(g_ceiling_palette_rgb[i][1], ceiling_scale);
            u8 b = shade_channel(g_ceiling_palette_rgb[i][2], ceiling_scale);
            pal_set((u16)(PAL_CEILING_GRAD_BASE + row), (u16)(i + 1), RGB(r, g, b));
        }
        for (u16 i = 0; i < FLOOR_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_floor_palette_rgb[i][0], floor_scale);
            u8 g = shade_channel(g_floor_palette_rgb[i][1], floor_scale);
            u8 b = shade_channel(g_floor_palette_rgb[i][2], floor_scale);
            pal_set((u16)(PAL_FLOOR_GRAD_BASE + row), (u16)(i + 1),
                    RGB(sector_floor_tint(r, row_base_kind, 0, pulse),
                        sector_floor_tint(g, row_base_kind, 1, pulse),
                        sector_floor_tint(b, row_base_kind, 2, pulse)));
        }
    }
}

static void restore_flat_palettes(void) {
    u8 kind = sector_floor_visual_kind == 0xFF ? 0 : sector_floor_visual_kind;
    u8 light = sector_light_band == 0xFF ? 3 : sector_light_band;
    set_sector_flat_palette(kind, light);
}

static void set_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 256 - (b * 112) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 204 : 256;
            u16 pal = (u16)(base + s * DEPTH_BANDS + b);
            for (u16 i = 0; i < colors; i++) {
                int r = rgb[i][0] * fn / 256 * sf / 256;
                int g = rgb[i][1] * fn / 256 * sf / 256;
                int bl = rgb[i][2] * fn / 256 * sf / 256;
                pal_set(pal, (u16)(i + 1), RGB((u8)r, (u8)g, (u8)bl));
            }
        }
    }
}

static void restore_wall_depth_palettes(void) {
    set_depth_palette_range(PAL_DEPTH_BASE, g_wall_palette_rgb, WALL_PALETTE_COLORS);
    for (u16 alt = 0; alt < WALL_ALT_TEXTURE_COUNT; alt++) {
        u16 base = (u16)(PAL_WALL_ALT_DEPTH_BASE + alt * PAL_WALL_ALT_DEPTH_STRIDE);
        set_depth_palette_range(base, g_wall_alt_palette_rgb[alt], WALL_ALT_PALETTE_COLORS);
    }
    set_depth_palette_range(PAL_DOOR_DEPTH_BASE, g_door_palette_rgb, DOOR_PALETTE_COLORS);
}

static u8 clamp31(int value) {
    if (value < 0) return 0;
    if (value > 31) return 31;
    return (u8)value;
}

static void restore_weapon_palette(void);
static void set_flash_checker_overlay(u16 color);
static void clear_flash_checker_overlay(void);

static void set_muzzle_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 390 - (b * 135) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 260 : 390;
            u16 pal = (u16)(base + s * DEPTH_BANDS + b);
            for (u16 i = 0; i < colors; i++) {
                int r = rgb[i][0] * fn / 256 * sf / 256 + 8;
                int g = rgb[i][1] * fn / 256 * sf / 256 + 6;
                int bl = rgb[i][2] * fn / 256 * sf / 256 + 2;
                pal_set(pal, (u16)(i + 1), RGB(clamp31(r), clamp31(g), clamp31(bl)));
            }
        }
    }
}

static void set_muzzle_palettes(void) {
    /* Intentionally no wall/depth palette rewrite here.  Shot lighting uses a
     * Neo Geo-friendly backdrop/weapon pulse so it can start in the same vblank
     * as the trigger without touching thousands of palette RAM entries. */
    REG_BACKDROP = RGB(1, 1, 0);
}

static void set_bonus_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 300 - (b * 145) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 170 : 260;
            u16 pal = (u16)(base + s * DEPTH_BANDS + b);
            for (u16 i = 0; i < colors; i++) {
                int r = rgb[i][0] * fn / 256 * sf / 256 + 6;
                int g = rgb[i][1] * fn / 256 * sf / 256 + 5;
                int bl = rgb[i][2] * fn / 256 * sf / 256;
                pal_set(pal, (u16)(i + 1), RGB(clamp31(r), clamp31(g), clamp31(bl)));
            }
        }
    }
}

static void set_bonus_palettes(void) {
#if DOOM_FAST_BONUS_FLASH
#if DOOM_CHECKER_FLASH_OVERLAY
    set_flash_checker_overlay(RGB(12, 8, 0));
#else
    REG_BACKDROP = RGB(7, 5, 0);
#endif
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        u8 r = clamp31(g_weapon_palette_rgb[i][0] + 5);
        u8 g = clamp31(g_weapon_palette_rgb[i][1] + 4);
        u8 b = g_weapon_palette_rgb[i][2];
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(r, g, b));
    }
#else
    for (u16 row = 0; row < BG_SPLIT; row++) {
        u16 ceiling_scale = (u16)(128 + row * 24);
        u16 floor_scale = (u16)(190 + row * 42);
        set_shaded_palette((u16)(PAL_CEILING_GRAD_BASE + row), g_ceiling_palette_rgb, CEILING_PALETTE_COLORS, ceiling_scale);
        set_shaded_palette((u16)(PAL_FLOOR_GRAD_BASE + row), g_floor_palette_rgb, FLOOR_PALETTE_COLORS, floor_scale);
    }
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        u8 r = clamp31(g_weapon_palette_rgb[i][0] + 5);
        u8 g = clamp31(g_weapon_palette_rgb[i][1] + 4);
        u8 b = g_weapon_palette_rgb[i][2];
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(r, g, b));
    }
    set_bonus_depth_palette_range(PAL_DEPTH_BASE, g_wall_palette_rgb, WALL_PALETTE_COLORS);
    for (u16 alt = 0; alt < WALL_ALT_TEXTURE_COUNT; alt++) {
        u16 base = (u16)(PAL_WALL_ALT_DEPTH_BASE + alt * PAL_WALL_ALT_DEPTH_STRIDE);
        set_bonus_depth_palette_range(base, g_wall_alt_palette_rgb[alt], WALL_ALT_PALETTE_COLORS);
    }
    set_bonus_depth_palette_range(PAL_DOOR_DEPTH_BASE, g_door_palette_rgb, DOOR_PALETTE_COLORS);
#endif
}

static void restore_bonus_palettes(void) {
#if DOOM_FAST_BONUS_FLASH
#if DOOM_CHECKER_FLASH_OVERLAY
    clear_flash_checker_overlay();
#else
    REG_BACKDROP = RGB(0, 0, 0);
#endif
    restore_weapon_palette();
#else
    restore_play_palettes();
#endif
}

static u8 hurt_tint_r(u8 value) {
    return clamp31(value + 9);
}

static u8 hurt_tint_dim(u8 value) {
    return (u8)(value * 3 / 5);
}

static void set_hurt_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 256 - (b * 200) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 140 : 256;
            u16 pal = (u16)(base + s * DEPTH_BANDS + b);
            for (u16 i = 0; i < colors; i++) {
                u8 r = (u8)(rgb[i][0] * fn / 256 * sf / 256);
                u8 g = (u8)(rgb[i][1] * fn / 256 * sf / 256);
                u8 bl = (u8)(rgb[i][2] * fn / 256 * sf / 256);
                pal_set(pal, (u16)(i + 1), RGB(hurt_tint_r(r), hurt_tint_dim(g), hurt_tint_dim(bl)));
            }
        }
    }
}

static u8 power_tint_channel(u8 value, u8 kind, u8 component) {
    int out = value;
    switch (kind) {
    case 1: /* invulnerability: bright washed-out playfield */
        out = value + 9;
        break;
    case 2: /* radiation suit: restrained green cast */
        if (component == 1) out = value + 6;
        else out = value * 3 / 4;
        break;
    case 3: /* partial invisibility: cool dimming */
        if (component == 2) out = value + 7;
        else out = value * 2 / 3;
        break;
    case 4: /* light goggles: brighter but not muzzle-flash bright */
        out = value + 4;
        break;
    default:
        break;
    }
    return clamp31(out);
}

static void set_power_tinted_palette(u16 pal, const u8 rgb[][3], u16 colors, u8 kind) {
    for (u16 i = 0; i < colors; i++) {
        u8 r = power_tint_channel(rgb[i][0], kind, 0);
        u8 g = power_tint_channel(rgb[i][1], kind, 1);
        u8 b = power_tint_channel(rgb[i][2], kind, 2);
        pal_set(pal, (u16)(i + 1), RGB(r, g, b));
    }
}

static void set_power_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors, u8 kind) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 256 - (b * 200) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 140 : 256;
            u16 pal = (u16)(base + s * DEPTH_BANDS + b);
            for (u16 i = 0; i < colors; i++) {
                u8 r = (u8)(rgb[i][0] * fn / 256 * sf / 256);
                u8 g = (u8)(rgb[i][1] * fn / 256 * sf / 256);
                u8 bl = (u8)(rgb[i][2] * fn / 256 * sf / 256);
                pal_set(pal, (u16)(i + 1), RGB(power_tint_channel(r, kind, 0),
                                               power_tint_channel(g, kind, 1),
                                               power_tint_channel(bl, kind, 2)));
            }
        }
    }
}

static void set_power_flat_gradients(u8 kind) {
    set_power_tinted_palette(PAL_CEILING, g_ceiling_palette_rgb, CEILING_PALETTE_COLORS, kind);
    set_power_tinted_palette(PAL_FLOOR, g_floor_palette_rgb, FLOOR_PALETTE_COLORS, kind);
    for (u16 row = 0; row < BG_SPLIT; row++) {
        u16 ceiling_scale = (u16)(90 + row * 24);
        u16 floor_scale = (u16)(150 + row * 42);
        for (u16 i = 0; i < CEILING_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_ceiling_palette_rgb[i][0], ceiling_scale);
            u8 g = shade_channel(g_ceiling_palette_rgb[i][1], ceiling_scale);
            u8 b = shade_channel(g_ceiling_palette_rgb[i][2], ceiling_scale);
            pal_set((u16)(PAL_CEILING_GRAD_BASE + row), (u16)(i + 1),
                    RGB(power_tint_channel(r, kind, 0), power_tint_channel(g, kind, 1), power_tint_channel(b, kind, 2)));
        }
        for (u16 i = 0; i < FLOOR_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_floor_palette_rgb[i][0], floor_scale);
            u8 g = shade_channel(g_floor_palette_rgb[i][1], floor_scale);
            u8 b = shade_channel(g_floor_palette_rgb[i][2], floor_scale);
            pal_set((u16)(PAL_FLOOR_GRAD_BASE + row), (u16)(i + 1),
                    RGB(power_tint_channel(r, kind, 0), power_tint_channel(g, kind, 1), power_tint_channel(b, kind, 2)));
        }
    }
}

static void set_power_palettes(u8 kind) {
    set_power_flat_gradients(kind);
    set_power_tinted_palette(PAL_WEAPON, g_weapon_palette_rgb, WEAPON_PALETTE_COLORS, kind);
    set_power_depth_palette_range(PAL_DEPTH_BASE, g_wall_palette_rgb, WALL_PALETTE_COLORS, kind);
    for (u16 alt = 0; alt < WALL_ALT_TEXTURE_COUNT; alt++) {
        u16 base = (u16)(PAL_WALL_ALT_DEPTH_BASE + alt * PAL_WALL_ALT_DEPTH_STRIDE);
        set_power_depth_palette_range(base, g_wall_alt_palette_rgb[alt], WALL_ALT_PALETTE_COLORS, kind);
    }
    set_power_depth_palette_range(PAL_DOOR_DEPTH_BASE, g_door_palette_rgb, DOOR_PALETTE_COLORS, kind);
}

static u8 current_power_palette_kind(void) {
    if (power_invuln_timer) return 1;
    if (power_radsuit_timer) return 2;
    if (power_invis_timer) return 3;
    if (power_lightamp_timer) return 4;
    return 0;
}

static void restore_weapon_palette(void) {
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(g_weapon_palette_rgb[i][0], g_weapon_palette_rgb[i][1], g_weapon_palette_rgb[i][2]));
    }
}

static void restore_counter_palette(void) {
    for (int i = 1; i < 16; i++) pal_set(PAL_AMMO_COUNTER_SHADOW, (u16)i, RGB(4, 2, 0));
    for (int i = 1; i < 15; i++) pal_set(PAL_AMMO_COUNTER, (u16)i, RGB(5, 3, 0));
    pal_set(PAL_AMMO_COUNTER, 15, RGB(20, 17, 4));
}

static void init_flash_overlay_sprites(void) {
#if DOOM_CHECKER_FLASH_OVERLAY
    for (u16 col = 0; col < FLASH_OVERLAY_COUNT; col++) {
        u16 spr = (u16)(FLASH_OVERLAY_BASE + col);
        scb1_fill(spr, FLASH_OVERLAY_WIN, TILE_FLASH_CHECKER, PAL_MAP_WALL);
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
        scb4(spr, (u16)(col * 16));
    }
    flash_overlay_active = 0;
#endif
}

static void set_flash_checker_overlay(u16 color) {
#if DOOM_CHECKER_FLASH_OVERLAY
    pal_set(PAL_MAP_WALL, 1, color);
    if (flash_overlay_active) return;
    for (u16 col = 0; col < FLASH_OVERLAY_COUNT; col++) {
        u16 spr = (u16)(FLASH_OVERLAY_BASE + col);
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, 0, 0, FLASH_OVERLAY_WIN);
    }
    flash_overlay_active = 1;
#else
    (void)color;
#endif
}

static void clear_flash_checker_overlay(void) {
#if DOOM_CHECKER_FLASH_OVERLAY
    if (!flash_overlay_active) return;
    for (u16 col = 0; col < FLASH_OVERLAY_COUNT; col++) {
        u16 spr = (u16)(FLASH_OVERLAY_BASE + col);
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
    }
    flash_overlay_active = 0;
#endif
}

static void set_weapon_flash_palette(void) {
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        u8 r = g_weapon_palette_rgb[i][0];
        u8 g = g_weapon_palette_rgb[i][1];
        u8 b = g_weapon_palette_rgb[i][2];
        r = clamp31(r + 7);
        g = clamp31(g + 5);
        b = clamp31(b + 2);
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(r, g, b));
    }
}

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

    restore_wall_depth_palettes();

    restore_flat_palettes();

    /* minimap */
    pal_set(PAL_MAP_WALL,   15, RGB(20, 20, 22)); /* walls */
    for (int i = 1; i < 16; i++) {
        pal_set(PAL_MAP_PLAYER, (u16)i, RGB(4, 31, 8)); /* player + HUD digits */
    }
    for (int i = 0; i < HUD_PALETTE_COLORS; i++) {
        u8 r = g_hud_palette_rgb[i][0];
        u8 g = g_hud_palette_rgb[i][1];
        u8 b = g_hud_palette_rgb[i][2];
        pal_set(PAL_HUD, (u16)(i + 1), RGB(r, g, b));
    }
    for (int i = 0; i < TITLE_PALETTE_COLORS; i++) {
        u8 r = g_title_palette_rgb[i][0];
        u8 g = g_title_palette_rgb[i][1];
        u8 b = g_title_palette_rgb[i][2];
        pal_set(PAL_TITLE, (u16)(i + 1), RGB(r, g, b));
    }
    restore_counter_palette();
    restore_weapon_palette();
    REG_BACKDROP = RGB(0, 0, 0);
}

static void restore_play_palettes(void) {
    restore_flat_palettes();
    for (int i = 0; i < HUD_PALETTE_COLORS; i++) {
        pal_set(PAL_HUD, (u16)(i + 1), RGB(g_hud_palette_rgb[i][0], g_hud_palette_rgb[i][1], g_hud_palette_rgb[i][2]));
    }
    restore_counter_palette();
    restore_weapon_palette();
    restore_wall_depth_palettes();
}

static u8 sector_floor_visual_priority(u8 kind) {
    switch (kind) {
    case 2: return 4; /* damaging liquid/hazard must read before contact */
    case 5: return 4; /* visible hazard preview */
    case 3: return 3; /* blood */
    case 6: return 3; /* visible blood preview */
    case 1: return 2; /* water */
    case 4: return 2; /* visible water preview */
    default: return 1;
    }
}

static u8 sector_floor_visual_is_liquid(u8 kind) {
    return kind >= 1 && kind <= 6;
}

static void consider_sector_palette_cell(int cell_x, int cell_y, u8 *best_kind, u8 *best_light, u8 *best_priority) {
    u8 kind;
    u8 priority;
    if (map_at(cell_x, cell_y)) return;
    kind = map_cell_floor_visual(cell_x, cell_y);
    if (!kind) return;
    priority = sector_floor_visual_priority(kind);
    if (priority <= *best_priority) return;
    *best_kind = (u8)(kind + 3);
    *best_light = map_cell_light(cell_x, cell_y);
    *best_priority = priority;
}

static void sample_sector_palette_ray(int px_q8, int py_q8, int ray_x_q8, int ray_y_q8,
                                      u8 *best_kind, u8 *best_light, u8 *best_priority) {
    static const short distances_q8[] = {192, 384, 640, DOOM_SECTOR_PREVIEW_MAX_Q8};
    u8 peek_blocks = 0;
    for (u8 i = 0; i < (u8)(sizeof(distances_q8) / sizeof(distances_q8[0])); i++) {
        int sample_x_q8 = px_q8 + (int)(((long)ray_x_q8 * distances_q8[i]) >> 8);
        int sample_y_q8 = py_q8 + (int)(((long)ray_y_q8 * distances_q8[i]) >> 8);
        int cell_x = sample_x_q8 >> 8;
        int cell_y = sample_y_q8 >> 8;
        if (map_at(cell_x, cell_y)) {
            if (++peek_blocks > 2) break;
            continue;
        }
        consider_sector_palette_cell(cell_x, cell_y, best_kind, best_light, best_priority);
    }
}

static u8 sector_palette_cell_visible(int from_x, int from_y, int to_x, int to_y) {
    int dx = to_x - from_x;
    int dy = to_y - from_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    int x_acc;
    int y_acc;
    int x_step;
    int y_step;
    u8 wall_peek = 0;
    if (steps <= 1) return 1;
    x_acc = (from_x << 8) + 128;
    y_acc = (from_y << 8) + 128;
    x_step = (dx << 8) / steps;
    y_step = (dy << 8) / steps;
    for (int i = 1; i < steps; i++) {
        int x;
        int y;
        x_acc += x_step;
        y_acc += y_step;
        x = x_acc >> 8;
        y = y_acc >> 8;
        if (x == to_x && y == to_y) break;
        if (map_at(x, y) && ++wall_peek > 2) return 0;
    }
    return 1;
}

static void sample_sector_palette_cone(int px_q8, int py_q8, int dir_x_q8, int dir_y_q8,
                                       int plane_x_q8, int plane_y_q8,
                                       u8 *best_kind, u8 *best_light, u8 *best_priority) {
    int pcx = px_q8 >> 8;
    int pcy = py_q8 >> 8;
    for (int y = pcy - 16; y <= pcy + 16; y++) {
        for (int x = pcx - 16; x <= pcx + 16; x++) {
            int rel_x_q8;
            int rel_y_q8;
            long front;
            long side;
            if (map_at(x, y)) continue;
            if (!map_cell_floor_visual(x, y)) continue;
            rel_x_q8 = ((x << 8) + 128) - px_q8;
            rel_y_q8 = ((y << 8) + 128) - py_q8;
            front = ((long)rel_x_q8 * dir_x_q8 + (long)rel_y_q8 * dir_y_q8) >> 8;
            if (front < 384 || front > DOOM_SECTOR_PREVIEW_MAX_Q8) continue;
            side = ((long)rel_x_q8 * plane_x_q8 + (long)rel_y_q8 * plane_y_q8) >> 8;
            if (side < 0) side = -side;
            if (side > front + 384) continue;
            if (!sector_palette_cell_visible(pcx, pcy, x, y)) continue;
            consider_sector_palette_cell(x, y, best_kind, best_light, best_priority);
        }
    }
}

static void update_sector_flat_palette(void) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    int px_key;
    int py_key;
    u8 kind;
    u8 light;
    u8 priority;
    u8 phase_dirty = 0;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    px_key = px >> 6;
    py_key = py >> 6;
    if (px_key == sector_palette_px_key && py_key == sector_palette_py_key
        && dir_x == sector_palette_dir_x && dir_y == sector_palette_dir_y) {
        if (!sector_floor_visual_is_liquid(sector_floor_visual_kind)) return;
        if (++sector_liquid_tick < 12) return;
        sector_liquid_tick = 0;
        sector_liquid_phase = (u8)((sector_liquid_phase + 1) & 3);
        if (palette_effect == 0) restore_flat_palettes();
        return;
    }
    sector_palette_px_key = px_key;
    sector_palette_py_key = py_key;
    sector_palette_dir_x = dir_x;
    sector_palette_dir_y = dir_y;
    kind = map_cell_floor_visual(px >> 8, py >> 8);
    light = map_cell_light(px >> 8, py >> 8);
    priority = sector_floor_visual_priority(kind);
    sample_sector_palette_ray(px, py, dir_x, dir_y, &kind, &light, &priority);
    sample_sector_palette_ray(px, py, dir_x + (plane_x >> 1), dir_y + (plane_y >> 1), &kind, &light, &priority);
    sample_sector_palette_ray(px, py, dir_x - (plane_x >> 1), dir_y - (plane_y >> 1), &kind, &light, &priority);
    sample_sector_palette_cone(px, py, dir_x, dir_y, plane_x, plane_y, &kind, &light, &priority);
    if (sector_floor_visual_is_liquid(kind)) {
        if (++sector_liquid_tick >= 12) {
            sector_liquid_tick = 0;
            sector_liquid_phase = (u8)((sector_liquid_phase + 1) & 3);
            phase_dirty = 1;
        }
    } else {
        sector_liquid_tick = 0;
        if (sector_liquid_phase) {
            sector_liquid_phase = 0;
            phase_dirty = 1;
        }
    }
    if (kind == sector_floor_visual_kind && light == sector_light_band && !phase_dirty) return;
    sector_floor_visual_kind = kind;
    sector_light_band = light;
    if (palette_effect == 0) restore_flat_palettes();
}

static void set_hurt_palettes(void) {
#if DOOM_FAST_DAMAGE_FLASH
#if DOOM_CHECKER_FLASH_OVERLAY
    set_flash_checker_overlay(RGB(18, 0, 0));
#else
    REG_BACKDROP = RGB(14, 0, 0);
#endif
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(hurt_tint_r(g_weapon_palette_rgb[i][0]),
                                             hurt_tint_dim(g_weapon_palette_rgb[i][1]),
                                             hurt_tint_dim(g_weapon_palette_rgb[i][2])));
    }
#else
    for (u16 row = 0; row < BG_SPLIT; row++) {
        u16 ceiling_scale = (u16)(90 + row * 24);
        u16 floor_scale = (u16)(150 + row * 42);
        for (u16 i = 0; i < CEILING_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_ceiling_palette_rgb[i][0], ceiling_scale);
            u8 g = shade_channel(g_ceiling_palette_rgb[i][1], ceiling_scale);
            u8 b = shade_channel(g_ceiling_palette_rgb[i][2], ceiling_scale);
            pal_set((u16)(PAL_CEILING_GRAD_BASE + row), (u16)(i + 1), RGB(hurt_tint_r(r), hurt_tint_dim(g), hurt_tint_dim(b)));
        }
        for (u16 i = 0; i < FLOOR_PALETTE_COLORS; i++) {
            u8 r = shade_channel(g_floor_palette_rgb[i][0], floor_scale);
            u8 g = shade_channel(g_floor_palette_rgb[i][1], floor_scale);
            u8 b = shade_channel(g_floor_palette_rgb[i][2], floor_scale);
            pal_set((u16)(PAL_FLOOR_GRAD_BASE + row), (u16)(i + 1), RGB(hurt_tint_r(r), hurt_tint_dim(g), hurt_tint_dim(b)));
        }
    }
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(hurt_tint_r(g_weapon_palette_rgb[i][0]),
                                             hurt_tint_dim(g_weapon_palette_rgb[i][1]),
                                             hurt_tint_dim(g_weapon_palette_rgb[i][2])));
    }
    set_hurt_depth_palette_range(PAL_DEPTH_BASE, g_wall_palette_rgb, WALL_PALETTE_COLORS);
    for (u16 alt = 0; alt < WALL_ALT_TEXTURE_COUNT; alt++) {
        u16 base = (u16)(PAL_WALL_ALT_DEPTH_BASE + alt * PAL_WALL_ALT_DEPTH_STRIDE);
        set_hurt_depth_palette_range(base, g_wall_alt_palette_rgb[alt], WALL_ALT_PALETTE_COLORS);
    }
    set_hurt_depth_palette_range(PAL_DOOR_DEPTH_BASE, g_door_palette_rgb, DOOR_PALETTE_COLORS);
#endif
}

static void restore_hurt_palettes(void) {
#if DOOM_FAST_DAMAGE_FLASH
#if DOOM_CHECKER_FLASH_OVERLAY
    clear_flash_checker_overlay();
#else
    REG_BACKDROP = RGB(0, 0, 0);
#endif
    restore_weapon_palette();
#else
    restore_play_palettes();
#endif
}

static void update_hurt_flash(void) {
    u8 active_power_kind = current_power_palette_kind();
    if (hurt_flash) {
        if (palette_effect != 1) {
            set_hurt_palettes();
            palette_effect = 1;
        }
        hurt_flash--;
    } else if (bonus_flash) {
        if (palette_effect != 3) {
            set_bonus_palettes();
            palette_effect = 3;
        }
        bonus_flash--;
    } else if (muzzle_flash) {
        if (palette_effect != 2) {
            set_muzzle_palettes();
            palette_effect = 2;
        }
        muzzle_flash--;
    } else if (active_power_kind) {
        if (palette_effect != 4 || power_palette_kind != active_power_kind) {
            set_power_palettes(active_power_kind);
            palette_effect = 4;
            power_palette_kind = active_power_kind;
            REG_BACKDROP = RGB(0, 0, 0);
        }
    } else if (palette_effect) {
        if (palette_effect == 2) REG_BACKDROP = RGB(0, 0, 0);
        else if (palette_effect == 1) restore_hurt_palettes();
        else if (palette_effect == 3) restore_bonus_palettes();
        else restore_play_palettes();
        palette_effect = 0;
        power_palette_kind = 0;
    }
}

/* ---- clear the fix layer */
static void clear_fix(void) {
    vram_addr(VRAM_FIX);
    vram_mod(1);
    for (int i = 0; i < 40 * 32; i++) vram_w(0x0000);
}

static void disable_sprites(void);

static const char *intro_glyph_rows(char ch) {
    switch (ch) {
    case 'A': return "111101111101101";
    case 'B': return "110101110101110";
    case 'C': return "111100100100111";
    case 'D': return "110101101101110";
    case 'E': return "111100110100111";
    case 'F': return "111100110100100";
    case 'G': return "111100101101111";
    case 'H': return "101101111101101";
    case 'I': return "111010010010111";
    case 'K': return "101101110101101";
    case 'L': return "100100100100111";
    case 'M': return "101111111101101";
    case 'N': return "101111111111101";
    case 'O': return "111101101101111";
    case 'P': return "110101110100100";
    case 'R': return "110101110101101";
    case 'S': return "111100111001111";
    case 'T': return "111010010010010";
    case 'U': return "101101101101111";
    case 'V': return "101101101101010";
    case 'X': return "101101010101101";
    case 'Y': return "101101010010010";
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    default: return "000000000000000";
    }
}

static void intro_draw_glyph(u16 col, u16 row, char ch, u16 pal) {
    const char *bits = intro_glyph_rows(ch);
    for (u16 y = 0; y < 5; y++) {
        for (u16 x = 0; x < 3; x++) {
            if (bits[y * 3 + x] == '1') fix_poke((u16)(col + x), (u16)(row + y), pal, FIX_SOLID);
        }
    }
}

static void intro_draw_word(u16 col, u16 row, const char *text, u16 pal) {
    u16 x = col;
    while (*text) {
        if (*text != ' ') intro_draw_glyph(x, row, *text, pal);
        x = (u16)(x + 4);
        text++;
    }
}

static void intro_draw_rule(u16 col, u16 row, u16 width, u16 pal) {
    for (u16 x = 0; x < width; x++) fix_poke((u16)(col + x), row, pal, FIX_SOLID);
}

static void intro_draw_map_code_value(u16 col, u16 row, u16 pal, u8 episode, u8 mission) {
    intro_draw_glyph(col, row, 'E', pal);
    intro_draw_glyph((u16)(col + 4), row, (char)('0' + episode), pal);
    intro_draw_glyph((u16)(col + 8), row, 'M', pal);
    intro_draw_glyph((u16)(col + 12), row, (char)('0' + mission), pal);
}

static void intro_draw_map_code(u16 col, u16 row, u16 pal) {
    intro_draw_map_code_value(col, row, pal, DOOM_MAP_EPISODE, DOOM_MAP_MISSION);
}

static void intro_draw_menu_item(u16 row, const char *text, u8 selected, u8 blink_on) {
    if (selected && blink_on) {
        fix_poke(6, (u16)(row + 2), PAL_MAP_PLAYER, FIX_SOLID);
        fix_poke(7, (u16)(row + 2), PAL_MAP_PLAYER, FIX_SOLID);
    }
    intro_draw_word(10, row, text, selected ? PAL_HUD : PAL_MAP_WALL);
}

static void intro_draw_skill_name(u16 col, u16 row, u16 pal) {
    if (DOOM_RUNTIME_SKILL_MASK == 1) intro_draw_word(col, row, "EASY", pal);
    else if (DOOM_RUNTIME_SKILL_MASK == 2) intro_draw_word(col, row, "MED", pal);
    else if (DOOM_RUNTIME_SKILL_MASK == 4) intro_draw_word(col, row, "HARD", pal);
    else intro_draw_word(col, row, "BUILD", pal);
}

static void draw_intro_main_menu(u8 selected, u8 blink_on) {
    const char *label = "START";
    if (selected == 1) label = "SKILL";
    else if (selected == 2) label = "MAP";
    else if (selected == 3) label = "OPTS";
    clear_fix();
    intro_draw_menu_item(21, label, 1, blink_on);
}

static void draw_intro_skill_menu(void) {
    clear_fix();
    intro_draw_rule(6, 2, 28, PAL_MAP_WALL);
    intro_draw_word(8, 5, "SKILL", PAL_MAP_PLAYER);
    intro_draw_rule(6, 11, 28, PAL_MAP_WALL);
    intro_draw_skill_name(10, 15, PAL_HUD);
    intro_draw_word(4, 24, "A BACK", PAL_MAP_PLAYER);
    intro_draw_word(24, 24, "B OK", PAL_MAP_PLAYER);
}

static void draw_intro_map_menu(void) {
    clear_fix();
    intro_draw_rule(6, 2, 28, PAL_MAP_WALL);
    intro_draw_word(12, 5, "MAP", PAL_MAP_PLAYER);
    intro_draw_rule(6, 11, 28, PAL_MAP_WALL);
    intro_draw_map_code(12, 14, PAL_HUD);
    if (DOOM_NEXT_MAP_EPISODE && DOOM_NEXT_MAP_MISSION) {
        intro_draw_word(6, 21, "NEXT", PAL_MAP_PLAYER);
        intro_draw_map_code_value(22, 21, PAL_HUD, DOOM_NEXT_MAP_EPISODE, DOOM_NEXT_MAP_MISSION);
    } else {
        intro_draw_word(12, 21, "END", PAL_MAP_PLAYER);
    }
    intro_draw_word(4, 27, "A BACK", PAL_MAP_PLAYER);
}

static void draw_intro_options_menu(void) {
    clear_fix();
    intro_draw_rule(6, 2, 28, PAL_MAP_WALL);
    intro_draw_word(6, 5, "OPTIONS", PAL_MAP_PLAYER);
    intro_draw_rule(6, 11, 28, PAL_MAP_WALL);
    intro_draw_word(4, 15, "NO SOUND", PAL_HUD);
    intro_draw_word(4, 24, "A BACK", PAL_MAP_PLAYER);
}

static void draw_intro_titlepic(void) {
    for (u16 col = 0; col < TILE_TITLEPIC_COLS; col++) {
        u16 spr = (u16)(BG_BASE + col);
        for (u16 row = 0; row < TILE_TITLEPIC_ROWS; row++) {
            scb1_tile(spr, row, (u16)(TILE_TITLEPIC_BASE + row * TILE_TITLEPIC_COLS + col), PAL_TITLE);
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, 0, 0, TILE_TITLEPIC_ROWS);
        scb4(spr, (u16)(col * 16));
    }
}

static void run_intro_menu(void) {
    enum { UP = 0x01, DOWN = 0x02, A = 0x10, B = 0x20, D = 0x80 };
    enum { INTRO_MAIN = 0, INTRO_SKILL = 1, INTRO_MAP = 2, INTRO_OPTIONS = 3 };
    u8 page = INTRO_MAIN;
    u8 selected = 0;
    u8 prev_pressed = 0;
    u8 tick = 0;
    u8 dirty = 1;
    init_palettes();
    disable_sprites();
    draw_intro_titlepic();
    REG_BACKDROP = RGB(0, 0, 0);
    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;
        u8 edge = (u8)(pressed & ~prev_pressed);
        if (page == INTRO_MAIN) {
            if (edge & UP) {
                selected = selected ? (u8)(selected - 1) : 3;
                dirty = 1;
            } else if (edge & DOWN) {
                selected = (u8)((selected + 1) & 3);
                dirty = 1;
            } else if (edge & (B | D)) {
                if (selected == 0) break;
                page = selected;
                dirty = 1;
            }
        } else if (edge & A) {
            page = INTRO_MAIN;
            dirty = 1;
        } else if (edge & (B | D)) {
            if (page == INTRO_SKILL) {
                page = INTRO_MAIN;
                dirty = 1;
            }
        }
        prev_pressed = pressed;
        wait_vblank();
        watchdog_kick();
        tick++;
        if ((tick & 31) == 0) dirty = 1;
        if (dirty) {
            u8 blink_on = (tick & 32) == 0;
            if (page == INTRO_MAIN) draw_intro_main_menu(selected, blink_on);
            else if (page == INTRO_SKILL) draw_intro_skill_menu();
            else if (page == INTRO_MAP) draw_intro_map_menu();
            else draw_intro_options_menu();
            dirty = 0;
        }
    }
    clear_fix();
    disable_sprites();
}

static int prev_px = -1, prev_py = -1;
static u8  map_on = 0;              /* minimap visible?                       */
static u8  minimap_redraw_active = 0;
static u16 minimap_redraw_index = 0;
static u8  minimap_clear_active = 0;
static u16 minimap_clear_index = 0;
static u8  weapon_frame = 0xFF;
static u8  hud_face_frame = 0xFF;
static u8  weapon_bob_phase = 0;
static signed char weapon_bob_x = 0;
static signed char weapon_bob_y = 0;
static u8  weapon_flash_timer = 0;
static u8  weapon_flash_on = 0;
static u8  fire_timer = 0;
static u8  fire_prev = 0;
static u8  door_prev = 0;
static u8  map_prev = 0;
static u8  shortcut_prev = 0;
static u8  restart_prev = 0;
static u8  input_catchup_pending = 0;
static u8  hurt_timer = 0;
static u8  floor_damage_timer = 0;
static u8  armor_flash_timer = 0;
static u8  level_complete = 0;
static u8  level_next_episode = DOOM_NEXT_MAP_EPISODE;
static u8  level_next_mission = DOOM_NEXT_MAP_MISSION;
static u32 bg_scroll_key = 0xFFFFFFFFUL;
static u32 bg_pending_key = 0xFFFFFFFFUL;
static u32 bg_col_key[BG_COUNT];
static u8  bg_col_hidden[BG_COUNT];
static u8  bg_update_col = 0;
static int bg_direction_dir_x = 0x7FFFFFFF;
static int bg_direction_dir_y = 0x7FFFFFFF;
static u8  bg_direction_bucket = 0;
static u8  key_message_timer = 0;
static u8  ammo_message_timer = 0;
static u8  door_message_timer = 0;
static u8  secret_message_timer = 0;
static u8  pickup_message_timer = 0;
static u8  weapon_message_timer = 0;
static u8  weapon_message_digit = 0;
static u8  pickup_message_type = 0;
static u8  pickup_message_key = 0;
static u8  key_message_visible = 0;
static u8  missing_key_bits = 0;
static u8  monster_ai_tick = 0;
static u8  projectile_active = 0;
static u8  projectile_from_player = 0;
static int projectile_source_thing = -1;
static u16 projectile_type = 0;
static u8  projectile_timer = 0;
static u8  projectile_damage = 0;
static short projectile_x_q8 = 0;
static short projectile_y_q8 = 0;
static short projectile_dx_q8 = 0;
static short projectile_dy_q8 = 0;
static short projectile_hit_range_q8 = 0;
static u8  projectile_hit_coarse_cells = 0;
static u8  impact_active = 0;
static u8  impact_timer = 0;
static short impact_x_q8 = 0;
static short impact_y_q8 = 0;
static u8  player_keys = 0;
static u8  player_has_shotgun = 0;
static u8  player_has_chaingun = 0;
static u8  player_has_rocket_launcher = 0;
static u8  player_has_plasma = 0;
static u8  player_has_bfg = 0;
static u8  player_has_chainsaw = 0;
static u8  player_has_backpack = 0;
static u8  current_weapon = 0;
static u8  pickup_message_weapon = 0;
static u8  chaingun_flash = 0;
static u8  enemy_dead[NG_RUNTIME_THING_COUNT];
static u8  enemy_hp[NG_RUNTIME_THING_COUNT];
static u8  enemy_hit_flash[NG_RUNTIME_THING_COUNT];
static u8  enemy_awake[NG_RUNTIME_THING_COUNT];
static u8  enemy_attack_cooldown[NG_RUNTIME_THING_COUNT];
static u8  enemy_attack_anim[NG_RUNTIME_THING_COUNT];
static u8  enemy_ranged_readable_ticks[NG_RUNTIME_THING_COUNT];
static u8  enemy_hidden_timer[NG_RUNTIME_THING_COUNT];
static signed char monster_face_x[NG_RUNTIME_THING_COUNT];
static signed char monster_face_y[NG_RUNTIME_THING_COUNT];
static u8  explosion_timer[NG_RUNTIME_THING_COUNT];
static u8  death_anim_timer[NG_RUNTIME_THING_COUNT];
static u8  death_drop_timer[NG_RUNTIME_THING_COUNT];
static u16 thing_type_override[NG_RUNTIME_THING_COUNT];
static u16 death_anim_final_type[NG_RUNTIME_THING_COUNT];
static u16 death_anim_drop_type[NG_RUNTIME_THING_COUNT];
static u16 death_drop_type[NG_RUNTIME_THING_COUNT];
static short thing_x_q8[NG_RUNTIME_THING_COUNT];
static short thing_y_q8[NG_RUNTIME_THING_COUNT];
static u8 thing_static_class[NG_RUNTIME_THING_COUNT];
static u16 thing_monster_indices[NG_RUNTIME_THING_COUNT];
static u16 thing_monster_count = 0;
static u16 thing_shootable_indices[NG_RUNTIME_THING_COUNT];
static u16 thing_shootable_count = 0;
static u16 thing_render_indices[NG_RUNTIME_THING_COUNT];
static u16 thing_render_count = 0;
static u16 thing_pickup_indices[NG_RUNTIME_THING_COUNT];
static u16 thing_pickup_count = 0;
static u8  dynamic_drop_active[8];
static u16 dynamic_drop_type[8];
static short dynamic_drop_x_q8[8];
static short dynamic_drop_y_q8[8];
static u8 secret_found_bits[MAP_SECRET_BYTES ? MAP_SECRET_BYTES : 1];
static u8 monster_path_dist[ACTIVE_MAP_H][ACTIVE_MAP_W];
static u16 monster_path_queue[ACTIVE_MAP_W * ACTIVE_MAP_H];
static u8 monster_path_valid = 0;
static u8 monster_path_timer = 0;
static short monster_path_player_cell_x = -1;
static short monster_path_player_cell_y = -1;
static int enemy_palette_def[ENEMY_VISIBLE_COUNT] = {-1};
static int enemy_tile_key[ENEMY_VISIBLE_COUNT] = {-1};
static u8 enemy_slot_flash[ENEMY_VISIBLE_COUNT];
static u8 enemy_slot_hidden[ENEMY_VISIBLE_COUNT];
static short ranged_readable_prev[ENEMY_VISIBLE_COUNT];
static u8 ranged_readable_prev_count = 0;
static volatile u16 player_health = 100;
static volatile u16 player_armor = 0;
static u8 player_armor_class = 0;
static volatile u16 player_ammo = 50;
static volatile u16 player_shells = 0;
static volatile u16 player_rockets = 0;
static volatile u16 player_cells = 0;
static u8 player_berserk = 0;
static u16 player_max_bullets = 200;
static u16 player_max_shells = 50;
static u16 player_max_rockets = 50;
static u16 player_max_cells = 300;
static u16 player_score = 0;
static u16 player_kills = 0;
static u16 player_items = 0;
static u16 player_secrets = 0;
static u16 level_total_kills = 0;
static u16 level_total_items = 0;
static u16 level_total_secrets = 0;
static u16 shown_health = 0xFFFF;
static u16 shown_armor = 0xFFFF;
static u16 shown_ammo = 0xFFFF;
static u16 shown_frags = 0xFFFF;
static u16 shown_counter_current[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
static u16 shown_counter_max[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
static u8 shown_keys = 0xFF;
static u16 shown_weapon_status = 0xFFFF;
#ifdef DOOM_FRAME_STATS
static u8 frame_stats_frames = 0;
static u8 frame_stats_overruns = 0;
static u8 frame_stats_shown = 0xFF;
#endif

enum {
    PLAYER_MAX_BULLETS = 200,
    PLAYER_MAX_SHELLS = 50,
    PLAYER_MAX_ROCKETS = 50,
    PLAYER_MAX_CELLS = 300
};

enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_CHAINGUN = 2,
    WEAPON_ROCKET = 3,
    WEAPON_PLASMA = 4,
    WEAPON_BFG = 5,
    WEAPON_FIST = 6,
    WEAPON_CHAINSAW = 7,
    WEAPON_TOTAL = 8
};

#ifndef WEAPON_ASSET_MASK
#define WEAPON_ASSET_MASK 0xFF
#endif

static u8 weapon_asset_available(u8 weapon) {
    return (WEAPON_ASSET_MASK & (1u << weapon)) != 0;
}

typedef struct EnemyDraw {
    int thing_index;
    int sprite_def;
    u16 thing_type;
    int screen_x;
    int screen_w;
    int screen_h;
    int dist_q8;
    u8 fallback_projection;
    u8 is_monster;
    u8 shootable;
    u8 readable;
    u8 attackable;
    u8 ranged_attackable;
} EnemyDraw;

static void trigger_weapon_flash(void) {
    /* Keep the shot response on the tiny Neo Geo palette path: weapon palette
     * plus backdrop pulse only.  The old muzzle pass recolored every floor,
     * ceiling, wall, door and alt-wall depth palette on the first shot frame,
     * which made firing hitch exactly when the effect needed to be instant. */
    weapon_flash_timer = 3;
    muzzle_flash = 3;
}

static void update_weapon_flash(void) {
    if (palette_effect == 1) return;
    if (weapon_flash_timer) {
        if (!weapon_flash_on) {
            set_weapon_flash_palette();
            weapon_flash_on = 1;
        }
        weapon_flash_timer--;
    } else if (weapon_flash_on) {
        restore_weapon_palette();
        weapon_flash_on = 0;
        if (palette_effect == 4) power_palette_kind = 0;
    }
}

static EnemyDraw enemies[ENEMY_VISIBLE_COUNT];
static u8 player_line_of_sight_to(short x_q8, short y_q8);
static u8 projected_thing_is_hittable(int thing_index, int lateral_limit, int near_height);

static int enemy_visible_width_for_geometry(int screen_x, int screen_w) {
    int left, right;
    left = screen_x;
    right = screen_x + screen_w;
    if (left < 0) left = 0;
    if (right > SCRW) right = SCRW;
    return right > left ? right - left : 0;
}

static void update_enemy_slot_flags(u16 slot) {
    int center_x;
    int visible_w;
    u8 readable = 0;
    u8 attackable = 0;
    u8 ranged_attackable = 0;

    if (slot < ENEMY_VISIBLE_COUNT && enemies[slot].thing_index >= 0
        && enemies[slot].screen_w > 0 && enemies[slot].screen_h > 0) {
        visible_w = enemy_visible_width_for_geometry(enemies[slot].screen_x, enemies[slot].screen_w);
        center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
        if (visible_w >= 12 && enemies[slot].screen_x + enemies[slot].screen_w >= 16
            && enemies[slot].screen_x <= SCRW - 16 && center_x >= 24 && center_x <= SCRW - 24) {
            readable = 1;
            if (!enemies[slot].fallback_projection
                || projected_thing_is_hittable(enemies[slot].thing_index, 76, 112)
                || player_line_of_sight_to(thing_x_q8[enemies[slot].thing_index], thing_y_q8[enemies[slot].thing_index])) {
                attackable = 1;
                if (visible_w >= 24 && enemies[slot].screen_h >= 48 && center_x >= 48 && center_x <= SCRW - 48) {
                    ranged_attackable = 1;
                }
            }
        }
    }

    enemies[slot].readable = readable;
    enemies[slot].attackable = attackable;
    enemies[slot].ranged_attackable = ranged_attackable;
}

static u8 enemy_slot_is_readable(u16 slot) {
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    return enemies[slot].readable;
}

static u8 enemy_slot_can_attack(u16 slot) {
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    return enemies[slot].attackable;
}

static u8 enemy_slot_can_ranged_attack(u16 slot) {
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    return enemies[slot].ranged_attackable;
}

static u8 enemy_slot_is_monster(u16 slot) {
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    return enemies[slot].is_monster;
}

static u8 enemy_slot_is_shootable(u16 slot) {
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    return enemies[slot].shootable;
}

static u8 thing_has_readable_slot(int thing_index) {
    if (thing_index < 0) return 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemies[slot].thing_index == thing_index && enemy_slot_can_attack(slot)) return 1;
    }
    return 0;
}

static void reset_enemy_slot_cache(void);
static void hide_enemy_slot(u16 slot);
static void hide_enemies(void);
static void map_cell(int mx, int my, u16 pal, u16 tile);
static u8 map_bit_get(const u8 *bits, u16 index);
static void map_bit_set(u8 *bits, u16 index);

static void draw_minimap_cell(int mx, int my);
static void draw_minimap_source_cell(int map_x, int map_y);
static void redraw_minimap_thing_cell(int thing_index);
static void set_runtime_thing_position(int thing_index, short x_q8, short y_q8);
static void close_minimap_for_terminal_message(void);
static void start_minimap_redraw(void);
static void start_minimap_clear(void);
static void update_minimap_clear(void);
static void rebuild_monster_path(void);

static int iabs16(int value) {
    return value < 0 ? -value : value;
}

#define WORLD_Q8(value) ((value) * MAP_RENDER_SCALE)
#define MONSTER_SEPARATION_Q8 32
#define MONSTER_PATH_REBUILD_TICKS 3

static void explode_barrel_at(int thing_index, short x_q8, short y_q8);
static void player_take_damage(u16 amount);
static void spawn_impact_effect(short x_q8, short y_q8, u8 timer);
static u8 key_bit_for_thing(u16 thing_type);
static u8 thing_render_class(u16 thing_type);
static u16 runtime_thing_type(int thing_index);
static u8 runtime_thing_is_monster(int thing_index);
static u8 runtime_thing_is_pickup(int thing_index);
static u8 runtime_thing_is_threat(int thing_index);
static u8 runtime_thing_is_shootable(int thing_index);

static u8 line_of_sight_q8(short ax, short ay, short bx, short by) {
    int dx = bx - ax;
    int dy = by - ay;
    int adx;
    int ady;
    int steps;
    int x_acc;
    int y_acc;
    int x_step;
    int y_step;
    if ((ax >> 8) == (bx >> 8) && (ay >> 8) == (by >> 8)) return 1;
    adx = iabs16(dx);
    ady = iabs16(dy);
    steps = adx > ady ? adx : ady;
    if (steps <= 0) return 1;
    steps >>= 6;
    if (steps < 1) steps = 1;
    if (steps > 40) steps = 40;

    x_acc = ((int)ax) << 8;
    y_acc = ((int)ay) << 8;
    x_step = (dx << 8) / steps;
    y_step = (dy << 8) / steps;
    for (int i = 1; i < steps; i++) {
        x_acc += x_step;
        y_acc += y_step;
        if (map_at(x_acc >> 16, y_acc >> 16)) return 0;
    }
    return 1;
}

static u8 player_line_of_sight_to(short x_q8, short y_q8) {
    int px, py;
    rc_player_q8(&px, &py);
    return line_of_sight_q8((short)px, (short)py, x_q8, y_q8);
}

static u8 project_point_from_view_q8(short world_x_q8, short world_y_q8, int px, int py,
                                     int dir_x, int dir_y, int plane_x, int plane_y,
                                     int *screen_x, int *height, int *dist_q8) {
    long sprite_x;
    long sprite_y;
    long det;
    long transform_x;
    long transform_y;
    sprite_x = (long)world_x_q8 - px;
    sprite_y = (long)world_y_q8 - py;
    det = (long)plane_x * dir_y - (long)dir_x * plane_y;
    if (det > -1 && det < 1) return 0;
    transform_x = (((long)dir_y * sprite_x - (long)dir_x * sprite_y) << 8) / det;
    transform_y = ((-(long)plane_y * sprite_x + (long)plane_x * sprite_y) << 8) / det;
    if (transform_y < 32) return 0;
    *screen_x = (SCRW / 2) + (int)(((long)(SCRW / 2) * transform_x) / transform_y);
    *height = (int)(((long)GAME_H * MAP_RENDER_SCALE * 256) / transform_y);
    if (*height < 1) return 0;
    if (*height > GAME_H) *height = GAME_H;
    *dist_q8 = (int)transform_y;
    return 1;
}

static u8 project_point_q8(short world_x_q8, short world_y_q8, int *screen_x, int *height, int *dist_q8) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    return project_point_from_view_q8(world_x_q8, world_y_q8, px, py, dir_x, dir_y, plane_x, plane_y,
                                      screen_x, height, dist_q8);
}

static u8 projected_thing_is_hittable(int thing_index, int lateral_limit, int near_height) {
    int sx;
    int h;
    int dist_q8;
    int lateral;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (!project_point_q8(thing_x_q8[thing_index], thing_y_q8[thing_index], &sx, &h, &dist_q8)) return 0;
    lateral = iabs16(sx - SCRW / 2);
    if (lateral > lateral_limit && h < near_height) return 0;
    return rc_sprite_strip_visible(sx - 12, sx + 12, dist_q8);
}

static u8 game_active(void) {
    return player_health != 0 && !level_complete;
}

static void index_monster_candidate(u16 thing_index) {
    for (u16 i = 0; i < thing_monster_count; i++) {
        if (thing_monster_indices[i] == thing_index) return;
    }
    if (thing_monster_count < NG_RUNTIME_THING_COUNT) {
        thing_monster_indices[thing_monster_count++] = thing_index;
    }
}

static void index_shootable_candidate(u16 thing_index) {
    for (u16 i = 0; i < thing_shootable_count; i++) {
        if (thing_shootable_indices[i] == thing_index) return;
    }
    if (thing_shootable_count < NG_RUNTIME_THING_COUNT) {
        thing_shootable_indices[thing_shootable_count++] = thing_index;
    }
}

static void index_render_candidate(u16 thing_index) {
    for (u16 i = 0; i < thing_render_count; i++) {
        if (thing_render_indices[i] == thing_index) return;
    }
    if (thing_render_count < NG_RUNTIME_THING_COUNT) {
        thing_render_indices[thing_render_count++] = thing_index;
    }
}

static void index_pickup_candidate(u16 thing_index) {
    for (u16 i = 0; i < thing_pickup_count; i++) {
        if (thing_pickup_indices[i] == thing_index) return;
    }
    if (thing_pickup_count < NG_RUNTIME_THING_COUNT) {
        thing_pickup_indices[thing_pickup_count++] = thing_index;
    }
}

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
static int active_chunk_origin_x_q8(void) {
    return (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * NG_CHUNK_STREAM_PAGE_W_Q8;
}

static int active_chunk_origin_y_q8(void) {
    return (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * NG_CHUNK_STREAM_PAGE_H_Q8;
}

static void persist_runtime_slot_to_chunk_state(u16 slot) {
    unsigned short chunk_index;
    u16 type;
    if (slot >= NG_RUNTIME_THING_COUNT) return;
    chunk_index = thing_chunk_index[slot];
    if (chunk_index == 0xFFFF || chunk_index >= DOOM_CHUNK_THING_COUNT) return;
    type = runtime_thing_type(slot);
    chunk_thing_state_type[chunk_index] = type;
    chunk_thing_state_x_q8[chunk_index] = (short)(thing_x_q8[slot] - thing_chunk_offset_x_q8[slot]);
    chunk_thing_state_y_q8[chunk_index] = (short)(thing_y_q8[slot] - thing_chunk_offset_y_q8[slot]);
    chunk_thing_state_dead[chunk_index] = (u8)(enemy_dead[slot] || type == 0);
    chunk_thing_state_hp[chunk_index] = enemy_hp[slot];
}

static void clear_chunk_dynamic_drop_state(void) {
    for (u16 chunk = 0; chunk < DOOM_CHUNK_COUNT; chunk++) {
        for (u8 slot = 0; slot < CHUNK_DYNAMIC_DROP_SLOTS; slot++) {
            chunk_drop_active[chunk][slot] = 0;
            chunk_drop_type[chunk][slot] = 0;
            chunk_drop_x_q8[chunk][slot] = 0;
            chunk_drop_y_q8[chunk][slot] = 0;
        }
    }
}

static void init_chunk_thing_state(void) {
    for (u16 i = 0; i < DOOM_CHUNK_THING_COUNT; i++) {
        chunk_thing_state_type[i] = g_chunk_things[i].type;
        chunk_thing_state_x_q8[i] = g_chunk_things[i].x_q8;
        chunk_thing_state_y_q8[i] = g_chunk_things[i].y_q8;
        chunk_thing_state_dead[i] = 0;
        chunk_thing_state_hp[i] = 0;
    }
    clear_chunk_dynamic_drop_state();
    g_simple_active_chunk = DOOM_CHUNK_START_CHUNK;
}

static void save_active_chunk_dynamic_drops(void) {
    unsigned short chunk = SIMPLE_ACTIVE_CHUNK;
    for (u8 slot = 0; slot < CHUNK_DYNAMIC_DROP_SLOTS; slot++) {
        chunk_drop_active[chunk][slot] = dynamic_drop_active[slot];
        chunk_drop_type[chunk][slot] = dynamic_drop_type[slot];
        chunk_drop_x_q8[chunk][slot] = dynamic_drop_x_q8[slot];
        chunk_drop_y_q8[chunk][slot] = dynamic_drop_y_q8[slot];
    }
}

static void save_chunk_drop(unsigned short chunk, u16 thing_type, short local_x_q8, short local_y_q8) {
    u8 slot = 0;
    if (!thing_type) return;
    if (chunk >= DOOM_CHUNK_COUNT) return;
    for (u8 i = 0; i < CHUNK_DYNAMIC_DROP_SLOTS; i++) {
        if (!chunk_drop_active[chunk][i]) {
            slot = i;
            break;
        }
    }
    chunk_drop_active[chunk][slot] = 1;
    chunk_drop_type[chunk][slot] = thing_type;
    chunk_drop_x_q8[chunk][slot] = local_x_q8;
    chunk_drop_y_q8[chunk][slot] = local_y_q8;
}

static void save_active_chunk_drop(u16 thing_type, short local_x_q8, short local_y_q8) {
    save_chunk_drop(SIMPLE_ACTIVE_CHUNK, thing_type, local_x_q8, local_y_q8);
}

static void save_active_chunk_runtime_things(void) {
    save_active_chunk_dynamic_drops();
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        unsigned short chunk_index = thing_chunk_index[i];
        u16 type;
        if (chunk_index == 0xFFFF || chunk_index >= DOOM_CHUNK_THING_COUNT) continue;
        type = runtime_thing_type(i);
        if (death_anim_final_type[i]) {
            if (death_anim_drop_type[i]) {
                save_chunk_drop(
                    g_chunk_things[chunk_index].chunk,
                    death_anim_drop_type[i],
                    (short)(thing_x_q8[i] - thing_chunk_offset_x_q8[i]),
                    (short)(thing_y_q8[i] - thing_chunk_offset_y_q8[i])
                );
            }
            type = death_anim_final_type[i];
        } else if (death_drop_type[i]) {
            type = death_drop_type[i];
        }
        if (enemy_dead[i] || type == 0) {
            chunk_thing_state_type[chunk_index] = 0;
            chunk_thing_state_dead[chunk_index] = 1;
            chunk_thing_state_hp[chunk_index] = 0;
            continue;
        }
        chunk_thing_state_type[chunk_index] = type;
        chunk_thing_state_x_q8[chunk_index] = (short)(thing_x_q8[i] - thing_chunk_offset_x_q8[i]);
        chunk_thing_state_y_q8[chunk_index] = (short)(thing_y_q8[i] - thing_chunk_offset_y_q8[i]);
        chunk_thing_state_dead[chunk_index] = 0;
        chunk_thing_state_hp[chunk_index] = enemy_hp[i];
    }
}

static void clear_dynamic_drops(void) {
    for (u8 i = 0; i < 8; i++) {
        dynamic_drop_active[i] = 0;
        dynamic_drop_type[i] = 0;
        dynamic_drop_x_q8[i] = 0;
        dynamic_drop_y_q8[i] = 0;
    }
}

static void load_active_chunk_dynamic_drops(void) {
    unsigned short chunk = SIMPLE_ACTIVE_CHUNK;
    clear_dynamic_drops();
    for (u8 slot = 0; slot < CHUNK_DYNAMIC_DROP_SLOTS; slot++) {
        short local_x;
        short local_y;
        if (!chunk_drop_active[chunk][slot]) continue;
        local_x = chunk_drop_x_q8[chunk][slot];
        local_y = chunk_drop_y_q8[chunk][slot];
        if (local_x < 0 || local_y < 0 || local_x >= NG_CHUNK_STREAM_PAGE_W_Q8 || local_y >= NG_CHUNK_STREAM_PAGE_H_Q8) continue;
        dynamic_drop_active[slot] = 1;
        dynamic_drop_type[slot] = chunk_drop_type[chunk][slot];
        dynamic_drop_x_q8[slot] = local_x;
        dynamic_drop_y_q8[slot] = local_y;
    }
}

static void reset_chunk_runtime_slot(u16 i) {
    enemy_dead[i] = 1;
    enemy_hp[i] = 0;
    enemy_hit_flash[i] = 0;
    enemy_awake[i] = 0;
    enemy_attack_cooldown[i] = 0;
    enemy_attack_anim[i] = 0;
    enemy_ranged_readable_ticks[i] = 0;
    enemy_hidden_timer[i] = 0;
    monster_face_x[i] = 0;
    monster_face_y[i] = 1;
    explosion_timer[i] = 0;
    death_anim_timer[i] = 0;
    death_drop_timer[i] = 0;
    thing_type_override[i] = 0;
    death_anim_final_type[i] = 0;
    death_anim_drop_type[i] = 0;
    death_drop_type[i] = 0;
    thing_chunk_offset_x_q8[i] = 0;
    thing_chunk_offset_y_q8[i] = 0;
}

static u8 load_chunk_runtime_slot(
    u16 slot,
    unsigned short chunk_index,
    short offset_x_q8,
    short offset_y_q8,
    u8 *thing_class,
    u8 *thing_info
) {
    u16 type;
    short x_q8;
    short y_q8;

    if (slot >= NG_RUNTIME_THING_COUNT) return 0;
    if (chunk_index >= DOOM_CHUNK_THING_COUNT) return 0;

    type = chunk_thing_state_type[chunk_index];
    if (chunk_thing_state_dead[chunk_index] || type == 0) return 0;

    x_q8 = (short)(chunk_thing_state_x_q8[chunk_index] + offset_x_q8);
    y_q8 = (short)(chunk_thing_state_y_q8[chunk_index] + offset_y_q8);

    thing_chunk_index[slot] = chunk_index;
    thing_chunk_offset_x_q8[slot] = offset_x_q8;
    thing_chunk_offset_y_q8[slot] = offset_y_q8;
    thing_x_q8[slot] = x_q8;
    thing_y_q8[slot] = y_q8;
    thing_type_override[slot] = type;

    *thing_class = thing_render_class(type);
    *thing_info = 0;
    if (*thing_class != THING_CLASS_NONE) *thing_info |= NG_THING_INFO_RENDER;
    if (*thing_class == THING_CLASS_MONSTER) *thing_info |= NG_THING_INFO_MONSTER | NG_THING_INFO_SHOOTABLE;
    if (*thing_class == THING_CLASS_THREAT) *thing_info |= NG_THING_INFO_THREAT | NG_THING_INFO_SHOOTABLE;
    if (*thing_class == THING_CLASS_PICKUP) *thing_info |= NG_THING_INFO_PICKUP;
    enemy_dead[slot] = 0;
    enemy_hp[slot] = chunk_thing_state_hp[chunk_index];
    return 1;
}
#endif

static void init_runtime_things(void) {
    thing_monster_count = 0;
    thing_shootable_count = 0;
    thing_render_count = 0;
    thing_pickup_count = 0;
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        reset_chunk_runtime_slot(i);
        thing_chunk_index[i] = 0xFFFF;
        thing_x_q8[i] = 0;
        thing_y_q8[i] = 0;
        thing_type_override[i] = 0;
        thing_static_class[i] = THING_CLASS_NONE;
    }
    {
        int active_chunk_x = SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS;
        int active_chunk_y = SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS;
        u16 slot = 0;
        for (u8 radius = 0; radius <= 1 && slot < NG_RUNTIME_THING_COUNT; radius++) {
            for (signed char dy = -(signed char)radius; dy <= (signed char)radius && slot < NG_RUNTIME_THING_COUNT; dy++) {
                for (signed char dx = -(signed char)radius; dx <= (signed char)radius && slot < NG_RUNTIME_THING_COUNT; dx++) {
                    int chunk_x;
                    int chunk_y;
                    unsigned short chunk;
                    unsigned short chunk_first;
                    unsigned char chunk_count;
                    if (radius && iabs16(dx) < radius && iabs16(dy) < radius) continue;
                    chunk_x = active_chunk_x + dx;
                    chunk_y = active_chunk_y + dy;
                    if (chunk_x < 0 || chunk_y < 0 || chunk_x >= DOOM_CHUNK_COLS || chunk_y >= DOOM_CHUNK_ROWS) continue;
                    chunk = (unsigned short)(chunk_y * DOOM_CHUNK_COLS + chunk_x);
                    chunk_first = g_chunk_thing_first[chunk];
                    chunk_count = g_chunk_thing_count[chunk];
                    for (u8 n = 0; n < chunk_count && slot < NG_RUNTIME_THING_COUNT; n++) {
                        u8 thing_class;
                        u8 thing_info;
                        unsigned short chunk_index = (unsigned short)(chunk_first + n);
                        short offset_x_q8 = (short)(dx * NG_CHUNK_STREAM_PAGE_W_Q8);
                        short offset_y_q8 = (short)(dy * NG_CHUNK_STREAM_PAGE_H_Q8);
                        if (!load_chunk_runtime_slot(slot, chunk_index, offset_x_q8, offset_y_q8, &thing_class, &thing_info)) continue;
                        thing_static_class[slot] = thing_class;
                        if (thing_info & NG_THING_INFO_RENDER) index_render_candidate(slot);
                        if (thing_info & NG_THING_INFO_MONSTER) index_monster_candidate(slot);
                        if (thing_info & NG_THING_INFO_SHOOTABLE) index_shootable_candidate(slot);
                        if (thing_info & NG_THING_INFO_PICKUP) index_pickup_candidate(slot);
                        slot++;
                    }
                }
            }
        }
    }
#else
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        u8 thing_class;
        u8 thing_info;
        thing_x_q8[i] = g_runtime_things[i].x_q8;
        thing_y_q8[i] = g_runtime_things[i].y_q8;
#if DOOM_SIMPLE_MAP
        thing_class = thing_render_class(g_runtime_things[i].type);
        thing_info = 0;
        if (thing_class != THING_CLASS_NONE) thing_info |= NG_THING_INFO_RENDER;
        if (thing_class == THING_CLASS_MONSTER) thing_info |= NG_THING_INFO_MONSTER | NG_THING_INFO_SHOOTABLE;
        if (thing_class == THING_CLASS_THREAT) thing_info |= NG_THING_INFO_THREAT | NG_THING_INFO_SHOOTABLE;
        if (thing_class == THING_CLASS_PICKUP) thing_info |= NG_THING_INFO_PICKUP;
#elif NG_RUNTIME_THING_INFO_GENERATED
        thing_class = g_runtime_thing_class[i];
        thing_info = g_runtime_thing_info[i];
#else
        thing_class = thing_render_class(g_runtime_things[i].type);
        thing_info = 0;
        if (thing_class != THING_CLASS_NONE) thing_info |= NG_THING_INFO_RENDER;
        if (thing_class == THING_CLASS_MONSTER) thing_info |= NG_THING_INFO_MONSTER | NG_THING_INFO_SHOOTABLE;
        if (thing_class == THING_CLASS_THREAT) thing_info |= NG_THING_INFO_THREAT | NG_THING_INFO_SHOOTABLE;
        if (thing_class == THING_CLASS_PICKUP) thing_info |= NG_THING_INFO_PICKUP;
#endif
        thing_static_class[i] = thing_class;
        if (thing_info & NG_THING_INFO_RENDER) index_render_candidate((u16)i);
        if (thing_info & NG_THING_INFO_MONSTER) {
            index_monster_candidate((u16)i);
        }
        if (thing_info & NG_THING_INFO_SHOOTABLE) {
            index_shootable_candidate((u16)i);
        }
        if (thing_info & NG_THING_INFO_PICKUP) {
            index_pickup_candidate((u16)i);
        }
    }
#endif
}

static u16 runtime_thing_type(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    if (thing_chunk_index[thing_index] == 0xFFFF && !thing_type_override[thing_index]) return 0;
#endif
    return thing_type_override[thing_index] ? thing_type_override[thing_index] : g_runtime_things[thing_index].type;
}

static u8 thing_is_monster(u16 thing_type) {
    switch (thing_type) {
    case 7:
    case 9:
    case 16:
    case 58:
    case 64:
    case 65:
    case 66:
    case 67:
    case 68:
    case 69:
    case 71:
    case 84:
    case 3001:
    case 3002:
    case 3003:
    case 3004:
    case 3005:
    case 3006:
        return 1;
    default:
        return 0;
    }
}

static u8 thing_is_pickup(u16 thing_type) {
    switch (thing_type) {
    case 5:
    case 6:
    case 8:
    case 13:
    case 38:
    case 39:
    case 40:
    case 17:
    case 2007:
    case 2001:
    case 2002:
    case 2003:
    case 2004:
    case 2005:
    case 2006:
    case 2008:
    case 2010:
    case 2011:
    case 2012:
    case 2013:
    case 2014:
    case 2015:
    case 2018:
    case 2019:
    case 2022:
    case 2023:
    case 2024:
    case 2025:
    case 2026:
    case 2045:
    case 2046:
    case 2047:
    case 2048:
    case 2049:
        return 1;
    default:
        return 0;
    }
}

static u8 pickup_is_collectible(u16 thing_type) {
    u8 key = key_bit_for_thing(thing_type);
    if (key) return (player_keys & key) == 0;

    switch (thing_type) {
    case 2001: /* shotgun */
        return !player_has_shotgun || player_shells < player_max_shells;
    case 2002: /* chaingun */
        return !player_has_chaingun || player_ammo < player_max_bullets;
    case 2003: /* rocket launcher */
        return !player_has_rocket_launcher || player_rockets < player_max_rockets;
    case 2004: /* plasma rifle */
        if (!weapon_asset_available(WEAPON_PLASMA)) return player_cells < player_max_cells;
        return !player_has_plasma || player_cells < player_max_cells;
    case 2005: /* chainsaw */
        return !player_has_chainsaw;
    case 2006: /* BFG 9000 */
        if (!weapon_asset_available(WEAPON_BFG)) return player_cells < player_max_cells;
        return !player_has_bfg || player_cells < player_max_cells;
    case 8:    /* backpack */
        return !player_has_backpack || player_ammo < player_max_bullets || player_shells < player_max_shells || player_rockets < player_max_rockets || player_cells < player_max_cells;
    case 2007: /* clip */
    case 2048: /* ammo box */
        return player_ammo < player_max_bullets;
    case 2008: /* shells */
    case 2049: /* box of shells */
        return player_shells < player_max_shells;
    case 2010: /* rocket */
    case 2046: /* box of rockets */
        return player_rockets < player_max_rockets;
    case 17:   /* cell pack */
    case 2047: /* cell charge */
        return player_cells < player_max_cells;
    case 2011: /* stimpack */
    case 2012: /* medikit */
        return player_health < 100;
    case 2013: /* supercharge */
    case 2014: /* health bonus */
        return player_health < 200;
    case 2015: /* armor bonus */
        return player_armor < 200;
    case 2018: /* green armor */
        return player_armor < 100 || player_armor_class == 0;
    case 2019: /* blue armor */
        return player_armor < 200 || player_armor_class < 2;
    case 2022: /* invulnerability */
        return power_invuln_timer < 1050;
    case 2023: /* berserk */
        return !player_berserk || player_health < 100;
    case 2024: /* partial invisibility */
        return power_invis_timer < 1050;
    case 2025: /* radiation suit */
        return power_radsuit_timer < 1050;
    case 2026: /* computer area map */
        return !power_computer_map;
    case 2045: /* light amplification visor */
        return power_lightamp_timer < 1050;
    default:
        return 0;
    }
}

static void compute_level_totals(void) {
    level_total_kills = 0;
    level_total_items = 0;
    level_total_secrets = 0;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
#if DOOM_SIMPLE_MAP
        u16 type;
        if (enemy_dead[i]) continue;
        type = runtime_thing_type(i);
#else
        u16 type = g_runtime_things[i].type;
#endif
        if (thing_is_monster(type)) {
            if (level_total_kills < 999) level_total_kills++;
        } else if (thing_is_pickup(type)) {
            if (level_total_items < 999) level_total_items++;
        }
    }
    for (u16 y = 0; y < ACTIVE_MAP_H; y++) {
        for (u16 x = 0; x < ACTIVE_MAP_W; x++) {
            if (map_cell_secret(x, y) && level_total_secrets < 999) level_total_secrets++;
        }
    }
}

static u16 completion_percent(u16 value, u16 total) {
    unsigned long pct;
    if (!total) return 100;
    pct = ((unsigned long)value * 100UL + (total / 2)) / total;
    return pct > 100 ? 100 : (u16)pct;
}

static u8 thing_is_barrel(u16 thing_type) {
    return thing_type == 2035;
}

static u8 thing_is_explosion(u16 thing_type) {
    return thing_type == 9000;
}

static u8 thing_is_projectile(u16 thing_type) {
    return thing_type == 9006 || thing_type == 9007 || thing_type == 9008;
}

static u8 thing_is_runtime_threat(u16 thing_type) {
    return thing_is_monster(thing_type) || thing_is_barrel(thing_type) || thing_is_explosion(thing_type);
}

static int runtime_threat_priority_bias(u16 thing_type) {
    if (thing_is_monster(thing_type)) return -1024;
    if (thing_is_barrel(thing_type)) return -256;
    return 0;
}

static u8 thing_is_corpse(u16 thing_type) {
    return (thing_type >= 9001 && thing_type <= 9005) || thing_type == 9009
        || (thing_type >= 9010 && thing_type <= 9036);
}

static u8 thing_is_shootable(u16 thing_type) {
    return thing_is_monster(thing_type) || thing_is_barrel(thing_type);
}

static u8 player_has_weapon(u8 weapon) {
    if (!weapon_asset_available(weapon)) return 0;
    switch (weapon) {
    case WEAPON_PISTOL:
        return 1;
    case WEAPON_SHOTGUN:
        return player_has_shotgun;
    case WEAPON_CHAINGUN:
        return player_has_chaingun;
    case WEAPON_ROCKET:
        return player_has_rocket_launcher;
    case WEAPON_PLASMA:
        return player_has_plasma;
    case WEAPON_BFG:
        return player_has_bfg;
    case WEAPON_FIST:
        return 1;
    case WEAPON_CHAINSAW:
        return player_has_chainsaw;
    default:
        return 0;
    }
}

static u8 weapon_has_ammo(u8 weapon) {
    if (!weapon_asset_available(weapon)) return 0;
    switch (weapon) {
    case WEAPON_PISTOL:
        return player_ammo > 0;
    case WEAPON_SHOTGUN:
        return player_has_shotgun && player_shells > 0;
    case WEAPON_CHAINGUN:
        return player_has_chaingun && player_ammo > 0;
    case WEAPON_ROCKET:
        return player_has_rocket_launcher && player_rockets > 0;
    case WEAPON_PLASMA:
        return player_has_plasma && player_cells > 0;
    case WEAPON_BFG:
        return player_has_bfg && player_cells >= 40;
    case WEAPON_FIST:
        return 1;
    case WEAPON_CHAINSAW:
        return player_has_chainsaw;
    default:
        return 0;
    }
}

static u8 switch_to_ready_weapon(void) {
    static const u8 fallback_order[] = {WEAPON_SHOTGUN, WEAPON_CHAINGUN, WEAPON_PLASMA, WEAPON_ROCKET, WEAPON_PISTOL, WEAPON_BFG, WEAPON_CHAINSAW, WEAPON_FIST};
    if (weapon_has_ammo(current_weapon)) return 1;
    for (u16 i = 0; i < sizeof(fallback_order); i++) {
        u8 weapon = fallback_order[i];
        if (weapon_has_ammo(weapon)) {
            current_weapon = weapon;
            weapon_frame = 0xFF;
            shown_ammo = 0xFFFF;
            return 1;
        }
    }
    return 0;
}

static u8 weapon_slot_digit(u8 weapon) {
    switch (weapon) {
    case WEAPON_FIST:
    case WEAPON_CHAINSAW:
        return 1;
    case WEAPON_PISTOL:
        return 2;
    case WEAPON_SHOTGUN:
        return 3;
    case WEAPON_CHAINGUN:
        return 4;
    case WEAPON_ROCKET:
        return 5;
    case WEAPON_PLASMA:
        return 6;
    case WEAPON_BFG:
        return 7;
    default:
        return 0;
    }
}

static void trigger_weapon_message(void) {
    weapon_message_digit = weapon_slot_digit(current_weapon);
    weapon_message_timer = 24;
}

static u8 key_bit_for_thing(u16 thing_type) {
    switch (thing_type) {
    case 5:
    case 40:
        return 1; /* blue */
    case 13:
    case 38:
        return 2; /* red */
    case 6:
    case 39:
        return 4; /* yellow */
    default:
        return 0;
    }
}

static u8 key_bit_for_door(u16 special) {
    switch (special) {
    case 26:
    case 32:
        return 1; /* blue */
    case 28:
    case 33:
        return 2; /* red */
    case 27:
    case 34:
        return 4; /* yellow */
    default:
        return 0;
    }
}

static u8 monster_start_hp(u16 thing_type) {
    switch (thing_type) {
    case 84:   /* Wolfenstein SS */
        return 5;
    case 65:   /* heavy weapon dude */
        return 7;
    case 69:   /* hell knight */
        return 10;
    case 66:   /* revenant */
    case 68:   /* arachnotron */
        return 14;
    case 64:   /* arch-vile */
        return 18;
    case 67:   /* mancubus */
    case 71:   /* pain elemental */
        return 20;
    case 7:    /* spider mastermind */
        return 30;
    case 16:   /* cyberdemon */
        return 40;
    case 3004: /* former human */
        return 2;
    case 9:    /* shotgun guy */
        return 3;
    case 3001: /* imp */
        return 4;
    case 3002: /* demon */
    case 58:   /* spectre */
        return 8;
    case 3003: /* baron */
        return 20;
    case 3005: /* cacodemon */
        return 12;
    case 3006: /* lost soul */
        return 5;
    default:
        return 5;
    }
}

static u16 monster_drop_type(u16 thing_type) {
    switch (thing_type) {
    case 3004: /* former human */
        return 2007; /* clip */
    case 9:    /* shotgun guy */
        return 2001; /* shotgun */
    case 65:   /* heavy weapon dude */
        return 2002; /* chaingun */
    default:
        return 0;
    }
}

static u16 monster_corpse_type(u16 thing_type) {
    switch (thing_type) {
    case 3004:
        return 9001; /* former human corpse */
    case 9:
        return 9002; /* shotgun guy corpse */
    case 3001:
        return 9003; /* imp corpse */
    case 3002:
    case 58:
        return 9004; /* demon/spectre corpse */
    case 3003:
        return 9005; /* baron corpse */
    case 69:
        return 9009; /* hell knight corpse */
    case 3005:
        return 9028; /* cacodemon corpse */
    default:
        return 0;
    }
}

static u16 monster_death_anim_type(u16 thing_type) {
    switch (thing_type) {
    case 3004:
        return 9010; /* former human death sequence */
    case 9:
        return 9011; /* shotgun guy death sequence */
    case 3001:
        return 9012; /* imp death sequence */
    case 3002:
    case 58:
        return 9013; /* demon/spectre death sequence */
    case 3003:
        return 9014; /* baron death sequence */
    case 69:
        return 9025; /* hell knight death sequence */
    case 3005:
        return 9029; /* cacodemon death sequence */
    default:
        return 0;
    }
}

static u16 death_anim_next_stage_type(u16 thing_type) {
    if (thing_type >= 9010 && thing_type <= 9019) return (u16)(thing_type + 5);
    if (thing_type >= 9020 && thing_type <= 9024) return (u16)(9032 + (thing_type - 9020));
    if (thing_type == 9025) return 9026;
    if (thing_type == 9026) return 9027;
    if (thing_type == 9029) return 9030;
    if (thing_type == 9030) return 9031;
    return 0;
}

static u16 monster_score_value(u16 thing_type) {
    switch (thing_type) {
    case 3004: /* former human */
        return 100;
    case 9:    /* shotgun guy */
        return 150;
    case 65:   /* heavy weapon dude */
        return 180;
    case 84:   /* Wolfenstein SS */
        return 200;
    case 3001: /* imp */
        return 200;
    case 3002: /* demon */
    case 58:   /* spectre */
        return 400;
    case 3003: /* baron */
        return 1000;
    case 69:   /* hell knight */
        return 500;
    case 3005: /* cacodemon */
        return 700;
    case 3006: /* lost soul */
        return 100;
    case 66:   /* revenant */
    case 67:   /* mancubus */
    case 68:   /* arachnotron */
        return 700;
    case 64:   /* arch-vile */
    case 71:   /* pain elemental */
        return 800;
    case 16:   /* cyberdemon */
        return 2000;
    case 7:    /* spider mastermind */
        return 2500;
    default:
        return 100;
    }
}

static u8 monster_hp(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (enemy_hp[thing_index] == 0) {
        enemy_hp[thing_index] = monster_start_hp(runtime_thing_type(thing_index));
    }
    return enemy_hp[thing_index];
}

static void invalidate_background_cache(void) {
    bg_scroll_key = 0xFFFFFFFFUL;
    bg_pending_key = 0xFFFFFFFFUL;
    bg_update_col = 0;
    bg_direction_dir_x = 0x7FFFFFFF;
    bg_direction_dir_y = 0x7FFFFFFF;
    bg_direction_bucket = 0;
    for (u16 i = 0; i < BG_COUNT; i++) {
        bg_col_key[i] = 0xFFFFFFFFUL;
        bg_col_hidden[i] = 0xFF;
    }
}

static void spawn_dynamic_drop(u16 thing_type, short x_q8, short y_q8) {
    u8 slot = 0;
    if (!thing_type) return;
    for (u8 i = 0; i < 8; i++) {
        if (!dynamic_drop_active[i]) {
            slot = i;
            break;
        }
    }
    dynamic_drop_active[slot] = 1;
    dynamic_drop_type[slot] = thing_type;
    dynamic_drop_x_q8[slot] = x_q8;
    dynamic_drop_y_q8[slot] = y_q8;
    invalidate_background_cache();
    hide_enemies();
}

static int first_sprite_def_for_type(u16 thing_type) {
    int first = -1;
    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type != thing_type) continue;
        if (first < 0) first = i;
        if (g_enemy_sprite_defs[i].angle == 0) return i;
    }
    return first;
}

static u16 fallback_sprite_type_for_missing_pickup(u16 thing_type) {
    switch (thing_type) {
    case 17:   /* cell pack */
    case 2047: /* cell charge */
        return 2048; /* ammo box */
    case 2004: /* plasma rifle */
    case 2006: /* BFG 9000 */
        return 2003; /* rocket launcher pickup */
    case 38:   /* red skull */
        return 13;
    case 39:   /* yellow skull */
        return 6;
    case 40:   /* blue skull */
        return 5;
    case 2049: /* box of shells */
        return 2008; /* shells */
    case 2022: /* invulnerability */
    case 2024: /* partial invisibility */
    case 2045: /* light amplification visor */
        return 2013; /* soulsphere */
    case 2023: /* berserk */
        return 2012; /* medikit */
    case 2025: /* radiation suit */
        return 2018; /* green armor */
    case 2026: /* computer area map */
        return 2048; /* ammo box */
    default:
        return 0;
    }
}

static u8 monster_view_angle_bucket(int thing_index, int px, int py) {
    int to_x;
    int to_y;
    int dot;
    int cross;
    int abs_cross;
    int face_x;
    int face_y;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 1;
    to_x = px - thing_x_q8[thing_index];
    to_y = py - thing_y_q8[thing_index];
    face_x = monster_face_x[thing_index];
    face_y = monster_face_y[thing_index];
    if (face_x == 0 && face_y == 0) {
        face_x = (to_x > 0) ? 1 : (to_x < 0 ? -1 : 0);
        face_y = (to_y > 0) ? 1 : (to_y < 0 ? -1 : 0);
    }
    dot = face_x * to_x + face_y * to_y;
    cross = face_x * to_y - face_y * to_x;
    abs_cross = iabs16(cross);
    if (dot > abs_cross * 2) return 1;
    if (dot > 0) return cross >= 0 ? 2 : 8;
    if (-dot < abs_cross) return cross >= 0 ? 3 : 7;
    if (dot < -(abs_cross * 2)) return 5;
    return cross >= 0 ? 4 : 6;
}

static int enemy_sprite_def_for_type(u16 thing_type, int thing_index, int view_px, int view_py) {
    int first = -1;
    int first_walk_angle = -1;
    u8 is_monster = thing_is_monster(thing_type);
    u8 walk_angle = is_monster ? monster_view_angle_bucket(thing_index, view_px, view_py) : 0;
    u8 wanted_angle = walk_angle;
    u8 fallback_angle = 0;
    u8 wanted_anim = is_monster ? (u8)((monster_ai_tick >> 3) & 1) : 0;
    if (is_monster && thing_index >= 0 && enemy_attack_anim[thing_index]) {
        wanted_angle = (u8)(100 + walk_angle);
        fallback_angle = 9;
    } else if (is_monster && thing_index >= 0 && enemy_hit_flash[thing_index]) {
        wanted_angle = (u8)(200 + walk_angle);
        fallback_angle = 10;
    }

    for (u8 pass = 0; pass < 3; pass++) {
        u8 target_angle = (pass == 0) ? wanted_angle : (pass == 1 ? fallback_angle : walk_angle);
        u8 angle_hits = 0;
        int first_angle = -1;
        if (pass == 1 && fallback_angle == 0) continue;
        if (pass == 2 && !is_monster) continue;
        for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
            if (g_enemy_sprite_defs[i].thing_type != thing_type) continue;
            if (first < 0) first = i;
            if (!is_monster) return first;
            if (g_enemy_sprite_defs[i].angle == target_angle || (target_angle == 0 && g_enemy_sprite_defs[i].angle == 0)) {
                if (first_angle < 0) first_angle = i;
                if (angle_hits == wanted_anim) return i;
                angle_hits++;
            }
        }
        if (first_angle >= 0) return first_angle;
    }

    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type != thing_type) continue;
        if (first < 0) first = i;
        if (!is_monster) return first;
        if (g_enemy_sprite_defs[i].angle == walk_angle && first_walk_angle < 0) first_walk_angle = i;
    }
    if (thing_is_pickup(thing_type)) {
        u16 fallback_type = fallback_sprite_type_for_missing_pickup(thing_type);
        if (fallback_type) {
            int fallback = first_sprite_def_for_type(fallback_type);
            if (fallback >= 0) return fallback;
        }
    }
    if (first_walk_angle >= 0) return first_walk_angle;
    return first >= 0 ? first : -1;
}

static void load_enemy_palette(u16 slot, int def) {
    u8 brighten_pickup;
    if (def == enemy_palette_def[slot]) return;
    brighten_pickup = thing_is_pickup(g_enemy_sprite_defs[def].thing_type);
    for (int i = 0; i < ENEMY_PALETTE_COLORS; i++) {
        u8 r = g_enemy_palette_rgb[def][i][0];
        u8 g = g_enemy_palette_rgb[def][i][1];
        u8 b = g_enemy_palette_rgb[def][i][2];
        if (brighten_pickup && (r || g || b)) {
            r = (u8)((r * 3) / 2 + 4);
            g = (u8)((g * 3) / 2 + 4);
            b = (u8)((b * 3) / 2 + 4);
            if (r > 31) r = 31;
            if (g > 31) g = 31;
            if (b > 31) b = 31;
        }
        pal_set((u16)(PAL_ENEMY_BASE + slot), (u16)(i + 1), RGB(r, g, b));
    }
    enemy_palette_def[slot] = def;
}

static void load_enemy_hit_palette(u16 slot) {
    for (int i = 1; i < 16; i++) {
        pal_set((u16)(PAL_ENEMY_BASE + slot), (u16)i, RGB(31, 31, 24));
    }
    enemy_palette_def[slot] = -1;
}

static void check_e1m8_boss_exit(void);

static u8 damage_enemy_at(int thing_index, u8 damage) {
    u8 killed = 0;
    u16 source_type;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    source_type = runtime_thing_type(thing_index);
    if (!thing_is_shootable(source_type)) return 0;
    if (enemy_dead[thing_index]) return 0;
    if (thing_is_barrel(source_type)) {
        thing_type_override[thing_index] = 9000;
        explosion_timer[thing_index] = 12;
        enemy_dead[thing_index] = 0;
        enemy_hp[thing_index] = 0;
        enemy_hit_flash[thing_index] = 0;
        enemy_awake[thing_index] = 0;
        enemy_attack_cooldown[thing_index] = 0;
        explode_barrel_at(thing_index, thing_x_q8[thing_index], thing_y_q8[thing_index]);
        redraw_minimap_thing_cell(thing_index);
        return 1;
    }
    {
        short x = thing_x_q8[thing_index];
        short y = thing_y_q8[thing_index];
        u16 drop_type = monster_drop_type(source_type);
        u16 corpse_type = monster_corpse_type(source_type);
        u16 death_type = monster_death_anim_type(source_type);
        u8 score_awarded = 0;
        u8 hp = monster_hp(thing_index);
        if (damage >= hp) hp = 0;
        else hp = (u8)(hp - damage);

        for (u16 mi = 0; mi < thing_monster_count; mi++) {
            int i = thing_monster_indices[mi];
            u16 type;
            if (thing_x_q8[i] != x || thing_y_q8[i] != y) continue;
            type = runtime_thing_type(i);
            if (thing_is_monster(type)) {
                enemy_hp[i] = hp;
                enemy_awake[i] = 1;
                if (hp == 0) {
                    u16 final_type = corpse_type ? corpse_type : drop_type;
                    u16 final_drop = (drop_type && corpse_type) ? drop_type : 0;
                    if (death_type && final_type) {
                        thing_type_override[i] = death_type;
                        death_anim_final_type[i] = final_type;
                        death_anim_drop_type[i] = final_drop;
                        death_anim_timer[i] = 18;
                        death_drop_type[i] = 0;
                        death_drop_timer[i] = 0;
                        enemy_dead[i] = 0;
                    } else if (drop_type) {
                        thing_type_override[i] = drop_type;
                        index_pickup_candidate((u16)i);
                        enemy_dead[i] = 0;
                    } else if (corpse_type) {
                        thing_type_override[i] = corpse_type;
                        enemy_dead[i] = 0;
                    } else {
                        enemy_dead[i] = 1;
                    }
                    enemy_hit_flash[i] = 0;
                    enemy_awake[i] = 0;
                    enemy_attack_cooldown[i] = 0;
                    redraw_minimap_thing_cell(i);
                    if (!score_awarded && player_score <= 999) {
                        u16 value = monster_score_value(source_type);
                        player_score = (u16)(player_score + value > 999 ? 999 : player_score + value);
                        if (player_kills < 999) player_kills++;
                        score_awarded = 1;
                    }
                    killed = 1;
                } else {
                    enemy_hit_flash[i] = 30;
                    enemy_attack_cooldown[i] = 24;
                }
            }
        }
    }
    if (killed) check_e1m8_boss_exit();
    return killed;
}

static void explode_barrel_at(int thing_index, short x_q8, short y_q8) {
    int px, py;
    rc_player_q8(&px, &py);
    if (iabs16(px - x_q8) + iabs16(py - y_q8) < WORLD_Q8(520)) player_take_damage(12);
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        int range = iabs16(thing_x_q8[i] - x_q8) + iabs16(thing_y_q8[i] - y_q8);
        u16 type;
        if (range >= WORLD_Q8(520)) continue;
        if (i == thing_index || enemy_dead[i]) continue;
        type = runtime_thing_type(i);
        if (!thing_is_shootable(type)) continue;
        damage_enemy_at(i, thing_is_barrel(type) ? 1 : 5);
    }
}

static void flash_visible_enemy(int thing_index) {
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemies[slot].thing_index == thing_index) {
            enemy_slot_flash[slot] = 30;
            load_enemy_hit_palette(slot);
            return;
        }
    }
}

static void damage_visible_enemy(int thing_index, u8 damage) {
    if (thing_index < 0) return;
    if (damage_enemy_at(thing_index, damage)) hide_enemies();
    else flash_visible_enemy(thing_index);
}

static int best_visible_enemy(void) {
    int best_thing = -1;
    int best_score = 9999;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        int center_x;
        int score;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        if (enemy_dead[thing]) continue;
        if (!enemy_slot_is_shootable(slot)) continue;
        center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
        if (iabs16(center_x - SCRW / 2) > 76 && enemies[slot].screen_h < 112) continue;
        score = iabs16(center_x - SCRW / 2) + (enemies[slot].dist_q8 >> 7) - (enemies[slot].screen_h >> 2);
        if (score < best_score) {
            best_score = score;
            best_thing = thing;
        }
    }
    if (best_thing >= 0) return best_thing;
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int thing = thing_shootable_indices[si];
        int sx;
        int h;
        int dist_q8;
        int lateral;
        int score;
        if (enemy_dead[thing]) continue;
        if (!runtime_thing_is_shootable(thing)) continue;
        if (!project_point_q8(thing_x_q8[thing], thing_y_q8[thing], &sx, &h, &dist_q8)) continue;
        lateral = iabs16(sx - SCRW / 2);
        if (lateral > 52 && h < 112) continue;
        if (!projected_thing_is_hittable(thing, 52, 112)
            && !player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) continue;
        score = lateral + (dist_q8 >> 7) - (h >> 2);
        if (score < best_score) {
            best_score = score;
            best_thing = thing;
        }
    }
    return best_thing;
}

static void forward_impact_effect_point(short *x, short *y) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    *x = (short)(px + ((dir_x * WORLD_Q8(640)) >> 8));
    *y = (short)(py + ((dir_y * WORLD_Q8(640)) >> 8));
}

static void spawn_weapon_impact_for_target(int target) {
    short x;
    short y;
    if (target >= 0) {
        x = thing_x_q8[target];
        y = thing_y_q8[target];
    } else {
        forward_impact_effect_point(&x, &y);
    }
    spawn_impact_effect(x, y, 10);
}

static void fire_single_target_damage(u8 damage) {
    int target = best_visible_enemy();
    spawn_weapon_impact_for_target(target);
    damage_visible_enemy(target, damage);
}

static void fire_melee_damage(u8 damage) {
    enum { PLAYER_MELEE_RANGE_Q8 = WORLD_Q8(320) };
    int best_thing = -1;
    int best_score = 9999;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        int center_x;
        int score;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        if (enemies[slot].dist_q8 > PLAYER_MELEE_RANGE_Q8) continue;
        if (enemy_dead[thing]) continue;
        if (!enemy_slot_is_shootable(slot)) continue;
        center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
        if (iabs16(center_x - SCRW / 2) > 80) continue;
        score = iabs16(center_x - SCRW / 2) + enemies[slot].dist_q8;
        if (score < best_score) {
            best_score = score;
            best_thing = thing;
        }
    }
    if (best_thing >= 0) {
        spawn_weapon_impact_for_target(best_thing);
        damage_visible_enemy(best_thing, damage);
    } else {
        spawn_weapon_impact_for_target(-1);
    }
}

static void damage_rocket_radius(short x, short y) {
    int px, py;
    rc_player_q8(&px, &py);
    if (iabs16(px - x) + iabs16(py - y) < WORLD_Q8(420)) player_take_damage(8);
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        int range = iabs16(thing_x_q8[i] - x) + iabs16(thing_y_q8[i] - y);
        u16 type;
        if (range >= WORLD_Q8(560)) continue;
        type = runtime_thing_type(i);
        if (enemy_dead[i] || !thing_is_shootable(type)) continue;
        damage_enemy_at(i, thing_is_barrel(type) ? 1 : 8);
    }
    hide_enemies();
}

static void rocket_forward_impact(short *x, short *y) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    int last_x, last_y;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    last_x = px;
    last_y = py;
    for (int step = WORLD_Q8(128); step <= WORLD_Q8(2304); step += WORLD_Q8(128)) {
        int tx = px + ((dir_x * step) >> 8);
        int ty = py + ((dir_y * step) >> 8);
        if (map_at(tx >> 8, ty >> 8)) {
            *x = (short)tx;
            *y = (short)ty;
            return;
        }
        last_x = tx;
        last_y = ty;
    }
    *x = (short)last_x;
    *y = (short)last_y;
}

static void damage_rocket_target(void) {
    int target = best_visible_enemy();
    short x;
    short y;
    if (target >= 0) {
        x = thing_x_q8[target];
        y = thing_y_q8[target];
    } else {
        rocket_forward_impact(&x, &y);
    }
    spawn_impact_effect(x, y, 8);
    damage_rocket_radius(x, y);
}

static void damage_shotgun_spread(void) {
    enum { SHOTGUN_TARGET_COUNT = 3 };
    int targets[SHOTGUN_TARGET_COUNT] = {-1, -1, -1};
    int scores[SHOTGUN_TARGET_COUNT] = {9999, 9999, 9999};

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        int lateral;
        int score;
        int insert_at;
        int center_x;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        if (enemy_dead[thing]) continue;
        if (!enemy_slot_is_shootable(slot)) continue;
        if (!projected_thing_is_hittable(thing, 54, 100)
            && !player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) continue;

        center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
        lateral = iabs16(center_x - SCRW / 2);
        if (lateral > 54 && enemies[slot].screen_h < 100) continue;
        score = lateral + (enemies[slot].dist_q8 >> 8);
        insert_at = SHOTGUN_TARGET_COUNT;
        for (u16 i = 0; i < SHOTGUN_TARGET_COUNT; i++) {
            if (score < scores[i]) {
                insert_at = i;
                break;
            }
        }
        if (insert_at >= SHOTGUN_TARGET_COUNT) continue;
        for (int i = SHOTGUN_TARGET_COUNT - 1; i > insert_at; i--) {
            targets[i] = targets[i - 1];
            scores[i] = scores[i - 1];
        }
        targets[insert_at] = thing;
        scores[insert_at] = score;
    }

    spawn_weapon_impact_for_target(targets[0]);
    damage_visible_enemy(targets[0], 5);
    damage_visible_enemy(targets[1], 2);
    damage_visible_enemy(targets[2], 1);
}

static void damage_bfg_targets(void) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    int primary = best_visible_enemy();
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        if (enemy_dead[thing]) continue;
        if (!enemy_slot_is_shootable(slot)) continue;
        damage_visible_enemy(thing, thing == primary ? 18 : 9);
    }
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int thing = thing_shootable_indices[si];
        int dx, dy, front, side, abs_side;
        dx = thing_x_q8[thing] - px;
        dy = thing_y_q8[thing] - py;
        front = ((dx * dir_x) + (dy * dir_y)) >> 8;
        if (front < WORLD_Q8(192) || front > WORLD_Q8(2816)) continue;
        if (enemy_dead[thing]) continue;
        if (!runtime_thing_is_shootable(thing)) continue;
        if (thing_has_readable_slot(thing)) continue;
        side = ((dx * plane_x) + (dy * plane_y)) >> 8;
        abs_side = iabs16(side);
        if (abs_side > front || abs_side > WORLD_Q8(896)) continue;
        if (!line_of_sight_q8((short)px, (short)py, thing_x_q8[thing], thing_y_q8[thing])) continue;
        damage_enemy_at(thing, 7);
    }
}

static void alert_monsters_by_sound(void) {
    int px, py;
    rc_player_q8(&px, &py);
    if (!monster_path_valid) {
        rebuild_monster_path();
        monster_path_timer = MONSTER_PATH_REBUILD_TICKS;
    }
    for (u16 mi = 0; mi < thing_monster_count; mi++) {
        int i = thing_monster_indices[mi];
        int dx, dy, range;
        u8 audible = 0;
        if (enemy_dead[i] || enemy_awake[i]) continue;
        dx = iabs16(px - thing_x_q8[i]);
        dy = iabs16(py - thing_y_q8[i]);
        range = dx + dy;
        if (range > WORLD_Q8(8192)) continue;
        if (!runtime_thing_is_monster(i)) continue;
        if (range <= WORLD_Q8(1024)) {
            audible = 1;
        } else if (monster_path_valid) {
            int cx = thing_x_q8[i] >> 8;
            int cy = thing_y_q8[i] >> 8;
            if (cx >= 0 && cy >= 0 && cx < ACTIVE_MAP_W && cy < ACTIVE_MAP_H) {
                u8 path_dist = monster_path_dist[cy][cx];
                if (path_dist != 0xFF && path_dist <= 32 && range <= WORLD_Q8(8192)) audible = 1;
            }
        }
        if (!audible && range <= WORLD_Q8(4096)
            && line_of_sight_q8((short)px, (short)py, thing_x_q8[i], thing_y_q8[i])) {
            audible = 1;
        }
        if (!audible) continue;
        enemy_awake[i] = 1;
        enemy_attack_cooldown[i] = 28;
    }
}

static void update_enemy_hit_flash(void) {
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        if (!(enemy_hit_flash[i] | enemy_attack_cooldown[i] | enemy_attack_anim[i]
            | explosion_timer[i] | death_anim_timer[i] | death_drop_timer[i])) continue;
        if (enemy_hit_flash[i]) enemy_hit_flash[i]--;
        if (enemy_attack_cooldown[i]) enemy_attack_cooldown[i]--;
        if (enemy_attack_anim[i]) enemy_attack_anim[i]--;
        if (explosion_timer[i]) {
            explosion_timer[i]--;
            if (!explosion_timer[i]) {
                enemy_dead[i] = 1;
                thing_type_override[i] = 0;
                redraw_minimap_thing_cell(i);
            }
        }
        if (death_anim_timer[i]) {
            death_anim_timer[i]--;
            if (death_anim_timer[i] == 15 || death_anim_timer[i] == 10 || death_anim_timer[i] == 5) {
                u16 next_type = death_anim_next_stage_type(thing_type_override[i]);
                if (next_type) {
                    thing_type_override[i] = next_type;
                    hide_enemies();
                }
            }
            if (!death_anim_timer[i]) {
                thing_type_override[i] = death_anim_final_type[i];
                if (death_anim_drop_type[i]) {
                    spawn_dynamic_drop(death_anim_drop_type[i], thing_x_q8[i], thing_y_q8[i]);
                } else {
                    death_drop_type[i] = 0;
                    death_drop_timer[i] = 0;
                }
                death_anim_final_type[i] = 0;
                death_anim_drop_type[i] = 0;
                redraw_minimap_thing_cell(i);
                hide_enemies();
            }
        } else if (death_drop_timer[i]) {
            death_drop_timer[i]--;
            if (!death_drop_timer[i]) {
                thing_type_override[i] = death_drop_type[i];
                if (thing_is_pickup(thing_type_override[i])) index_pickup_candidate((u16)i);
                death_drop_type[i] = 0;
                redraw_minimap_thing_cell(i);
                hide_enemies();
            }
        }
    }
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemy_slot_flash[slot]) enemy_slot_flash[slot]--;
    }
}

static void player_take_damage(u16 amount) {
    u16 original_amount = amount;
    if (amount && power_invuln_timer) {
        hurt_flash = 2;
        return;
    }
    if (amount && player_armor && player_armor_class) {
        u16 saved = player_armor_class >= 2 ? (amount >> 1) : (amount / 3);
        if (saved > player_armor) saved = player_armor;
        player_armor = (u16)(player_armor - saved);
        amount = (u16)(amount - saved);
        if (saved) armor_flash_timer = 18;
        if (!player_armor) player_armor_class = 0;
    }
    hurt_flash = 5;
    if (amount >= player_health) {
        player_health = 0;
        close_minimap_for_terminal_message();
    } else {
        player_health = (u16)(player_health - amount);
    }
    if (original_amount && player_health) face_pain_timer = 18;
}

static u8 monster_ranged_damage(u16 thing_type) {
    switch (thing_type) {
    case 7:    /* spider mastermind */
    case 16:   /* cyberdemon */
        return 10;
    case 64:   /* arch-vile */
        return 8;
    case 69:   /* hell knight */
        return 6;
    case 65:   /* heavy weapon dude */
    case 66:   /* revenant */
    case 67:   /* mancubus */
    case 68:   /* arachnotron */
    case 84:   /* Wolfenstein SS */
        return 5;
    case 3004: /* former human */
        return 3;
    case 9:    /* shotgun guy */
        return 5;
    case 3001: /* imp */
        return 4;
    case 3005: /* cacodemon */
        return 6;
    case 3003: /* baron */
        return 8;
    default:
        return 0;
    }
}

static u16 monster_projectile_type(u16 thing_type) {
    switch (thing_type) {
    case 3001: /* imp */
    case 68:   /* arachnotron */
        return 9006;
    case 3005: /* cacodemon */
    case 67:   /* mancubus */
        return 9008;
    case 16:   /* cyberdemon */
    case 66:   /* revenant */
    case 69:   /* hell knight */
    case 3003: /* baron */
        return 9007;
    default:
        return 0;
    }
}

static u8 spawn_monster_projectile(int thing, u16 type, u8 damage, int px, int py) {
    int dx, dy, adx, ady, steps;
    if (thing < 0 || projectile_active) return 0;
    projectile_x_q8 = thing_x_q8[thing];
    projectile_y_q8 = thing_y_q8[thing];
    dx = px - projectile_x_q8;
    dy = py - projectile_y_q8;
    adx = iabs16(dx);
    ady = iabs16(dy);
    steps = ((adx > ady ? adx : ady) / 76);
    if (steps < 8) steps = 8;
    if (steps > 34) steps = 34;
    projectile_dx_q8 = (short)(dx / steps);
    projectile_dy_q8 = (short)(dy / steps);
    projectile_timer = (u8)steps;
    projectile_type = type;
    projectile_damage = damage;
    projectile_from_player = 0;
    projectile_source_thing = thing;
    projectile_hit_range_q8 = 0;
    projectile_hit_coarse_cells = 0;
    projectile_active = 1;
    return 1;
}

static u8 spawn_player_projectile(u16 type, u8 timer) {
    int px, py, dir_x, dir_y, plane_x, plane_y;
    short hit_range_q8 = 0;
    if (projectile_active) return 0;
    if (type == 9006) hit_range_q8 = WORLD_Q8(112);
    else if (type == 9007) hit_range_q8 = WORLD_Q8(160);
    else if (type == 9008) hit_range_q8 = WORLD_Q8(144);
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    projectile_x_q8 = (short)(px + ((dir_x * WORLD_Q8(192)) >> 8));
    projectile_y_q8 = (short)(py + ((dir_y * WORLD_Q8(192)) >> 8));
    projectile_dx_q8 = (short)((dir_x * WORLD_Q8(96)) >> 8);
    projectile_dy_q8 = (short)((dir_y * WORLD_Q8(96)) >> 8);
    projectile_timer = timer;
    projectile_type = type;
    projectile_damage = 0;
    projectile_from_player = 1;
    projectile_source_thing = -1;
    projectile_hit_range_q8 = hit_range_q8;
    projectile_hit_coarse_cells = (u8)((hit_range_q8 + 255) >> 8);
    projectile_active = 1;
    return 1;
}

static void clear_projectile(void) {
    projectile_active = 0;
    projectile_from_player = 0;
    projectile_source_thing = -1;
    projectile_type = 0;
    projectile_timer = 0;
    projectile_damage = 0;
    projectile_hit_range_q8 = 0;
    projectile_hit_coarse_cells = 0;
}

static int player_projectile_hit_shootable(void) {
    int projectile_cell_x;
    int projectile_cell_y;
    short hit_range_q8 = projectile_hit_range_q8;
    u8 coarse_cells = projectile_hit_coarse_cells;
    if (!hit_range_q8) return -1;
    projectile_cell_x = projectile_x_q8 >> 8;
    projectile_cell_y = projectile_y_q8 >> 8;
    for (u16 si = 0; si < thing_shootable_count; si++) {
        u16 i = thing_shootable_indices[si];
        short thing_x = thing_x_q8[i];
        short thing_y = thing_y_q8[i];
        if (iabs16((thing_x >> 8) - projectile_cell_x) > coarse_cells) continue;
        if (iabs16((thing_y >> 8) - projectile_cell_y) > coarse_cells) continue;
        if (enemy_dead[i] || !runtime_thing_is_shootable(i)) continue;
        if (iabs16(thing_x - projectile_x_q8) <= hit_range_q8
            && iabs16(thing_y - projectile_y_q8) <= hit_range_q8) {
            return i;
        }
    }
    return -1;
}

static void damage_plasma_projectile_target(int thing) {
    spawn_impact_effect(projectile_x_q8, projectile_y_q8, 8);
    if (thing >= 0) {
        if (damage_enemy_at(thing, 3)) hide_enemies();
        else {
            enemy_hit_flash[thing] = 18;
            flash_visible_enemy(thing);
        }
    }
    clear_projectile();
}

static void detonate_player_projectile(void) {
    u16 type = projectile_type;
    spawn_impact_effect(projectile_x_q8, projectile_y_q8, 12);
    if (type == 9007) {
        damage_bfg_targets();
    } else if (type == 9008) {
        damage_rocket_radius(projectile_x_q8, projectile_y_q8);
    }
    clear_projectile();
    hide_enemies();
}

static void spawn_impact_effect(short x_q8, short y_q8, u8 timer) {
    impact_x_q8 = x_q8;
    impact_y_q8 = y_q8;
    impact_timer = timer;
    impact_active = 1;
}

static void update_impact_effect(void) {
    if (!impact_active) return;
    if (!game_active()) {
        impact_active = 0;
        return;
    }
    if (impact_timer) impact_timer--;
    if (!impact_timer) impact_active = 0;
}

static void update_projectile(void) {
    if (!projectile_active) return;
    if (!game_active()) {
        clear_projectile();
        return;
    }
    if (!projectile_from_player) {
        if (projectile_source_thing < 0 || enemy_dead[projectile_source_thing]
            || !thing_has_readable_slot(projectile_source_thing)) {
            clear_projectile();
            return;
        }
    }
    if (projectile_type == 9000 && projectile_damage == 0) {
        if (projectile_timer) projectile_timer--;
        if (!projectile_timer) {
            clear_projectile();
        }
        return;
    }
    projectile_x_q8 = (short)(projectile_x_q8 + projectile_dx_q8);
    projectile_y_q8 = (short)(projectile_y_q8 + projectile_dy_q8);
    if (map_at(projectile_x_q8 >> 8, projectile_y_q8 >> 8)) {
        if (projectile_from_player) {
            detonate_player_projectile();
            return;
        }
        spawn_impact_effect(projectile_x_q8, projectile_y_q8, 8);
        clear_projectile();
        return;
    }
    if (projectile_from_player) {
        int hit_thing = player_projectile_hit_shootable();
        if (hit_thing >= 0) {
            if (projectile_type == 9006) damage_plasma_projectile_target(hit_thing);
            else detonate_player_projectile();
            return;
        }
    } else {
        int px, py;
        rc_player_q8(&px, &py);
        if (iabs16(px - projectile_x_q8) <= WORLD_Q8(112) && iabs16(py - projectile_y_q8) <= WORLD_Q8(112)) {
            spawn_impact_effect(projectile_x_q8, projectile_y_q8, 8);
            player_take_damage(projectile_damage);
            hurt_timer = 24;
            clear_projectile();
            return;
        }
    }
    if (projectile_timer) projectile_timer--;
    if (!projectile_timer) {
        if (projectile_from_player) detonate_player_projectile();
        else {
            clear_projectile();
        }
    }
}

static u8 update_close_monster_melee(int px, int py) {
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (enemy_dead[thing] || !enemy_slot_is_monster(slot)) continue;
        if (enemy_hit_flash[thing] || enemy_attack_cooldown[thing]) continue;
        if (!enemy_slot_can_attack(slot)) continue;
        if (iabs16(px - thing_x_q8[thing]) < WORLD_Q8(288) && iabs16(py - thing_y_q8[thing]) < WORLD_Q8(288)
            && line_of_sight_q8(thing_x_q8[thing], thing_y_q8[thing], (short)px, (short)py)) {
            player_take_damage(4);
            enemy_awake[thing] = 1;
            enemy_attack_cooldown[thing] = 32;
            enemy_attack_anim[thing] = 10;
            hurt_timer = 18;
            return 1;
        }
    }
    return 0;
}

static void update_monster_damage(void) {
    enum { RANGED_READABLE_WARMUP = 6 };
    int px, py;
    if (hurt_timer) {
        hurt_timer--;
        return;
    }
    if (!game_active()) return;
    rc_player_q8(&px, &py);
    if (update_close_monster_melee(px, py)) return;

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        u16 type;
        u8 ranged_damage;
        if (thing < 0) continue;
        if (enemy_hit_flash[thing]) continue;
        if (enemy_attack_cooldown[thing]) continue;
        if (!enemy_slot_can_ranged_attack(slot)) continue;
        if (enemy_ranged_readable_ticks[thing] < RANGED_READABLE_WARMUP) continue;
        if (!enemy_slot_is_monster(slot)) continue;
        type = enemies[slot].thing_type;
        ranged_damage = monster_ranged_damage(type);
        if (ranged_damage && enemies[slot].dist_q8 < 1700 && enemies[slot].screen_h > 18
            && line_of_sight_q8((short)px, (short)py, thing_x_q8[thing], thing_y_q8[thing])) {
            u16 projectile = monster_projectile_type(type);
            if (power_invis_timer && ((monster_ai_tick + thing) & 1)) {
                enemy_attack_cooldown[thing] = 24;
                continue;
            }
            if (projectile) {
                if (!spawn_monster_projectile(thing, projectile, ranged_damage, px, py)) continue;
                enemy_attack_cooldown[thing] = 72;
                enemy_attack_anim[thing] = 12;
                return;
            }
            enemy_attack_anim[thing] = 10;
            player_take_damage(ranged_damage);
            enemy_attack_cooldown[thing] = 64;
            hurt_timer = 32;
            return;
        }
    }
}

static u8 monster_step_occupied(int self, short x_q8, short y_q8) {
    int cell_x = x_q8 >> 8;
    int cell_y = y_q8 >> 8;
    enum { MONSTER_SEPARATION_CELLS = (MONSTER_SEPARATION_Q8 + 255) >> 8 };
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        short thing_x;
        short thing_y;
        if (i == self) continue;
        thing_x = thing_x_q8[i];
        thing_y = thing_y_q8[i];
        if (iabs16((thing_x >> 8) - cell_x) > MONSTER_SEPARATION_CELLS) continue;
        if (iabs16((thing_y >> 8) - cell_y) > MONSTER_SEPARATION_CELLS) continue;
        if (enemy_dead[i]) continue;
        if (!runtime_thing_is_shootable(i)) continue;
        if (iabs16(x_q8 - thing_x) < MONSTER_SEPARATION_Q8 && iabs16(y_q8 - thing_y) < MONSTER_SEPARATION_Q8) return 1;
    }
    return 0;
}

static u8 can_monster_step(int self, short x_q8, short y_q8) {
    int cx = x_q8 >> 8;
    int cy = y_q8 >> 8;
    if (map_at(cx, cy)) return 0;
    if (monster_step_occupied(self, x_q8, y_q8)) return 0;
    return 1;
}

static void set_monster_facing_from_delta(int i, int dx, int dy) {
    if (i < 0 || i >= NG_RUNTIME_THING_COUNT) return;
    if (iabs16(dx) + iabs16(dy) < 8) return;
    monster_face_x[i] = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    monster_face_y[i] = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
}

static void move_monster_toward(int i, int dx, int dy, int adx, int ady) {
    short x = thing_x_q8[i];
    short y = thing_y_q8[i];
    short sx = (short)(dx < 0 ? -WORLD_Q8(18) : WORLD_Q8(18));
    short sy = (short)(dy < 0 ? -WORLD_Q8(18) : WORLD_Q8(18));
    set_monster_facing_from_delta(i, dx, dy);

    if (adx > ady) {
        if (can_monster_step(i, (short)(x + sx), y)) {
            set_runtime_thing_position(i, (short)(x + sx), y);
        } else if (can_monster_step(i, x, (short)(y + sy))) {
            set_runtime_thing_position(i, x, (short)(y + sy));
        }
    } else {
        if (can_monster_step(i, x, (short)(y + sy))) {
            set_runtime_thing_position(i, x, (short)(y + sy));
        } else if (can_monster_step(i, (short)(x + sx), y)) {
            set_runtime_thing_position(i, (short)(x + sx), y);
        }
    }
}

static void rebuild_monster_path(void) {
    int px, py;
    u16 head = 0;
    u16 tail = 0;
    rc_player_cell(&px, &py);
    monster_path_player_cell_x = (short)px;
    monster_path_player_cell_y = (short)py;
    for (u16 y = 0; y < ACTIVE_MAP_H; y++) {
        for (u16 x = 0; x < ACTIVE_MAP_W; x++) monster_path_dist[y][x] = 0xFF;
    }
    if (map_at(px, py)) {
        monster_path_valid = 0;
        return;
    }
    monster_path_dist[py][px] = 0;
    monster_path_queue[tail++] = (u16)((py << 8) | (px & 0xFF));
    while (head < tail) {
        u16 cell = monster_path_queue[head++];
        u8 d;
        int x = cell & 0xFF;
        int y = cell >> 8;
        static const signed char dirs[4][2] = {
            { 1,  0}, {-1,  0}, { 0,  1}, { 0, -1}
        };
        d = monster_path_dist[y][x];
        if (d >= 254) continue;
        for (u8 i = 0; i < 4; i++) {
            int nx = x + dirs[i][0];
            int ny = y + dirs[i][1];
            if (nx < 0 || ny < 0 || nx >= ACTIVE_MAP_W || ny >= ACTIVE_MAP_H) continue;
            if (map_at(nx, ny)) continue;
            if (monster_path_dist[ny][nx] != 0xFF) continue;
            monster_path_dist[ny][nx] = (u8)(d + 1);
            monster_path_queue[tail++] = (u16)((ny << 8) | (nx & 0xFF));
        }
    }
    monster_path_valid = 1;
}

static void refresh_monster_path(void) {
    int px, py;
    rc_player_cell(&px, &py);
    if (!monster_path_valid) {
        rebuild_monster_path();
        monster_path_timer = MONSTER_PATH_REBUILD_TICKS;
    } else if (px != monster_path_player_cell_x || py != monster_path_player_cell_y) {
        if (monster_path_timer) {
            monster_path_timer--;
        } else {
            rebuild_monster_path();
            monster_path_timer = MONSTER_PATH_REBUILD_TICKS;
        }
    } else {
        if (monster_path_timer) monster_path_timer--;
        else monster_path_timer = MONSTER_PATH_REBUILD_TICKS;
    }
}

static u8 move_monster_along_path(int i) {
    int cx = thing_x_q8[i] >> 8;
    int cy = thing_y_q8[i] >> 8;
    u8 best_dist;
    int best_x;
    int best_y;
    static const signed char dirs[4][2] = {
        { 1,  0}, {-1,  0}, { 0,  1}, { 0, -1}
    };
    if (!monster_path_valid) return 0;
    if (cx < 0 || cy < 0 || cx >= ACTIVE_MAP_W || cy >= ACTIVE_MAP_H) return 0;
    best_dist = monster_path_dist[cy][cx];
    if (best_dist == 0xFF || best_dist == 0) return 0;
    best_x = cx;
    best_y = cy;
    for (u8 dir = 0; dir < 4; dir++) {
        int nx = cx + dirs[dir][0];
        int ny = cy + dirs[dir][1];
        u8 d;
        if (nx < 0 || ny < 0 || nx >= ACTIVE_MAP_W || ny >= ACTIVE_MAP_H) continue;
        d = monster_path_dist[ny][nx];
        if (d < best_dist) {
            best_dist = d;
            best_x = nx;
            best_y = ny;
        }
    }
    if (best_x == cx && best_y == cy) return 0;
    {
        int target_x = (best_x << 8) + 128;
        int target_y = (best_y << 8) + 128;
        int dx = target_x - thing_x_q8[i];
        int dy = target_y - thing_y_q8[i];
        move_monster_toward(i, dx, dy, iabs16(dx), iabs16(dy));
    }
    return 1;
}

static u8 visible_monster_slots(void) {
    u8 count = 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        if (enemy_slot_is_monster(slot)) count++;
    }
    return count;
}

#ifdef DOOM_REVEAL_HIDDEN_MONSTERS
static u8 reveal_hidden_monster_near_player(int i, int px, int py) {
    int dir_x, dir_y, plane_x, plane_y;
    int best_x = 0;
    int best_y = 0;
    int best_score = 0x3FFFFFFF;
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    for (signed char cy = -5; cy <= 5; cy++) {
        for (signed char cx = -5; cx <= 5; cx++) {
            int x = px + ((int)cx << 8);
            int y = py + ((int)cy << 8);
            int vx = x - px;
            int vy = y - py;
            int front = ((vx * dir_x) + (vy * dir_y)) >> 8;
            int side = ((vx * plane_x) + (vy * plane_y)) >> 8;
            int abs_side = iabs16(side);
            int score;
            if (front < 384 || front > 1280) continue;
            if (abs_side > 960) continue;
            if (map_at(x >> 8, y >> 8)) continue;
            if (!line_of_sight_q8((short)x, (short)y, (short)px, (short)py)) continue;
            score = abs_side + iabs16(front - 640);
            if (score < best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }
    if (best_score == 0x3FFFFFFF) return 0;
    set_runtime_thing_position(i, (short)best_x, (short)best_y);
    set_monster_facing_from_delta(i, px - best_x, py - best_y);
    enemy_hidden_timer[i] = 0;
    enemy_attack_cooldown[i] = 24;
    return 1;
}
#endif

static void update_monster_ai(void) {
    int px, py;
    u8 visible_monsters;
    if (++monster_ai_tick & 3) return;
#ifdef DOOM_HIDDEN_ATTACK_TEST
    return;
#endif
    rc_player_q8(&px, &py);
    refresh_monster_path();
    visible_monsters = visible_monster_slots();
    for (u16 mi = 0; mi < thing_monster_count; mi++) {
        int i = thing_monster_indices[mi];
        int dx, dy, adx, ady;
        if (enemy_dead[i] || enemy_hit_flash[i]) continue;
        dx = px - thing_x_q8[i];
        dy = py - thing_y_q8[i];
        adx = iabs16(dx);
        ady = iabs16(dy);
        if (adx + ady > WORLD_Q8(4608)) continue;
        if (!runtime_thing_is_monster(i)) continue;
        if (adx < WORLD_Q8(288) && ady < WORLD_Q8(288)
            && line_of_sight_q8(thing_x_q8[i], thing_y_q8[i], (short)px, (short)py)) continue;
        if (!enemy_awake[i]) {
            if (!line_of_sight_q8(thing_x_q8[i], thing_y_q8[i], (short)px, (short)py)) continue;
            enemy_awake[i] = 1;
            enemy_attack_cooldown[i] = 28;
        }

        if (!move_monster_along_path(i)) move_monster_toward(i, dx, dy, adx, ady);
        if (visible_monsters != 0) {
            enemy_hidden_timer[i] = 0;
        }
#ifdef DOOM_REVEAL_HIDDEN_MONSTERS
        else if (adx + ady < WORLD_Q8(4608) && visible_monsters == 0) {
            if (enemy_hidden_timer[i] < 255) enemy_hidden_timer[i]++;
            if (enemy_hidden_timer[i] > 18 && reveal_hidden_monster_near_player(i, px, py)) visible_monsters = 1;
        }
#endif
    }
}

#if DOOM_SIMPLE_MAP && !defined(DOOM_FOCUSED_TEST)
typedef struct SimpleMapThingSeed {
    u16 type;
    u8 x;
    u8 y;
    u8 awake;
} SimpleMapThingSeed;

static void seed_simple_map_things(void) {
    static const SimpleMapThingSeed seeds[ENEMY_VISIBLE_COUNT] = {
        {3004, 4, 9, 0},   /* former human in the side lane */
        {3001, 12, 7, 0},  /* imp guarding the side lane */
        {2035, 6, 7, 0},   /* barrel near the first encounter */
        {5,    3, 13, 0},  /* blue keycard for the exit door */
        {2007, 5, 13, 0},  /* clip */
        {2011, 6, 13, 0},  /* stimpack */
        {2018, 10, 13, 0}, /* green armor */
        {2001, 12, 13, 0}, /* shotgun */
    };
    int px;
    int py;

    thing_monster_count = 0;
    thing_shootable_count = 0;
    thing_render_count = 0;
    thing_pickup_count = 0;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 1;
        enemy_hp[i] = 0;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
        enemy_hidden_timer[i] = 0;
        enemy_hit_flash[i] = 0;
        thing_type_override[i] = 0;
        thing_static_class[i] = THING_CLASS_NONE;
    }

    rc_player_q8(&px, &py);
    for (u16 i = 0; i < ENEMY_VISIBLE_COUNT && i < NG_RUNTIME_THING_COUNT; i++) {
        u16 type = seeds[i].type;
        u8 thing_class;
        short x_q8;
        short y_q8;
        if (map_at(seeds[i].x, seeds[i].y)) continue;
        x_q8 = (short)((seeds[i].x << 8) + 128);
        y_q8 = (short)((seeds[i].y << 8) + 128);
        thing_x_q8[i] = x_q8;
        thing_y_q8[i] = y_q8;
        thing_type_override[i] = type;
        thing_class = thing_render_class(type);
        thing_static_class[i] = thing_class;
        enemy_dead[i] = 0;
        if (thing_is_shootable(type)) {
            enemy_hp[i] = thing_is_barrel(type) ? 1 : monster_start_hp(type);
            enemy_awake[i] = seeds[i].awake;
            enemy_attack_cooldown[i] = seeds[i].awake ? 56 : 0;
            index_shootable_candidate(i);
            if (thing_is_monster(type)) {
                index_monster_candidate(i);
                set_monster_facing_from_delta(i, px - x_q8, py - y_q8);
            }
        }
        if (thing_class != THING_CLASS_NONE) index_render_candidate(i);
        if (thing_is_pickup(type)) index_pickup_candidate(i);
    }
    monster_path_valid = 0;
}
#endif

#if defined(DOOM_COMBAT_TEST) || defined(DOOM_MELEE_TEST) || defined(DOOM_MONSTER_GALLERY_TEST) || defined(DOOM_ARSENAL_TEST) || defined(DOOM_DEATH_TEST) || defined(DOOM_POWERUP_TEST) || defined(DOOM_KEY_DOOR_TEST) || defined(DOOM_HIDDEN_ATTACK_TEST) || defined(DOOM_E1M8_BOSS_TEST)
static u8 test_position(short *out_x, short *out_y, short forward, short lateral) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    short x;
    short y;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    x = (short)(px + ((dir_x * forward) >> 8) + ((plane_x * lateral) >> 8));
    y = (short)(py + ((dir_y * forward) >> 8) + ((plane_y * lateral) >> 8));
    if (map_at(x >> 8, y >> 8)) return 0;
    *out_x = x;
    *out_y = y;
    return 1;
}

static u8 place_test_thing(u16 thing, u16 type, short forward, short lateral) {
#if NG_RUNTIME_THING_COUNT > 0
    short x;
    short y;
    if (thing >= NG_RUNTIME_THING_COUNT) return 0;
    if (!test_position(&x, &y, forward, lateral)) return 0;
    thing_x_q8[thing] = x;
    thing_y_q8[thing] = y;
    thing_type_override[thing] = type;
    enemy_dead[thing] = 0;
    enemy_hp[thing] = 0;
    enemy_awake[thing] = 0;
    enemy_attack_cooldown[thing] = 0;
    enemy_hit_flash[thing] = 0;
    enemy_attack_anim[thing] = 0;
    enemy_ranged_readable_ticks[thing] = 0;
    if (thing_render_class(type) != THING_CLASS_NONE) index_render_candidate(thing);
    if (thing_is_pickup(type)) index_pickup_candidate(thing);
    if (thing_is_monster(type)) index_monster_candidate(thing);
    if (thing_is_shootable(type)) index_shootable_candidate(thing);
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    persist_runtime_slot_to_chunk_state(thing);
#endif
    return 1;
#else
    (void)thing;
    (void)type;
    (void)forward;
    (void)lateral;
    return 0;
#endif
}

static void place_test_imp(void) {
#if NG_RUNTIME_THING_COUNT > 0
    int px, py;
    static const short forward_steps[] = { WORLD_Q8(896), WORLD_Q8(1152), WORLD_Q8(640), WORLD_Q8(1408) };
    static const short lateral_steps[] = { 0, WORLD_Q8(192), -WORLD_Q8(192), WORLD_Q8(384), -WORLD_Q8(384) };
    rc_player_q8(&px, &py);

    for (u8 f = 0; f < sizeof(forward_steps) / sizeof(forward_steps[0]); f++) {
        for (u8 l = 0; l < sizeof(lateral_steps) / sizeof(lateral_steps[0]); l++) {
            short x;
            short y;
            if (!test_position(&x, &y, forward_steps[f], lateral_steps[l])) continue;
            thing_x_q8[0] = x;
            thing_y_q8[0] = y;
            thing_type_override[0] = 3001; /* imp: present in shareware and has full frame coverage */
            enemy_dead[0] = 0;
            enemy_hp[0] = monster_start_hp(3001);
            enemy_awake[0] = 1;
            enemy_attack_cooldown[0] = 56;
            enemy_hit_flash[0] = 0;
            enemy_attack_anim[0] = 0;
            index_render_candidate(0);
            index_monster_candidate(0);
            index_shootable_candidate(0);
            set_monster_facing_from_delta(0, px - x, py - y);
            return;
        }
    }
#endif
}

#ifdef DOOM_POWERUP_TEST
static void place_powerup_test_imp(void) {
#if NG_RUNTIME_THING_COUNT > 6
    int px, py;
    if (!place_test_thing(6, 3001, WORLD_Q8(900), WORLD_Q8(360))) return;
    rc_player_q8(&px, &py);
    enemy_hp[6] = monster_start_hp(3001);
    enemy_awake[6] = 0;
    enemy_attack_cooldown[6] = 0;
    set_monster_facing_from_delta(6, px - thing_x_q8[6], py - thing_y_q8[6]);
#endif
}
#endif
#endif

#ifdef DOOM_KEY_DOOR_TEST
static void configure_key_door_test(void) {
#if NG_RUNTIME_THING_COUNT > 0
    u8 door_found = 0;
    u16 door_y_sum = 0;
    u8 door_count = 0;
    u8 door_x = 27;
    u8 door_y = 32;
    u8 player_x = 30;
    u8 key_x = 29;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 1;
        enemy_hp[i] = 0;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
        thing_type_override[i] = 0;
    }

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    thing_monster_count = 0;
    thing_shootable_count = 0;
    thing_render_count = 0;
    thing_pickup_count = 0;
    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) {
        if (g_chunk_doors[i].special != 28) continue;
        if (!door_found || g_chunk_doors[i].x < door_x) {
            g_simple_active_chunk = g_chunk_doors[i].chunk;
            door_x = (u8)(g_chunk_doors[i].x - (g_simple_active_chunk % DOOM_CHUNK_COLS) * SIMPLE_MAP_W);
        }
        door_y_sum = (u16)(door_y_sum + (u8)(g_chunk_doors[i].y - (g_simple_active_chunk / DOOM_CHUNK_COLS) * SIMPLE_MAP_H));
        door_count++;
        door_found = 1;
    }
    if (door_count) door_y = (u8)((door_y_sum + door_count / 2) / door_count);
    for (u16 i = 0; i < MAP_RUNTIME_OPEN_BYTES; i++) g_runtime_cell_open[i] = 0;
    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
    for (u16 i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
#else
#if NG_RUNTIME_DOOR_COUNT > 0
    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        if (g_runtime_doors[i].special != 28) continue;
        if (!door_found || g_runtime_doors[i].x < door_x) door_x = g_runtime_doors[i].x;
        door_y_sum = (u16)(door_y_sum + g_runtime_doors[i].y);
        door_count++;
        door_found = 1;
    }
    if (door_count) door_y = (u8)((door_y_sum + door_count / 2) / door_count);
#endif
#endif

    /* Stage far enough back that the focused smoke reads as a doorway, not a
     * wall close-up. The E1M2 red door has a shallow wall behind it in the
     * converted grid, so the verification pose must favor readable framing. */
    for (u8 offset = 6; offset >= 3; offset--) {
        u8 candidate_x = (u8)(door_x + offset);
        u8 candidate_key_x = (u8)(candidate_x - 1);
        if (candidate_x < ACTIVE_MAP_W && candidate_key_x < ACTIVE_MAP_W
            && !map_at(candidate_x, door_y) && !map_at(candidate_key_x, door_y)) {
            player_x = candidate_x;
            key_x = candidate_key_x;
            break;
        }
        if (offset == 3) break;
    }

    rc_set_pose_q8((short)((player_x << 8) + 128), (short)((door_y << 8) + 128), -256, 0);
    thing_x_q8[0] = (short)((key_x << 8) + 128);
    thing_y_q8[0] = (short)((door_y << 8) + 128);
    thing_type_override[0] = 13; /* red keycard */
    enemy_dead[0] = 0;
    index_render_candidate(0);
    index_pickup_candidate(0);

    player_keys = 0;
    shown_keys = 0xFF;
    key_message_timer = 0;
    missing_key_bits = 0;
    shown_weapon_status = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
#endif
}
#endif

#ifdef DOOM_COMBAT_TEST
static void configure_combat_test(void) {
    player_has_shotgun = 1;
    player_shells = 24;
    current_weapon = WEAPON_SHOTGUN;
    place_test_imp();
}
#endif

#ifdef DOOM_HIDDEN_ATTACK_TEST
static void configure_hidden_attack_test(void) {
#if NG_RUNTIME_THING_COUNT > 0
    int px, py;
    player_health = 100;
    hurt_timer = 0;
    if (!place_test_thing(0, 3004, WORLD_Q8(520), WORLD_Q8(1152))) return;
    rc_player_q8(&px, &py);
    enemy_hp[0] = monster_start_hp(3004);
    enemy_awake[0] = 1;
    enemy_attack_cooldown[0] = 0;
    enemy_attack_anim[0] = 0;
    enemy_ranged_readable_ticks[0] = 255;
    set_monster_facing_from_delta(0, px - thing_x_q8[0], py - thing_y_q8[0]);
    shown_health = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
#endif
}
#endif

#ifdef DOOM_E1M1_ENCOUNTER_TEST
static void configure_e1m1_encounter_test(void) {
#if NG_RUNTIME_THING_COUNT > 13
    int px, py;
    rc_set_pose_q8((short)((17 << 8) + 128), (short)((18 << 8) + 128), 0, 256);
    rc_player_q8(&px, &py);
    current_weapon = WEAPON_PISTOL;
    player_ammo = 50;
    enemy_hp[13] = monster_start_hp(runtime_thing_type(13));
    enemy_awake[13] = 1;
    enemy_attack_cooldown[13] = 56;
    enemy_hit_flash[13] = 0;
    enemy_attack_anim[13] = 0;
    enemy_ranged_readable_ticks[13] = 0;
    set_monster_facing_from_delta(13, px - thing_x_q8[13], py - thing_y_q8[13]);
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
#endif
}
#endif

#ifdef DOOM_E1M1_SCOUT_TEST
static void configure_e1m1_scout_test(void) {
#if NG_RUNTIME_THING_COUNT > 13
    int px, py;
    rc_set_pose_q8((short)((23 << 8) + 128), (short)((20 << 8) + 128), -243, 81);
    rc_player_q8(&px, &py);
    current_weapon = WEAPON_PISTOL;
    player_ammo = 50;
    enemy_hp[13] = monster_start_hp(runtime_thing_type(13));
    enemy_awake[13] = 1;
    enemy_attack_cooldown[13] = 72;
    enemy_hit_flash[13] = 0;
    enemy_attack_anim[13] = 0;
    enemy_ranged_readable_ticks[13] = 0;
    set_monster_facing_from_delta(13, px - thing_x_q8[13], py - thing_y_q8[13]);
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
#endif
}
#endif

#ifdef DOOM_E1M1_EXIT_TEST
static void configure_e1m1_exit_test(void) {
#if NG_RUNTIME_EXIT_COUNT > 0
    rc_set_pose_q8((short)(g_runtime_exits[0].x_q8 - (2 << 8)), g_runtime_exits[0].y_q8, 256, 0);
#else
    rc_set_pose_q8((short)((57 << 8) + 128), (short)((44 << 8) + 128), 256, 0);
#endif
    current_weapon = WEAPON_PISTOL;
    player_ammo = 50;
    player_health = 100;
    player_armor = 0;
    player_armor_class = 0;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 1;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
    }
    shown_health = 0xFFFF;
    shown_armor = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
}
#endif

#ifdef DOOM_E1M8_BOSS_TEST
static void configure_e1m8_boss_test(void) {
    u8 placed = 0;
    static const short forward_steps[] = {WORLD_Q8(700), WORLD_Q8(820), WORLD_Q8(940)};
    static const short lateral_steps[] = {WORLD_Q8(32), WORLD_Q8(80), WORLD_Q8(128), 0, -WORLD_Q8(32)};
    int px, py;
    player_has_shotgun = 1;
    player_shells = 24;
    current_weapon = WEAPON_SHOTGUN;
    player_health = 100;
    rc_player_q8(&px, &py);
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 1;
        enemy_hp[i] = 0;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
        thing_type_override[i] = 0;
        if (g_runtime_things[i].type == 3003 && placed < 2) {
            short x;
            short y;
            u8 found = 0;
            for (u16 f = 0; f < sizeof(forward_steps) / sizeof(forward_steps[0]) && !found; f++) {
                for (u16 l = 0; l < sizeof(lateral_steps) / sizeof(lateral_steps[0]); l++) {
                    short lateral = lateral_steps[(l + placed) % (sizeof(lateral_steps) / sizeof(lateral_steps[0]))];
                    if (!test_position(&x, &y, forward_steps[f], lateral)) continue;
                    thing_x_q8[i] = x;
                    thing_y_q8[i] = y;
                    found = 1;
                    break;
                }
            }
            if (!found) continue;
            enemy_dead[i] = 0;
            enemy_hp[i] = 1;
            enemy_awake[i] = 1;
            enemy_attack_cooldown[i] = 96;
            set_monster_facing_from_delta(i, px - thing_x_q8[i], py - thing_y_q8[i]);
            placed++;
        }
    }
    shown_health = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;
    reset_enemy_slot_cache();
    hide_enemies();
}
#endif

#ifdef DOOM_MELEE_TEST
static void configure_melee_test(void) {
    player_has_chainsaw = 1;
    current_weapon = WEAPON_CHAINSAW;
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;
#if NG_RUNTIME_THING_COUNT > 0
    {
        int px, py;
        if (place_test_thing(0, 3001, WORLD_Q8(300), 0)) {
            rc_player_q8(&px, &py);
            enemy_hp[0] = monster_start_hp(3001);
            enemy_awake[0] = 1;
            enemy_attack_cooldown[0] = 80;
            set_monster_facing_from_delta(0, px - thing_x_q8[0], py - thing_y_q8[0]);
        }
    }
#endif
}
#endif

#ifdef DOOM_MONSTER_GALLERY_TEST
static void configure_monster_gallery_test(void) {
#if NG_RUNTIME_THING_COUNT > 5
    static const u16 types[] = {3004, 9, 3001, 3002, 3003, 2035};
    static const short laterals[] = {
        -WORLD_Q8(520), -WORLD_Q8(312), -WORLD_Q8(104),
         WORLD_Q8(104),  WORLD_Q8(312),  WORLD_Q8(520)
    };
    int px, py;
    rc_player_q8(&px, &py);
    player_has_shotgun = 1;
    player_shells = 24;
    current_weapon = WEAPON_PISTOL;
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;

    for (u8 i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (!place_test_thing(i, types[i], WORLD_Q8(880), laterals[i])) continue;
        enemy_hp[i] = monster_start_hp(types[i]);
        enemy_awake[i] = thing_is_barrel(types[i]) ? 0 : 1;
        enemy_attack_cooldown[i] = 96;
        set_monster_facing_from_delta(i, px - thing_x_q8[i], py - thing_y_q8[i]);
    }
#endif
}
#endif

#ifdef DOOM_ARSENAL_TEST
static void configure_arsenal_test(void) {
    player_has_shotgun = 1;
    player_has_chaingun = 1;
    player_has_rocket_launcher = 1;
    player_has_plasma = weapon_asset_available(WEAPON_PLASMA);
    player_has_bfg = weapon_asset_available(WEAPON_BFG);
    player_has_chainsaw = 1;
    player_has_backpack = 1;
    player_keys = 1 | 2 | 4;
    player_ammo = player_max_bullets;
    player_shells = player_max_shells;
    player_rockets = player_max_rockets;
    player_cells = player_max_cells;
    player_armor = 200;
    player_armor_class = 2;
    current_weapon = player_has_plasma ? WEAPON_PLASMA : WEAPON_ROCKET;
    weapon_frame = 0xFF;
    shown_ammo = 0xFFFF;
    shown_keys = 0xFF;
    shown_weapon_status = 0xFFFF;
    place_test_imp();
}
#endif

#ifdef DOOM_DEATH_TEST
static void configure_death_test(void) {
    player_has_shotgun = 1;
    player_shells = 24;
    current_weapon = WEAPON_SHOTGUN;
    shown_ammo = 0xFFFF;
    shown_weapon_status = 0xFFFF;

    place_test_thing(0, 9001, WORLD_Q8(760), -WORLD_Q8(360));  /* former human corpse */
    place_test_thing(1, 9002, WORLD_Q8(760), -WORLD_Q8(120));  /* shotgun guy corpse */
    place_test_thing(2, 9003, WORLD_Q8(760), WORLD_Q8(120));   /* imp corpse */
    place_test_thing(3, 9004, WORLD_Q8(760), WORLD_Q8(360));   /* demon corpse */
    if (test_position(&dynamic_drop_x_q8[0], &dynamic_drop_y_q8[0], WORLD_Q8(620), 0)) {
        dynamic_drop_type[0] = 2001;
        dynamic_drop_active[0] = 1;
    }
}
#endif

#ifdef DOOM_POWERUP_TEST
static void configure_powerup_test(void) {
    static const u16 power_types[6] = {2013, 2018, 2048, 2012, 2001, 2008};
    static const short power_forward[6] = {
        WORLD_Q8(512), WORLD_Q8(576), WORLD_Q8(640),
        WORLD_Q8(704), WORLD_Q8(768), WORLD_Q8(832)
    };
    static const short power_lateral[6] = {
        -WORLD_Q8(240), -WORLD_Q8(144), -WORLD_Q8(48),
        WORLD_Q8(48), WORLD_Q8(144), WORLD_Q8(240)
    };
#if NG_RUNTIME_THING_COUNT > 0
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 1;
        enemy_hp[i] = 0;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
        thing_type_override[i] = 0;
    }
    thing_monster_count = 0;
    thing_shootable_count = 0;
    thing_render_count = 0;
    thing_pickup_count = 0;
#endif
    for (u8 i = 0; i < 8; i++) {
        dynamic_drop_active[i] = 0;
        dynamic_drop_type[i] = 0;
    }
    player_health = 65;
    player_armor = 50;
    player_armor_class = 1;
    player_ammo = 30;
    player_kills = 0;
    shown_health = 0xFFFF;
    shown_armor = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_frags = 0xFFFF;

    for (u8 i = 0; i < 6; i++) {
        if (place_test_thing(i, power_types[i], power_forward[i], power_lateral[i])) player_kills++;
    }
    power_lightamp_timer = 0;
}
#endif

#if defined(DOOM_CHUNK_MOVEMENT_TEST) && DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
static u16 chunk_movement_test_tick = 0;

static void configure_chunk_movement_test(void) {
    g_simple_active_chunk = DOOM_CHUNK_START_CHUNK;
    init_runtime_things();
    rc_set_pose_q8(
        (short)DOOM_CHUNK_START_X_Q8,
        (short)DOOM_CHUNK_START_Y_Q8,
        (short)(DOOM_CHUNK_START_DIR_X * 256.0),
        (short)(DOOM_CHUNK_START_DIR_Y * 256.0)
    );
    chunk_movement_test_tick = 0;
}

static u8 chunk_movement_test_pressed(u8 pressed) {
    enum { UP = 0x01, START_DELAY = 60, WALK_TICKS = 70 };
    (void)pressed;
    if (chunk_movement_test_tick < START_DELAY) {
        chunk_movement_test_tick++;
        return 0;
    }
    if (chunk_movement_test_tick < START_DELAY + WALK_TICKS) {
        chunk_movement_test_tick++;
        return UP;
    }
    return 0;
}
#endif

static u8 add_capped_u16(volatile u16 *value, u16 amount, u16 cap) {
    if (*value >= cap) return 0;
    *value = (u16)(*value + amount > cap ? cap : *value + amount);
    return 1;
}

static void update_power_timers(void) {
    if (power_invuln_timer) power_invuln_timer--;
    if (power_invis_timer) power_invis_timer--;
    if (power_radsuit_timer) power_radsuit_timer--;
    if (power_lightamp_timer) power_lightamp_timer--;
}

static u8 apply_pickup(u16 thing_type) {
    u8 key = key_bit_for_thing(thing_type);
    if (key) {
        if (player_keys & key) return 0;
        player_keys |= key;
        shown_keys = 0xFF;
        pickup_message_key = key;
        pickup_message_type = 1;
        pickup_message_timer = 35;
        bonus_flash = 10;
        return 1;
    }

    switch (thing_type) {
    case 8:    /* backpack */
        if (player_has_backpack && player_ammo >= player_max_bullets && player_shells >= player_max_shells && player_rockets >= player_max_rockets && player_cells >= player_max_cells) return 0;
        if (!player_has_backpack) {
            player_has_backpack = 1;
            player_max_bullets = 400;
            player_max_shells = 100;
            player_max_rockets = 100;
            player_max_cells = 600;
        }
        add_capped_u16(&player_ammo, 10, player_max_bullets);
        add_capped_u16(&player_shells, 4, player_max_shells);
        add_capped_u16(&player_rockets, 1, player_max_rockets);
        add_capped_u16(&player_cells, 20, player_max_cells);
        shown_ammo = 0xFFFF;
        pickup_message_type = 3;
        break;
    case 2001: /* shotgun */
        if (player_has_shotgun && player_shells >= player_max_shells) return 0;
        player_has_shotgun = 1;
        current_weapon = WEAPON_SHOTGUN;
        add_capped_u16(&player_shells, 8, player_max_shells);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2002: /* chaingun */
        if (player_has_chaingun && player_ammo >= player_max_bullets) return 0;
        player_has_chaingun = 1;
        current_weapon = WEAPON_CHAINGUN;
        add_capped_u16(&player_ammo, 20, player_max_bullets);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2003: /* rocket launcher */
        if (player_has_rocket_launcher && player_rockets >= player_max_rockets) return 0;
        player_has_rocket_launcher = 1;
        current_weapon = WEAPON_ROCKET;
        add_capped_u16(&player_rockets, 2, player_max_rockets);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2004: /* plasma rifle */
        if (!weapon_asset_available(WEAPON_PLASMA)) {
            if (!add_capped_u16(&player_cells, 40, player_max_cells)) return 0;
            shown_ammo = 0xFFFF;
            pickup_message_type = 3;
            break;
        }
        if (player_has_plasma && player_cells >= player_max_cells) return 0;
        player_has_plasma = 1;
        current_weapon = WEAPON_PLASMA;
        add_capped_u16(&player_cells, 40, player_max_cells);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2005: /* chainsaw */
        if (player_has_chainsaw) return 0;
        player_has_chainsaw = 1;
        current_weapon = WEAPON_CHAINSAW;
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2006: /* BFG 9000 */
        if (!weapon_asset_available(WEAPON_BFG)) {
            if (!add_capped_u16(&player_cells, 40, player_max_cells)) return 0;
            shown_ammo = 0xFFFF;
            pickup_message_type = 3;
            break;
        }
        if (player_has_bfg && player_cells >= player_max_cells) return 0;
        player_has_bfg = 1;
        current_weapon = WEAPON_BFG;
        add_capped_u16(&player_cells, 40, player_max_cells);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = weapon_slot_digit(current_weapon);
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2007: /* clip */
        if (!add_capped_u16(&player_ammo, 10, player_max_bullets)) return 0;
        pickup_message_type = 3;
        break;
    case 2008: /* shells */
        if (!add_capped_u16(&player_shells, 4, player_max_shells)) return 0;
        pickup_message_type = 3;
        break;
    case 2049: /* box of shells */
        if (!add_capped_u16(&player_shells, 20, player_max_shells)) return 0;
        pickup_message_type = 3;
        break;
    case 2010: /* rocket */
        if (!add_capped_u16(&player_rockets, 1, player_max_rockets)) return 0;
        pickup_message_type = 3;
        break;
    case 2047: /* cell charge */
        if (!add_capped_u16(&player_cells, 20, player_max_cells)) return 0;
        pickup_message_type = 3;
        break;
    case 17:   /* cell pack */
        if (!add_capped_u16(&player_cells, 100, player_max_cells)) return 0;
        pickup_message_type = 3;
        break;
    case 2011: /* stimpack */
        if (player_health >= 100) return 0;
        player_health = (u16)(player_health + 10 > 100 ? 100 : player_health + 10);
        pickup_message_type = 4;
        break;
    case 2012: /* medikit */
        if (player_health >= 100) return 0;
        player_health = (u16)(player_health + 25 > 100 ? 100 : player_health + 25);
        pickup_message_type = 4;
        break;
    case 2013: /* supercharge */
        if (player_health >= 200) return 0;
        player_health = (u16)(player_health + 100 > 200 ? 200 : player_health + 100);
        pickup_message_type = 4;
        break;
    case 2014: /* health bonus */
        if (player_health >= 200) return 0;
        if (player_health < 200) player_health++;
        pickup_message_type = 4;
        break;
    case 2015: /* armor bonus */
        if (player_armor >= 200) return 0;
        if (!player_armor_class) player_armor_class = 1;
        if (player_armor < 200) player_armor++;
        pickup_message_type = 5;
        break;
    case 2018: /* green armor */
        if (player_armor >= 100 && player_armor_class) return 0;
        if (player_armor < 100) player_armor = 100;
        player_armor_class = 1;
        pickup_message_type = 5;
        break;
    case 2019: /* blue armor */
        if (player_armor >= 200 && player_armor_class >= 2) return 0;
        if (player_armor < 200) player_armor = 200;
        player_armor_class = 2;
        pickup_message_type = 5;
        break;
    case 2022: /* invulnerability */
        if (power_invuln_timer >= 1050) return 0;
        power_invuln_timer = 1050;
        face_evil_timer = 70;
        pickup_message_type = 5;
        break;
    case 2023: /* berserk */
        if (player_berserk && player_health >= 100) return 0;
        player_berserk = 1;
        if (player_health < 100) player_health = 100;
        face_evil_timer = 70;
        pickup_message_type = 4;
        break;
    case 2024: /* partial invisibility */
        if (power_invis_timer >= 1050) return 0;
        power_invis_timer = 1050;
        pickup_message_type = 5;
        break;
    case 2025: /* radiation suit */
        if (power_radsuit_timer >= 1050) return 0;
        power_radsuit_timer = 1050;
        pickup_message_type = 5;
        break;
    case 2026: /* computer area map */
        if (power_computer_map) return 0;
        power_computer_map = 1;
        if (map_on) start_minimap_redraw();
        pickup_message_type = 3;
        break;
    case 2045: /* light amplification visor */
        if (power_lightamp_timer >= 1050) return 0;
        power_lightamp_timer = 1050;
        pickup_message_type = 5;
        break;
    case 2046: /* box of rockets */
        if (!add_capped_u16(&player_rockets, 5, player_max_rockets)) return 0;
        pickup_message_type = 3;
        break;
    case 2048: /* ammo box */
        if (!add_capped_u16(&player_ammo, 50, player_max_bullets)) return 0;
        pickup_message_type = 3;
        break;
    default:
        return 0;
    }
    pickup_message_timer = 35;
    bonus_flash = 10;
    return 1;
}

static void collect_nearby_pickups(void) {
    int px, py;
    int pcx, pcy;
    enum { PICKUP_RANGE_Q8 = WORLD_Q8(96), PICKUP_RANGE_CELLS = (PICKUP_RANGE_Q8 + 255) >> 8 };
    rc_player_q8(&px, &py);
    pcx = px >> 8;
    pcy = py >> 8;
    for (u16 pi = 0; pi < thing_pickup_count; pi++) {
        int i = thing_pickup_indices[pi];
        short thing_x;
        short thing_y;
        u16 type;
        if (enemy_dead[i]) continue;
        thing_x = thing_x_q8[i];
        thing_y = thing_y_q8[i];
        if (iabs16((thing_x >> 8) - pcx) > PICKUP_RANGE_CELLS) continue;
        if (iabs16((thing_y >> 8) - pcy) > PICKUP_RANGE_CELLS) continue;
        if (!runtime_thing_is_pickup(i)) continue;
        type = runtime_thing_type(i);
        if (iabs16(px - thing_x) <= PICKUP_RANGE_Q8 && iabs16(py - thing_y) <= PICKUP_RANGE_Q8) {
            if (apply_pickup(type)) {
                if (player_items < 999) player_items++;
                enemy_dead[i] = 1;
                redraw_minimap_thing_cell(i);
                hide_enemies();
            }
        }
    }
    for (u8 i = 0; i < 8; i++) {
        short drop_x;
        short drop_y;
        if (!dynamic_drop_active[i]) continue;
        drop_x = dynamic_drop_x_q8[i];
        drop_y = dynamic_drop_y_q8[i];
        if (iabs16((drop_x >> 8) - pcx) > PICKUP_RANGE_CELLS) continue;
        if (iabs16((drop_y >> 8) - pcy) > PICKUP_RANGE_CELLS) continue;
        if (iabs16(px - drop_x) <= PICKUP_RANGE_Q8 && iabs16(py - drop_y) <= PICKUP_RANGE_Q8) {
            if (apply_pickup(dynamic_drop_type[i])) {
                dynamic_drop_active[i] = 0;
                invalidate_background_cache();
                hide_enemies();
            }
        }
    }
}

static void draw_exit_message(void) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, (u16)(FIX_EXIT_BASE + 1));
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, (u16)(FIX_EXIT_BASE + 2));
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, (u16)(FIX_EXIT_BASE + 3));
}

static void draw_dead_message(void) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_DEAD_D);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_DEAD_A);
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, FIX_DEAD_D);
}

static u8 key_glyph_for_bits(u8 key_bits) {
    if (key_bits & 1) return 0; /* blue */
    if (key_bits & 2) return 1; /* red */
    if (key_bits & 4) return 2; /* yellow */
    return 2;
}

static void draw_key_message_for(u8 key_bits) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    u8 key = key_glyph_for_bits(key_bits);
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_KEY_MSG_K);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_KEY_MSG_Y);
    fix_poke((u16)(col + 3), row, (u16)(PAL_HUD_KEY_BASE + key), (u16)(FIX_KEY_BASE + key));
}

static void draw_ammo_message(void) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_DEAD_A);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, FIX_AMMO_O);
}

static void draw_door_message(void) {
    const u16 col = (SCRW / 16) - 1;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_DEAD_D);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_AMMO_O);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, (u16)(FIX_KEY_BASE + 1));
}

static void draw_secret_message(void) {
    const u16 col = (SCRW / 16) - 1;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_SECRET_S);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_SECRET_C);
}

static void draw_med_message(void) {
    const u16 col = (SCRW / 16) - 1;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_DEAD_D);
}

static void draw_armor_message(void) {
    const u16 col = (SCRW / 16) - 1;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_DEAD_A);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, (u16)(FIX_KEY_BASE + 1));
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_AMMO_M);
}

static void draw_weapon_pickup_message(void) {
    const u16 col = SCRW / 16;
    const u16 row = (GAME_H / 16) - 2;
    u8 weapon = pickup_message_weapon ? pickup_message_weapon : 2;
    fix_poke(col, row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + weapon));
}

static void draw_weapon_select_message(void) {
    const u16 col = SCRW / 16;
    const u16 row = (GAME_H / 16) - 2;
    u8 weapon = weapon_message_digit ? weapon_message_digit : weapon_slot_digit(current_weapon);
    fix_poke(col, row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + weapon));
}

static void draw_stat3(u16 col, u16 row, u16 label, u16 value) {
    u16 capped = value > 999 ? 999 : value;
    fix_poke(col, row, PAL_MAP_PLAYER, label);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (capped / 100) % 10));
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (capped / 10) % 10));
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + capped % 10));
}

#ifdef DOOM_FRAME_STATS
enum { FRAME_STATS_COL = 28, FRAME_STATS_ROW = (GAME_H / 16) - 2 };

static void draw_frame_stats_overlay(u8 overruns) {
    fix_poke(FRAME_STATS_COL, FRAME_STATS_ROW, PAL_MAP_PLAYER, FIX_SOLID);
    fix_poke((u16)(FRAME_STATS_COL + 1), FRAME_STATS_ROW, PAL_MAP_PLAYER,
             (u16)(FIX_DIGIT_BASE + (overruns / 10) % 10));
    fix_poke((u16)(FRAME_STATS_COL + 2), FRAME_STATS_ROW, PAL_MAP_PLAYER,
             (u16)(FIX_DIGIT_BASE + overruns % 10));
}

static void update_frame_stats_overlay(u8 frame_overrun) {
    enum { FRAME_STATS_WINDOW = 64 };
    if (frame_overrun && frame_stats_overruns < 99) frame_stats_overruns++;
    if (++frame_stats_frames < FRAME_STATS_WINDOW) return;
    frame_stats_frames = 0;
    if (frame_stats_overruns != frame_stats_shown) {
        draw_frame_stats_overlay(frame_stats_overruns);
        frame_stats_shown = frame_stats_overruns;
    }
    frame_stats_overruns = 0;
}
#endif

#ifdef DOOM_INPUT_DEBUG
static void update_input_debug_overlay(u8 pressed) {
    int px;
    int py;
    rc_player_q8(&px, &py);
    draw_stat3(0, 0, FIX_SOLID, pressed);
#if defined(DOOM_CHUNK_MOVEMENT_TEST) && DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    draw_stat3(0, 1, FIX_AMMO_M, (u16)(px >> 8));
    draw_stat3(0, 2, FIX_KEY_MSG_Y, (u16)(py >> 8));
    draw_stat3(0, 3, FIX_DEAD_D, SIMPLE_ACTIVE_CHUNK);
    return;
#endif
    draw_stat3(0, 1, FIX_AMMO_M, (u16)(px >> 8));
    draw_stat3(0, 2, FIX_KEY_MSG_Y, (u16)(py >> 8));
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    draw_stat3(0, 3, FIX_DEAD_D, SIMPLE_ACTIVE_CHUNK);
#endif
}
#endif

static void draw_fix_map_code(u16 col, u16 row, u8 episode, u8 mission) {
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + episode));
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + mission));
}

static void draw_exit_stats(void) {
    const u16 col = (SCRW / 16) - 2;
    draw_exit_message();
    draw_stat3(col, 12, FIX_KEY_MSG_K, completion_percent(player_kills, level_total_kills));
    draw_stat3(col, 13, (u16)(FIX_EXIT_BASE + 2), completion_percent(player_items, level_total_items));
    draw_stat3(col, 14, FIX_SECRET_S, completion_percent(player_secrets, level_total_secrets));
    if (level_next_episode && level_next_mission) {
        draw_fix_map_code(col, 15, level_next_episode, level_next_mission);
    }
}

static void draw_pickup_message(void) {
    switch (pickup_message_type) {
    case 1:
        draw_key_message_for(pickup_message_key);
        break;
    case 2:
        draw_weapon_pickup_message();
        break;
    case 3:
        draw_ammo_message();
        break;
    case 4:
        draw_med_message();
        break;
    case 5:
        draw_armor_message();
        break;
    default:
        break;
    }
}

static void clear_center_message(void) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    for (u16 i = 0; i < 4; i++) fix_poke((u16)(col + i), row, 0, FIX_BLANK);
}

static void update_center_message(void) {
    if (player_health == 0) {
        draw_dead_message();
    } else if (level_complete) {
        draw_exit_stats();
    } else if (key_message_timer) {
        clear_center_message();
        draw_key_message_for(missing_key_bits);
        key_message_timer--;
        key_message_visible = 1;
    } else if (ammo_message_timer) {
        clear_center_message();
        draw_ammo_message();
        ammo_message_timer--;
        key_message_visible = 1;
    } else if (door_message_timer) {
        clear_center_message();
        draw_door_message();
        door_message_timer--;
        key_message_visible = 1;
    } else if (secret_message_timer) {
        clear_center_message();
        draw_secret_message();
        secret_message_timer--;
        key_message_visible = 1;
    } else if (pickup_message_timer) {
        clear_center_message();
        draw_pickup_message();
        pickup_message_timer--;
        key_message_visible = 1;
    } else if (weapon_message_timer) {
        clear_center_message();
        draw_weapon_select_message();
        weapon_message_timer--;
        key_message_visible = 1;
    } else if (key_message_visible) {
        clear_center_message();
        key_message_visible = 0;
        missing_key_bits = 0;
    }
}

static u8 map_bit_get(const u8 *bits, u16 index) {
    return (bits[index >> 3] & (1 << (index & 7))) ? 1 : 0;
}

static void map_bit_set(u8 *bits, u16 index) {
    bits[index >> 3] |= (u8)(1 << (index & 7));
}

static void check_secret_reached(void) {
    int px, py;
    u16 cell;
    rc_player_q8(&px, &py);
    px >>= 8;
    py >>= 8;
    if (!map_cell_secret(px, py)) return;
    cell = (u16)(py * ACTIVE_MAP_W + px);
    if (map_bit_get(secret_found_bits, cell)) return;
    map_bit_set(secret_found_bits, cell);
    if (player_secrets < 999) player_secrets++;
    secret_message_timer = 70;
}

static void update_floor_damage(void) {
    int px, py;
    u8 damage;
    if (!game_active()) return;
    if (floor_damage_timer) {
        floor_damage_timer--;
        return;
    }
    if (power_radsuit_timer) return;
    rc_player_q8(&px, &py);
    damage = map_cell_damage(px >> 8, py >> 8);
    if (!damage) return;
    player_take_damage(damage);
    floor_damage_timer = 32;
}

static void complete_level_to(u8 next_episode, u8 next_mission) {
    if (level_complete) return;
    level_next_episode = next_episode;
    level_next_mission = next_mission;
    level_complete = 1;
    close_minimap_for_terminal_message();
    hide_enemies();
    draw_exit_message();
}

static void complete_level(void) {
    complete_level_to(DOOM_NEXT_MAP_EPISODE, DOOM_NEXT_MAP_MISSION);
}

static u8 is_e1m8_boss_exit_map(void) {
    return DOOM_MAP_EPISODE == 1 && DOOM_MAP_MISSION == 8;
}

static void check_e1m8_boss_exit(void) {
    u8 bosses = 0;
    if (!is_e1m8_boss_exit_map() || level_complete) return;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (g_runtime_things[i].type != 3003) continue;
        bosses++;
        if (!enemy_dead[i] && thing_type_override[i] == 0) return;
    }
    if (bosses) complete_level();
}

static void check_exit_reached(void) {
    int px, py;
    if (level_complete) return;
    rc_player_q8(&px, &py);
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    {
        int global_px = active_chunk_origin_x_q8() + px;
        int global_py = active_chunk_origin_y_q8() + py;
        for (u16 i = 0; i < DOOM_CHUNK_EXIT_COUNT; i++) {
            const NgChunkExit *exit = &g_chunk_exits[i];
            if (iabs16(global_px - exit->x_q8) <= WORLD_Q8(128)
                && iabs16(global_py - exit->y_q8) <= WORLD_Q8(128)) {
                complete_level_to(exit->next_episode, exit->next_mission);
                return;
            }
        }
    }
#else
    for (u16 i = 0; i < NG_RUNTIME_EXIT_COUNT; i++) {
        const NgRuntimeExit *exit = &g_runtime_exits[i];
        if (iabs16(px - exit->x_q8) <= WORLD_Q8(128) && iabs16(py - exit->y_q8) <= WORLD_Q8(128)) {
            complete_level_to(exit->next_episode, exit->next_mission);
            return;
        }
    }
#endif
}

static int closed_door_at_cell(int cell_x, int cell_y) {
    u8 cell = map_cell_value(cell_x, cell_y);
    if (cell < 2) return -1;
    int door_index = cell - 2;
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    if (door_index < 0 || door_index >= DOOM_CHUNK_DOOR_COUNT) return -1;
    if (g_chunk_door_open[door_index]) return -1;
#else
    if (cell_x < 0 || cell_y < 0 || cell_x >= ACTIVE_MAP_W || cell_y >= ACTIVE_MAP_H) return -1;
    if (door_index < 0 || door_index >= NG_RUNTIME_DOOR_COUNT) return -1;
    if (g_runtime_door_open[door_index]) return -1;
#endif
    return door_index;
}

static int trace_closed_door_in_view(int px, int py, int dir_x, int dir_y) {
    enum { USE_RANGE_Q8 = WORLD_Q8(896), HUGE_DIST_Q8 = 0x3FFFFFFF };
    int map_x = px >> 8;
    int map_y = py >> 8;
    int step_x = dir_x < 0 ? -1 : 1;
    int step_y = dir_y < 0 ? -1 : 1;
    int abs_dir_x = iabs16(dir_x);
    int abs_dir_y = iabs16(dir_y);
    int delta_x = abs_dir_x ? (65536 / abs_dir_x) : HUGE_DIST_Q8;
    int delta_y = abs_dir_y ? (65536 / abs_dir_y) : HUGE_DIST_Q8;
    int side_x;
    int side_y;

    if (abs_dir_x) {
        int next_x = dir_x < 0 ? (map_x << 8) : ((map_x + 1) << 8);
        side_x = ((iabs16(next_x - px)) << 8) / abs_dir_x;
    } else {
        side_x = HUGE_DIST_Q8;
    }

    if (abs_dir_y) {
        int next_y = dir_y < 0 ? (map_y << 8) : ((map_y + 1) << 8);
        side_y = ((iabs16(next_y - py)) << 8) / abs_dir_y;
    } else {
        side_y = HUGE_DIST_Q8;
    }

    for (;;) {
        int hit_dist;
        if (side_x < side_y) {
            hit_dist = side_x;
            side_x += delta_x;
            map_x += step_x;
        } else {
            hit_dist = side_y;
            side_y += delta_y;
            map_y += step_y;
        }

        if (hit_dist > USE_RANGE_Q8) return -1;

        {
            int door_index = closed_door_at_cell(map_x, map_y);
            if (door_index >= 0) return door_index;
        }

        if (map_at(map_x, map_y)) return -1;
    }
}

static int touching_closed_door(int px, int py) {
    enum { TOUCH_Q8 = WORLD_Q8(72) };
    static const signed char probes[5][2] = {
        { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };
    int best = -1;
    int best_score = 0x7FFFFFFF;
    for (u8 i = 0; i < 5; i++) {
        int x = px + probes[i][0] * TOUCH_Q8;
        int y = py + probes[i][1] * TOUCH_Q8;
        int door_index = closed_door_at_cell(x >> 8, y >> 8);
        if (door_index >= 0) {
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
            const NgChunkDoor *door = &g_chunk_doors[door_index];
            int door_x = (int)door->x - (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W;
            int door_y = (int)door->y - (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H;
            int dx = door_x * 256 + 128 - px;
            int dy = door_y * 256 + 128 - py;
#else
            const NgRuntimeDoor *door = &g_runtime_doors[door_index];
            int dx = (int)door->x * 256 + 128 - px;
            int dy = (int)door->y * 256 + 128 - py;
#endif
            int score = iabs16(dx) + iabs16(dy);
            if (score < best_score) {
                best = door_index;
                best_score = score;
            }
        }
    }
    return best;
}

static void mark_runtime_cell_open(int x, int y) {
    if (x < 0 || y < 0 || x >= ACTIVE_MAP_W || y >= ACTIVE_MAP_H) return;
    map_bit_set(g_runtime_cell_open, (u16)(y * ACTIVE_MAP_W + x));
    if (map_on) draw_minimap_source_cell(x, y);
}

static u8 map_cell_runtime_open(int x, int y) {
    if (x < 0 || y < 0 || x >= ACTIVE_MAP_W || y >= ACTIVE_MAP_H) return 0;
    return map_bit_get(g_runtime_cell_open, (u16)(y * ACTIVE_MAP_W + x));
}

static void carve_door_bridge_from_cell(int x, int y) {
    static const signed char dirs[4][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };
    mark_runtime_cell_open(x, y);
    for (u8 d = 0; d < 4; d++) {
        for (u8 step = 1; step <= 4; step++) {
            int nx = x + dirs[d][0] * step;
            int ny = y + dirs[d][1] * step;
            if (nx < 0 || ny < 0 || nx >= ACTIVE_MAP_W || ny >= ACTIVE_MAP_H) break;
            if (map_cell_value(nx, ny) == 0 || map_cell_runtime_open(nx, ny)) {
                for (u8 carve = 1; carve < step; carve++) {
                    mark_runtime_cell_open(x + dirs[d][0] * carve, y + dirs[d][1] * carve);
                }
                break;
            }
        }
    }
}

static void open_lift_index(u8 lift_index) {
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    const NgChunkLift *lift;
    u8 opened = 0;
    if (lift_index >= DOOM_CHUNK_LIFT_COUNT) return;
    if (g_chunk_lift_open[lift_index]) return;
    g_chunk_lift_open[lift_index] = 1;
    lift = &g_chunk_lifts[lift_index];
    for (u16 i = 0; i < lift->cell_count; i++) {
        u16 cell = g_chunk_lift_cells[lift->first_cell + i];
        int global_x = cell % DOOM_CHUNK_GRID_W;
        int global_y = cell / DOOM_CHUNK_GRID_W;
        int local_x = global_x - (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W;
        int local_y = global_y - (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H;
        if (local_x >= 0 && local_y >= 0 && local_x < ACTIVE_MAP_W && local_y < ACTIVE_MAP_H) {
            mark_runtime_cell_open(local_x, local_y);
            opened = 1;
        }
    }
    if (opened) {
        door_message_timer = 35;
        monster_path_valid = 0;
        monster_path_player_cell_x = -1;
        monster_path_player_cell_y = -1;
        invalidate_background_cache();
        rc_invalidate_view();
    }
#else
    const NgRuntimeLift *lift;
    u8 opened = 0;
    if (lift_index >= NG_RUNTIME_LIFT_COUNT) return;
    if (g_runtime_lift_open[lift_index]) return;
    g_runtime_lift_open[lift_index] = 1;
    lift = &g_runtime_lifts[lift_index];
    for (u16 i = 0; i < lift->cell_count; i++) {
        u16 cell = g_runtime_lift_cells[lift->first_cell + i];
        int x = cell % ACTIVE_MAP_W;
        int y = cell / ACTIVE_MAP_W;
        mark_runtime_cell_open(x, y);
        opened = 1;
    }
    if (opened) {
        door_message_timer = 35;
        monster_path_valid = 0;
        monster_path_player_cell_x = -1;
        monster_path_player_cell_y = -1;
        invalidate_background_cache();
        rc_invalidate_view();
    }
#endif
}

static u8 try_lift_trigger_cell(int cell_x, int cell_y, u8 require_walk) {
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    int global_x = (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W + cell_x;
    int global_y = (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H + cell_y;
    for (u16 i = 0; i < DOOM_CHUNK_LIFT_TRIGGER_COUNT; i++) {
        const NgChunkLiftTrigger *trigger = &g_chunk_lift_triggers[i];
        if (require_walk && !trigger->walk) continue;
        if (iabs16((int)trigger->x - global_x) > 1 || iabs16((int)trigger->y - global_y) > 1) continue;
        if (trigger->lift >= DOOM_CHUNK_LIFT_COUNT) continue;
        if (g_chunk_lift_open[trigger->lift]) continue;
        open_lift_index(trigger->lift);
        return 1;
    }
#else
    for (u16 i = 0; i < NG_RUNTIME_LIFT_TRIGGER_COUNT; i++) {
        const NgRuntimeLiftTrigger *trigger = &g_runtime_lift_triggers[i];
        if (require_walk && !trigger->walk) continue;
        if (iabs16((int)trigger->x - cell_x) > 1 || iabs16((int)trigger->y - cell_y) > 1) continue;
        if (trigger->lift >= NG_RUNTIME_LIFT_COUNT) continue;
        if (g_runtime_lift_open[trigger->lift]) continue;
        open_lift_index(trigger->lift);
        return 1;
    }
#endif
    return 0;
}

static void check_lift_walk_triggers(void) {
    int px, py;
    rc_player_cell(&px, &py);
    (void)try_lift_trigger_cell(px, py, 1);
}

static u8 open_nearby_lift(void) {
    int px, py, dir_x, dir_y, plane_x, plane_y;
    int best = -1;
    int best_score = 0x7FFFFFFF;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    (void)plane_x;
    (void)plane_y;

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    for (u16 i = 0; i < DOOM_CHUNK_LIFT_TRIGGER_COUNT; i++) {
        const NgChunkLiftTrigger *trigger = &g_chunk_lift_triggers[i];
        int local_x;
        int local_y;
        int to_x;
        int to_y;
        int adx;
        int ady;
        int dist;
        int dot;
        int lateral;
        if (trigger->lift >= DOOM_CHUNK_LIFT_COUNT) continue;
        if (g_chunk_lift_open[trigger->lift]) continue;
        local_x = (int)trigger->x - (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W;
        local_y = (int)trigger->y - (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H;
        to_x = local_x * 256 + 128 - px;
        to_y = local_y * 256 + 128 - py;
        adx = iabs16(to_x);
        ady = iabs16(to_y);
        dist = adx + ady;
        dot = to_x * dir_x + to_y * dir_y;
        lateral = iabs16(to_x * dir_y - to_y * dir_x);
        if (adx > WORLD_Q8(640) || ady > WORLD_Q8(640) || dist > WORLD_Q8(960)) continue;
        if (dot <= -WORLD_Q8(128) || lateral > (dot > 0 ? dot * 3 : WORLD_Q8(384))) continue;
        if (dist < best_score) {
            best = i;
            best_score = dist;
        }
    }
    if (best < 0) return 0;
    open_lift_index(g_chunk_lift_triggers[best].lift);
#else
    for (u16 i = 0; i < NG_RUNTIME_LIFT_TRIGGER_COUNT; i++) {
        const NgRuntimeLiftTrigger *trigger = &g_runtime_lift_triggers[i];
        int to_x;
        int to_y;
        int adx;
        int ady;
        int dist;
        int dot;
        int lateral;
        if (trigger->lift >= NG_RUNTIME_LIFT_COUNT) continue;
        if (g_runtime_lift_open[trigger->lift]) continue;
        to_x = (int)trigger->x * 256 + 128 - px;
        to_y = (int)trigger->y * 256 + 128 - py;
        adx = iabs16(to_x);
        ady = iabs16(to_y);
        dist = adx + ady;
        dot = to_x * dir_x + to_y * dir_y;
        lateral = iabs16(to_x * dir_y - to_y * dir_x);
        if (adx > WORLD_Q8(640) || ady > WORLD_Q8(640) || dist > WORLD_Q8(960)) continue;
        if (dot <= -WORLD_Q8(128) || lateral > (dot > 0 ? dot * 3 : WORLD_Q8(384))) continue;
        if (dist < best_score) {
            best = i;
            best_score = dist;
        }
    }
    if (best < 0) return 0;
    open_lift_index(g_runtime_lift_triggers[best].lift);
#endif
    return 1;
}

static void open_door_index(u16 door_index) {
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    const NgChunkDoor *door;
    u8 in_group[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
    u8 opened = 0;
    u8 required_key;
    if (door_index >= DOOM_CHUNK_DOOR_COUNT) return;
    door = &g_chunk_doors[door_index];
    required_key = key_bit_for_door(door->special);
    if (required_key && (player_keys & required_key) == 0) {
        missing_key_bits = required_key;
        key_message_timer = 60;
        return;
    }

    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) in_group[i] = 0;
    in_group[door_index] = 1;
    for (;;) {
        u8 changed = 0;
        for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) {
            if (in_group[i]) continue;
            if (g_chunk_doors[i].special != door->special) continue;
            for (u16 j = 0; j < DOOM_CHUNK_DOOR_COUNT; j++) {
                int dx;
                int dy;
                if (!in_group[j]) continue;
                dx = (int)g_chunk_doors[i].x - (int)g_chunk_doors[j].x;
                dy = (int)g_chunk_doors[i].y - (int)g_chunk_doors[j].y;
                if (iabs16(dx) + iabs16(dy) == 1) {
                    in_group[i] = 1;
                    changed = 1;
                    break;
                }
            }
        }
        if (!changed) break;
    }

    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) {
        int local_x;
        int local_y;
        if (!in_group[i]) continue;
        if (!g_chunk_door_open[i]) opened = 1;
        g_chunk_door_open[i] = 1;
        local_x = (int)g_chunk_doors[i].x - (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W;
        local_y = (int)g_chunk_doors[i].y - (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H;
        if (local_x >= 0 && local_y >= 0 && local_x < ACTIVE_MAP_W && local_y < ACTIVE_MAP_H) {
            carve_door_bridge_from_cell(local_x, local_y);
            if (map_on) draw_minimap_source_cell(local_x, local_y);
        }
    }
    if (opened) {
        door_message_timer = 35;
        monster_path_valid = 0;
        monster_path_player_cell_x = -1;
        monster_path_player_cell_y = -1;
        invalidate_background_cache();
        rc_invalidate_view();
    }
#else
    const NgRuntimeDoor *door = &g_runtime_doors[door_index];
    u8 required_key = key_bit_for_door(door->special);
    u8 in_group[NG_RUNTIME_DOOR_COUNT];
    u8 opened = 0;
    if (required_key && (player_keys & required_key) == 0) {
        missing_key_bits = required_key;
        key_message_timer = 60;
        return;
    }

    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) in_group[i] = 0;
    in_group[door_index] = 1;
    for (;;) {
        u8 changed = 0;
        for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
            if (in_group[i]) continue;
            if (g_runtime_doors[i].special != door->special) continue;
            for (u16 j = 0; j < NG_RUNTIME_DOOR_COUNT; j++) {
                int dx;
                int dy;
                if (!in_group[j]) continue;
                dx = (int)g_runtime_doors[i].x - (int)g_runtime_doors[j].x;
                dy = (int)g_runtime_doors[i].y - (int)g_runtime_doors[j].y;
                if (iabs16(dx) + iabs16(dy) == 1) {
                    in_group[i] = 1;
                    changed = 1;
                    break;
                }
            }
        }
        if (!changed) break;
    }

    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        if (!in_group[i]) continue;
        if (!g_runtime_door_open[i]) opened = 1;
        g_runtime_door_open[i] = 1;
        carve_door_bridge_from_cell(g_runtime_doors[i].x, g_runtime_doors[i].y);
        if (map_on) draw_minimap_source_cell(g_runtime_doors[i].x, g_runtime_doors[i].y);
    }
    if (opened) {
        door_message_timer = 35;
        monster_path_valid = 0;
        monster_path_player_cell_x = -1;
        monster_path_player_cell_y = -1;
        rc_invalidate_view();
    }
#endif
}

static void open_nearby_door(void) {
    int px, py, dir_x, dir_y, plane_x, plane_y;
    int best = -1;
    int best_score = 0x7FFFFFFF;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);

    {
        int door_index = touching_closed_door(px, py);
        if (door_index >= 0) {
            open_door_index((u16)door_index);
            return;
        }
    }

    {
        int door_index = trace_closed_door_in_view(px, py, dir_x, dir_y);
        if (door_index >= 0) {
            open_door_index((u16)door_index);
            return;
        }
    }

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) {
        const NgChunkDoor *door = &g_chunk_doors[i];
        int local_x = (int)door->x - (int)(SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W;
        int local_y = (int)door->y - (int)(SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H;
        int to_x = local_x * 256 + 128 - px;
        int to_y = local_y * 256 + 128 - py;
        int adx = iabs16(to_x);
        int ady = iabs16(to_y);
        int dist = adx + ady;
        int dot = to_x * dir_x + to_y * dir_y;
        int lateral = iabs16(to_x * dir_y - to_y * dir_x);
        if (g_chunk_door_open[i]) continue;
        if (adx > WORLD_Q8(512) || ady > WORLD_Q8(512) || dist > WORLD_Q8(768)) continue;
        if (dot <= 0 || lateral > dot * 2) continue;
        if (dist < best_score) {
            best = i;
            best_score = dist;
        }
    }
#else
    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        const NgRuntimeDoor *door = &g_runtime_doors[i];
        int to_x = (int)door->x * 256 + 128 - px;
        int to_y = (int)door->y * 256 + 128 - py;
        int adx = iabs16(to_x);
        int ady = iabs16(to_y);
        int dist = adx + ady;
        int dot = to_x * dir_x + to_y * dir_y;
        int lateral = iabs16(to_x * dir_y - to_y * dir_x);
        if (g_runtime_door_open[i]) continue;
        if (adx > WORLD_Q8(512) || ady > WORLD_Q8(512) || dist > WORLD_Q8(768)) continue;
        if (dot <= 0 || lateral > dot * 2) continue;
        if (dist < best_score) {
            best = i;
            best_score = dist;
        }
    }
#endif
    if (best < 0) return;
    open_door_index((u16)best);
}

static void map_cell(int mx, int my, u16 pal, u16 tile) {
    fix_poke((u16)(MAP_FIX_COL + mx), (u16)(MAP_FIX_ROW + my), pal, tile);
}

enum {
    HUD_FIX_TOP_ROW = (GAME_H / 8) + 3,
    HUD_FIX_BOTTOM_ROW = HUD_FIX_TOP_ROW + 1
};

enum {
    HUD_VALUE_Y = GAME_H - 7 + 11,
    HUD_AMMO_X = 16 + 17 - 33,
    HUD_HEALTH_X = 72 + 21 - 41,
    HUD_FRAG_X = 136 + 27 - 54,
    HUD_ARMOR_X = 192 + 8 - 16
};

static void clear_fix_area(u16 col, u16 row, u16 cols, u16 rows) {
    for (u16 y = 0; y < rows; y++) {
        for (u16 x = 0; x < cols; x++) {
            fix_poke((u16)(col + x), (u16)(row + y), 0, FIX_BLANK);
        }
    }
}

static void render_hud_value(u16 base_spr, int base_x, u16 value, u8 digits_count, u16 pal) {
    u8 digits[3] = { 0, 0, 0 };
    if (digits_count == 2) {
        if (value > 99) value = 99;
        digits[0] = (u8)(value / 10);
        digits[1] = (u8)(value % 10);
    } else {
        if (value > 999) value = 999;
        digits[0] = (u8)(value / 100);
        digits[1] = (u8)((value / 10) % 10);
        digits[2] = (u8)(value % 10);
    }
    for (u8 i = 0; i < 3; i++) {
        u16 spr = (u16)(base_spr + i);
        if (i >= digits_count) {
            scb2(spr, 0x0F, 0x00);
            scb3(spr, SCRH + 32, 0, 1);
            continue;
        }
        scb1_tile(spr, 0, (u16)(TILE_HUD_DIGIT_BASE + digits[i]), pal);
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, HUD_VALUE_Y, 0, 1);
        scb4(spr, (u16)(base_x + i * 16));
    }
}

static void render_counter_digit(u16 spr, u8 digit, int x, int y, u16 pal) {
    scb1_tile(spr, 0, (u16)(TILE_HUD_SMALL_DIGIT_BASE + digit), pal);
    scb2(spr, 0x0F, 0xFF);
    scb3(spr, y, 0, 1);
    scb4(spr, (u16)x);
}

static void hide_counter_digit(u16 spr) {
    scb2(spr, 0x0F, 0x00);
    scb3(spr, SCRH + 32, 0, 1);
}

static void render_counter_value(u16 base_spr, u16 shadow_base_spr, int x, int y, u16 value) {
    u16 capped = value > 999 ? 999 : value;
    u8 digits[3] = {
        (u8)((capped / 100) % 10),
        (u8)((capped / 10) % 10),
        (u8)(capped % 10),
    };
    for (u8 i = 0; i < 3; i++) {
        u16 spr = (u16)(base_spr + i);
        u16 shadow_spr = (u16)(shadow_base_spr + i);
        if ((i == 0 && capped < 100) || (i == 1 && capped < 10)) {
            hide_counter_digit(shadow_spr);
            hide_counter_digit(spr);
            continue;
        }
        render_counter_digit(shadow_spr, digits[i], (int)(x + i * 5 + 1), y + 1, PAL_AMMO_COUNTER_SHADOW);
        render_counter_digit(spr, digits[i], (int)(x + i * 5), y, PAL_AMMO_COUNTER);
    }
}

static void render_ammo_counters(void) {
    static const u8 row_y[4] = {195, 202, 209, 216};
    const u16 current_values[4] = {player_ammo, player_shells, player_rockets, player_cells};
    const u16 max_values[4] = {player_max_bullets, player_max_shells, player_max_rockets, player_max_cells};
    for (u8 row = 0; row < 4; row++) {
        if (current_values[row] != shown_counter_current[row]) {
            render_counter_value((u16)(HUD_COUNTER_BASE + row * 6), (u16)(HUD_COUNTER_SHADOW_BASE + row * 6), 270, row_y[row], current_values[row]);
            shown_counter_current[row] = current_values[row];
        }
        if (max_values[row] != shown_counter_max[row]) {
            render_counter_value((u16)(HUD_COUNTER_BASE + row * 6 + 3), (u16)(HUD_COUNTER_SHADOW_BASE + row * 6 + 3), 298, row_y[row], max_values[row]);
            shown_counter_max[row] = max_values[row];
        }
    }
}

static u8 face_frame_for_health(void);
static void set_hud_face_frame(u8 frame);
static void update_hud_face(u8 pressed);

static u16 weapon_ammo(void) {
    if (current_weapon == WEAPON_FIST || current_weapon == WEAPON_CHAINSAW) return 0;
    if (current_weapon == WEAPON_SHOTGUN && player_has_shotgun) return player_shells;
    if (current_weapon == WEAPON_ROCKET && player_has_rocket_launcher) return player_rockets;
    if ((current_weapon == WEAPON_PLASMA && player_has_plasma) || (current_weapon == WEAPON_BFG && player_has_bfg)) return player_cells;
    return player_ammo;
}

static u16 weapon_status_bits(void) {
    u16 bits = 0;
    if (player_has_weapon(WEAPON_PISTOL)) bits |= (1 << WEAPON_PISTOL);
    if (player_has_weapon(WEAPON_SHOTGUN)) bits |= (1 << WEAPON_SHOTGUN);
    if (player_has_weapon(WEAPON_CHAINGUN)) bits |= (1 << WEAPON_CHAINGUN);
    if (player_has_weapon(WEAPON_ROCKET)) bits |= (1 << WEAPON_ROCKET);
    if (player_has_weapon(WEAPON_PLASMA)) bits |= (1 << WEAPON_PLASMA);
    if (player_has_weapon(WEAPON_BFG)) bits |= (1 << WEAPON_BFG);
    if (player_has_weapon(WEAPON_FIST)) bits |= (1 << WEAPON_FIST);
    if (player_has_weapon(WEAPON_CHAINSAW)) bits |= (1 << WEAPON_CHAINSAW);
    return (u16)(bits | (current_weapon << 8));
}

static void load_hud_key_palette(u16 key) {
    for (int i = 0; i < HUD_KEY_PALETTE_COLORS; i++) {
        u8 r = g_hud_key_palette_rgb[i][0];
        u8 g = g_hud_key_palette_rgb[i][1];
        u8 b = g_hud_key_palette_rgb[i][2];
        pal_set((u16)(PAL_HUD_KEY_BASE + key), (u16)(i + 1), RGB(r, g, b));
    }
}

static void render_hud_keys(void) {
    static const u8 key_bits[HUD_KEY_COUNT] = {1, 2, 4};
    static const u8 key_row[HUD_KEY_COUNT] = {24, 25, 26};
    static const u8 key_col = 30;

    for (u16 key = 0; key < HUD_KEY_COUNT; key++) {
        u16 spr = (u16)(HUD_KEY_BASE + key);
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
        scb4(spr, 0);
        fix_poke(key_col, key_row[key], 0, FIX_BLANK);
        if (!(player_keys & key_bits[key])) continue;

        load_hud_key_palette(key);
        scb1_tile(spr, 0, (u16)(TILE_HUD_KEYCARD_BASE + key), (u16)(PAL_HUD_KEY_BASE + key));
        scb2(spr, 0x07, 0x7F);
        scb3(spr, (int)(key_row[key] * 8), 0, 1);
        scb4(spr, (u16)(key_col * 8));
    }
    shown_keys = player_keys;
}

static void draw_weapon_status(u16 bits) {
    static const u8 arms_weapons[6] = {
        WEAPON_PISTOL, WEAPON_SHOTGUN, WEAPON_CHAINGUN,
        WEAPON_ROCKET, WEAPON_PLASMA, WEAPON_BFG
    };
    static const u8 arms_cols[3] = {11, 13, 15};

    for (u8 col = 11; col < 20; col++) {
        fix_poke(col, HUD_FIX_TOP_ROW, 0, FIX_BLANK);
        fix_poke(col, HUD_FIX_BOTTOM_ROW, 0, FIX_BLANK);
    }

    for (u8 i = 0; i < 6; i++) {
        u8 weapon = arms_weapons[i];
        u8 row = (i < 3) ? HUD_FIX_TOP_ROW : HUD_FIX_BOTTOM_ROW;
        u8 col = arms_cols[i % 3];
        u16 pal = (bits & (1 << weapon)) ? PAL_HUD : PAL_MAP_WALL;
        if (weapon == current_weapon) pal = PAL_HUD;
        fix_poke(col, row, pal, (u16)(FIX_DIGIT_BASE + i + 2));
    }
    shown_weapon_status = bits;
}

static void update_status_numbers(u8 pressed) {
    u16 health = player_health;
    u16 ammo = weapon_ammo();
    u16 armor = player_armor;
    u16 frags = player_kills;
    u16 weapon_bits = weapon_status_bits();

    if (health != shown_health) {
        render_hud_value((u16)(HUD_VALUE_BASE + 3), HUD_HEALTH_X, health, 3, PAL_HUD);
        shown_health = health;
    }
    update_hud_face(pressed);
    if (ammo != shown_ammo) {
        render_hud_value(HUD_VALUE_BASE, HUD_AMMO_X, ammo, 3, PAL_HUD);
        shown_ammo = ammo;
    }
    if (frags != shown_frags) {
        render_hud_value((u16)(HUD_VALUE_BASE + 6), HUD_FRAG_X, frags, 2, PAL_HUD);
        shown_frags = frags;
    }
    if (armor_flash_timer) {
        render_hud_value((u16)(HUD_VALUE_BASE + 8), HUD_ARMOR_X, armor, 3, PAL_HUD);
        shown_armor = 0xFFFF;
        armor_flash_timer--;
    } else if (armor != shown_armor) {
        render_hud_value((u16)(HUD_VALUE_BASE + 8), HUD_ARMOR_X, armor, 3, PAL_HUD);
        shown_armor = armor;
    }
    if (player_keys != shown_keys) render_hud_keys();
    render_ammo_counters();
    if (weapon_bits != shown_weapon_status) draw_weapon_status(weapon_bits);
}

static void clear_crosshair(void) {
    fix_poke(SCRW / 16, HORIZON / 8, 0, FIX_BLANK);
}

static void force_fix_hud_redraw(void) {
    shown_health = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_armor = 0xFFFF;
    shown_frags = 0xFFFF;
    for (u8 i = 0; i < 4; i++) {
        shown_counter_current[i] = 0xFFFF;
        shown_counter_max[i] = 0xFFFF;
    }
    shown_keys = 0xFF;
    shown_weapon_status = 0xFFFF;
    hud_face_frame = 0xFF;
    update_status_numbers(0);
    clear_crosshair();
#ifdef DOOM_FRAME_STATS
    draw_frame_stats_overlay(0);
    frame_stats_shown = 0;
#endif
    update_center_message();
}

enum {
    MINIMAP_W = 38,
    MINIMAP_H = 23
};

static int minimap_view_x(int map_x) {
    if (map_x < 0) return 0;
    if (map_x >= ACTIVE_MAP_W) return MINIMAP_W - 1;
    return (map_x * MINIMAP_W) / ACTIVE_MAP_W;
}

static int minimap_view_y(int map_y) {
    if (map_y < 0) return 0;
    if (map_y >= ACTIVE_MAP_H) return MINIMAP_H - 1;
    return (map_y * MINIMAP_H) / ACTIVE_MAP_H;
}

static int minimap_src_x0(int view_x) {
    return (view_x * ACTIVE_MAP_W) / MINIMAP_W;
}

static int minimap_src_x1(int view_x) {
    return (((view_x + 1) * ACTIVE_MAP_W) + MINIMAP_W - 1) / MINIMAP_W;
}

static int minimap_src_y0(int view_y) {
    return (view_y * ACTIVE_MAP_H) / MINIMAP_H;
}

static int minimap_src_y1(int view_y) {
    return (((view_y + 1) * ACTIVE_MAP_H) + MINIMAP_H - 1) / MINIMAP_H;
}

static void draw_minimap_source_cell(int map_x, int map_y);

static u8 minimap_has_closed_door(int vx, int vy) {
    int x0 = minimap_src_x0(vx);
    int x1 = minimap_src_x1(vx);
    int y0 = minimap_src_y0(vy);
    int y1 = minimap_src_y1(vy);
    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        int x;
        int y;
        if (g_runtime_door_open[i]) continue;
        x = g_runtime_doors[i].x;
        y = g_runtime_doors[i].y;
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return 1;
    }
    return 0;
}

static u8 minimap_has_exit(int vx, int vy) {
    int x0 = minimap_src_x0(vx);
    int x1 = minimap_src_x1(vx);
    int y0 = minimap_src_y0(vy);
    int y1 = minimap_src_y1(vy);
    for (u16 i = 0; i < NG_RUNTIME_EXIT_COUNT; i++) {
        int x = g_runtime_exits[i].x_q8 >> 8;
        int y = g_runtime_exits[i].y_q8 >> 8;
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return 1;
    }
    return 0;
}

static u8 minimap_has_pickup(int vx, int vy) {
    int x0 = minimap_src_x0(vx);
    int x1 = minimap_src_x1(vx);
    int y0 = minimap_src_y0(vy);
    int y1 = minimap_src_y1(vy);
    for (u16 pi = 0; pi < thing_pickup_count; pi++) {
        int i = thing_pickup_indices[pi];
        int x;
        int y;
        if (enemy_dead[i]) continue;
        x = thing_x_q8[i] >> 8;
        y = thing_y_q8[i] >> 8;
        if (x < x0 || x >= x1 || y < y0 || y >= y1) continue;
        if (!runtime_thing_is_pickup(i)) continue;
        return 1;
    }
    for (u8 i = 0; i < 8; i++) {
        int x;
        int y;
        if (!dynamic_drop_active[i]) continue;
        x = dynamic_drop_x_q8[i] >> 8;
        y = dynamic_drop_y_q8[i] >> 8;
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return 1;
    }
    return 0;
}

static u8 minimap_has_threat(int vx, int vy) {
    int x0 = minimap_src_x0(vx);
    int x1 = minimap_src_x1(vx);
    int y0 = minimap_src_y0(vy);
    int y1 = minimap_src_y1(vy);
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        int x;
        int y;
        if (enemy_dead[i]) continue;
        x = thing_x_q8[i] >> 8;
        y = thing_y_q8[i] >> 8;
        if (x < x0 || x >= x1 || y < y0 || y >= y1) continue;
        if (!runtime_thing_is_threat(i)) continue;
        return 1;
    }
    return 0;
}

static u8 minimap_view_has_wall(int vx, int vy) {
    int x0 = minimap_src_x0(vx);
    int x1 = minimap_src_x1(vx);
    int y0 = minimap_src_y0(vy);
    int y1 = minimap_src_y1(vy);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (map_at(x, y)) return 1;
    return 0;
}

static void draw_minimap_cell(int vx, int vy) {
    if (minimap_has_threat(vx, vy)) {
        map_cell(vx, vy, PAL_MAP_PLAYER, FIX_DEAD_A);
    } else if (minimap_has_pickup(vx, vy)) {
        map_cell(vx, vy, PAL_HUD, (u16)(FIX_DIGIT_BASE + 2));
    } else if (minimap_has_exit(vx, vy)) {
        map_cell(vx, vy, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    } else if (minimap_has_closed_door(vx, vy)) {
        map_cell(vx, vy, PAL_HUD, FIX_DEAD_D);
    } else if (minimap_view_has_wall(vx, vy)) {
        map_cell(vx, vy, PAL_MAP_WALL, FIX_SOLID);
    } else {
        map_cell(vx, vy, 0, FIX_BLANK);
    }
}

static void draw_minimap_source_cell(int map_x, int map_y) {
    draw_minimap_cell(minimap_view_x(map_x), minimap_view_y(map_y));
}

static void redraw_minimap_thing_cell(int thing_index) {
    if (!map_on || thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return;
    draw_minimap_source_cell(thing_x_q8[thing_index] >> 8, thing_y_q8[thing_index] >> 8);
    prev_px = -1;
}

static void set_runtime_thing_position(int thing_index, short x_q8, short y_q8) {
    short old_x;
    short old_y;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return;
    old_x = thing_x_q8[thing_index];
    old_y = thing_y_q8[thing_index];
    if (old_x == x_q8 && old_y == y_q8) return;
    thing_x_q8[thing_index] = x_q8;
    thing_y_q8[thing_index] = y_q8;
    if (map_on) {
        draw_minimap_source_cell(old_x >> 8, old_y >> 8);
        draw_minimap_source_cell(x_q8 >> 8, y_q8 >> 8);
        prev_px = -1;
    }
}

static void draw_minimap(void) {
    for (int my = 0; my < MINIMAP_H; my++)
        for (int mx = 0; mx < MINIMAP_W; mx++)
            draw_minimap_cell(mx, my);
    minimap_redraw_active = 0;
}

static void start_minimap_redraw(void) {
    minimap_redraw_index = 0;
    minimap_redraw_active = 1;
    minimap_clear_active = 0;
    prev_px = -1;
}

static void update_minimap_redraw(void) {
    enum { MINIMAP_CELLS_PER_FRAME = 64 };
    u8 budget = MINIMAP_CELLS_PER_FRAME;
    if (!map_on || !minimap_redraw_active) return;
    while (budget && minimap_redraw_index < (u16)(MINIMAP_W * MINIMAP_H)) {
        u16 index = minimap_redraw_index++;
        draw_minimap_cell(index % MINIMAP_W, index / MINIMAP_W);
        budget--;
    }
    if (minimap_redraw_index >= (u16)(MINIMAP_W * MINIMAP_H)) minimap_redraw_active = 0;
}

/* blank just the minimap's fix region */
static void clear_minimap_now(void) {
    minimap_redraw_active = 0;
    minimap_clear_active = 0;
    for (int my = 0; my < MINIMAP_H; my++)
        for (int mx = 0; mx < MINIMAP_W; mx++)
            map_cell(mx, my, 0, FIX_BLANK);
}

static void start_minimap_clear(void) {
    map_on = 0;
    minimap_redraw_active = 0;
    minimap_clear_index = 0;
    minimap_clear_active = 1;
    prev_px = -1;
}

static void update_minimap_clear(void) {
    enum { MINIMAP_CLEAR_CELLS_PER_FRAME = 96 };
    u8 budget = MINIMAP_CLEAR_CELLS_PER_FRAME;
    if (!minimap_clear_active) return;
    while (budget && minimap_clear_index < (u16)(MINIMAP_W * MINIMAP_H)) {
        u16 index = minimap_clear_index++;
        map_cell(index % MINIMAP_W, index / MINIMAP_W, 0, FIX_BLANK);
        budget--;
    }
    if (minimap_clear_index >= (u16)(MINIMAP_W * MINIMAP_H)) minimap_clear_active = 0;
}

static void close_minimap_for_terminal_message(void) {
    if (!map_on && !minimap_redraw_active && !minimap_clear_active) return;
    map_on = 0;
    clear_minimap_now();
    prev_px = -1;
}

/* restore the cell the marker was on, then mark the new one */
static void update_marker(void) {
    int px, py;
    if (!map_on) return;
    rc_player_cell(&px, &py);
    px = minimap_view_x(px);
    py = minimap_view_y(py);
    if (px == prev_px && py == prev_py && !minimap_redraw_active) return;
    if (prev_px >= 0) {                 /* repaint old cell as its map content */
        draw_minimap_cell(prev_px, prev_py);
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

/* ---- floor/ceiling backdrop: BG_COUNT full-width columns ---------------
 * Use the offline perspective flat cache as a coarse Neo Geo substitute for
 * Doom's software spans. Direction and coarse player position select tile
 * columns so the planes move with the camera without runtime floor casting.
 */
static u8 plane_direction_bucket(int dir_x, int dir_y) {
    static const short dirs[16][2] = {
        { 256,    0}, { 237,   98}, { 181,  181}, {  98,  237},
        {   0,  256}, { -98,  237}, {-181,  181}, {-237,   98},
        {-256,    0}, {-237,  -98}, {-181, -181}, { -98, -237},
        {   0, -256}, {  98, -237}, { 181, -181}, { 237,  -98},
    };
    long best_dot = -2147483647L;
    u8 best = 0;
    for (u8 i = 0; i < 16; i++) {
        long dot = (long)dir_x * dirs[i][0] + (long)dir_y * dirs[i][1];
        if (dot > best_dot) {
            best_dot = dot;
            best = i;
        }
    }
    return (u8)(((u16)best * TILE_PLANE_PERSPECTIVE_DIRS) >> 4);
}

static u8 wrap_background_scroll(int scroll) {
    while (scroll < 0) scroll += BG_COUNT * 8;
    while (scroll >= BG_COUNT * 8) scroll -= BG_COUNT * 8;
    while (scroll >= BG_COUNT * 4) scroll -= BG_COUNT * 4;
    while (scroll >= BG_COUNT) scroll -= BG_COUNT;
    return (u8)scroll;
}

static void init_background(void) {
    for (u16 i = 0; i < BG_COUNT; i++) {
        u16 spr = BG_BASE + i;
        for (u16 t = 0; t < BG_WIN; t++) {
            u16 pal = (t < BG_SPLIT)
                ? (u16)(PAL_CEILING_GRAD_BASE + t)
                : (u16)(PAL_FLOOR_GRAD_BASE + (t - BG_SPLIT));
            scb1_tile(spr, t, TILE_SOLID, pal);
        }
        scb2(spr, 0x0F, 0xFF);        /* full size, no shrink (16-tile ref)  */
        scb3(spr, 0, 0, BG_WIN);      /* top of screen                       */
        scb4(spr, i * 16);
    }
    invalidate_background_cache();
}

static void set_background_column_visible(u16 col, u8 visible) {
    u16 spr = BG_BASE + col;
    if (visible) {
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, 0, 0, BG_WIN);
    } else {
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
    }
}

static void write_background_column_tiles(u16 col, u8 scroll_col, u16 ceiling_direction_base, u16 floor_direction_base) {
    u16 spr = BG_BASE + col;
    u16 plane_col = (u16)(col + scroll_col);
    u16 ceiling_tile;
    u16 floor_tile;

    if (plane_col >= BG_COUNT) plane_col = (u16)(plane_col - BG_COUNT);
    ceiling_tile = (u16)(ceiling_direction_base + plane_col);
    floor_tile = (u16)(floor_direction_base + plane_col);

    vram_addr(VRAM_SCB1 + spr * 64);
    vram_mod(1);
    for (u16 row = 0; row < BG_WIN; row++) {
        u16 pal;
        u16 tile;
        if (row < BG_SPLIT) {
            pal = (u16)(PAL_CEILING_GRAD_BASE + row);
            tile = ceiling_tile;
            ceiling_tile = (u16)(ceiling_tile + TILE_PLANE_PERSPECTIVE_COLS);
        } else {
            u16 floor_row = (u16)(row - BG_SPLIT);
            pal = (u16)(PAL_FLOOR_GRAD_BASE + floor_row);
            tile = floor_tile;
            floor_tile = (u16)(floor_tile + TILE_PLANE_PERSPECTIVE_COLS);
        }
        vram_w(tile);
        vram_w((u16)(pal << 8));
    }
}

static void update_background_scroll(u8 frame_overrun) {
#if DOOM_FLAT_PLANES
    (void)frame_overrun;
    return;
#else
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    u8 direction;
    u8 scroll_col;
    u16 direction_tile_offset;
    u16 ceiling_direction_base;
    u16 floor_direction_base;
    u32 key;
    u8 columns_this_frame = frame_overrun ? BG_SCROLL_COLUMNS_OVERRUN : BG_SCROLL_COLUMNS_PER_FRAME;
    u8 all_current;

    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
    if (dir_x != bg_direction_dir_x || dir_y != bg_direction_dir_y) {
        bg_direction_dir_x = dir_x;
        bg_direction_dir_y = dir_y;
        bg_direction_bucket = plane_direction_bucket(dir_x, dir_y);
    }
    direction = bg_direction_bucket;
    /* Horizontal column wrapping is driven by camera-lateral motion so walking
     * forward does not force unrelated floor/ceiling column shifts. */
    scroll_col = wrap_background_scroll((int)(((long)px * plane_x + (long)py * plane_y) >> 14));
    key = (u32)direction | ((u32)scroll_col << 8);
    if (key != bg_pending_key) {
        bg_pending_key = key;
        bg_update_col = 0;
    }
    direction_tile_offset = (u16)(direction * TILE_PLANE_PERSPECTIVE_PHASES * TILE_PLANE_PERSPECTIVE_PHASES
        * TILE_PLANE_PERSPECTIVE_ROWS * TILE_PLANE_PERSPECTIVE_COLS);
    ceiling_direction_base = (u16)(TILE_CEILING_PERSPECTIVE_BASE + direction_tile_offset);
    floor_direction_base = (u16)(TILE_FLOOR_PERSPECTIVE_BASE + direction_tile_offset);

    for (u16 col = 0; col < BG_COUNT; col++) {
        u8 hidden = rc_background_column_hidden((u8)col);
        if (hidden != bg_col_hidden[col]) {
            set_background_column_visible(col, (u8)!hidden);
            bg_col_hidden[col] = hidden;
            if (!hidden) bg_col_key[col] = 0xFFFFFFFFUL;
        }
        if (!hidden && bg_col_key[col] != key) bg_scroll_key = 0xFFFFFFFFUL;
    }

    if (bg_scroll_key != bg_pending_key) {
        u8 scanned = 0;
        u8 col = bg_update_col;
        while (scanned < BG_COUNT && columns_this_frame) {
            if (!bg_col_hidden[col] && bg_col_key[col] != key) {
                write_background_column_tiles(col, scroll_col, ceiling_direction_base, floor_direction_base);
                bg_col_key[col] = key;
                columns_this_frame--;
            }
            col++;
            if (col >= BG_COUNT) col = 0;
            scanned++;
        }
        bg_update_col = col;
    }

    all_current = 1;
    for (u16 col = 0; col < BG_COUNT; col++) {
        if (!bg_col_hidden[col] && bg_col_key[col] != key) {
            all_current = 0;
            break;
        }
    }
    bg_scroll_key = all_current ? key : 0xFFFFFFFFUL;
#endif
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

static u8 face_health_band(void) {
    if (player_health >= 80) return 0;
    if (player_health >= 60) return 1;
    if (player_health >= 40) return 2;
    if (player_health >= 20) return 3;
    return 4;
}

enum {
    FACE_STRAIGHT_BASE = 0,
    FACE_TURN_RIGHT_BASE = 15,
    FACE_TURN_LEFT_BASE = 20,
    FACE_OUCH_BASE = 25,
    FACE_EVIL_BASE = 30,
    FACE_DEAD_FRAME = 35
};

static u8 face_frame_for_health(void) {
    u8 band = face_health_band();
    if (player_health == 0) return FACE_DEAD_FRAME;
    if (face_pain_timer) return (u8)(FACE_OUCH_BASE + band);
    if (face_evil_timer) return (u8)(FACE_EVIL_BASE + band);
    if (face_turn_timer) return (u8)(face_turn_frame + band);
    return (u8)(FACE_STRAIGHT_BASE + band * 3 + face_idle_variant);
}

static void set_hud_face_frame(u8 frame) {
    if (frame >= TILE_HUD_FACE_FRAMES) frame = 0;
    if (frame == hud_face_frame) return;
    u16 frame_base = (u16)(TILE_HUD_FACE_BASE + frame * TILE_HUD_FACE_FRAME_TILES);
    for (u16 col = 0; col < TILE_HUD_FACE_COLS; col++) {
        u16 spr = HUD_BASE + TILE_HUD_FACE_COL + col;
        for (u16 row = 0; row < TILE_HUD_FACE_ROWS; row++) {
            u16 tile = (u16)(frame_base + row * TILE_HUD_FACE_COLS + col);
            scb1_tile(spr, row, tile, PAL_HUD);
        }
    }
    hud_face_frame = frame;
}

static void update_hud_face(u8 pressed) {
    enum { LEFT = 0x04, RIGHT = 0x08 };
    if (player_health && !face_pain_timer && !face_evil_timer) {
        if (pressed & LEFT) {
            face_turn_frame = FACE_TURN_LEFT_BASE;
            face_turn_timer = 18;
        } else if (pressed & RIGHT) {
            face_turn_frame = FACE_TURN_RIGHT_BASE;
            face_turn_timer = 18;
        } else if (face_idle_tick++ >= 18) {
            face_idle_tick = 0;
            face_idle_variant = (u8)((face_idle_variant + 1) % 3);
        }
    }
    set_hud_face_frame(face_frame_for_health());
    if (face_pain_timer) face_pain_timer--;
    if (face_evil_timer) face_evil_timer--;
    if (face_turn_timer) face_turn_timer--;
}

static void init_hud(void) {
    for (u16 i = 0; i < HUD_COUNT; i++) {
        u16 spr = HUD_BASE + i;
        for (u16 row = 0; row < HUD_WIN; row++) {
            u16 src_row = row;
            u16 tile = (u16)(TILE_HUD_BASE + src_row * HUD_COUNT + i);
            scb1_tile(spr, row, tile, PAL_HUD);
        }
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, GAME_H + HUD_Y_OFFSET, 0, HUD_WIN);
        scb4(spr, i * 16);
    }
    hud_face_frame = 0xFF;
    set_hud_face_frame(face_frame_for_health());
    render_hud_keys();
}

static void set_weapon_frame(u8 frame) {
    if (frame >= TILE_WEAPON_FRAMES) frame = 0;
    u16 frame_base = (u16)(TILE_WEAPON_BASE + frame * TILE_WEAPON_FRAME_TILES);
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

static void toggle_weapon(void) {
    u8 start = current_weapon;
    for (u8 i = 0; i < WEAPON_TOTAL; i++) {
        u8 weapon = (u8)((start + i + 1) % WEAPON_TOTAL);
        if (weapon_has_ammo(weapon)) {
            current_weapon = weapon;
            weapon_frame = 0xFF;
            shown_ammo = 0xFFFF;
            trigger_weapon_message();
            return;
        }
    }
    for (u8 i = 0; i < WEAPON_TOTAL; i++) {
        u8 weapon = (u8)((start + i + 1) % WEAPON_TOTAL);
        if (player_has_weapon(weapon)) {
            current_weapon = weapon;
            break;
        }
    }
    weapon_frame = 0xFF;
    shown_ammo = 0xFFFF;
    trigger_weapon_message();
}

static u8 select_next_weapon_from_list(const u8 *weapons, u8 count, u8 require_ammo) {
    u8 start_index = 0xFF;
    for (u8 i = 0; i < count; i++) {
        if (weapons[i] == current_weapon) {
            start_index = i;
            break;
        }
    }
    for (u8 i = 0; i < count; i++) {
        u8 index = start_index == 0xFF ? i : (u8)((start_index + i + 1) % count);
        u8 weapon = weapons[index];
        if (require_ammo ? weapon_has_ammo(weapon) : player_has_weapon(weapon)) {
            current_weapon = weapon;
            weapon_frame = 0xFF;
            shown_ammo = 0xFFFF;
            trigger_weapon_message();
            return 1;
        }
    }
    return 0;
}

static u8 select_weapon_direct(u8 weapon) {
    if (!weapon_has_ammo(weapon) && !player_has_weapon(weapon)) return 0;
    current_weapon = weapon;
    weapon_frame = 0xFF;
    shown_ammo = 0xFFFF;
    trigger_weapon_message();
    return 1;
}

static void select_weapon_shortcut(u8 direction_bits) {
    static const u8 shotgun_group[] = {WEAPON_SHOTGUN};
    static const u8 rapid_group[] = {WEAPON_CHAINGUN};
    static const u8 heavy_group[] = {WEAPON_ROCKET, WEAPON_PLASMA, WEAPON_BFG};
    static const u8 close_group[] = {WEAPON_CHAINSAW, WEAPON_FIST, WEAPON_PISTOL};
    const u8 *group = shotgun_group;
    u8 count = sizeof(shotgun_group);
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08 };

    if ((direction_bits & DOWN) && (direction_bits & RIGHT)) {
        if (select_weapon_direct(WEAPON_PLASMA)) return;
        group = heavy_group;
        count = sizeof(heavy_group);
    } else if ((direction_bits & DOWN) && (direction_bits & LEFT)) {
        if (select_weapon_direct(WEAPON_BFG)) return;
        group = heavy_group;
        count = sizeof(heavy_group);
    } else if (direction_bits & DOWN) {
        if (select_weapon_direct(WEAPON_ROCKET)) return;
        group = heavy_group;
        count = sizeof(heavy_group);
    } else if (direction_bits & RIGHT) {
        if (select_weapon_direct(WEAPON_CHAINGUN)) return;
        group = rapid_group;
        count = sizeof(rapid_group);
    } else if (direction_bits & LEFT) {
        group = close_group;
        count = sizeof(close_group);
    } else if (direction_bits & UP) {
        if (select_weapon_direct(WEAPON_SHOTGUN)) return;
        group = shotgun_group;
        count = sizeof(shotgun_group);
    }

    if (!select_next_weapon_from_list(group, count, 1)) {
        select_next_weapon_from_list(group, count, 0);
    }
}

static void set_weapon_position(signed char bob_x, signed char bob_y) {
    u16 start_x = (u16)((SCRW - WEAPON_COUNT * 16) / 2);
    int top = GAME_H - WEAPON_WIN * 16 + WEAPON_Y_OFFSET + bob_y;
    if (bob_x == weapon_bob_x && bob_y == weapon_bob_y) return;
    for (u16 i = 0; i < WEAPON_COUNT; i++) {
        u16 spr = WEAPON_BASE + i;
        scb3(spr, top, 0, WEAPON_WIN);
        scb4(spr, (u16)(start_x + i * 16 + bob_x));
    }
    weapon_bob_x = bob_x;
    weapon_bob_y = bob_y;
}

static void update_weapon_bob(u8 pressed) {
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, A = 0x10 };
    static const signed char bx[8] = { 0, 1, 1, 0, 0, -1, -1, 0 };
    static const signed char by[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
    u8 moving = (pressed & (UP | DOWN)) || ((pressed & A) && (pressed & (LEFT | RIGHT)));
    if (moving) {
        weapon_bob_phase = (u8)((weapon_bob_phase + 1) & 7);
        set_weapon_position(bx[weapon_bob_phase], by[weapon_bob_phase]);
    } else {
        weapon_bob_phase = 0;
        set_weapon_position(0, 0);
    }
}

static void update_weapon(u8 pressed) {
    enum { B = 0x20 };
    u8 b_now = pressed & B;
    if (!weapon_asset_available(current_weapon)) switch_to_ready_weapon();
    if (b_now && fire_timer == 0) {
        if (!switch_to_ready_weapon()) {
            if (!fire_prev) ammo_message_timer = 45;
        } else if (current_weapon == WEAPON_CHAINSAW && player_has_chainsaw) {
            fire_timer = 4;
            trigger_weapon_flash();
            alert_monsters_by_sound();
            fire_melee_damage(2);
        } else if (!fire_prev && current_weapon == WEAPON_FIST) {
            fire_timer = 12;
            trigger_weapon_flash();
            alert_monsters_by_sound();
            fire_melee_damage(player_berserk ? 12 : 2);
        } else if (current_weapon == WEAPON_CHAINGUN && player_has_chaingun) {
            if (player_ammo > 0) {
                player_ammo--;
                fire_timer = 4;
                chaingun_flash ^= 1;
                trigger_weapon_flash();
                alert_monsters_by_sound();
                fire_single_target_damage(1);
            } else if (!fire_prev) {
                ammo_message_timer = 45;
            }
        } else if (!fire_prev && current_weapon == WEAPON_ROCKET && player_has_rocket_launcher && player_rockets > 0) {
            player_rockets--;
            fire_timer = 20;
            trigger_weapon_flash();
            alert_monsters_by_sound();
            if (!spawn_player_projectile(9008, 24)) {
                damage_rocket_target();
            }
        } else if (current_weapon == WEAPON_PLASMA && player_has_plasma) {
            if (player_cells > 0) {
                player_cells--;
                fire_timer = 5;
                chaingun_flash ^= 1;
                trigger_weapon_flash();
                alert_monsters_by_sound();
                if (!spawn_player_projectile(9006, 16)) {
                    fire_single_target_damage(3);
                }
            } else if (!fire_prev) {
                ammo_message_timer = 45;
            }
        } else if (!fire_prev && current_weapon == WEAPON_BFG && player_has_bfg) {
            if (player_cells >= 40) {
                player_cells = (u16)(player_cells - 40);
                fire_timer = 28;
                trigger_weapon_flash();
                alert_monsters_by_sound();
                if (!spawn_player_projectile(9007, 26)) {
                    spawn_weapon_impact_for_target(best_visible_enemy());
                    damage_bfg_targets();
                }
            } else {
                ammo_message_timer = 45;
            }
        } else if (!fire_prev && current_weapon == WEAPON_SHOTGUN && player_has_shotgun && player_shells > 0) {
            player_shells--;
            fire_timer = 16;
            trigger_weapon_flash();
            alert_monsters_by_sound();
            damage_shotgun_spread();
        } else if (!fire_prev && current_weapon == WEAPON_PISTOL && player_ammo > 0) {
            current_weapon = WEAPON_PISTOL;
            player_ammo--;
            fire_timer = 15;
            trigger_weapon_flash();
            alert_monsters_by_sound();
            fire_single_target_damage(1);
        } else if (!fire_prev) {
            ammo_message_timer = 45;
        }
    }
    fire_prev = b_now;

    u8 frame = current_weapon == WEAPON_ROCKET && player_has_rocket_launcher ? 10
        : (current_weapon == WEAPON_PLASMA && player_has_plasma ? 12
        : (current_weapon == WEAPON_BFG && player_has_bfg ? 16
        : (current_weapon == WEAPON_CHAINSAW && player_has_chainsaw ? 24
        : (current_weapon == WEAPON_FIST ? 20
        : (current_weapon == WEAPON_CHAINGUN && player_has_chaingun ? 8
        : (current_weapon == WEAPON_SHOTGUN && player_has_shotgun ? 4 : 0))))));
    if (fire_timer) {
        if (current_weapon == WEAPON_CHAINSAW && player_has_chainsaw) {
            frame = (u8)(24 + (fire_timer & 3));
        } else if (current_weapon == WEAPON_FIST) {
            if (fire_timer > 8) frame = 21;
            else if (fire_timer > 4) frame = 22;
            else frame = 23;
        } else if (current_weapon == WEAPON_CHAINGUN && player_has_chaingun) {
            frame = (u8)(8 + (chaingun_flash & 1));
        } else if (current_weapon == WEAPON_ROCKET && player_has_rocket_launcher) {
            frame = fire_timer > 10 ? 11 : 10;
        } else if (current_weapon == WEAPON_PLASMA && player_has_plasma) {
            frame = (u8)(12 + (chaingun_flash & 3));
        } else if (current_weapon == WEAPON_BFG && player_has_bfg) {
            if (fire_timer > 18) frame = 17;
            else if (fire_timer > 8) frame = 18;
            else frame = 19;
        } else {
            u8 base = current_weapon == WEAPON_SHOTGUN && player_has_shotgun ? 4 : 0;
            if (fire_timer > 10) frame = (u8)(base + 1);
            else if (fire_timer > 5) frame = (u8)(base + 2);
            else frame = (u8)(base + 3);
        }
        fire_timer--;
    }
    if (frame != weapon_frame) set_weapon_frame(frame);
    update_weapon_bob(fire_timer ? 0 : pressed);
}

static void init_weapon(void) {
    u16 start_x = (u16)((SCRW - WEAPON_COUNT * 16) / 2);
    int top = GAME_H - WEAPON_WIN * 16 + WEAPON_Y_OFFSET;
    for (u16 i = 0; i < WEAPON_COUNT; i++) {
        u16 spr = WEAPON_BASE + i;
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, top, 0, WEAPON_WIN);
        scb4(spr, (u16)(start_x + i * 16));
    }
    weapon_bob_x = 0;
    weapon_bob_y = 0;
    set_weapon_frame(0);
}

static void hide_enemy_slot(u16 slot) {
    u8 already_hidden;
    if (slot >= ENEMY_VISIBLE_COUNT) return;
    already_hidden = enemy_slot_hidden[slot];
    if (!already_hidden) {
        for (u16 i = 0; i < ENEMY_STRIPS; i++) {
            u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + i;
            scb2(spr, 0x0F, 0x00);
            scb3(spr, SCRH + 32, 0, 1);
            scb4(spr, 0);
        }
    }
    enemies[slot].thing_index = -1;
    enemies[slot].thing_type = 0;
    enemies[slot].screen_w = 0;
    enemies[slot].screen_h = 0;
    enemies[slot].fallback_projection = 0;
    enemies[slot].is_monster = 0;
    enemies[slot].shootable = 0;
    enemies[slot].readable = 0;
    enemies[slot].attackable = 0;
    enemies[slot].ranged_attackable = 0;
    enemy_tile_key[slot] = -1;
    enemy_slot_hidden[slot] = 1;
}

static void reset_enemy_slot_cache(void) {
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        enemy_palette_def[slot] = -1;
        enemy_tile_key[slot] = -1;
        enemy_slot_flash[slot] = 0;
        enemy_slot_hidden[slot] = 0;
        ranged_readable_prev[slot] = -1;
        enemies[slot].thing_index = -1;
        enemies[slot].thing_type = 0;
        enemies[slot].is_monster = 0;
        enemies[slot].shootable = 0;
        enemies[slot].readable = 0;
        enemies[slot].attackable = 0;
        enemies[slot].ranged_attackable = 0;
    }
    ranged_readable_prev_count = 0;
}

static void hide_enemies(void) {
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) hide_enemy_slot(slot);
}

static void set_enemy_tiles(u16 slot, const DoomSpriteScale *meta) {
    u16 pal = (u16)(PAL_ENEMY_BASE + slot);
    for (u16 i = 0; i < ENEMY_STRIPS; i++) {
        u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + i;
        vram_addr(VRAM_SCB1 + spr * 64);
        vram_mod(1);
        for (u16 row = 0; row < ENEMY_WIN; row++) {
            if (i < meta->strips && row < meta->rows) {
                vram_w((u16)(meta->tile_base + row * meta->strips + i));
                vram_w((u16)(pal << 8));
            } else {
                vram_w(TILE_BLANK);
                vram_w(0);
            }
        }
    }
}

static int world_sprite_floor_y(int h) {
    int floor_y = (GAME_H + h) / 2;
    if (floor_y < HORIZON + 8) floor_y = HORIZON + 8;
    if (floor_y > GAME_H - 4) floor_y = GAME_H - 4;
    return floor_y;
}

static int world_sprite_origin_y(u16 thing_type, int h) {
    int origin_y = world_sprite_floor_y(h);
    int weapon_top = GAME_H - WEAPON_WIN * 16 + WEAPON_Y_OFFSET;

    if (thing_is_monster(thing_type)) {
        int lift = h < 48 ? 18 : (h < 80 ? 12 : 6);
        origin_y += lift;
        if (origin_y < GAME_H - 70) origin_y = GAME_H - 70;
        if (origin_y > GAME_H - 10) origin_y = GAME_H - 10;
        return origin_y;
    }

    if (thing_is_corpse(thing_type)) return origin_y + 2;
    if (thing_is_pickup(thing_type)) {
        int lift = h < 48 ? 14 : (h < 96 ? 18 : 22);
        if (origin_y > GAME_H - 18) origin_y = GAME_H - 18;
        return origin_y - lift;
    }
    if (thing_is_barrel(thing_type)) return origin_y + 1;

    if (h < 80 && origin_y > weapon_top + 6) origin_y = weapon_top + 6;
    return origin_y;
}

static int projected_floor_screen_offset(short world_x_q8, short world_y_q8, int h, int player_x_q8, int player_y_q8) {
    int player_floor = map_cell_floor_height(player_x_q8 >> 8, player_y_q8 >> 8);
    int thing_floor = map_cell_floor_height(world_x_q8 >> 8, world_y_q8 >> 8);
    int delta = thing_floor - player_floor;
    int offset = (delta * h) / 128;
    if (offset < -GAME_H) offset = -GAME_H;
    if (offset > GAME_H) offset = GAME_H;
    return offset;
}

static u8 render_type_slot(u16 slot, int thing_index, u16 thing_type, short world_x_q8, short world_y_q8,
                           int sx, int h, int dist_q8,
                           u8 flash, u8 fallback_projection, int view_px, int view_py) {
    int idx;
    u8 is_monster = thing_is_monster(thing_type);
    u8 is_shootable = thing_is_shootable(thing_type);
    u8 is_pickup = thing_is_pickup(thing_type);
    u8 is_projectile = thing_is_projectile(thing_type);
    u8 is_explosion = thing_is_explosion(thing_type);
    int def_idx = enemy_sprite_def_for_type(thing_type, thing_index, view_px, view_py);
    const DoomEnemySpriteDef *def;
    const DoomSpriteScale *meta;
    enum { MONSTER_MIN_H = 52, MONSTER_FALLBACK_MIN_H = 52 };

    if (def_idx < 0) {
        hide_enemy_slot(slot);
        return 0;
    }
    def = &g_enemy_sprite_defs[def_idx];

    if (is_monster && h > 0 && h < (fallback_projection ? MONSTER_FALLBACK_MIN_H : MONSTER_MIN_H)) {
        h = fallback_projection ? MONSTER_FALLBACK_MIN_H : MONSTER_MIN_H;
    }
#if DOOM_SIMPLE_MAP
    if (is_pickup && h > 0 && h < 112) h = 112;
#else
    if (is_pickup && h > 0 && h < 64) h = 64;
#endif
    if (thing_is_corpse(thing_type) && h > 0 && h < 36) h = 36;
    if (is_projectile && h > 0 && h < 18) h = 18;
    if (is_explosion && h > 0 && h < 26) h = 26;

    enemies[slot].thing_index = thing_index;
    enemies[slot].sprite_def = def_idx;
    enemies[slot].thing_type = thing_type;
    enemies[slot].dist_q8 = dist_q8;
    enemies[slot].screen_h = h;
    enemies[slot].fallback_projection = fallback_projection;
    enemies[slot].is_monster = is_monster;
    enemies[slot].shootable = is_shootable;
    if (flash || enemy_slot_flash[slot]) load_enemy_hit_palette(slot);
    else load_enemy_palette(slot, def_idx);

    if (h > 116) idx = 0;
    else if (h > 78) idx = 1;
    else if (h > 46) idx = 2;
    else if (h > 24) idx = 3;
    else idx = 4;
    if (is_monster && idx > 1) idx = 1;
#if DOOM_SIMPLE_MAP
    if (is_pickup) idx = 0;
#else
    if (is_pickup && idx > 1) idx = 1;
#endif
    if (is_projectile && idx > 3) idx = 3;
    if (idx >= def->scale_count) idx = def->scale_count - 1;
    meta = &g_enemy_scales[def->first_scale + idx];

    {
        int tile_key = def_idx * 8 + idx;
        if (enemy_tile_key[slot] != tile_key) {
            set_enemy_tiles(slot, meta);
            enemy_tile_key[slot] = tile_key;
        }
    }
    {
        u8 rendered = 0;
        int sprite_x = sx - meta->origin_x;
        int visible_left = SCRW;
        int visible_right = 0;
        int top;
        enemies[slot].screen_x = sprite_x;
        enemies[slot].screen_w = meta->width;
        if ((is_explosion && thing_index < 0) || is_projectile) {
            top = (GAME_H - meta->height) / 2;
        } else {
            top = world_sprite_origin_y(thing_type, h)
                - projected_floor_screen_offset(world_x_q8, world_y_q8, h, view_px, view_py)
                - meta->origin_y + ENEMY_GROUND_LIFT;
        }
        if (top < 0) top = 0;
        for (u16 j = 0; j < ENEMY_STRIPS; j++) {
            u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + j;
            int strip_x = sprite_x + j * 16;
            if (j < meta->strips && strip_x > -16 && strip_x < SCRW
                && (fallback_projection || rc_sprite_strip_visible(strip_x, strip_x + 15, dist_q8))) {
#if DOOM_SIMPLE_MAP
                if (!is_projectile && !is_explosion) {
                    rc_reserve_sprite_budget_for_screen_range(strip_x - 8, strip_x + 23);
                }
#endif
                int strip_left = strip_x < 0 ? 0 : strip_x;
                int strip_right = strip_x + 16;
                if (strip_right > SCRW) strip_right = SCRW;
                scb2(spr, 0x0F, 0xFF);
                scb3(spr, top, 0, meta->rows);
                scb4(spr, (u16)strip_x);
                if (strip_left < visible_left) visible_left = strip_left;
                if (strip_right > visible_right) visible_right = strip_right;
                rendered = 1;
            } else {
                scb2(spr, 0x0F, 0x00);
                scb3(spr, SCRH + 32, 0, 1);
                scb4(spr, 0);
            }
        }
        if (!rendered) {
            hide_enemy_slot(slot);
            return 0;
        }
        if (visible_right > visible_left) {
            enemies[slot].screen_x = visible_left;
            enemies[slot].screen_w = visible_right - visible_left;
        }
        update_enemy_slot_flags(slot);
        enemy_slot_hidden[slot] = 0;
    }
    return 1;
}

typedef struct ThingCandidate {
    int thing_index;
    signed char dynamic_index;
    u16 thing_type;
    short x_q8;
    short y_q8;
    int sx;
    int h;
    int dist_q8;
    u8 fallback_projection;
    int score;
} ThingCandidate;

#define THING_CANDIDATE_COUNT (ENEMY_VISIBLE_COUNT * 3)

static u8 candidate_coord_selected(const ThingCandidate *candidates, int count, short x, short y, u16 thing_type) {
    u8 new_is_pickup = thing_is_pickup(thing_type);
    for (int slot = 0; slot < count; slot++) {
        if (candidates[slot].x_q8 != x || candidates[slot].y_q8 != y) continue;
        if (new_is_pickup != thing_is_pickup(candidates[slot].thing_type)) continue;
        return 1;
    }
    return 0;
}

static void insert_thing_candidate(ThingCandidate *candidates, int *count, const ThingCandidate *candidate) {
    int insert_at = *count;
    while (insert_at > 0 && candidate->score < candidates[insert_at - 1].score) insert_at--;
    if (insert_at >= THING_CANDIDATE_COUNT) return;
    for (int j = THING_CANDIDATE_COUNT - 1; j > insert_at; j--) candidates[j] = candidates[j - 1];
    candidates[insert_at] = *candidate;
    if (*count < THING_CANDIDATE_COUNT) (*count)++;
}

#if DOOM_SIMPLE_MAP
static u8 candidate_is_collectible_pickup(const ThingCandidate *candidate) {
    return thing_is_pickup(candidate->thing_type) && pickup_is_collectible(candidate->thing_type);
}

static u8 candidate_is_threat(const ThingCandidate *candidate) {
    return thing_is_monster(candidate->thing_type) || thing_is_runtime_threat(candidate->thing_type);
}

static void reserve_visible_pickups(ThingCandidate *candidates, int count, int selected) {
    enum { PICKUP_RESERVE_MAX = ENEMY_VISIBLE_COUNT, PICKUP_RESERVE_DIST_Q8 = WORLD_Q8(8192) };
    u8 selected_pickups = 0;

    if (selected <= 0) return;
    for (int i = 0; i < selected; i++) {
        if (candidate_is_collectible_pickup(&candidates[i])) selected_pickups++;
    }
    if (selected_pickups >= PICKUP_RESERVE_MAX) return;

    for (int outside = selected; outside < count && selected_pickups < PICKUP_RESERVE_MAX; outside++) {
        int replace = -1;
        ThingCandidate pickup;
        if (!candidate_is_collectible_pickup(&candidates[outside])) continue;
        if (candidates[outside].dist_q8 > PICKUP_RESERVE_DIST_Q8) continue;

        for (int inside = selected - 1; inside >= 0; inside--) {
            if (candidate_is_collectible_pickup(&candidates[inside])) continue;
            if (!candidate_is_threat(&candidates[inside])) {
                replace = inside;
                break;
            }
        }
        if (replace < 0) {
            for (int inside = selected - 1; inside >= 0; inside--) {
                if (candidate_is_collectible_pickup(&candidates[inside])) continue;
                if (candidates[outside].dist_q8 <= candidates[inside].dist_q8 || selected_pickups == 0) {
                    replace = inside;
                    break;
                }
            }
        }
        if (replace < 0) continue;

        pickup = candidates[outside];
        candidates[outside] = candidates[replace];
        candidates[replace] = pickup;
        selected_pickups++;
    }
}
#endif

static u8 thing_render_class(u16 thing_type) {
    if (thing_is_monster(thing_type)) return 1;
    if (thing_is_runtime_threat(thing_type)) return 2;
    if (thing_is_pickup(thing_type)) return 3;
    if (thing_is_corpse(thing_type)) return 4;
    return 0;
}

static u8 thing_render_bucket_for_class(u8 thing_class, u16 thing_type) {
    if (thing_class == THING_CLASS_PICKUP) return pickup_is_collectible(thing_type) ? 3 : 5;
    return thing_class;
}

static u8 thing_render_bucket(u16 thing_type) {
    return thing_render_bucket_for_class(thing_render_class(thing_type), thing_type);
}

static u8 runtime_thing_is_monster(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (thing_type_override[thing_index]) return thing_is_monster(thing_type_override[thing_index]);
#if DOOM_SIMPLE_MAP
    return thing_static_class[thing_index] == THING_CLASS_MONSTER;
#elif NG_RUNTIME_THING_INFO_GENERATED
    return (g_runtime_thing_info[thing_index] & NG_THING_INFO_MONSTER) != 0;
#else
    return thing_static_class[thing_index] == THING_CLASS_MONSTER;
#endif
}

static u8 runtime_thing_is_pickup(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (thing_type_override[thing_index]) return thing_is_pickup(thing_type_override[thing_index]);
#if DOOM_SIMPLE_MAP
    return thing_static_class[thing_index] == THING_CLASS_PICKUP;
#elif NG_RUNTIME_THING_INFO_GENERATED
    return (g_runtime_thing_info[thing_index] & NG_THING_INFO_PICKUP) != 0;
#else
    return thing_static_class[thing_index] == THING_CLASS_PICKUP;
#endif
}

static u8 runtime_thing_is_threat(int thing_index) {
    u8 thing_class;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (thing_type_override[thing_index]) return thing_is_runtime_threat(thing_type_override[thing_index]);
#if DOOM_SIMPLE_MAP
    thing_class = thing_static_class[thing_index];
    return thing_class == THING_CLASS_MONSTER || thing_class == THING_CLASS_THREAT;
#elif NG_RUNTIME_THING_INFO_GENERATED
    return (g_runtime_thing_info[thing_index] & NG_THING_INFO_THREAT) != 0;
#else
    thing_class = thing_static_class[thing_index];
    return thing_class == THING_CLASS_MONSTER || thing_class == THING_CLASS_THREAT;
#endif
}

static u8 runtime_thing_is_shootable(int thing_index) {
    u8 thing_class;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (thing_type_override[thing_index]) return thing_is_shootable(thing_type_override[thing_index]);
#if DOOM_SIMPLE_MAP
    thing_class = thing_static_class[thing_index];
    return thing_class == THING_CLASS_MONSTER || thing_class == THING_CLASS_THREAT;
#elif NG_RUNTIME_THING_INFO_GENERATED
    return (g_runtime_thing_info[thing_index] & NG_THING_INFO_SHOOTABLE) != 0;
#else
    thing_class = thing_static_class[thing_index];
    return thing_class == THING_CLASS_MONSTER || thing_class == THING_CLASS_THREAT;
#endif
}

u8 rc_dynamic_blocked_q8(short x_q8, short y_q8) {
    enum { BLOCK_RANGE_Q8 = WORLD_Q8(104), BLOCK_RANGE_CELLS = (BLOCK_RANGE_Q8 + 255) >> 8 };
    int cx = x_q8 >> 8;
    int cy = y_q8 >> 8;
    for (u16 si = 0; si < thing_shootable_count; si++) {
        int i = thing_shootable_indices[si];
        short thing_x;
        short thing_y;
        if (enemy_dead[i]) continue;
        if (!runtime_thing_is_shootable(i)) continue;
        thing_x = thing_x_q8[i];
        thing_y = thing_y_q8[i];
        if (iabs16((thing_x >> 8) - cx) > BLOCK_RANGE_CELLS) continue;
        if (iabs16((thing_y >> 8) - cy) > BLOCK_RANGE_CELLS) continue;
        if (iabs16(x_q8 - thing_x) <= BLOCK_RANGE_Q8 && iabs16(y_q8 - thing_y) <= BLOCK_RANGE_Q8) return 1;
    }
    return 0;
}

static u8 runtime_thing_render_bucket(int thing_index, u16 *thing_type) {
    u16 type;
    u8 thing_class;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) {
        if (thing_type) *thing_type = 0;
        return 0;
    }
    type = runtime_thing_type(thing_index);
    if (thing_type) *thing_type = type;
    thing_class = thing_type_override[thing_index] ? thing_render_class(type) : thing_static_class[thing_index];
    return thing_render_bucket_for_class(thing_class, type);
}

static int thing_candidate_score(u8 bucket, u16 thing_type, int sx, int h, int dist_q8) {
    int score = dist_q8 + (iabs16(sx - SCRW / 2) >> 1) - (h >> 2);
    if (bucket == 1) score += runtime_threat_priority_bias(thing_type);
#if DOOM_SIMPLE_MAP
    if (bucket == 3 || bucket == 5) {
        int pickup_score = (dist_q8 >> 2) + (iabs16(sx - SCRW / 2) >> 1) - (h >> 2);
        if (!pickup_is_collectible(thing_type)) pickup_score += (1 << 13);
        return pickup_score - (1 << 20);
    }
#endif
    return score + ((int)bucket << 15);
}

static u8 thing_maybe_projectable(short x_q8, short y_q8, int px, int py, int dir_x, int dir_y, int plane_x, int plane_y) {
    int dx = x_q8 - px;
    int dy = y_q8 - py;
    long front = ((long)dx * dir_x + (long)dy * dir_y) >> 8;
    int range;
    long side;
    if (front < WORLD_Q8(16)) return 0;
    range = iabs16(dx) + iabs16(dy);
    if (range > WORLD_Q8(8192)) return 0;
    side = ((long)dx * plane_x + (long)dy * plane_y) >> 8;
    if (side < 0) side = -side;
    if (side > front + WORLD_Q8(1280)) return 0;
    return 1;
}

static int select_visible_things(int found) {
    ThingCandidate candidates[THING_CANDIDATE_COUNT];
    int count = 0;
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    if (found >= ENEMY_VISIBLE_COUNT) return found;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);

    for (u16 ri = 0; ri < thing_render_count; ri++) {
        int i = thing_render_indices[ri];
        int sx, h, dist_q8;
        u8 bucket;
        ThingCandidate candidate;
        u16 thing_type;
        if (enemy_dead[i]) continue;
        if (!thing_maybe_projectable(thing_x_q8[i], thing_y_q8[i], px, py, dir_x, dir_y, plane_x, plane_y)) continue;
        bucket = runtime_thing_render_bucket(i, &thing_type);
        if (!bucket) continue;
        if (candidate_coord_selected(candidates, count, thing_x_q8[i], thing_y_q8[i], thing_type)) continue;
        u8 fallback_projection = 0;
        if (!rc_project_point(thing_x_q8[i], thing_y_q8[i], &sx, &h, &dist_q8)) {
            if (!project_point_from_view_q8(thing_x_q8[i], thing_y_q8[i], px, py, dir_x, dir_y, plane_x, plane_y,
                                            &sx, &h, &dist_q8)) continue;
            if (sx < -48 || sx > SCRW + 48) continue;
            if (!line_of_sight_q8((short)px, (short)py, thing_x_q8[i], thing_y_q8[i])) continue;
            fallback_projection = 1;
        }
        if (sx < -48 || sx > SCRW + 48) continue;
#if DOOM_SIMPLE_MAP
        if (thing_is_pickup(thing_type) || thing_is_corpse(thing_type)) {
            fallback_projection = 1;
        }
#endif

        candidate.thing_index = i;
        candidate.dynamic_index = -1;
        candidate.thing_type = thing_type;
        candidate.x_q8 = thing_x_q8[i];
        candidate.y_q8 = thing_y_q8[i];
        candidate.sx = sx;
        candidate.h = h;
        candidate.dist_q8 = dist_q8;
        candidate.fallback_projection = fallback_projection;
        candidate.score = thing_candidate_score(bucket, thing_type, sx, h, dist_q8);
        insert_thing_candidate(candidates, &count, &candidate);
    }

    for (u8 i = 0; i < 8; i++) {
        int sx, h, dist_q8;
        u8 bucket;
        ThingCandidate candidate;
        u16 thing_type = dynamic_drop_type[i];
        if (!dynamic_drop_active[i]) continue;
        bucket = thing_render_bucket(thing_type);
        if (bucket != 3 && bucket != 5) continue;
        if (candidate_coord_selected(candidates, count, dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], thing_type)) continue;
        if (!thing_maybe_projectable(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], px, py, dir_x, dir_y, plane_x, plane_y)) continue;
        u8 fallback_projection = 0;
        if (!rc_project_point(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], &sx, &h, &dist_q8)) {
            if (!project_point_from_view_q8(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], px, py, dir_x, dir_y, plane_x, plane_y,
                                            &sx, &h, &dist_q8)) continue;
            if (sx < -48 || sx > SCRW + 48) continue;
            if (!line_of_sight_q8((short)px, (short)py, dynamic_drop_x_q8[i], dynamic_drop_y_q8[i])) continue;
            fallback_projection = 1;
        }
        if (sx < -48 || sx > SCRW + 48) continue;
#if DOOM_SIMPLE_MAP
        fallback_projection = 1;
#endif
        candidate.thing_index = -1;
        candidate.dynamic_index = (signed char)i;
        candidate.thing_type = thing_type;
        candidate.x_q8 = dynamic_drop_x_q8[i];
        candidate.y_q8 = dynamic_drop_y_q8[i];
        candidate.sx = sx;
        candidate.h = h;
        candidate.dist_q8 = dist_q8;
        candidate.fallback_projection = fallback_projection;
        candidate.score = thing_candidate_score(bucket, thing_type, sx, h, dist_q8);
        insert_thing_candidate(candidates, &count, &candidate);
    }

    {
        int selected = count;
        if (selected > ENEMY_VISIBLE_COUNT - found) selected = ENEMY_VISIBLE_COUNT - found;
#if DOOM_SIMPLE_MAP
        reserve_visible_pickups(candidates, count, selected);
#endif
        for (int i = 0; i < selected && found < ENEMY_VISIBLE_COUNT; i++) {
        u8 rendered;
        if (candidates[i].dynamic_index >= 0) {
            rendered = render_type_slot((u16)found, -1, candidates[i].thing_type, candidates[i].x_q8, candidates[i].y_q8,
                                        candidates[i].sx, candidates[i].h, candidates[i].dist_q8,
                                        0, candidates[i].fallback_projection, px, py);
        } else {
            rendered = render_type_slot((u16)found, candidates[i].thing_index, candidates[i].thing_type,
                                        candidates[i].x_q8, candidates[i].y_q8,
                                        candidates[i].sx, candidates[i].h, candidates[i].dist_q8,
                                        (candidates[i].thing_index >= 0 && enemy_hit_flash[candidates[i].thing_index]) ? 1 : 0,
                                        candidates[i].fallback_projection, px, py);
        }
        if (rendered) found++;
        }
    }
    return found;
}

static int render_visible_projectile(int found) {
    int sx, h, dist_q8;
    if (!projectile_active || found >= ENEMY_VISIBLE_COUNT) return found;
    if (!rc_project_point(projectile_x_q8, projectile_y_q8, &sx, &h, &dist_q8)) {
        if (!project_point_q8(projectile_x_q8, projectile_y_q8, &sx, &h, &dist_q8)) return found;
    }
    if (render_type_slot((u16)found, -1, projectile_type, projectile_x_q8, projectile_y_q8,
                         sx, h, dist_q8, 0, 0, 0, 0)) return found + 1;
    return found;
}

static int render_visible_impact(int found) {
    int sx, h, dist_q8;
    if (!impact_active || found >= ENEMY_VISIBLE_COUNT) return found;
    if (!rc_project_point(impact_x_q8, impact_y_q8, &sx, &h, &dist_q8)) {
        if (!project_point_q8(impact_x_q8, impact_y_q8, &sx, &h, &dist_q8)) return found;
    }
    if (render_type_slot((u16)found, -1, 9000, impact_x_q8, impact_y_q8,
                         sx, h, dist_q8, 0, 0, 0, 0)) return found + 1;
    return found;
}

static void update_enemy_ranged_readiness(void) {
    short current[ENEMY_VISIBLE_COUNT];
    u8 current_count = 0;

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (enemy_dead[thing]) continue;
        if (!enemy_slot_can_ranged_attack(slot)) continue;
        if (!enemy_slot_is_monster(slot)) continue;
        current[current_count++] = (short)thing;
        if (enemy_ranged_readable_ticks[thing] < 255) enemy_ranged_readable_ticks[thing]++;
    }

    for (u8 i = 0; i < ranged_readable_prev_count; i++) {
        short thing = ranged_readable_prev[i];
        u8 still_readable = 0;
        for (u8 j = 0; j < current_count; j++) {
            if (current[j] == thing) {
                still_readable = 1;
                break;
            }
        }
        if (!still_readable && thing >= 0 && thing < NG_RUNTIME_THING_COUNT) enemy_ranged_readable_ticks[thing] = 0;
    }

    ranged_readable_prev_count = current_count;
    for (u8 i = 0; i < current_count; i++) {
        ranged_readable_prev[i] = current[i];
    }
}

static void update_enemy(void) {
    int found = 0;

    found = render_visible_projectile(found);
    found = select_visible_things(found);
    found = render_visible_impact(found);
    for (u16 slot = (u16)found; slot < ENEMY_VISIBLE_COUNT; slot++) hide_enemy_slot(slot);
    update_enemy_ranged_readiness();
}

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
static void update_chunk_streaming(void) {
    int px;
    int py;
    NgChunkStreamState stream;

#if defined(DOOM_FOCUSED_TEST) && !defined(DOOM_CHUNK_MOVEMENT_TEST)
    return;
#endif
    rc_player_q8(&px, &py);
    stream = ng_chunk_stream_update(px, py, SIMPLE_ACTIVE_CHUNK);
    if (!stream.changed) return;

    save_active_chunk_runtime_things();
    g_simple_active_chunk = stream.chunk;
    rc_shift_player_q8(stream.shift_x_q8, stream.shift_y_q8);
    for (u16 i = 0; i < MAP_RUNTIME_OPEN_BYTES; i++) g_runtime_cell_open[i] = 0;
    load_active_chunk_dynamic_drops();
    init_runtime_things();
    reset_enemy_slot_cache();
    hide_enemies();
    invalidate_background_cache();
    rc_invalidate_view();
    monster_path_valid = 0;
    monster_path_timer = 0;
    prev_px = -1;
    prev_py = -1;
}
#endif

static void restart_level(void) {
    prev_px = -1;
    prev_py = -1;
    map_on = 0;
    minimap_redraw_active = 0;
    minimap_clear_active = 0;
    weapon_frame = 0xFF;
    weapon_bob_phase = 0;
    weapon_bob_x = 0;
    weapon_bob_y = 0;
    weapon_flash_timer = 0;
    weapon_flash_on = 0;
    fire_timer = 0;
    fire_prev = 0;
    door_prev = 0;
    map_prev = 0;
    shortcut_prev = 0;
    restart_prev = 0;
    hurt_timer = 0;
    floor_damage_timer = 0;
    input_catchup_pending = 0;
#ifdef DOOM_FRAME_STATS
    frame_stats_frames = 0;
    frame_stats_overruns = 0;
    frame_stats_shown = 0xFF;
#endif
    level_complete = 0;
    level_next_episode = DOOM_NEXT_MAP_EPISODE;
    level_next_mission = DOOM_NEXT_MAP_MISSION;
    invalidate_background_cache();
    key_message_timer = 0;
    missing_key_bits = 0;
    ammo_message_timer = 0;
    door_message_timer = 0;
    secret_message_timer = 0;
    pickup_message_timer = 0;
    weapon_message_timer = 0;
    weapon_message_digit = 0;
    pickup_message_type = 0;
    pickup_message_key = 0;
    key_message_visible = 0;
    monster_ai_tick = 0;
    monster_path_valid = 0;
    monster_path_timer = 0;
    monster_path_player_cell_x = -1;
    monster_path_player_cell_y = -1;
    projectile_active = 0;
    projectile_from_player = 0;
    projectile_source_thing = -1;
    projectile_type = 0;
    projectile_timer = 0;
    projectile_damage = 0;
    projectile_x_q8 = 0;
    projectile_y_q8 = 0;
    projectile_dx_q8 = 0;
    projectile_dy_q8 = 0;
    projectile_hit_range_q8 = 0;
    projectile_hit_coarse_cells = 0;
    impact_active = 0;
    impact_timer = 0;
    impact_x_q8 = 0;
    impact_y_q8 = 0;
    player_keys = 0;
    player_has_shotgun = 0;
    player_has_chaingun = 0;
    player_has_rocket_launcher = 0;
    player_has_plasma = 0;
    player_has_bfg = 0;
    player_has_chainsaw = 0;
    player_has_backpack = 0;
    current_weapon = WEAPON_PISTOL;
    pickup_message_weapon = 0;
    chaingun_flash = 0;
    player_health = 100;
    player_armor = 0;
    player_armor_class = 0;
    player_ammo = 50;
    player_shells = 0;
    player_rockets = 0;
    player_cells = 0;
    power_invuln_timer = 0;
    power_invis_timer = 0;
    power_radsuit_timer = 0;
    power_lightamp_timer = 0;
    power_computer_map = 0;
    player_berserk = 0;
    player_max_bullets = PLAYER_MAX_BULLETS;
    player_max_shells = PLAYER_MAX_SHELLS;
    player_max_rockets = PLAYER_MAX_ROCKETS;
    player_max_cells = PLAYER_MAX_CELLS;
    player_score = 0;
    player_kills = 0;
    player_items = 0;
    player_secrets = 0;
    hurt_flash = 0;
    muzzle_flash = 0;
    bonus_flash = 0;
    armor_flash_timer = 0;
    palette_effect = 0;
    power_palette_kind = 0;
    face_pain_timer = 0;
    face_evil_timer = 0;
    face_turn_timer = 0;
    face_turn_frame = FACE_STRAIGHT_BASE;
    face_idle_tick = 0;
    face_idle_variant = 0;
    sector_floor_visual_kind = 0xFF;
    sector_light_band = 0xFF;
    sector_liquid_phase = 0;
    sector_liquid_tick = 0;
    flash_overlay_active = 0;
    sector_palette_px_key = 0x7FFFFFFF;
    sector_palette_py_key = 0x7FFFFFFF;
    sector_palette_dir_x = 0x7FFFFFFF;
    sector_palette_dir_y = 0x7FFFFFFF;

    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) g_runtime_door_open[i] = 0;
    for (u16 i = 0; i < NG_RUNTIME_LIFT_COUNT; i++) g_runtime_lift_open[i] = 0;
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    for (u16 i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
    for (u16 i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
#endif
    for (u16 i = 0; i < MAP_RUNTIME_OPEN_BYTES; i++) g_runtime_cell_open[i] = 0;
    for (u16 i = 0; i < MAP_SECRET_BYTES; i++) secret_found_bits[i] = 0;
    for (u8 i = 0; i < 8; i++) {
        dynamic_drop_active[i] = 0;
        dynamic_drop_type[i] = 0;
        dynamic_drop_x_q8[i] = 0;
        dynamic_drop_y_q8[i] = 0;
    }
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 0;
        enemy_hp[i] = 0;
        enemy_hit_flash[i] = 0;
        enemy_awake[i] = 0;
        enemy_attack_cooldown[i] = 0;
        enemy_attack_anim[i] = 0;
        enemy_ranged_readable_ticks[i] = 0;
        enemy_hidden_timer[i] = 0;
        monster_face_x[i] = 0;
        monster_face_y[i] = 1;
        explosion_timer[i] = 0;
        death_anim_timer[i] = 0;
        death_drop_timer[i] = 0;
        thing_type_override[i] = 0;
        death_anim_final_type[i] = 0;
        death_anim_drop_type[i] = 0;
        death_drop_type[i] = 0;
    }
    reset_enemy_slot_cache();

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    init_chunk_thing_state();
#endif
    init_palettes();
    clear_fix();
    disable_sprites();
    init_runtime_things();
    rc_init();
#if DOOM_SIMPLE_MAP && !DOOM_CHUNKED_SIMPLE_MAP && !defined(DOOM_FOCUSED_TEST)
    seed_simple_map_things();
#endif
#ifdef DOOM_COMBAT_TEST
    configure_combat_test();
#endif
#ifdef DOOM_E1M1_ENCOUNTER_TEST
    configure_e1m1_encounter_test();
#endif
#ifdef DOOM_E1M1_SCOUT_TEST
    configure_e1m1_scout_test();
#endif
#ifdef DOOM_E1M1_EXIT_TEST
    configure_e1m1_exit_test();
#endif
#ifdef DOOM_HIDDEN_ATTACK_TEST
    configure_hidden_attack_test();
#endif
#ifdef DOOM_MELEE_TEST
    configure_melee_test();
#endif
#ifdef DOOM_MONSTER_GALLERY_TEST
    configure_monster_gallery_test();
#endif
#ifdef DOOM_ARSENAL_TEST
    configure_arsenal_test();
#endif
#ifdef DOOM_DEATH_TEST
    configure_death_test();
#endif
#ifdef DOOM_POWERUP_TEST
    configure_powerup_test();
#endif
#ifdef DOOM_KEY_DOOR_TEST
    configure_key_door_test();
#endif
#ifdef DOOM_E1M8_BOSS_TEST
    configure_e1m8_boss_test();
#endif
#if defined(DOOM_CHUNK_MOVEMENT_TEST) && DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    configure_chunk_movement_test();
#endif
    update_sector_flat_palette();
    init_background();
    init_walls();
    init_hud();
    init_weapon();
    init_flash_overlay_sprites();
    reset_enemy_slot_cache();
    hide_enemies();
    compute_level_totals();
    force_fix_hud_redraw();
}

int main(void) {
    watchdog_kick();
#ifndef DOOM_SKIP_INTRO
    run_intro_menu();
#endif
    restart_level();

    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;
#if defined(DOOM_CHUNK_MOVEMENT_TEST) && DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
        pressed = chunk_movement_test_pressed(pressed);
#endif
        u8 catchup_input = input_catchup_pending;
        input_catchup_pending = 0;
        if (game_active()) {
            enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, A = 0x10, C = 0x40, D = 0x80 };
            const u8 dpad = UP | DOWN | LEFT | RIGHT;
            u8 d_now = pressed & D;
            u8 move_pressed = pressed;
            if ((pressed & C) && !(pressed & A) && (pressed & dpad)) {
                move_pressed = (u8)(move_pressed & ~dpad);
            }
            rc_input(move_pressed);
            if (catchup_input && (move_pressed & dpad)) {
                rc_input(move_pressed);
            }
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
            update_chunk_streaming();
#if defined(DOOM_CHUNK_MOVEMENT_TEST)
            {
                int debug_px;
                int debug_py;
                rc_player_q8(&debug_px, &debug_py);
                player_ammo = (u16)(debug_px >> 8);
                player_kills = (u16)(debug_py >> 8);
                player_armor = SIMPLE_ACTIVE_CHUNK;
            }
            shown_ammo = 0xFFFF;
            shown_armor = 0xFFFF;
            shown_frags = 0xFFFF;
#endif
#endif
            update_floor_damage();
            check_secret_reached();
            check_lift_walk_triggers();
            update_monster_ai();
            collect_nearby_pickups();
            check_exit_reached();
            if (d_now && !door_prev) {
                if (!open_nearby_lift()) open_nearby_door();
            }
            door_prev = d_now;
        } else {
            enum { D = 0x80 };
            u8 restart_now = pressed & D;
            if (restart_now && !restart_prev) restart_level();
            restart_prev = restart_now;
            input_catchup_pending = 0;
            pressed = 0;
        }
        rc_render();                    /* DDA during active display          */
        {
            u8 frame_overrun = wait_vblank_status();
            rc_set_frame_overrun(frame_overrun);
            input_catchup_pending = frame_overrun;
#ifdef DOOM_FRAME_STATS
            update_frame_stats_overlay(frame_overrun);
#endif
#ifdef DOOM_INPUT_DEBUG
            update_input_debug_overlay(pressed);
#endif
            watchdog_kick();
            update_sector_flat_palette();
            update_hurt_flash();
        update_weapon_flash();
        update_background_scroll(frame_overrun);
        }
        rc_blit();                      /* push to VRAM during vblank         */
        update_impact_effect();
        if (level_complete) hide_enemies();
        else update_enemy();
        /* Projectile ownership is tied to the current readable enemy slots.
         * Refresh world sprites first so a monster fireball cannot keep
         * advancing from stale visibility after the source leaves view. */
        update_projectile();
        update_monster_damage();
        update_weapon(pressed);
        update_enemy_hit_flash();
        update_power_timers();
        update_status_numbers(pressed);
        update_center_message();

        /* C cycles weapons. Holding C and pressing a direction jumps directly
         * to weapon shortcuts; A+C keeps the minimap out of normal weapon flow. */
        {
            enum { A = 0x10, C = 0x40, UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08 };
            const u8 dpad = UP | DOWN | LEFT | RIGHT;
            u8 c_now = pressed & C;
            u8 shortcut_now = (u8)(pressed & dpad);
            if (c_now && (pressed & A) && !map_prev) {
                if (map_on) start_minimap_clear();
                else {
                    map_on = 1;
                    start_minimap_redraw();
                }
                force_fix_hud_redraw();
            } else if (c_now && !(pressed & A) && shortcut_now) {
                if (!map_prev || shortcut_now != shortcut_prev) {
                    select_weapon_shortcut(shortcut_now);
                }
            } else if (c_now && !map_prev) {
                toggle_weapon();
            }
            if (!c_now || (pressed & A)) {
                shortcut_prev = 0;
            } else {
                shortcut_prev = shortcut_now;
            }
            map_prev = c_now;
        }

        update_minimap_redraw();
        update_minimap_clear();
        update_marker();                /* 2 fix writes when the cell changes */
    }
    return 0;
}
