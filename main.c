/* main.c - boot setup and the game loop.
 
 */
#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"
#include "raycast.h"
#include "map.h"

unsigned char g_runtime_door_open[NG_RUNTIME_DOOR_COUNT ? NG_RUNTIME_DOOR_COUNT : 1];
unsigned char g_runtime_cell_open[MAP_RUNTIME_OPEN_BYTES ? MAP_RUNTIME_OPEN_BYTES : 1];
static u8 hurt_flash = 0;
static u8 muzzle_flash = 0;
static u8 bonus_flash = 0;
static u8 palette_effect = 0;
static u8 face_pain_timer = 0;
static u8 face_evil_timer = 0;
static u8 face_turn_timer = 0;
static u8 face_turn_frame = 0;
static u8 face_idle_tick = 0;
static u8 face_idle_variant = 0;

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

static void restore_flat_palettes(void) {
    for (int i = 0; i < CEILING_PALETTE_COLORS; i++) {
        pal_set(PAL_CEILING, (u16)(i + 1), RGB(g_ceiling_palette_rgb[i][0], g_ceiling_palette_rgb[i][1], g_ceiling_palette_rgb[i][2]));
    }
    for (int i = 0; i < FLOOR_PALETTE_COLORS; i++) {
        pal_set(PAL_FLOOR, (u16)(i + 1), RGB(g_floor_palette_rgb[i][0], g_floor_palette_rgb[i][1], g_floor_palette_rgb[i][2]));
    }

    for (u16 row = 0; row < BG_SPLIT; row++) {
        u16 ceiling_scale = (u16)(90 + row * 24);
        u16 floor_scale = (u16)(150 + row * 42);
        set_shaded_palette((u16)(PAL_CEILING_GRAD_BASE + row), g_ceiling_palette_rgb, CEILING_PALETTE_COLORS, ceiling_scale);
        set_shaded_palette((u16)(PAL_FLOOR_GRAD_BASE + row), g_floor_palette_rgb, FLOOR_PALETTE_COLORS, floor_scale);
    }
}

static void set_depth_palette_range(u16 base, const u8 rgb[][3], u16 colors) {
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 256 - (b * 200) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 140 : 256;
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
}

static void restore_weapon_palette(void) {
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(g_weapon_palette_rgb[i][0], g_weapon_palette_rgb[i][1], g_weapon_palette_rgb[i][2]));
    }
}

static void restore_counter_palette(void) {
    for (int i = 1; i < 15; i++) pal_set(PAL_AMMO_COUNTER, (u16)i, RGB(5, 3, 0));
    pal_set(PAL_AMMO_COUNTER, 15, RGB(20, 17, 4));
}

static void set_weapon_flash_palette(void) {
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        u8 r = g_weapon_palette_rgb[i][0];
        u8 g = g_weapon_palette_rgb[i][1];
        u8 b = g_weapon_palette_rgb[i][2];
        r = (u8)(r + ((31 - r) * 3) / 4);
        g = (u8)(g + ((28 - g) * 2) / 3);
        b = (u8)(b + ((12 - b) / 3));
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

static void set_hurt_palettes(void) {
    REG_BACKDROP = RGB(1, 0, 0);
}

static void update_hurt_flash(void) {
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
    } else if (palette_effect) {
        if (palette_effect == 1 || palette_effect == 2) REG_BACKDROP = RGB(0, 0, 0);
        else restore_play_palettes();
        palette_effect = 0;
    }
}

/* ---- clear the fix layer */
static void clear_fix(void) {
    vram_addr(VRAM_FIX);
    vram_mod(1);
    for (int i = 0; i < 40 * 32; i++) vram_w(0x0000);
}

static int prev_px = -1, prev_py = -1;
static u8  map_on = 0;              /* minimap visible?                       */
static u8  minimap_redraw_active = 0;
static u16 minimap_redraw_index = 0;
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
static u8  restart_prev = 0;
static u8  hurt_timer = 0;
static u8  floor_damage_timer = 0;
static u8  armor_flash_timer = 0;
static u8  level_complete = 0;
static u32 bg_scroll_key = 0xFFFFFFFFUL;
static u8  key_message_timer = 0;
static u8  ammo_message_timer = 0;
static u8  door_message_timer = 0;
static u8  secret_message_timer = 0;
static u8  pickup_message_timer = 0;
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
static u8  dynamic_drop_active[8];
static u16 dynamic_drop_type[8];
static short dynamic_drop_x_q8[8];
static short dynamic_drop_y_q8[8];
static u8 secret_found_bits[MAP_SECRET_BYTES ? MAP_SECRET_BYTES : 1];
static u8 monster_path_dist[MAP_H][MAP_W];
static u16 monster_path_queue[MAP_W * MAP_H];
static u8 monster_path_valid = 0;
static u8 monster_path_timer = 0;
static int enemy_palette_def[ENEMY_VISIBLE_COUNT] = {-1};
static int enemy_tile_key[ENEMY_VISIBLE_COUNT] = {-1};
static u8 enemy_slot_flash[ENEMY_VISIBLE_COUNT];
static volatile u16 player_health = 100;
static volatile u16 player_armor = 0;
static u8 player_armor_class = 0;
static volatile u16 player_ammo = 50;
static volatile u16 player_shells = 0;
static volatile u16 player_rockets = 0;
static volatile u16 player_cells = 0;
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
static u8 shown_keys = 0xFF;
static u16 shown_weapon_status = 0xFFFF;

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

typedef struct EnemyDraw {
    int thing_index;
    int sprite_def;
    int screen_x;
    int screen_w;
    int screen_h;
    int dist_q8;
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
    }
}

static EnemyDraw enemies[ENEMY_VISIBLE_COUNT];

static u8 enemy_slot_is_readable(u16 slot) {
    int center_x;
    if (slot >= ENEMY_VISIBLE_COUNT) return 0;
    if (enemies[slot].thing_index < 0) return 0;
    if (enemies[slot].screen_w <= 0 || enemies[slot].screen_h <= 0) return 0;
    if (enemies[slot].screen_x + enemies[slot].screen_w < 16) return 0;
    if (enemies[slot].screen_x > SCRW - 16) return 0;
    center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
    if (center_x < 24 || center_x > SCRW - 24) return 0;
    return 1;
}

static u8 thing_has_readable_slot(int thing_index) {
    if (thing_index < 0) return 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemies[slot].thing_index == thing_index && enemy_slot_is_readable(slot)) return 1;
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
static void close_minimap_for_terminal_message(void);

static int iabs16(int value) {
    return value < 0 ? -value : value;
}

#define WORLD_Q8(value) ((value) * MAP_RENDER_SCALE)
#define MONSTER_SEPARATION_Q8 32

static void explode_barrel_at(int thing_index, short x_q8, short y_q8);
static void player_take_damage(u16 amount);
static void spawn_impact_effect(short x_q8, short y_q8, u8 timer);
static u8 key_bit_for_thing(u16 thing_type);

static u8 line_of_sight_q8(short ax, short ay, short bx, short by) {
    int dx = bx - ax;
    int dy = by - ay;
    int steps = iabs16(dx) > iabs16(dy) ? iabs16(dx) : iabs16(dy);
    if (steps <= 0) return 1;
    steps >>= 6;
    if (steps < 1) steps = 1;
    if (steps > 40) steps = 40;

    for (int i = 1; i < steps; i++) {
        int x = ax + (dx * i) / steps;
        int y = ay + (dy * i) / steps;
        if (map_at(x >> 8, y >> 8)) return 0;
    }
    return 1;
}

