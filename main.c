/* main.c - boot setup and the game loop.
 
 */
#include "hw.h"
#include "config.h"
#include "doom_gfx_generated.h"
#include "raycast.h"
#include "map.h"

unsigned char g_runtime_door_open[NG_RUNTIME_DOOR_COUNT ? NG_RUNTIME_DOOR_COUNT : 1];
static u8 hurt_flash = 0;
static u8 hurt_flash_on = 0;

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
    for (int i = 1; i < 16; i++) {
        pal_set(PAL_MAP_PLAYER, (u16)i, RGB(4, 31, 8)); /* player + HUD digits */
    }
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

static void restore_play_palettes(void) {
    for (int i = 0; i < CEILING_PALETTE_COLORS; i++) {
        pal_set(PAL_CEILING, (u16)(i + 1), RGB(g_ceiling_palette_rgb[i][0], g_ceiling_palette_rgb[i][1], g_ceiling_palette_rgb[i][2]));
    }
    for (int i = 0; i < FLOOR_PALETTE_COLORS; i++) {
        pal_set(PAL_FLOOR, (u16)(i + 1), RGB(g_floor_palette_rgb[i][0], g_floor_palette_rgb[i][1], g_floor_palette_rgb[i][2]));
    }
    for (int i = 0; i < HUD_PALETTE_COLORS; i++) {
        pal_set(PAL_HUD, (u16)(i + 1), RGB(g_hud_palette_rgb[i][0], g_hud_palette_rgb[i][1], g_hud_palette_rgb[i][2]));
    }
    for (int i = 0; i < WEAPON_PALETTE_COLORS; i++) {
        pal_set(PAL_WEAPON, (u16)(i + 1), RGB(g_weapon_palette_rgb[i][0], g_weapon_palette_rgb[i][1], g_weapon_palette_rgb[i][2]));
    }
    for (int b = 0; b < DEPTH_BANDS; b++) {
        int fn = 256 - (b * 200) / (DEPTH_BANDS - 1);
        for (int s = 0; s < 2; s++) {
            int sf = s ? 140 : 256;
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

static void set_hurt_palettes(void) {
    for (int i = 1; i < 16; i++) {
        pal_set(PAL_CEILING, (u16)i, RGB(31, 2, 2));
        pal_set(PAL_FLOOR, (u16)i, RGB(28, 2, 1));
        pal_set(PAL_HUD, (u16)i, RGB(31, 4, 4));
        pal_set(PAL_WEAPON, (u16)i, RGB(31, 5, 4));
    }
    for (int p = 0; p < DEPTH_BANDS * 2; p++) {
        for (int i = 1; i < 16; i++) pal_set((u16)(PAL_DEPTH_BASE + p), (u16)i, RGB(28, 2, 2));
    }
}

static void update_hurt_flash(void) {
    if (hurt_flash) {
        if (!hurt_flash_on) {
            set_hurt_palettes();
            hurt_flash_on = 1;
        }
        hurt_flash--;
    } else if (hurt_flash_on) {
        restore_play_palettes();
        hurt_flash_on = 0;
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
static u8  bg_phase = 0xFF;
static u8  weapon_frame = 0xFF;
static u8  weapon_bob_phase = 0;
static signed char weapon_bob_x = 0;
static signed char weapon_bob_y = 0;
static u8  fire_timer = 0;
static u8  hurt_timer = 0;
static u8  level_complete = 0;
static u8  key_message_timer = 0;
static u8  ammo_message_timer = 0;
static u8  key_message_visible = 0;
static u8  monster_ai_tick = 0;
static u8  player_keys = 0;
static u8  player_has_shotgun = 0;
static u8  current_weapon = 0;
static u8  enemy_dead[NG_RUNTIME_THING_COUNT];
static u8  enemy_hp[NG_RUNTIME_THING_COUNT];
static u8  enemy_hit_flash[NG_RUNTIME_THING_COUNT];
static u16 thing_type_override[NG_RUNTIME_THING_COUNT];
static short thing_x_q8[NG_RUNTIME_THING_COUNT];
static short thing_y_q8[NG_RUNTIME_THING_COUNT];
static int enemy_palette_def[ENEMY_VISIBLE_COUNT] = {-1, -1};
static int enemy_tile_key[ENEMY_VISIBLE_COUNT] = {-1, -1};
static u8 enemy_slot_flash[ENEMY_VISIBLE_COUNT];
static volatile u16 player_health = 100;
static volatile u16 player_armor = 0;
static volatile u16 player_ammo = 50;
static volatile u16 player_shells = 0;
static u16 shown_health = 0xFFFF;
static u16 shown_armor = 0xFFFF;
static u16 shown_ammo = 0xFFFF;
static u8 shown_keys = 0xFF;
static u8 shown_weapon = 0xFF;

typedef struct EnemyDraw {
    int thing_index;
    int sprite_def;
    int screen_x;
    int screen_w;
    int screen_h;
    int dist_q8;
} EnemyDraw;

static EnemyDraw enemies[ENEMY_VISIBLE_COUNT];

static void hide_enemy_slot(u16 slot);
static void hide_enemies(void);
static void map_cell(int mx, int my, u16 pal, u16 tile);

static int iabs16(int value) {
    return value < 0 ? -value : value;
}

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
    case 13:
    case 38:
    case 39:
    case 40:
    case 2007:
    case 2001:
    case 2008:
    case 2010:
    case 2011:
    case 2012:
    case 2014:
    case 2015:
    case 2018:
    case 2019:
    case 2048:
        return 1;
    default:
        return 0;
    }
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

static u8 monster_hp(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return 0;
    if (enemy_hp[thing_index] == 0) {
        enemy_hp[thing_index] = monster_start_hp(runtime_thing_type(thing_index));
    }
    return enemy_hp[thing_index];
}

static int enemy_sprite_def_for_type(u16 thing_type) {
    for (int i = 0; i < ENEMY_SPRITE_COUNT; i++) {
        if (g_enemy_sprite_defs[i].thing_type == thing_type) return i;
    }
    return 0;
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
    if (!thing_is_monster(runtime_thing_type(thing_index))) return 0;
    {
        short x = thing_x_q8[thing_index];
        short y = thing_y_q8[thing_index];
        u16 source_type = runtime_thing_type(thing_index);
        u16 drop_type = monster_drop_type(source_type);
        u8 hp = monster_hp(thing_index);
        if (damage >= hp) hp = 0;
        else hp = (u8)(hp - damage);

        for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
            if (thing_is_monster(runtime_thing_type(i)) && thing_x_q8[i] == x && thing_y_q8[i] == y) {
                enemy_hp[i] = hp;
                if (hp == 0) {
                    if (drop_type) {
                        thing_type_override[i] = drop_type;
                        enemy_dead[i] = 0;
                    } else {
                        enemy_dead[i] = 1;
                    }
                    enemy_hit_flash[i] = 0;
                    killed = 1;
                } else {
                    enemy_hit_flash[i] = 30;
                }
            }
        }
    }
    return killed;
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
    for (int thing = 0; thing < NG_RUNTIME_THING_COUNT; thing++) {
        int sx, h, dist_q8;
        int score;
        if (!thing_is_monster(runtime_thing_type(thing))) continue;
        if (enemy_dead[thing]) continue;
        if (!player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) continue;
        if (!rc_project_point(thing_x_q8[thing], thing_y_q8[thing], &sx, &h, &dist_q8)) continue;
        score = iabs16(sx - SCRW / 2) + (dist_q8 >> 7);
        if (score < best_score) {
            best_score = score;
            best_thing = thing;
        }
    }
    return best_thing;
}

static void damage_best_visible_enemy(u8 damage) {
    damage_visible_enemy(best_visible_enemy(), damage);
}

static void damage_shotgun_spread(void) {
    int targets[ENEMY_VISIBLE_COUNT] = {-1, -1};
    int scores[ENEMY_VISIBLE_COUNT] = {9999, 9999};

    for (int thing = 0; thing < NG_RUNTIME_THING_COUNT; thing++) {
        int sx, h, dist_q8;
        int lateral;
        int score;
        int insert_at;
        if (!thing_is_monster(runtime_thing_type(thing))) continue;
        if (enemy_dead[thing]) continue;
        if (!player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) continue;
        if (!rc_project_point(thing_x_q8[thing], thing_y_q8[thing], &sx, &h, &dist_q8)) continue;

        lateral = iabs16(sx - SCRW / 2);
        if (lateral > 54 && h < 100) continue;
        score = lateral + (dist_q8 >> 8);
        insert_at = ENEMY_VISIBLE_COUNT;
        for (u16 i = 0; i < ENEMY_VISIBLE_COUNT; i++) {
            if (score < scores[i]) {
                insert_at = i;
                break;
            }
        }
        if (insert_at >= ENEMY_VISIBLE_COUNT) continue;
        for (int i = ENEMY_VISIBLE_COUNT - 1; i > insert_at; i--) {
            targets[i] = targets[i - 1];
            scores[i] = scores[i - 1];
        }
        targets[insert_at] = thing;
        scores[insert_at] = score;
    }

    damage_visible_enemy(targets[0], 5);
    damage_visible_enemy(targets[1], 2);
}

static void update_enemy_hit_flash(void) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (enemy_hit_flash[i]) enemy_hit_flash[i]--;
    }
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemy_slot_flash[slot]) enemy_slot_flash[slot]--;
    }
}

