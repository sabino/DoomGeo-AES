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
static u8  hurt_timer = 0;
static u8  enemy_dead[NG_RUNTIME_THING_COUNT];
static int enemy_palette_def[ENEMY_VISIBLE_COUNT] = {-1, -1};
static int enemy_tile_key[ENEMY_VISIBLE_COUNT] = {-1, -1};
static volatile u16 player_health = 100;
static volatile u16 player_armor = 0;
static volatile u16 player_ammo = 50;
static u16 shown_health = 0xFFFF;
static u16 shown_armor = 0xFFFF;
static u16 shown_ammo = 0xFFFF;

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

static int iabs16(int value) {
    return value < 0 ? -value : value;
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
    case 2007:
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

static void kill_enemy_at(int thing_index) {
    if (thing_index < 0 || thing_index >= NG_RUNTIME_THING_COUNT) return;
    if (!thing_is_monster(g_runtime_things[thing_index].type)) return;
    {
        short x = g_runtime_things[thing_index].x_q8;
        short y = g_runtime_things[thing_index].y_q8;
        for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
            if (thing_is_monster(g_runtime_things[i].type) && g_runtime_things[i].x_q8 == x && g_runtime_things[i].y_q8 == y) {
                enemy_dead[i] = 1;
            }
        }
    }
}

static void kill_visible_enemies(void) {
    u8 killed = 0;
    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        if (enemies[slot].thing_index < 0) continue;
        kill_enemy_at(enemies[slot].thing_index);
        killed = 1;
    }
    if (killed) hide_enemies();
}

static void player_take_damage(u16 amount) {
    while (amount && player_armor) {
        player_armor--;
        amount--;
    }
    if (amount >= player_health) player_health = 0;
    else player_health = (u16)(player_health - amount);
}

static void update_monster_damage(void) {
    if (hurt_timer) {
        hurt_timer--;
        return;
    }
    if (player_health == 0) return;

    for (u16 slot = 0; slot < ENEMY_VISIBLE_COUNT; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing < 0) continue;
        if (!thing_is_monster(g_runtime_things[thing].type)) continue;
        if (enemies[slot].dist_q8 < 300) {
            player_take_damage(4);
            hurt_timer = 24;
            return;
        }
    }
}

static void apply_pickup(u16 thing_type) {
    switch (thing_type) {
    case 2007: /* clip */
        player_ammo += 10;
        break;
    case 2008: /* shells */
        player_ammo += 4;
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
}

static void collect_nearby_pickups(void) {
    int px, py;
    rc_player_q8(&px, &py);
    for (int i = 0; i < NG_RUNTIME_THING_COUNT; i++) {
        const NgRuntimeThing *thing = &g_runtime_things[i];
        if (enemy_dead[i] || !thing_is_pickup(thing->type)) continue;
        if (iabs16(px - thing->x_q8) <= 96 && iabs16(py - thing->y_q8) <= 96) {
            apply_pickup(thing->type);
            enemy_dead[i] = 1;
            hide_enemies();
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

static void update_status_numbers(void) {
    u16 health = player_health;
    u16 ammo = player_ammo;
    u16 armor = player_armor;
    if (health != shown_health) {
        draw_number3(3, 26, health, PAL_MAP_PLAYER);
        shown_health = health;
    }
    if (ammo != shown_ammo) {
        draw_number3(18, 26, ammo, PAL_MAP_PLAYER);
        shown_ammo = ammo;
    }
    if (armor != shown_armor) {
        draw_number3(33, 26, armor, PAL_MAP_PLAYER);
        shown_armor = armor;
    }
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
    if (b_now && !b_prev && fire_timer == 0) {
        if (player_ammo > 0) {
            player_ammo--;
            fire_timer = 12;
            kill_visible_enemies();
        }
    }
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

static u8 enemy_coord_selected(int count, short x, short y) {
    for (int slot = 0; slot < count; slot++) {
        int thing = enemies[slot].thing_index;
        if (thing >= 0 && g_runtime_things[thing].x_q8 == x && g_runtime_things[thing].y_q8 == y) return 1;
    }
    return 0;
}

static u8 enemy_obscured_by_weapon(int sx, int h) {
    return h < 80 && sx > (SCRW / 2 - 40) && sx < (SCRW / 2 + 40);
}

static void render_thing_slot(u16 slot, int thing_index, int sx, int h, int dist_q8) {
    const NgRuntimeThing *thing = &g_runtime_things[thing_index];
    int idx;
    int def_idx = enemy_sprite_def_for_type(thing->type);
    const DoomEnemySpriteDef *def = &g_enemy_sprite_defs[def_idx];
    const DoomSpriteScale *meta;

    enemies[slot].thing_index = thing_index;
    enemies[slot].sprite_def = def_idx;
    enemies[slot].dist_q8 = dist_q8;
    enemies[slot].screen_h = h;
    load_enemy_palette(slot, def_idx);

    if (h > 110) idx = 0;
    else if (h > 76) idx = 1;
    else if (h > 48) idx = 2;
    else if (h > 30) idx = 3;
    else idx = 4;
    if (thing_is_pickup(thing->type) && idx > 1) idx = 1;
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

static int select_visible_things(int found, u8 want_monsters) {
    for (int i = 0; i < NG_RUNTIME_THING_COUNT && found < ENEMY_VISIBLE_COUNT; i++) {
        const NgRuntimeThing *thing = &g_runtime_things[i];
        int sx, h, dist_q8;
        u8 is_monster = thing_is_monster(thing->type);
        if (enemy_dead[i]) continue;
        if (want_monsters != is_monster) continue;
        if (enemy_coord_selected(found, thing->x_q8, thing->y_q8)) continue;
        if (!rc_project_point(thing->x_q8, thing->y_q8, &sx, &h, &dist_q8)) continue;
        if (enemy_obscured_by_weapon(sx, h)) continue;

        render_thing_slot((u16)found, i, sx, h, dist_q8);
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
    update_status_numbers();
    rc_init();

    for (;;) {
        u8 pressed = (u8)~REG_P1CNT;    
        rc_input(pressed);
        collect_nearby_pickups();
        rc_render();                    /* DDA during active display          */
        wait_vblank();
        watchdog_kick();
        rc_blit();                      /* push to VRAM during vblank         */
        update_background_phase(rc_bg_phase());
        update_enemy();
        update_monster_damage();
        update_weapon(pressed);
        update_status_numbers();

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