static u8 player_line_of_sight_to(short x_q8, short y_q8) {
    int px, py;
    rc_player_q8(&px, &py);
    return line_of_sight_q8((short)px, (short)py, x_q8, y_q8);
}

static u8 project_point_q8(short world_x_q8, short world_y_q8, int *screen_x, int *height, int *dist_q8) {
    int px, py;
    int dir_x, dir_y, plane_x, plane_y;
    long sprite_x;
    long sprite_y;
    long det;
    long transform_x;
    long transform_y;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);
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

static u8 game_active(void) {
    return player_health != 0 && !level_complete;
}

static void init_runtime_things(void) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        thing_x_q8[i] = g_runtime_things[i].x_q8;
        thing_y_q8[i] = g_runtime_things[i].y_q8;
    }
}

static u16 runtime_thing_type(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    return thing_type_override[thing_index] ? thing_type_override[thing_index] : g_runtime_things[thing_index].type;
}

static u8 thing_is_monster(u16 thing_type) {
    switch (thing_type) {
    case 9:
    case 58:
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
    case 2046:
    case 2047:
    case 2048:
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
        return !player_has_plasma || player_cells < player_max_cells;
    case 2005: /* chainsaw */
        return !player_has_chainsaw;
    case 2006: /* BFG 9000 */
        return !player_has_bfg || player_cells < player_max_cells;
    case 8:    /* backpack */
        return !player_has_backpack || player_ammo < player_max_bullets || player_shells < player_max_shells || player_rockets < player_max_rockets || player_cells < player_max_cells;
    case 2007: /* clip */
    case 2048: /* ammo box */
        return player_ammo < player_max_bullets;
    case 2008: /* shells */
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
    case 2019: /* blue armor */
        return player_armor < 200;
    case 2018: /* green armor */
        return player_armor < 100;
    default:
        return 0;
    }
}

static void compute_level_totals(void) {
    level_total_kills = 0;
    level_total_items = 0;
    level_total_secrets = 0;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        u16 type = g_runtime_things[i].type;
        if (thing_is_monster(type)) {
            if (level_total_kills < 999) level_total_kills++;
        } else if (thing_is_pickup(type)) {
            if (level_total_items < 999) level_total_items++;
        }
    }
    for (u16 y = 0; y < MAP_H; y++) {
        for (u16 x = 0; x < MAP_W; x++) {
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

static u8 thing_is_corpse(u16 thing_type) {
    return (thing_type >= 9001 && thing_type <= 9005) || (thing_type >= 9010 && thing_type <= 9024);
}

static u8 thing_is_shootable(u16 thing_type) {
    return thing_is_monster(thing_type) || thing_is_barrel(thing_type);
}

static u8 player_has_weapon(u8 weapon) {
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
    default:
        return 0;
    }
}

static u16 death_anim_next_stage_type(u16 thing_type) {
    if (thing_type >= 9010 && thing_type <= 9019) return (u16)(thing_type + 5);
    return 0;
}

static u16 monster_score_value(u16 thing_type) {
    switch (thing_type) {
    case 3004: /* former human */
        return 100;
    case 9:    /* shotgun guy */
        return 150;
    case 3001: /* imp */
        return 200;
    case 3002: /* demon */
    case 58:   /* spectre */
        return 400;
    case 3003: /* baron */
        return 1000;
    case 3005: /* cacodemon */
        return 700;
    case 3006: /* lost soul */
        return 100;
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
    bg_scroll_key = 0xFFFFFFFFUL;
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
    default:
        return 0;
    }
}

static u8 monster_view_angle_bucket(int thing_index) {
    int px, py;
    int to_x;
    int to_y;
    int dot;
    int cross;
    int abs_cross;
    int face_x;
    int face_y;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 1;
    rc_player_q8(&px, &py);
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
    if (dot > 0) return 2;
    if (-dot < abs_cross) return 3;
    if (dot < -(abs_cross * 2)) return 5;
    return 4;
}

static int enemy_sprite_def_for_type(u16 thing_type, int thing_index) {
    int first = -1;
    int first_angle = -1;
    u8 wanted_angle = thing_is_monster(thing_type)
        ? ((thing_index >= 0 && enemy_attack_anim[thing_index]) ? 9
        : ((thing_index >= 0 && enemy_hit_flash[thing_index]) ? 10 : monster_view_angle_bucket(thing_index)))
        : 0;
    u8 wanted_anim = thing_is_monster(thing_type) ? (u8)((monster_ai_tick >> 3) & 1) : 0;
    u8 angle_hits = 0;
    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type != thing_type) continue;
        if (first < 0) first = i;
        if (!thing_is_monster(thing_type)) return first;
        if (g_enemy_sprite_defs[i].angle == wanted_angle || g_enemy_sprite_defs[i].angle == 0) {
            if (first_angle < 0) first_angle = i;
            if (angle_hits == wanted_anim) return i;
            angle_hits++;
        }
    }
    if (thing_is_pickup(thing_type)) {
        u16 fallback_type = fallback_sprite_type_for_missing_pickup(thing_type);
        if (fallback_type) {
            int fallback = first_sprite_def_for_type(fallback_type);
            if (fallback >= 0) return fallback;
        }
    }
    if (first_angle >= 0) return first_angle;
    return first >= 0 ? first : 0;
}