static void player_take_damage(u16 amount) {
    while (amount && player_armor) {
        player_armor--;
        amount--;
    }
    hurt_flash = 5;
    if (amount >= player_health) player_health = 0;
    else player_health = (u16)(player_health - amount);
}

static u8 monster_ranged_damage(u16 thing_type) {
    switch (thing_type) {
    case 3004: /* former human */
        return 3;
    case 9:    /* shotgun guy */
        return 5;
    case 3001: /* imp */
        return 4;
    default:
        return 0;
    }
}

static void update_monster_damage(void) {
    if (hurt_timer) {
        hurt_timer--;
        return;
    }
    if (!game_active()) return;

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        u8 ranged_damage;
        if (thing < 0) continue;
        if (!thing_is_monster(runtime_thing_type(thing))) continue;
        if (enemies[slot].dist_q8 < 300) {
            player_take_damage(4);
            hurt_timer = 24;
            return;
        }
        ranged_damage = monster_ranged_damage(runtime_thing_type(thing));
        if (ranged_damage && enemies[slot].dist_q8 < 1700 && enemies[slot].screen_h > 18
            && player_line_of_sight_to(thing_x_q8[thing], thing_y_q8[thing])) {
            player_take_damage(ranged_damage);
            hurt_timer = 48;
            return;
        }
    }
}

