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

/* The authored simple map owns its progression fixtures.  The tiny generated
 * 16x16 WAD projection may not find a usable door, so keep one runtime door
 * slot for the hand-authored blue-key exit door below. */
#undef DOOM_CONVERTED_DOORS
#undef NG_RUNTIME_DOOR_COUNT
#define DOOM_CONVERTED_DOORS 1
#define NG_RUNTIME_DOOR_COUNT 1
extern unsigned char g_runtime_door_open[NG_RUNTIME_DOOR_COUNT ? NG_RUNTIME_DOOR_COUNT : 1];

#if DOOM_CHUNKED_SIMPLE_MAP
#include "doom_chunks_generated.h"
#endif

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

#if DOOM_CHUNKED_SIMPLE_MAP
#define DOOM_MAP_SOURCE "chunk-map"
#define DOOM_MAP_NAME DOOM_CHUNK_MAP_NAME
#else
#define DOOM_MAP_SOURCE "simple-map"
#define DOOM_MAP_NAME "SIMPLE"
#endif
#define DOOM_NEXT_MAP_EPISODE 1
#define DOOM_NEXT_MAP_MISSION 1
#define DOOM_SECRET_NEXT_MAP_EPISODE 0
#define DOOM_SECRET_NEXT_MAP_MISSION 0
#if DOOM_CHUNKED_SIMPLE_MAP
#define DOOM_START_X DOOM_CHUNK_START_X
#define DOOM_START_Y DOOM_CHUNK_START_Y
#define DOOM_DIR_X DOOM_CHUNK_START_DIR_X
#define DOOM_DIR_Y DOOM_CHUNK_START_DIR_Y
#define DOOM_PLANE_X DOOM_CHUNK_START_PLANE_X
#define DOOM_PLANE_Y DOOM_CHUNK_START_PLANE_Y
#else
#define DOOM_START_X 8.5
#define DOOM_START_Y 12.5
#define DOOM_DIR_X 0.0
#define DOOM_DIR_Y -1.0
#define DOOM_PLANE_X 0.66
#define DOOM_PLANE_Y 0.0
#endif

#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#define ACTIVE_MAP_W SIMPLE_MAP_W
#define ACTIVE_MAP_H SIMPLE_MAP_H
#define ACTIVE_MAP_CELL_BYTES (((ACTIVE_MAP_W * ACTIVE_MAP_H) + 7) / 8)

#if DOOM_CHUNKED_SIMPLE_MAP

extern unsigned short g_simple_active_chunk;
extern unsigned char g_chunk_door_open[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
extern unsigned char g_chunk_lift_open[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];
#define SIMPLE_ACTIVE_CHUNK g_simple_active_chunk

static inline unsigned short simple_chunk_cell_index(int x, int y) {
    return (unsigned short)(y * SIMPLE_MAP_W + x);
}

static inline int simple_floor_div16(int value) {
    return value >= 0 ? value / SIMPLE_MAP_W : -((SIMPLE_MAP_W - 1 - value) / SIMPLE_MAP_W);
}

static inline unsigned short simple_chunk_for_cell(int x, int y, int *local_x, int *local_y) {
    int active_x = SIMPLE_ACTIVE_CHUNK % DOOM_CHUNK_COLS;
    int active_y = SIMPLE_ACTIVE_CHUNK / DOOM_CHUNK_COLS;
    int offset_x = simple_floor_div16(x);
    int offset_y = simple_floor_div16(y);
    int chunk_x = active_x + offset_x;
    int chunk_y = active_y + offset_y;
    if (chunk_x < 0 || chunk_y < 0 || chunk_x >= DOOM_CHUNK_COLS || chunk_y >= DOOM_CHUNK_ROWS) return 0xFFFF;
    *local_x = x - offset_x * SIMPLE_MAP_W;
    *local_y = y - offset_y * SIMPLE_MAP_H;
    return (unsigned short)(chunk_y * DOOM_CHUNK_COLS + chunk_x);
}

static inline int simple_map_in_bounds(int x, int y) {
    int local_x;
    int local_y;
    return simple_chunk_for_cell(x, y, &local_x, &local_y) != 0xFFFF;
}

static inline int map_at(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk;
    unsigned short cell;
    chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 1;
    if (chunk == SIMPLE_ACTIVE_CHUNK
        && (g_runtime_cell_open[((local_y * ACTIVE_MAP_W) + local_x) >> 3]
            & (1 << (((local_y * ACTIVE_MAP_W) + local_x) & 7)))) return 0;
    cell = simple_chunk_cell_index(local_x, local_y);
    {
        unsigned char door_id = g_chunk_door_cell[chunk][cell];
        if (door_id >= 2) return g_chunk_door_open[door_id - 2] ? 0 : 1;
    }
    {
        unsigned char lift_id = g_chunk_lift_cell[chunk][cell];
        if (lift_id) return g_chunk_lift_open[lift_id - 1] ? 0 : 1;
    }
    return g_chunk_solid[chunk][cell] ? 1 : 0;
}

static inline unsigned char map_cell_value(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 1;
    {
        unsigned short cell = simple_chunk_cell_index(local_x, local_y);
        unsigned char door_id = g_chunk_door_cell[chunk][cell];
        if (door_id >= 2) return door_id;
        return g_chunk_solid[chunk][cell] ? 1 : 0;
    }
}

static inline unsigned char map_cell_texture(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 0;
    return g_chunk_tex[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline unsigned char map_cell_texture_phase(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 0;
    return g_chunk_tex_phase[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline unsigned char map_cell_damage(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 0;
    return g_chunk_damage[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline unsigned char map_cell_floor_visual(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 0;
    return g_chunk_floor_visual[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline unsigned char map_cell_light(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 3;
    return g_chunk_light[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline short map_cell_floor_height(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 0;
    return g_chunk_floor_height[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline short map_cell_ceiling_height(int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = simple_chunk_for_cell(x, y, &local_x, &local_y);
    if (chunk == 0xFFFF) return 128;
    return g_chunk_ceiling_height[chunk][simple_chunk_cell_index(local_x, local_y)];
}

static inline unsigned char map_cell_secret(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

#else

static const unsigned char g_simple_map[SIMPLE_MAP_H][SIMPLE_MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,0,1,0,1,1,1,1,0,1,1},
    {1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
    {1,0,1,1,0,1,1,1,0,1,0,1,0,1,0,1},
    {1,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1},
    {1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const NgRuntimeThing g_simple_runtime_things[NG_RUNTIME_THING_COUNT] = {
    { 4 * 256 + 128,  9 * 256 + 128, 3004, 7 },
    {12 * 256 + 128,  7 * 256 + 128, 3001, 7 },
    { 6 * 256 + 128,  7 * 256 + 128, 2035, 7 },
    { 3 * 256 + 128, 13 * 256 + 128, 5,    7 },
    { 5 * 256 + 128, 13 * 256 + 128, 2007, 7 },
    { 6 * 256 + 128, 13 * 256 + 128, 2011, 7 },
    {10 * 256 + 128, 13 * 256 + 128, 2018, 7 },
    {12 * 256 + 128, 13 * 256 + 128, 2001, 7 }
};

static const NgRuntimeExit g_simple_runtime_exits[NG_RUNTIME_EXIT_COUNT] = {
    { 8 * 256 + 128, 3 * 256 + 128, 11, 1, 1}
};

static const NgRuntimeDoor g_simple_runtime_doors[NG_RUNTIME_DOOR_COUNT] = {
    {8, 5, 26}
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

#endif

#endif /* SIMPLE_MAP_H_INCLUDED */