static void load_enemy_palette(u16 slot, int def) {
    if (def == enemy_palette_def[slot]) return;
    for (int i = 0; i < ENEMY_PALETTE_COLORS; i++) {
        u8 r = g_enemy_palette_rgb[def][i][0];
        u8 g = g_enemy_palette_rgb[def][i][1];
        u8 b = g_enemy_palette_rgb[def][i][2];
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

static u8 damage_enemy_at(int thing_index, u8 damage) {
    u8 killed = 0;
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (!thing_is_shootable(runtime_thing_type(thing_index))) return 0;
    if (enemy_dead[thing_index]) return 0;
    if (thing_is_barrel(runtime_thing_type(thing_index))) {
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
        u16 source_type = runtime_thing_type(thing_index);
        u16 drop_type = monster_drop_type(source_type);
        u16 corpse_type = monster_corpse_type(source_type);
        u16 death_type = monster_death_anim_type(source_type);
        u8 score_awarded = 0;
        u8 hp = monster_hp(thing_index);
        if (damage >= hp) hp = 0;
        else hp = (u8)(hp - damage);

        for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
            if (thing_is_monster(runtime_thing_type(i)) && thing_x_q8[i] == x && thing_y_q8[i] == y) {
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
    return killed;
}

static void explode_barrel_at(int thing_index, short x_q8, short y_q8) {
    int px, py;
    rc_player_q8(&px, &py);
    if (iabs16(px - x_q8) + iabs16(py - y_q8) < WORLD_Q8(520)) player_take_damage(12);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        u16 type = runtime_thing_type(i);
        if (i == thing_index || enemy_dead[i] || !thing_is_shootable(type)) continue;
        if (iabs16(thing_x_q8[i] - x_q8) + iabs16(thing_y_q8[i] - y_q8) < WORLD_Q8(520)) {
            damage_enemy_at(i, thing_is_barrel(type) ? 1 : 5);
        }
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

static u8 weapon_target_project(int thing, int *sx, int *h, int *dist_q8) {
    int col;
    if (!project_point_q8(thing_x_q8[thing], thing_y_q8[thing], sx, h, dist_q8)) return 0;
    if (*sx < 0 || *sx >= SCRW) return 0;
    col = *sx / COLW;
    if (col < 0 || col >= NUM_COLS) return 0;
    return 1;
}

static int best_visible_enemy(void) {
    int best_thing = -1;
    int best_score = 9999;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        int center_x;
        int score;
        if (thing < 0) continue;
        if (enemies[slot].screen_w <= 0 || enemies[slot].screen_h <= 0) continue;
        if (!thing_is_shootable(runtime_thing_type(thing))) continue;
        if (enemy_dead[thing]) continue;
        center_x = enemies[slot].screen_x + enemies[slot].screen_w / 2;
        if (iabs16(center_x - SCRW / 2) > 76 && enemies[slot].screen_h < 112) continue;
        score = iabs16(center_x - SCRW / 2) + (enemies[slot].dist_q8 >> 7) - (enemies[slot].screen_h >> 2);
        if (score < best_score) {
            best_score = score;
            best_thing = thing;
        }
    }
    if (best_thing >= 0) return best_thing;
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
    int best_thing = -1;
    int best_score = 9999;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        int center_x;
        int score;
        if (thing < 0) continue;
        if (enemies[slot].screen_w <= 0 || enemies[slot].screen_h <= 0) continue;
        if (enemies[slot].dist_q8 > WORLD_Q8(2)) continue;
        if (!thing_is_shootable(runtime_thing_type(thing)) || enemy_dead[thing]) continue;
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
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        u16 type = runtime_thing_type(i);
        if (enemy_dead[i] || !thing_is_shootable(type)) continue;
        if (iabs16(thing_x_q8[i] - x) + iabs16(thing_y_q8[i] - y) < WORLD_Q8(560)) {
            damage_enemy_at(i, thing_is_barrel(type) ? 1 : 8);
        }
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

    for (int thing = 0; thing < NG_RUNTIME_THING_COUNT; thing++) {
        int sx, h, dist_q8;
        int lateral;
        int score;
        int insert_at;
        if (!thing_is_shootable(runtime_thing_type(thing))) continue;
        if (enemy_dead[thing]) continue;
        if (!player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) continue;
        if (!weapon_target_project(thing, &sx, &h, &dist_q8)) continue;

        lateral = iabs16(sx - SCRW / 2);
        if (lateral > 54 && h < 100) continue;
        score = lateral + (dist_q8 >> 8);
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

static void damage_bfg_visible_targets(void) {
    int primary = best_visible_enemy();
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (enemies[slot].screen_w <= 0 || enemies[slot].screen_h <= 0) continue;
        if (!thing_is_shootable(runtime_thing_type(thing)) || enemy_dead[thing]) continue;
        damage_visible_enemy(thing, thing == primary ? 18 : 9);
    }
}

static void alert_monsters_by_sound(void) {
    int px, py;
    rc_player_q8(&px, &py);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        int dx, dy, range;
        if (enemy_dead[i] || !thing_is_monster(runtime_thing_type(i))) continue;
        if (enemy_awake[i]) continue;
        dx = iabs16(px - thing_x_q8[i]);
        dy = iabs16(py - thing_y_q8[i]);
        range = dx + dy;
        if (range > WORLD_Q8(2816)) continue;
        if (range > WORLD_Q8(1024) && !line_of_sight_q8((short)px, (short)py, thing_x_q8[i], thing_y_q8[i])) continue;
        enemy_awake[i] = 1;
        enemy_attack_cooldown[i] = 28;
    }
}

static void update_enemy_hit_flash(void) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
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
            if (death_anim_timer[i] == 12 || death_anim_timer[i] == 6) {
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
        return 9006;
    case 3005: /* cacodemon */
        return 9008;
    case 3003: /* baron */
        return 9007;
    default:
        return 0;
    }
}

static u8 spawn_monster_projectile(int thing, u16 type, u8 damage) {
    int px, py, dx, dy, adx, ady, steps;
    if (thing < 0 || projectile_active) return 0;
    rc_player_q8(&px, &py);
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
    projectile_active = 1;
    return 1;
}

static u8 spawn_player_projectile(u16 type, u8 timer) {
    int px, py, dir_x, dir_y, plane_x, plane_y;
    if (projectile_active) return 0;
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
    projectile_active = 1;
    return 1;
}

static void detonate_player_projectile(void) {
    spawn_impact_effect(projectile_x_q8, projectile_y_q8, 12);
    if (projectile_type == 9007) damage_bfg_visible_targets();
    projectile_active = 0;
    projectile_from_player = 0;
    projectile_source_thing = -1;
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
    int px, py;
    int sx, h, dist_q8;
    u8 visible;
    if (!projectile_active) return;
    if (!game_active()) {
        projectile_active = 0;
        projectile_from_player = 0;
        projectile_source_thing = -1;
        return;
    }
    if (!projectile_from_player) {
        if (projectile_source_thing < 0 || enemy_dead[projectile_source_thing]
            || !thing_has_readable_slot(projectile_source_thing)) {
            projectile_active = 0;
            projectile_source_thing = -1;
            return;
        }
    }
    if (projectile_type == 9000 && projectile_damage == 0) {
        if (projectile_timer) projectile_timer--;
        if (!projectile_timer) {
            projectile_active = 0;
            projectile_source_thing = -1;
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
        projectile_active = 0;
        projectile_source_thing = -1;
        return;
    }
    visible = project_point_q8(projectile_x_q8, projectile_y_q8, &sx, &h, &dist_q8);
    rc_player_q8(&px, &py);
    if (!projectile_from_player && iabs16(px - projectile_x_q8) <= WORLD_Q8(112) && iabs16(py - projectile_y_q8) <= WORLD_Q8(112)) {
        spawn_impact_effect(projectile_x_q8, projectile_y_q8, 8);
        if (visible) player_take_damage(projectile_damage);
        projectile_active = 0;
        projectile_source_thing = -1;
        return;
    }
    if (projectile_timer) projectile_timer--;
    if (!projectile_timer) {
        if (projectile_from_player) detonate_player_projectile();
        else {
            projectile_active = 0;
            projectile_source_thing = -1;
        }
    }
}

static u8 update_close_monster_melee(void) {
    int px, py;
    rc_player_q8(&px, &py);
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        u16 type;
        if (thing < 0) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        type = runtime_thing_type(thing);
        if (enemy_dead[thing] || !thing_is_monster(type)) continue;
        if (enemy_hit_flash[thing] || enemy_attack_cooldown[thing]) continue;
        if (iabs16(px - thing_x_q8[thing]) < WORLD_Q8(288) && iabs16(py - thing_y_q8[thing]) < WORLD_Q8(288)) {
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
    if (hurt_timer) {
        hurt_timer--;
        return;
    }
    if (!game_active()) return;
    if (update_close_monster_melee()) return;

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        u8 ranged_damage;
        if (thing < 0) continue;
        if (!thing_is_monster(runtime_thing_type(thing))) continue;
        if (enemy_hit_flash[thing]) continue;
        if (enemy_attack_cooldown[thing]) continue;
        if (!enemy_slot_is_readable(slot)) continue;
        ranged_damage = monster_ranged_damage(runtime_thing_type(thing));
        if (ranged_damage && enemies[slot].dist_q8 < 1700 && enemies[slot].screen_h > 18
            && player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) {
            u16 projectile = monster_projectile_type(runtime_thing_type(thing));
            if (projectile) {
                if (!spawn_monster_projectile(thing, projectile, ranged_damage)) continue;
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
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (i == self) continue;
        if (enemy_dead[i] || (!thing_is_monster(runtime_thing_type(i)) && !thing_is_barrel(runtime_thing_type(i)))) continue;
        if (iabs16(x_q8 - thing_x_q8[i]) < MONSTER_SEPARATION_Q8 && iabs16(y_q8 - thing_y_q8[i]) < MONSTER_SEPARATION_Q8) return 1;
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
            thing_x_q8[i] = (short)(x + sx);
        } else if (can_monster_step(i, x, (short)(y + sy))) {
            thing_y_q8[i] = (short)(y + sy);
        }
    } else {
        if (can_monster_step(i, x, (short)(y + sy))) {
            thing_y_q8[i] = (short)(y + sy);
        } else if (can_monster_step(i, (short)(x + sx), y)) {
            thing_x_q8[i] = (short)(x + sx);
        }
    }
}

static void rebuild_monster_path(void) {
    int px, py;
    u16 head = 0;
    u16 tail = 0;
    rc_player_cell(&px, &py);
    for (u16 y = 0; y < MAP_H; y++) {
        for (u16 x = 0; x < MAP_W; x++) monster_path_dist[y][x] = 0xFF;
    }
    if (map_at(px, py)) {
        monster_path_valid = 0;
        return;
    }
    monster_path_dist[py][px] = 0;
    monster_path_queue[tail++] = (u16)(py * MAP_W + px);
    while (head < tail) {
        u16 cell = monster_path_queue[head++];
        u8 d;
        int x = cell % MAP_W;
        int y = cell / MAP_W;
        static const signed char dirs[4][2] = {
            { 1,  0}, {-1,  0}, { 0,  1}, { 0, -1}
        };
        d = monster_path_dist[y][x];
        if (d >= 254) continue;
        for (u8 i = 0; i < 4; i++) {
            int nx = x + dirs[i][0];
            int ny = y + dirs[i][1];
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (map_at(nx, ny)) continue;
            if (monster_path_dist[ny][nx] != 0xFF) continue;
            monster_path_dist[ny][nx] = (u8)(d + 1);
            monster_path_queue[tail++] = (u16)(ny * MAP_W + nx);
        }
    }
    monster_path_valid = 1;
}

static void refresh_monster_path(void) {
    if (!monster_path_valid || monster_path_timer == 0) {
        rebuild_monster_path();
        monster_path_timer = 12;
    } else {
        monster_path_timer--;
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
    if (cx < 0 || cy < 0 || cx >= MAP_W || cy >= MAP_H) return 0;
    best_dist = monster_path_dist[cy][cx];
    if (best_dist == 0xFF || best_dist == 0) return 0;
    best_x = cx;
    best_y = cy;
    for (u8 dir = 0; dir < 4; dir++) {
        int nx = cx + dirs[dir][0];
        int ny = cy + dirs[dir][1];
        u8 d;
        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
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
        if (thing >= 0 && thing_is_monster(runtime_thing_type(thing)) && enemies[slot].screen_w > 0 && enemies[slot].screen_h > 0) count++;
    }
    return count;
}

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
    thing_x_q8[i] = (short)best_x;
    thing_y_q8[i] = (short)best_y;
    set_monster_facing_from_delta(i, px - best_x, py - best_y);
    enemy_hidden_timer[i] = 0;
    enemy_attack_cooldown[i] = 24;
    return 1;
}

static void update_monster_ai(void) {
    int px, py;
    u8 visible_monsters;
    if (++monster_ai_tick & 3) return;
    rc_player_q8(&px, &py);
    refresh_monster_path();
    visible_monsters = visible_monster_slots();
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        int dx, dy, adx, ady;
        if (enemy_dead[i] || !thing_is_monster(runtime_thing_type(i))) continue;
        if (enemy_hit_flash[i]) continue;
        dx = px - thing_x_q8[i];
        dy = py - thing_y_q8[i];
        adx = iabs16(dx);
        ady = iabs16(dy);
        if (adx + ady > WORLD_Q8(4608)) continue;
        if (adx < WORLD_Q8(288) && ady < WORLD_Q8(288)
            && line_of_sight_q8(thing_x_q8[i], thing_y_q8[i], (short)px, (short)py)) continue;
        if (!enemy_awake[i]) {
            enemy_awake[i] = 1;
            enemy_attack_cooldown[i] = 28;
        }

        if (!move_monster_along_path(i)) move_monster_toward(i, dx, dy, adx, ady);
        if (visible_monsters != 0) {
            enemy_hidden_timer[i] = 0;
        } else if (adx + ady < WORLD_Q8(4608) && visible_monsters == 0) {
            if (enemy_hidden_timer[i] < 255) enemy_hidden_timer[i]++;
            if (enemy_hidden_timer[i] > 18 && reveal_hidden_monster_near_player(i, px, py)) visible_monsters = 1;
        }
    }
}

static u8 add_capped_u16(volatile u16 *value, u16 amount, u16 cap) {
    if (*value >= cap) return 0;
    *value = (u16)(*value + amount > cap ? cap : *value + amount);
    return 1;
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
        pickup_message_weapon = 2;
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
        pickup_message_weapon = 3;
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
        pickup_message_weapon = 4;
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2004: /* plasma rifle */
        if (player_has_plasma && player_cells >= player_max_cells) return 0;
        player_has_plasma = 1;
        current_weapon = WEAPON_PLASMA;
        add_capped_u16(&player_cells, 40, player_max_cells);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = 5;
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2005: /* chainsaw */
        if (player_has_chainsaw) return 0;
        player_has_chainsaw = 1;
        current_weapon = WEAPON_CHAINSAW;
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = 1;
        pickup_message_type = 2;
        face_evil_timer = 70;
        break;
    case 2006: /* BFG 9000 */
        if (player_has_bfg && player_cells >= player_max_cells) return 0;
        player_has_bfg = 1;
        current_weapon = WEAPON_BFG;
        add_capped_u16(&player_cells, 40, player_max_cells);
        weapon_frame = 0xFF;
        shown_ammo = 0xFFFF;
        pickup_message_weapon = 7;
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
        if (player_armor >= 100) return 0;
        if (player_armor < 100) player_armor = 100;
        player_armor_class = 1;
        pickup_message_type = 5;
        break;
    case 2019: /* blue armor */
        if (player_armor >= 200) return 0;
        if (player_armor < 200) player_armor = 200;
        player_armor_class = 2;
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
    rc_player_q8(&px, &py);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (enemy_dead[i] || !thing_is_pickup(runtime_thing_type(i))) continue;
        if (iabs16(px - thing_x_q8[i]) <= WORLD_Q8(96) && iabs16(py - thing_y_q8[i]) <= WORLD_Q8(96)) {
            if (apply_pickup(runtime_thing_type(i))) {
                if (player_items < 999) player_items++;
                enemy_dead[i] = 1;
                redraw_minimap_thing_cell(i);
                hide_enemies();
            }
        }
    }
    for (u8 i = 0; i < 8; i++) {
        if (!dynamic_drop_active[i]) continue;
        if (iabs16(px - dynamic_drop_x_q8[i]) <= WORLD_Q8(96) && iabs16(py - dynamic_drop_y_q8[i]) <= WORLD_Q8(96)) {
            if (apply_pickup(dynamic_drop_type[i])) {
                dynamic_drop_active[i] = 0;
                bg_scroll_key = 0xFFFFFFFFUL;
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
    fix_poke((u16)(col + 2), row, (u16)(PAL_HUD_KEY_BASE + key), (u16)(FIX_KEY_BASE + key));
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, FIX_KEY_MSG_K);
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

static void draw_stat3(u16 col, u16 row, u16 label, u16 value) {
    u16 capped = value > 999 ? 999 : value;
    fix_poke(col, row, PAL_MAP_PLAYER, label);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (capped / 100) % 10));
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + (capped / 10) % 10));
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, (u16)(FIX_DIGIT_BASE + capped % 10));
}

static void draw_exit_stats(void) {
    const u16 col = (SCRW / 16) - 2;
    draw_exit_message();
    draw_stat3(col, 12, FIX_KEY_MSG_K, completion_percent(player_kills, level_total_kills));
    draw_stat3(col, 13, (u16)(FIX_EXIT_BASE + 2), completion_percent(player_items, level_total_items));
    draw_stat3(col, 14, FIX_SECRET_S, completion_percent(player_secrets, level_total_secrets));
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
    cell = (u16)(py * MAP_W + px);
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
    rc_player_q8(&px, &py);
    damage = map_cell_damage(px >> 8, py >> 8);
    if (!damage) return;
    player_take_damage(damage);
    floor_damage_timer = 32;
}

static void check_exit_reached(void) {
    int px, py;
    if (level_complete) return;
    rc_player_q8(&px, &py);
    for (u16 i = 0; i < NG_RUNTIME_EXIT_COUNT; i++) {
        const NgRuntimeExit *exit = &g_runtime_exits[i];
        if (iabs16(px - exit->x_q8) <= WORLD_Q8(128) && iabs16(py - exit->y_q8) <= WORLD_Q8(128)) {
            level_complete = 1;
            close_minimap_for_terminal_message();
            hide_enemies();
            draw_exit_message();
            return;
        }
    }
}

static int closed_door_at_cell(int cell_x, int cell_y) {
    if (cell_x < 0 || cell_y < 0 || cell_x >= MAP_W || cell_y >= MAP_H) return -1;
    u8 cell = g_map[cell_y][cell_x];
    if (cell < 2) return -1;
    int door_index = cell - 2;
    if (door_index < 0 || door_index >= NG_RUNTIME_DOOR_COUNT) return -1;
    if (g_runtime_door_open[door_index]) return -1;
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

static void mark_runtime_cell_open(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
    map_bit_set(g_runtime_cell_open, (u16)(y * MAP_W + x));
    if (map_on) draw_minimap_source_cell(x, y);
}

static u8 map_cell_runtime_open(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
    return map_bit_get(g_runtime_cell_open, (u16)(y * MAP_W + x));
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
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) break;
            if (g_map[ny][nx] == 0 || map_cell_runtime_open(nx, ny)) {
                for (u8 carve = 1; carve < step; carve++) {
                    mark_runtime_cell_open(x + dirs[d][0] * carve, y + dirs[d][1] * carve);
                }
                break;
            }
        }
    }
}