static u8 can_monster_step(short x_q8, short y_q8) {
    int cx = x_q8 >> 8;
    int cy = y_q8 >> 8;
    if (map_at(cx, cy)) return 0;
    if (map_at((x_q8 - 52) >> 8, cy)) return 0;
    if (map_at((x_q8 + 52) >> 8, cy)) return 0;
    if (map_at(cx, (y_q8 - 52) >> 8)) return 0;
    if (map_at(cx, (y_q8 + 52) >> 8)) return 0;
    return 1;
}

static void update_monster_ai(void) {
    int px, py;
    if (++monster_ai_tick & 7) return;
    rc_player_q8(&px, &py);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        int dx, dy, adx, ady;
        short nx, ny;
        if (enemy_dead[i] || !thing_is_monster(runtime_thing_type(i))) continue;
        dx = px - thing_x_q8[i];
        dy = py - thing_y_q8[i];
        adx = iabs16(dx);
        ady = iabs16(dy);
        if (adx + ady > 2304) continue;
        if (adx < 288 && ady < 288) continue;
        if (!line_of_sight_q8((short)px, (short)py, thing_x_q8[i], thing_y_q8[i])) continue;

        nx = thing_x_q8[i];
        ny = thing_y_q8[i];
        if (adx > ady) nx = (short)(nx + (dx < 0 ? -12 : 12));
        else ny = (short)(ny + (dy < 0 ? -12 : 12));
        if (can_monster_step(nx, ny)) {
            thing_x_q8[i] = nx;
            thing_y_q8[i] = ny;
        }
    }
}

