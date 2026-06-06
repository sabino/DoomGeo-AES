/* simple_map.h - hand-authored NGRayEx-style map on top of generated assets.
 *
 * Keep the generated map's public sizes/counts so the rest of the runtime keeps
 * its normal memory layout.  Only the geometry and runtime object tables used
 * by C code are redirected to a compact authored test map.
 */
#ifndef SIMPLE_MAP_H_INCLUDED
#define SIMPLE_MAP_H_INCLUDED

#define map_at doom_generated_map_at
#define map_cell_value doom_generated_map_cell_value
#define map_cell_texture doom_generated_map_cell_texture
#define map_cell_texture_phase doom_generated_map_cell_texture_phase
#define map_cell_damage doom_generated_map_cell_damage
#define map_cell_floor_visual doom_generated_map_cell_floor_visual
#define map_cell_light doom_generated_map_cell_light
#define map_cell_floor_height doom_generated_map_cell_floor_height
#define map_cell_ceiling_height doom_generated_map_cell_ceiling_height
#define map_cell_secret doom_generated_map_cell_secret
#include "doom_map_generated.h"
#undef map_at
#undef map_cell_value
#undef map_cell_texture
#undef map_cell_texture_phase
#undef map_cell_damage
#undef map_cell_floor_visual
#undef map_cell_light
#undef map_cell_floor_height
#undef map_cell_ceiling_height
#undef map_cell_secret

#undef DOOM_MAP_SOURCE
#undef DOOM_MAP_NAME
#undef DOOM_NEXT_MAP_EPISODE
#undef DOOM_NEXT_MAP_MISSION
#undef DOOM_SECRET_NEXT_MAP_EPISODE
#undef DOOM_SECRET_NEXT_MAP_MISSION
#undef DOOM_START_X
#undef DOOM_START_Y
#undef DOOM_DIR_X
#undef DOOM_DIR_Y
#undef DOOM_PLANE_X
#undef DOOM_PLANE_Y

#define DOOM_MAP_SOURCE "simple-map"
#define DOOM_MAP_NAME "SIMPLE"
#define DOOM_NEXT_MAP_EPISODE 1
#define DOOM_NEXT_MAP_MISSION 1
#define DOOM_SECRET_NEXT_MAP_EPISODE 0
#define DOOM_SECRET_NEXT_MAP_MISSION 0
#define DOOM_START_X 8.5
#define DOOM_START_Y 12.5
#define DOOM_DIR_X 0.0
#define DOOM_DIR_Y -1.0
#define DOOM_PLANE_X 0.66
#define DOOM_PLANE_Y 0.0

#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#define ACTIVE_MAP_W SIMPLE_MAP_W
#define ACTIVE_MAP_H SIMPLE_MAP_H
#define ACTIVE_MAP_CELL_BYTES (((ACTIVE_MAP_W * ACTIVE_MAP_H) + 7) / 8)

static const unsigned char g_simple_map[SIMPLE_MAP_H][SIMPLE_MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,1,1,1,0,0,1,0,0,0,0,1},
    {1,0,1,0,0,0,0,1,0,0,1,0,1,1,0,1},
    {1,0,1,0,1,1,0,1,0,0,1,0,1,0,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,1,1,1,0,0,0,0,0,1,1,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,0,1,0,0,0,1,1,1,0,1,0,1,1},
    {1,0,1,0,0,0,0,0,1,0,0,0,1,0,0,1},
    {1,0,1,1,1,0,1,1,1,0,0,0,1,0,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const NgRuntimeThing g_simple_runtime_things[NG_RUNTIME_THING_COUNT] = {
    { 7 * 256 + 128,  9 * 256 + 128, 3004, 7 },
    { 8 * 256 + 128,  6 * 256 + 128, 3001, 7 },
    {13 * 256 + 128,  5 * 256 + 128, 3004, 7 },
    {11 * 256 + 128, 11 * 256 + 128, 2035, 7 },
    { 4 * 256 + 128, 12 * 256 + 128, 5,    7 },
    { 6 * 256 + 128, 12 * 256 + 128, 2011, 7 },
    { 9 * 256 + 128, 12 * 256 + 128, 2007, 7 },
    {13 * 256 + 128,  9 * 256 + 128, 2001, 7 },
    { 2 * 256 + 128,  2 * 256 + 128, 2018, 7 }
};

static const NgRuntimeExit g_simple_runtime_exits[NG_RUNTIME_EXIT_COUNT] = {
    {14 * 256 + 128, 1 * 256 + 128, 11, 1, 1}
};

static const NgRuntimeDoor g_simple_runtime_doors[1] = {
    {10, 7, 26}
};

#define g_runtime_things g_simple_runtime_things
#define g_runtime_exits g_simple_runtime_exits
#define g_runtime_doors g_simple_runtime_doors

static inline int simple_map_in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < SIMPLE_MAP_W && y < SIMPLE_MAP_H;
}

static inline int map_at(int x, int y) {
    unsigned char cell;
    if (!simple_map_in_bounds(x, y)) return 1;
    if (g_runtime_cell_open[((y * ACTIVE_MAP_W) + x) >> 3] & (1 << (((y * ACTIVE_MAP_W) + x) & 7))) return 0;
    cell = g_simple_map[y][x];
    if (!cell) return 0;
#if NG_RUNTIME_DOOR_COUNT > 0
    if (cell >= 2) return g_runtime_door_open[cell - 2] ? 0 : 1;
#else
    if (cell >= 2) return 1;
#endif
    return 1;
}

static inline unsigned char map_cell_value(int x, int y) {
    if (!simple_map_in_bounds(x, y)) return 1;
    return g_simple_map[y][x];
}

static inline unsigned char map_cell_texture(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

static inline unsigned char map_cell_texture_phase(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

static inline unsigned char map_cell_damage(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

static inline unsigned char map_cell_floor_visual(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

static inline unsigned char map_cell_light(int x, int y) {
    (void)x;
    (void)y;
    return 3;
}

static inline short map_cell_floor_height(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

static inline short map_cell_ceiling_height(int x, int y) {
    (void)x;
    (void)y;
    return 128;
}

static inline unsigned char map_cell_secret(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

#endif /* SIMPLE_MAP_H_INCLUDED */