static void open_door_index(u16 door_index) {
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
        rc_invalidate_view();
    }
}

static void open_nearby_door(void) {
    int px, py, dir_x, dir_y, plane_x, plane_y;
    int best = -1;
    int best_score = 0x7FFFFFFF;
    rc_player_q8(&px, &py);
    rc_view_q8(&dir_x, &dir_y, &plane_x, &plane_y);

    {
        int door_index = trace_closed_door_in_view(px, py, dir_x, dir_y);
        if (door_index >= 0) {
            open_door_index((u16)door_index);
            return;
        }
    }

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
    if (best < 0) return;
    open_door_index((u16)best);
}

static void map_cell(int mx, int my, u16 pal, u16 tile) {
    fix_poke((u16)(MAP_FIX_COL + mx), (u16)(MAP_FIX_ROW + my), pal, tile);
}

enum {
    HUD_FIX_TOP_ROW = (GAME_H / 8) + 1,
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

static void render_counter_value(u16 base_spr, int x, int y, u16 value) {
    u16 capped = value > 999 ? 999 : value;
    u8 digits[3] = {
        (u8)((capped / 100) % 10),
        (u8)((capped / 10) % 10),
        (u8)(capped % 10),
    };
    for (u8 i = 0; i < 3; i++) {
        u16 spr = (u16)(base_spr + i);
        if ((i == 0 && capped < 100) || (i == 1 && capped < 10)) {
            scb2(spr, 0x0F, 0x00);
            scb3(spr, SCRH + 32, 0, 1);
            continue;
        }
        scb1_tile(spr, 0, (u16)(TILE_HUD_SMALL_DIGIT_BASE + digits[i]), PAL_AMMO_COUNTER);
        scb2(spr, 0x0F, 0xFF);
        scb3(spr, y, 0, 1);
        scb4(spr, (u16)(x + i * 5));
    }
}

static void render_ammo_counters(void) {
    static const u8 row_y[4] = {195, 202, 209, 216};
    render_counter_value(HUD_COUNTER_BASE + 0, 270, row_y[0], player_ammo);
    render_counter_value(HUD_COUNTER_BASE + 3, 298, row_y[0], player_max_bullets);
    render_counter_value(HUD_COUNTER_BASE + 6, 270, row_y[1], player_shells);
    render_counter_value(HUD_COUNTER_BASE + 9, 298, row_y[1], player_max_shells);
    render_counter_value(HUD_COUNTER_BASE + 12, 270, row_y[2], player_rockets);
    render_counter_value(HUD_COUNTER_BASE + 15, 298, row_y[2], player_max_rockets);
    render_counter_value(HUD_COUNTER_BASE + 18, 270, row_y[3], player_cells);
    render_counter_value(HUD_COUNTER_BASE + 21, 298, row_y[3], player_max_cells);
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
    u16 bits = (1 << WEAPON_PISTOL) | (1 << WEAPON_FIST);
    if (player_has_shotgun) bits |= (1 << WEAPON_SHOTGUN);
    if (player_has_chaingun) bits |= (1 << WEAPON_CHAINGUN);
    if (player_has_rocket_launcher) bits |= (1 << WEAPON_ROCKET);
    if (player_has_plasma) bits |= (1 << WEAPON_PLASMA);
    if (player_has_bfg) bits |= (1 << WEAPON_BFG);
    if (player_has_chainsaw) bits |= (1 << WEAPON_CHAINSAW);
    return (u16)(bits | (current_weapon << 8));
}

static int key_sprite_def_for_type(u16 thing_type) {
    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type == thing_type) return i;
    }
    return -1;
}