static void apply_pickup(u16 thing_type) {
    u8 key = key_bit_for_thing(thing_type);
    if (key) {
        player_keys |= key;
        shown_keys = 0xFF;
        return;
    }

    switch (thing_type) {
    case 2001: /* shotgun */
        player_has_shotgun = 1;
        current_weapon = 1;
        player_shells += 8;
        weapon_frame = 0xFF;
        shown_weapon = 0xFF;
        break;
    case 2007: /* clip */
        player_ammo += 10;
        break;
    case 2008: /* shells */
        player_shells += 4;
        break;
    case 2010: /* rocket */
        player_ammo += 1;
        break;
    case 2011: /* stimpack */
        player_health = (u16)(player_health + 10 > 100 ? 100 : player_health + 10);
        break;
    case 2012: /* medikit */
        player_health = (u16)(player_health + 25 > 100 ? 100 : player_health + 25);
        break;
    case 2014: /* health bonus */
        if (player_health < 200) player_health++;
        break;
    case 2015: /* armor bonus */
        if (player_armor < 200) player_armor++;
        break;
    case 2018: /* green armor */
        if (player_armor < 100) player_armor = 100;
        break;
    case 2019: /* blue armor */
        if (player_armor < 200) player_armor = 200;
        break;
    case 2048: /* ammo box */
        player_ammo += 50;
        break;
    default:
        break;
    }
    if (player_ammo > 999) player_ammo = 999;
    if (player_shells > 999) player_shells = 999;
}

static void collect_nearby_pickups(void) {
    int px, py;
    rc_player_q8(&px, &py);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        if (enemy_dead[i] || !thing_is_pickup(runtime_thing_type(i))) continue;
        if (iabs16(px - thing_x_q8[i]) <= 96 && iabs16(py - thing_y_q8[i]) <= 96) {
            apply_pickup(runtime_thing_type(i));
            enemy_dead[i] = 1;
            hide_enemies();
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

static void draw_key_message(void) {
    const u16 col = (SCRW / 16) - 1;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_KEY_MSG_K);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_EXIT_BASE);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, (u16)(FIX_KEY_BASE + 2));
}

static void draw_ammo_message(void) {
    const u16 col = (SCRW / 16) - 2;
    const u16 row = (GAME_H / 16) - 2;
    fix_poke(col, row, PAL_MAP_PLAYER, FIX_DEAD_A);
    fix_poke((u16)(col + 1), row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 2), row, PAL_MAP_PLAYER, FIX_AMMO_M);
    fix_poke((u16)(col + 3), row, PAL_MAP_PLAYER, FIX_AMMO_O);
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
        draw_exit_message();
    } else if (key_message_timer) {
        draw_key_message();
        key_message_timer--;
        key_message_visible = 1;
    } else if (ammo_message_timer) {
        draw_ammo_message();
        ammo_message_timer--;
        key_message_visible = 1;
    } else if (key_message_visible) {
        clear_center_message();
        key_message_visible = 0;
    }
}

static void check_exit_reached(void) {
    int px, py;
    if (level_complete) return;
    rc_player_q8(&px, &py);
    for (u16 i = 0; i < NG_RUNTIME_EXIT_COUNT; i++) {
        const NgRuntimeExit *exit = &g_runtime_exits[i];
        if (iabs16(px - exit->x_q8) <= 128 && iabs16(py - exit->y_q8) <= 128) {
            level_complete = 1;
            hide_enemies();
            draw_exit_message();
            return;
        }
    }
}

static void open_nearby_door(void) {
    int px, py;
    rc_player_q8(&px, &py);
    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) {
        const NgRuntimeDoor *door = &g_runtime_doors[i];
        u8 required_key = key_bit_for_door(door->special);
        int dx = px - ((int)door->x * 256 + 128);
        int dy = py - ((int)door->y * 256 + 128);
        if (g_runtime_door_open[i]) continue;
        if (iabs16(dx) <= 384 && iabs16(dy) <= 384) {
            if (required_key && (player_keys & required_key) == 0) {
                key_message_timer = 60;
                return;
            }
            g_runtime_door_open[i] = 1;
            rc_invalidate_view();
            if (map_on) map_cell(door->x, door->y, 0, FIX_BLANK);
        }
    }
}

static void map_cell(int mx, int my, u16 pal, u16 tile) {
    fix_poke((u16)(MAP_FIX_COL + mx), (u16)(MAP_FIX_ROW + my), pal, tile);
}

static void draw_number3(u16 col, u16 row, u16 value, u16 pal) {
    if (value > 999) value = 999;
    fix_poke(col, row, pal, (u16)(FIX_DIGIT_BASE + value / 100));
    fix_poke((u16)(col + 1), row, pal, (u16)(FIX_DIGIT_BASE + (value / 10) % 10));
    fix_poke((u16)(col + 2), row, pal, (u16)(FIX_DIGIT_BASE + value % 10));
}

static u16 weapon_ammo(void) {
    return current_weapon && player_has_shotgun ? player_shells : player_ammo;
}

static void update_status_numbers(void) {
    u16 health = player_health;
    u16 ammo = weapon_ammo();
    u16 armor = player_armor;
    u8 weapon = current_weapon && player_has_shotgun ? 2 : 1;
    if (health != shown_health) {
        draw_number3(3, 26, health, PAL_MAP_PLAYER);
        shown_health = health;
    }
    if (weapon != shown_weapon) {
        fix_poke(16, 26, player_has_shotgun ? PAL_MAP_PLAYER : PAL_MAP_WALL, (u16)(FIX_DIGIT_BASE + weapon));
        shown_weapon = weapon;
    }
    if (ammo != shown_ammo) {
        draw_number3(18, 26, ammo, PAL_MAP_PLAYER);
        shown_ammo = ammo;
    }
    if (armor != shown_armor) {
        draw_number3(33, 26, armor, PAL_MAP_PLAYER);
        shown_armor = armor;
    }
    if (player_keys != shown_keys) {
        fix_poke(36, 26, (player_keys & 1) ? PAL_MAP_PLAYER : PAL_MAP_WALL, FIX_KEY_BASE);
        fix_poke(37, 26, (player_keys & 2) ? PAL_MAP_PLAYER : PAL_MAP_WALL, (u16)(FIX_KEY_BASE + 1));
        fix_poke(38, 26, (player_keys & 4) ? PAL_MAP_PLAYER : PAL_MAP_WALL, (u16)(FIX_KEY_BASE + 2));
        shown_keys = player_keys;
    }
}

static void draw_crosshair(void) {
    fix_poke(SCRW / 16, HORIZON / 8, PAL_MAP_PLAYER, FIX_AIM);
}