static void load_hud_key_palette(u16 key, int def_idx) {
    for (int i = 0; i < ENEMY_PALETTE_COLORS; i++) {
        u8 r = g_enemy_palette_rgb[def_idx][i][0];
        u8 g = g_enemy_palette_rgb[def_idx][i][1];
        u8 b = g_enemy_palette_rgb[def_idx][i][2];
        pal_set((u16)(PAL_HUD_KEY_BASE + key), (u16)(i + 1), RGB(r, g, b));
    }
}

static void render_hud_keys(void) {
    static const u8 key_bits[HUD_KEY_COUNT] = {1, 2, 4};
    static const u16 key_thing_types[HUD_KEY_COUNT] = {5, 13, 6};
    static const u8 key_row[HUD_KEY_COUNT] = {24, 25, 26};
    static const u8 key_col = 30;

    for (u16 key = 0; key < HUD_KEY_COUNT; key++) {
        u16 spr = (u16)(HUD_KEY_BASE + key);
        int def_idx = key_sprite_def_for_type(key_thing_types[key]);
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
        scb4(spr, 0);
        fix_poke(key_col, key_row[key], 0, FIX_BLANK);
        if (!(player_keys & key_bits[key]) || def_idx < 0) continue;

        load_hud_key_palette(key, def_idx);
        scb1_tile(spr, 0, (u16)(TILE_HUD_KEYCARD_BASE + key), (u16)(PAL_HUD_KEY_BASE + key));
        scb2(spr, 0x07, 0x7F);
        scb3(spr, (int)(key_row[key] * 8), 0, 1);
        scb4(spr, (u16)(key_col * 8));
    }
    shown_keys = player_keys;
}

static void draw_weapon_status(void) {
    static const u8 arms_weapons[6] = {
        WEAPON_PISTOL, WEAPON_SHOTGUN, WEAPON_CHAINGUN,
        WEAPON_ROCKET, WEAPON_PLASMA, WEAPON_BFG
    };
    static const u8 arms_cols[3] = {11, 13, 15};
    u16 bits = weapon_status_bits();

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
    shown_weapon_status = weapon_status_bits();
}

static void update_status_numbers(u8 pressed) {
    u16 health = player_health;
    u16 ammo = weapon_ammo();
    u16 armor = player_armor;
    u16 frags = player_kills;

    render_hud_value((u16)(HUD_VALUE_BASE + 3), HUD_HEALTH_X, health, 3, PAL_HUD);
    shown_health = health;
    update_hud_face(pressed);
    render_hud_value(HUD_VALUE_BASE, HUD_AMMO_X, ammo, 3, PAL_HUD);
    shown_ammo = ammo;
    render_hud_value((u16)(HUD_VALUE_BASE + 6), HUD_FRAG_X, frags, 2, PAL_HUD);
    shown_frags = frags;
    if (armor_flash_timer) {
        render_hud_value((u16)(HUD_VALUE_BASE + 8), HUD_ARMOR_X, armor, 3, PAL_HUD);
        shown_armor = 0xFFFF;
        armor_flash_timer--;
    } else {
        render_hud_value((u16)(HUD_VALUE_BASE + 8), HUD_ARMOR_X, armor, 3, PAL_HUD);
        shown_armor = armor;
    }
    render_hud_keys();
    render_ammo_counters();
    if (weapon_status_bits() != shown_weapon_status) draw_weapon_status();
}

static void clear_crosshair(void) {
    fix_poke(SCRW / 16, HORIZON / 8, 0, FIX_BLANK);
}

static void force_fix_hud_redraw(void) {
    shown_health = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_armor = 0xFFFF;
    shown_frags = 0xFFFF;
    shown_keys = 0xFF;
    shown_weapon_status = 0xFFFF;
    hud_face_frame = 0xFF;
    update_status_numbers(0);
    clear_crosshair();
    update_center_message();
}

enum {
    MINIMAP_W = 38,
    MINIMAP_H = 23
};

static int minimap_view_x(int map_x) {
    if (map_x < 0) return 0;
    if (map_x >= MAP_W) return MINIMAP_W - 1;
    return (map_x * MINIMAP_W) / MAP_W;
}

static int minimap_view_y(int map_y) {
    if (map_y < 0) return 0;
    if (map_y >= MAP_H) return MINIMAP_H - 1;
    return (map_y * MINIMAP_H) / MAP_H;
}

static int minimap_src_x0(int view_x) {
    return (view_x * MAP_W) / MINIMAP_W;
}

static int minimap_src_x1(int view_x) {
    return (((view_x + 1) * MAP_W) + MINIMAP_W - 1) / MINIMAP_W;
}

static int minimap_src_y0(int view_y) {
    return (view_y * MAP_H) / MINIMAP_H;
}

static int minimap_src_y1(int view_y) {
    return (((view_y + 1) * MAP_H) + MINIMAP_H - 1) / MINIMAP_H;
}

static void draw_minimap_source_cell(int map_x, int map_y);

static u8 minimap_has_closed_door(int vx, int vy) {
    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        if (g_runtime_door_open[i]) continue;
        if (minimap_view_x(g_runtime_doors[i].x) == vx && minimap_view_y(g_runtime_doors[i].y) == vy) return 1;
    }
    return 0;
}

static u8 minimap_has_exit(int vx, int vy) {
    for (u16 i = 0; i < NG_RUNTIME_EXIT_COUNT; i++) {
        if (minimap_view_x(g_runtime_exits[i].x_q8 >> 8) == vx && minimap_view_y(g_runtime_exits[i].y_q8 >> 8) == vy) return 1;
    }
    return 0;
}

static u8 minimap_has_pickup(int vx, int vy) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (enemy_dead[i] || !thing_is_pickup(runtime_thing_type(i))) continue;
        if (minimap_view_x(thing_x_q8[i] >> 8) == vx && minimap_view_y(thing_y_q8[i] >> 8) == vy) return 1;
    }
    for (u8 i = 0; i < 8; i++) {
        if (!dynamic_drop_active[i]) continue;
        if (minimap_view_x(dynamic_drop_x_q8[i] >> 8) == vx && minimap_view_y(dynamic_drop_y_q8[i] >> 8) == vy) return 1;
    }
    return 0;
}

static u8 minimap_has_threat(int vx, int vy) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        u16 type = runtime_thing_type(i);
        if (enemy_dead[i] || (!thing_is_monster(type) && !thing_is_barrel(type) && !thing_is_explosion(type))) continue;
        if (minimap_view_x(thing_x_q8[i] >> 8) == vx && minimap_view_y(thing_y_q8[i] >> 8) == vy) return 1;
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

static void draw_minimap(void) {
    for (int my = 0; my < MINIMAP_H; my++)
        for (int mx = 0; mx < MINIMAP_W; mx++)
            draw_minimap_cell(mx, my);
    minimap_redraw_active = 0;
}