static void force_fix_hud_redraw(void) {
    shown_health = 0xFFFF;
    shown_ammo = 0xFFFF;
    shown_armor = 0xFFFF;
    shown_keys = 0xFF;
    shown_weapon = 0xFF;
    update_status_numbers();
    draw_crosshair();
    update_center_message();
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

static void toggle_weapon(void) {
    if (!player_has_shotgun) return;
    current_weapon ^= 1;
    weapon_frame = 0xFF;
    shown_ammo = 0xFFFF;
    shown_weapon = 0xFF;
}

static void set_weapon_position(signed char bob_x, signed char bob_y) {
    u16 start_x = (u16)((SCRW - WEAPON_COUNT * 16) / 2);
    int top = GAME_H - WEAPON_WIN * 16 + bob_y;
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
    static u8 b_prev = 0;
    u8 b_now = pressed & B;
    if (b_now && !b_prev && fire_timer == 0) {
        if (current_weapon && player_has_shotgun && player_shells > 0) {
            player_shells--;
            fire_timer = 16;
            damage_shotgun_spread();
        } else if (player_ammo > 0) {
            current_weapon = 0;
            shown_weapon = 0xFF;
            player_ammo--;
            fire_timer = 12;
            damage_best_visible_enemy(1);
        } else {
            ammo_message_timer = 45;
        }
    }
    b_prev = b_now;

    u8 frame = current_weapon && player_has_shotgun ? 4 : 0;
    if (fire_timer) {
        u8 base = current_weapon && player_has_shotgun ? 4 : 0;
        if (fire_timer > 12) frame = (u8)(base + 1);
        else if (fire_timer > 6) frame = (u8)(base + 2);
        else frame = (u8)(base + 3);
        fire_timer--;
    }
    if (frame != weapon_frame) set_weapon_frame(frame);
    update_weapon_bob(pressed);
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

static u8 enemy_obscured_by_weapon(int sx, int h) {
    return h < 80 && sx > (SCRW / 2 - 40) && sx < (SCRW / 2 + 40);
}

static void render_thing_slot(u16 slot, int thing_index, int sx, int h, int dist_q8) {
    u16 thing_type = runtime_thing_type(thing_index);
    int idx;
    int def_idx = enemy_sprite_def_for_type(thing_type);
    const DoomEnemySpriteDef *def = &g_enemy_sprite_defs[def_idx];
    const DoomSpriteScale *meta;

    enemies[slot].thing_index = thing_index;
    enemies[slot].sprite_def = def_idx;
    enemies[slot].dist_q8 = dist_q8;
    enemies[slot].screen_h = h;
    if (enemy_hit_flash[thing_index] || enemy_slot_flash[slot]) load_enemy_hit_palette(slot);
    else load_enemy_palette(slot, def_idx);

    if (h > 110) idx = 0;
    else if (h > 76) idx = 1;
    else if (h > 48) idx = 2;
    else if (h > 30) idx = 3;
    else idx = 4;
    if (thing_is_pickup(thing_type) && idx > 1) idx = 1;
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
        int bottom = (GAME_H + h) / 2;
        if (h < 80 && bottom > GAME_H - WEAPON_WIN * 16 + 6) bottom = GAME_H - WEAPON_WIN * 16 + 6;
        int top = bottom - meta->height;
        if (top < 0) top = 0;
        for (u16 j = 0; j < ENEMY_STRIPS; j++) {
            u16 spr = ENEMY_BASE + slot * ENEMY_STRIPS + j;
            if (j < meta->strips) {
                scb2(spr, 0x0F, 0xFF);
                scb3(spr, top, 0, meta->rows);
                scb4(spr, (u16)(enemies[slot].screen_x + j * 16));
            } else {
                scb2(spr, 0x0F, 0x00);
                scb3(spr, SCRH + 32, 0, 1);
                scb4(spr, 0);
            }
        }
    }
}

typedef struct ThingCandidate {
    int thing_index;
    int sx;
    int h;
    int dist_q8;
    int score;
} ThingCandidate;

static u8 candidate_coord_selected(const ThingCandidate *candidates, int count, short x, short y) {
    for (int slot = 0; slot < count; slot++) {
        int thing = candidates[slot].thing_index;
        if (thing >= 0 && thing_x_q8[thing] == x && thing_y_q8[thing] == y) return 1;
    }
    return 0;
}

static int select_visible_things(int found, u8 want_monsters) {
    ThingCandidate candidates[ENEMY_VISIBLE_COUNT];
    int count = 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) candidates[slot].thing_index = -1;

    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        int sx, h, dist_q8;
        int score;
        int insert_at;
        u8 is_monster = thing_is_monster(runtime_thing_type(i));
        if (enemy_dead[i]) continue;
        if (want_monsters != is_monster) continue;
        if (candidate_coord_selected(candidates, count, thing_x_q8[i], thing_y_q8[i])) continue;
        if (!rc_project_point(thing_x_q8[i], thing_y_q8[i], &sx, &h, &dist_q8)) continue;
        if (enemy_obscured_by_weapon(sx, h)) continue;

        score = dist_q8 + (iabs16(sx - SCRW / 2) >> 1) - (h >> 2);
        insert_at = count;
        while (insert_at > 0 && score < candidates[insert_at - 1].score) insert_at--;
        if (insert_at >= ENEMY_VISIBLE_COUNT) continue;
        for (int j = ENEMY_VISIBLE_COUNT - 1; j > insert_at; j--) candidates[j] = candidates[j - 1];
        candidates[insert_at].thing_index = i;
        candidates[insert_at].sx = sx;
        candidates[insert_at].h = h;
        candidates[insert_at].dist_q8 = dist_q8;
        candidates[insert_at].score = score;
        if (count < ENEMY_VISIBLE_COUNT) count++;
    }

    for (int i = 0; i < count && found < ENEMY_VISIBLE_COUNT; i++) {
        render_thing_slot((u16)found, candidates[i].thing_index, candidates[i].sx, candidates[i].h, candidates[i].dist_q8);
        found++;
    }
    return found;
}