static void start_minimap_redraw(void) {
    minimap_redraw_index = 0;
    minimap_redraw_active = 1;
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
static void clear_minimap(void) {
    minimap_redraw_active = 0;
    for (int my = 0; my < MINIMAP_H; my++)
        for (int mx = 0; mx < MINIMAP_W; mx++)
            map_cell(mx, my, 0, FIX_BLANK);
}

static void close_minimap_for_terminal_message(void) {
    if (!map_on && !minimap_redraw_active) return;
    map_on = 0;
    clear_minimap();
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
 * Keep the current gameplay backdrop as flat shaded bands. The textured
 * perspective cache is still generated for experiments, but the live game uses
 * solid tiles here because the coarse floor/ceiling texture was reading noisier
 * than the original Doom-like wall pass.
 */
static u8 plane_direction_bucket(int dir_x, int dir_y) {
    static const short dirs[TILE_PLANE_PERSPECTIVE_DIRS][2] = {
        { 256,    0}, { 237,   98}, { 181,  181}, {  98,  237},
        {   0,  256}, { -98,  237}, {-181,  181}, {-237,   98},
        {-256,    0}, {-237,  -98}, {-181, -181}, { -98, -237},
        {   0, -256}, {  98, -237}, { 181, -181}, { 237,  -98},
    };
    long best_dot = -2147483647L;
    u8 best = 0;
    for (u8 i = 0; i < TILE_PLANE_PERSPECTIVE_DIRS; i++) {
        long dot = (long)dir_x * dirs[i][0] + (long)dir_y * dirs[i][1];
        if (dot > best_dot) {
            best_dot = dot;
            best = i;
        }
    }
    return best;
}

static u16 perspective_plane_tile(u16 base, u8 direction, u8 phase_x, u8 phase_y, u16 row, u16 col) {
    u16 index = direction;
    index = (u16)(index * TILE_PLANE_PERSPECTIVE_PHASES + phase_y);
    index = (u16)(index * TILE_PLANE_PERSPECTIVE_PHASES + phase_x);
    index = (u16)(index * TILE_PLANE_PERSPECTIVE_ROWS + row);
    index = (u16)(index * TILE_PLANE_PERSPECTIVE_COLS + col);
    return (u16)(base + index);
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
    bg_scroll_key = 0xFFFFFFFFUL;
}

static void update_background_scroll(void) {
    bg_scroll_key = 0;
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
            fire_melee_damage(2);
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
            damage_rocket_target();
        } else if (current_weapon == WEAPON_PLASMA && player_has_plasma) {
            if (player_cells > 0) {
                player_cells--;
                fire_timer = 5;
                chaingun_flash ^= 1;
                trigger_weapon_flash();
                alert_monsters_by_sound();
                fire_single_target_damage(3);
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
                    damage_bfg_visible_targets();
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
    for (u16 i = 0; i < ENEMY_STRIPS; i++) {
        u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + i;
        scb2(spr, 0x0F, 0x00);
        scb3(spr, SCRH + 32, 0, 1);
        scb4(spr, 0);
    }
    enemies[slot].thing_index = -1;
    enemies[slot].screen_w = 0;
    enemies[slot].screen_h = 0;
    enemy_tile_key[slot] = -1;
}

static void reset_enemy_slot_cache(void) {
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        enemy_palette_def[slot] = -1;
        enemy_tile_key[slot] = -1;
        enemy_slot_flash[slot] = 0;
    }
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

static void render_type_slot(u16 slot, int thing_index, u16 thing_type, int sx, int h, int dist_q8, u8 flash) {
    int idx;
    int def_idx = enemy_sprite_def_for_type(thing_type, thing_index);
    const DoomEnemySpriteDef *def = &g_enemy_sprite_defs[def_idx];
    const DoomSpriteScale *meta;

    if (thing_is_monster(thing_type) && h > 0 && h < 34) h = 34;

    enemies[slot].thing_index = thing_index;
    enemies[slot].sprite_def = def_idx;
    enemies[slot].dist_q8 = dist_q8;
    enemies[slot].screen_h = h;
    if (flash || enemy_slot_flash[slot]) load_enemy_hit_palette(slot);
    else load_enemy_palette(slot, def_idx);

    if (h > 110) idx = 0;
    else if (h > 76) idx = 1;
    else if (h > 48) idx = 2;
    else if (h > 30) idx = 3;
    else idx = 4;
    if (thing_is_pickup(thing_type) && idx > 1) idx = 1;
    if (thing_is_corpse(thing_type) && idx > 2) idx = 2;
    if (thing_is_explosion(thing_type) && idx > 2) idx = 2;
    if (thing_is_projectile(thing_type) && idx > 2) idx = 2;
    if (idx >= def->scale_count) idx = def->scale_count - 1;
    meta = &g_enemy_scales[def->first_scale + idx];

    {
        int tile_key = def_idx * 8 + idx;
        if (enemy_tile_key[slot] != tile_key) {
            set_enemy_tiles(slot, meta);
            enemy_tile_key[slot] = tile_key;
        }
    }
    enemies[slot].screen_x = sx - meta->width / 2;
    enemies[slot].screen_w = meta->width;
    {
        u8 rendered = 0;
        int bottom = (GAME_H + h) / 2;
        int top;
        if ((thing_is_explosion(thing_type) && thing_index < 0) || thing_is_projectile(thing_type)) {
            top = (GAME_H - meta->height) / 2;
        } else {
            if (h < 80 && bottom > GAME_H - WEAPON_WIN * 16 + 6) bottom = GAME_H - WEAPON_WIN * 16 + 6;
            top = bottom - meta->height + ENEMY_GROUND_LIFT;
        }
        if (top < 0) top = 0;
        for (u16 j = 0; j < ENEMY_STRIPS; j++) {
            u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + j;
            int strip_x = enemies[slot].screen_x + j * 16;
            if (j < meta->strips && strip_x > -16 && strip_x < SCRW) {
                scb2(spr, 0x0F, 0xFF);
                scb3(spr, top, 0, meta->rows);
                scb4(spr, (u16)strip_x);
                rendered = 1;
            } else {
                scb2(spr, 0x0F, 0x00);
                scb3(spr, SCRH + 32, 0, 1);
                scb4(spr, 0);
            }
        }
        if (!rendered) {
            enemies[slot].thing_index = -1;
            enemies[slot].screen_w = 0;
            enemies[slot].screen_h = 0;
        }
    }
}

static void render_thing_slot(u16 slot, int thing_index, int sx, int h, int dist_q8) {
    u8 flash = (thing_index >= 0 && enemy_hit_flash[thing_index]) ? 1 : 0;
    render_type_slot(slot, thing_index, runtime_thing_type(thing_index), sx, h, dist_q8, flash);
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
    int score;
} ThingCandidate;

static u8 candidate_coord_selected(const ThingCandidate *candidates, int count, short x, short y) {
    for (int slot = 0; slot < count; slot++) {
        if (candidates[slot].x_q8 == x && candidates[slot].y_q8 == y) return 1;
    }
    return 0;
}

static void insert_thing_candidate(ThingCandidate *candidates, int *count, const ThingCandidate *candidate) {
    int insert_at = *count;
    while (insert_at > 0 && candidate->score < candidates[insert_at - 1].score) insert_at--;
    if (insert_at >= ENEMY_VISIBLE_COUNT) return;
    for (int j = ENEMY_VISIBLE_COUNT - 1; j > insert_at; j--) candidates[j] = candidates[j - 1];
    candidates[insert_at] = *candidate;
    if (*count < ENEMY_VISIBLE_COUNT) (*count)++;
}

static int select_visible_things(int found, u8 pass) {
    ThingCandidate candidates[ENEMY_VISIBLE_COUNT];
    int count = 0;
    if (found >= ENEMY_VISIBLE_COUNT) return found;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        candidates[slot].thing_index = -1;
        candidates[slot].dynamic_index = -1;
    }

    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        int sx, h, dist_q8;
        int score;
        ThingCandidate candidate;
        u16 thing_type = runtime_thing_type(i);
        u8 is_monster = thing_is_runtime_threat(thing_type);
        if (enemy_dead[i]) continue;
        if (pass == 1 && !is_monster) continue;
        if (pass == 2 && (!thing_is_pickup(thing_type) || !pickup_is_collectible(thing_type))) continue;
        if (pass == 3 && !thing_is_corpse(thing_type)) continue;
        if (pass == 4 && (!thing_is_pickup(thing_type) || pickup_is_collectible(thing_type))) continue;
        if (candidate_coord_selected(candidates, count, thing_x_q8[i], thing_y_q8[i])) continue;
        if (!rc_project_point(thing_x_q8[i], thing_y_q8[i], &sx, &h, &dist_q8)) {
            if (!player_line_of_sight_to(thing_x_q8[i], thing_y_q8[i])) continue;
            if (!project_point_q8(thing_x_q8[i], thing_y_q8[i], &sx, &h, &dist_q8)) continue;
        }
        if (sx < -48 || sx > SCRW + 48) continue;

        score = dist_q8 + (iabs16(sx - SCRW / 2) >> 1) - (h >> 2);
        candidate.thing_index = i;
        candidate.dynamic_index = -1;
        candidate.thing_type = thing_type;
        candidate.x_q8 = thing_x_q8[i];
        candidate.y_q8 = thing_y_q8[i];
        candidate.sx = sx;
        candidate.h = h;
        candidate.dist_q8 = dist_q8;
        candidate.score = score;
        insert_thing_candidate(candidates, &count, &candidate);
    }

    if (pass == 2 || pass == 4) {
        for (u8 i = 0; i < 8; i++) {
            int sx, h, dist_q8;
            int score;
            ThingCandidate candidate;
            u16 thing_type = dynamic_drop_type[i];
            if (!dynamic_drop_active[i]) continue;
            if (pass == 2 && !pickup_is_collectible(thing_type)) continue;
            if (pass == 4 && pickup_is_collectible(thing_type)) continue;
            if (candidate_coord_selected(candidates, count, dynamic_drop_x_q8[i], dynamic_drop_y_q8[i])) continue;
            if (!rc_project_point(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], &sx, &h, &dist_q8)) {
                if (!player_line_of_sight_to(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i])) continue;
                if (!project_point_q8(dynamic_drop_x_q8[i], dynamic_drop_y_q8[i], &sx, &h, &dist_q8)) continue;
            }
            if (sx < -48 || sx > SCRW + 48) continue;
            score = dist_q8 + (iabs16(sx - SCRW / 2) >> 1) - (h >> 2);
            candidate.thing_index = -1;
            candidate.dynamic_index = (signed char)i;
            candidate.thing_type = thing_type;
            candidate.x_q8 = dynamic_drop_x_q8[i];
            candidate.y_q8 = dynamic_drop_y_q8[i];
            candidate.sx = sx;
            candidate.h = h;
            candidate.dist_q8 = dist_q8;
            candidate.score = score;
            insert_thing_candidate(candidates, &count, &candidate);
        }
    }

    for (int i = 0; i < count && found < ENEMY_VISIBLE_COUNT; i++) {
        if (candidates[i].dynamic_index >= 0) {
            render_type_slot((u16)found, -1, candidates[i].thing_type, candidates[i].sx, candidates[i].h, candidates[i].dist_q8, 0);
        } else {
            render_thing_slot((u16)found, candidates[i].thing_index, candidates[i].sx, candidates[i].h, candidates[i].dist_q8);
        }
        found++;
    }
    return found;
}