static void update_enemy(void) {
    int found = 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) enemies[slot].thing_index = -1;

    found = select_visible_things(found, 1);
    found = select_visible_things(found, 0);
    for (u16 slot = (u16)found; slot < ENEMY_VISIBLE_COUNT; slot++) hide_enemy_slot(slot);
}

static void restart_level(void) {
    prev_px = -1;
    prev_py = -1;
    map_on = 0;
    bg_phase = 0xFF;
    weapon_frame = 0xFF;
    weapon_bob_phase = 0;
    weapon_bob_x = 0;
    weapon_bob_y = 0;
    fire_timer = 0;
    hurt_timer = 0;
    level_complete = 0;
    key_message_timer = 0;
    ammo_message_timer = 0;
    key_message_visible = 0;
    monster_ai_tick = 0;
    player_keys = 0;
    player_has_shotgun = 0;
    current_weapon = 0;
    player_health = 100;
    player_armor = 0;
    player_ammo = 50;
    player_shells = 0;
    hurt_flash = 0;
    hurt_flash_on = 0;

    for (u16 i = 0; i < NG_RUNTIME_DOOR_COUNT; i++) g_runtime_door_open[i] = 0;
    for (u16 i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        enemy_dead[i] = 0;
        enemy_hp[i] = 0;
        enemy_hit_flash[i] = 0;
        thing_type_override[i] = 0;
    }
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        enemy_palette_def[slot] = -1;
        enemy_tile_key[slot] = -1;
        enemy_slot_flash[slot] = 0;
    }

    init_palettes();
    clear_fix();
    disable_sprites();
    init_runtime_things();
    rc_init();
    init_background();
    init_walls();
    init_hud();
    init_weapon();
    hide_enemies();
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

    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;    
        if (game_active()) {
            enum { A = 0x10, D = 0x80 };
            static u8 d_prev = 0;
            u8 d_now = pressed & D;
            rc_input(pressed);
            update_monster_ai();
            collect_nearby_pickups();
            check_exit_reached();
            if (d_now && !d_prev) {
                if (pressed & A) toggle_weapon();
                else open_nearby_door();
            }
            d_prev = d_now;
        } else {
            enum { D = 0x80 };
            static u8 restart_prev = 0;
            u8 restart_now = pressed & D;
            if (restart_now && !restart_prev) restart_level();
            restart_prev = restart_now;
            pressed = 0;
        }
        rc_render();                    /* DDA during active display          */
        wait_vblank();
        watchdog_kick();
        update_hurt_flash();
        rc_blit();                      /* push to VRAM during vblank         */
        update_background_phase(rc_bg_phase());
        if (level_complete) hide_enemies();
        else update_enemy();
        update_monster_damage();
        update_weapon(pressed);
        update_enemy_hit_flash();
        update_status_numbers();
        update_center_message();

        /* button C toggles the minimap  */
        {
            enum { C = 0x40 };          /* P1 bit 6                            */
            static u8 c_prev = 0;
            u8 c_now = pressed & C;
            if (c_now && !c_prev) {
                map_on = !map_on;
                if (map_on) { draw_minimap(); prev_px = -1; }  /* -1 forces marker repaint */
                else          clear_minimap();
                force_fix_hud_redraw();
            }
            c_prev = c_now;
        }

        update_marker();                /* 2 fix writes when the cell changes */
    }
    return 0;
}