static int render_visible_projectile(int found) {
    int sx, h, dist_q8;
    if (!projectile_active || found >= ENEMY_VISIBLE_COUNT) return found;
    if (!project_point_q8(projectile_x_q8, projectile_y_q8, &sx, &h, &dist_q8)) return found;
    render_type_slot((u16)found, -1, projectile_type, sx, h, dist_q8, 0);
    return found + 1;
}

static void render_visible_impact(u16 slot) {
    int sx, h, dist_q8;
    if (!impact_active) return;
    if (!project_point_q8(impact_x_q8, impact_y_q8, &sx, &h, &dist_q8)) return;
    render_type_slot(slot, -1, 9000, sx, h, dist_q8, 0);
}

static void update_enemy(void) {
    int found = 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) enemies[slot].thing_index = -1;

    found = render_visible_projectile(found);
    found = select_visible_things(found, 1);
    found = select_visible_things(found, 2);
    found = select_visible_things(found, 3);
    found = select_visible_things(found, 4);
    for (u16 slot = (u16)found; slot < ENEMY_VISIBLE_COUNT; slot++) hide_enemy_slot(slot);
    render_visible_impact((u16)(ENEMY_VISIBLE_COUNT - 1));
}

static void restart_level(void) {
    prev_px = -1;
    prev_py = -1;
    map_on = 0;
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
    restart_prev = 0;
    hurt_timer = 0;
    floor_damage_timer = 0;
    level_complete = 0;
    bg_scroll_key = 0xFFFFFFFFUL;
    key_message_timer = 0;
    missing_key_bits = 0;
    ammo_message_timer = 0;
    door_message_timer = 0;
    secret_message_timer = 0;
    pickup_message_timer = 0;
    pickup_message_type = 0;
    pickup_message_key = 0;
    key_message_visible = 0;
    monster_ai_tick = 0;
    monster_path_valid = 0;
    monster_path_timer = 0;
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
    face_pain_timer = 0;
    face_evil_timer = 0;
    face_turn_timer = 0;
    face_turn_frame = FACE_STRAIGHT_BASE;
    face_idle_tick = 0;
    face_idle_variant = 0;

    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) g_runtime_door_open[i] = 0;
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

    init_palettes();
    clear_fix();
    disable_sprites();
    init_runtime_things();
    rc_init();
    init_background();
    init_walls();
    init_hud();
    init_weapon();
    reset_enemy_slot_cache();
    hide_enemies();
    compute_level_totals();
    force_fix_hud_redraw();
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
    hide_enemies();
    force_fix_hud_redraw();
    rc_init();
    init_runtime_things();
    compute_level_totals();

    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;
        if (game_active()) {
            enum { D = 0x80 };
            u8 d_now = pressed & D;
            rc_input(pressed);
            update_floor_damage();
            check_secret_reached();
            update_monster_ai();
            collect_nearby_pickups();
            check_exit_reached();
            if (d_now && !door_prev) {
                open_nearby_door();
            }
            door_prev = d_now;
        } else {
            enum { D = 0x80 };
            u8 restart_now = pressed & D;
            if (restart_now && !restart_prev) restart_level();
            restart_prev = restart_now;
            pressed = 0;
        }
        rc_render();                    /* DDA during active display          */
        wait_vblank();
        watchdog_kick();
        update_hurt_flash();
        update_weapon_flash();
        update_background_scroll();
        rc_blit();                      /* push to VRAM during vblank         */
        update_impact_effect();
        update_projectile();
        if (level_complete) hide_enemies();
        else update_enemy();
        update_monster_damage();
        update_weapon(pressed);
        update_enemy_hit_flash();
        update_status_numbers(pressed);
        update_center_message();

        /* C cycles weapons. Hold A+C for the slower debug minimap toggle. */
        {
            enum { A = 0x10, C = 0x40 };
            u8 c_now = pressed & C;
            if (c_now && !map_prev) {
                if (pressed & A) {
                    map_on = !map_on;
                    if (map_on) start_minimap_redraw();
                    else          clear_minimap();
                    force_fix_hud_redraw();
                } else {
                    toggle_weapon();
                }
            }
            map_prev = c_now;
        }

        update_minimap_redraw();
        update_marker();                /* 2 fix writes when the cell changes */
    }
    return 0;
}
