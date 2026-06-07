#include <stdio.h>
#include <stdlib.h>

#include "ripdoom_runtime.h"
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#include "doom_chunks_generated.h"
#include "chunk_stream.h"

unsigned char g_chunk_door_open[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
unsigned char g_chunk_lift_open[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];
#endif

#define FBITS 8
#define FONE 256
#define FIX(v) ((int)((v) * FONE))
#ifndef DOOM_RIPDOOM_RENDER_BLOCK_RADIUS
#define DOOM_RIPDOOM_RENDER_BLOCK_RADIUS 8
#endif

static int sample_view(int start_x, int start_y, short view_x, short view_y, unsigned int *out_min, unsigned int *out_max, int *out_first);
static int sample_view_columns(
    int start_x,
    int start_y,
    short view_x,
    short view_y,
    unsigned int *out_min,
    unsigned int *out_max,
    int *out_first,
    unsigned short *out_dist,
    unsigned short *out_seg
);

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
enum {
    PLAYER_RADIUS_Q8 = 51,
    DYNAMIC_BLOCK_RANGE_Q8 = 104,
    MOVE_SPEED_Q8 = 31,
    MOVEMENT_RENDER_TICKS = 70,
    MIN_RENDER_MOVE_PROGRESS_Q8 = 256,
    MIN_RENDER_CHANGED_COLUMNS = 8,
    MIN_RENDER_COLUMN_DELTA_Q8 = 32,
    MIN_RENDER_TOTAL_DELTA_Q8 = 512,
    ROUTE_WAYPOINTS = 8,
    ROUTE_MIN_HITS = 40,
    MAX_ROUTE_CELLS = DOOM_CHUNK_COUNT * DOOM_CHUNK_CELLS
};

typedef struct DynamicBlocker {
    int x_q8;
    int y_q8;
} DynamicBlocker;

static DynamicBlocker dynamic_blockers[DOOM_CHUNK_MAX_ACTIVE_THINGS ? DOOM_CHUNK_MAX_ACTIVE_THINGS : 1];
static int dynamic_blocker_count = 0;
static int route_prev[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
static int route_queue[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
static int route_path[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];

static void ripdoom_pose_from_chunk_q8(
    unsigned short active_chunk,
    int local_x_q8,
    int local_y_q8,
    long start_global_x_q8,
    long start_global_y_q8,
    int start_rip_x,
    int start_rip_y,
    int *rip_x,
    int *rip_y
);

static int floor_div16(int value) {
    return value >= 0 ? value / SIMPLE_MAP_W : -((SIMPLE_MAP_W - 1 - value) / SIMPLE_MAP_W);
}

static int floor_q8_cell(int value_q8) {
    return value_q8 >= 0 ? value_q8 / 256 : -((255 - value_q8) / 256);
}

static unsigned short chunk_for_cell(unsigned short active_chunk, int x, int y, int *local_x, int *local_y) {
    int active_x = active_chunk % DOOM_CHUNK_COLS;
    int active_y = active_chunk / DOOM_CHUNK_COLS;
    int offset_x = floor_div16(x);
    int offset_y = floor_div16(y);
    int chunk_x = active_x + offset_x;
    int chunk_y = active_y + offset_y;
    if (chunk_x < 0 || chunk_y < 0 || chunk_x >= DOOM_CHUNK_COLS || chunk_y >= DOOM_CHUNK_ROWS) return 0xFFFF;
    *local_x = x - offset_x * SIMPLE_MAP_W;
    *local_y = y - offset_y * SIMPLE_MAP_H;
    return (unsigned short)(chunk_y * DOOM_CHUNK_COLS + chunk_x);
}

static int map_at_cell(unsigned short active_chunk, int x, int y) {
    int local_x;
    int local_y;
    unsigned short chunk = chunk_for_cell(active_chunk, x, y, &local_x, &local_y);
    unsigned short cell;
    unsigned char door_id;
    unsigned char lift_id;
    if (chunk == 0xFFFF) return 1;
    cell = (unsigned short)(local_y * SIMPLE_MAP_W + local_x);
    door_id = g_chunk_door_cell[chunk][cell];
    if (door_id >= 2) return g_chunk_door_open[door_id - 2] ? 0 : 1;
    lift_id = g_chunk_lift_cell[chunk][cell];
    if (lift_id) return g_chunk_lift_open[lift_id - 1] ? 0 : 1;
    return g_chunk_solid[chunk][cell] ? 1 : 0;
}

static int dynamic_blocked(int x_q8, int y_q8) {
    int cx = floor_q8_cell(x_q8);
    int cy = floor_q8_cell(y_q8);
    int block_range_cells = (DYNAMIC_BLOCK_RANGE_Q8 + 255) / 256;
    for (int i = 0; i < dynamic_blocker_count; i++) {
        int thing_x = dynamic_blockers[i].x_q8;
        int thing_y = dynamic_blockers[i].y_q8;
        if (abs(floor_q8_cell(thing_x) - cx) > block_range_cells) continue;
        if (abs(floor_q8_cell(thing_y) - cy) > block_range_cells) continue;
        if (abs(x_q8 - thing_x) <= DYNAMIC_BLOCK_RANGE_Q8 && abs(y_q8 - thing_y) <= DYNAMIC_BLOCK_RANGE_Q8) return 1;
    }
    return 0;
}

static int can_occupy(unsigned short active_chunk, int x_q8, int y_q8) {
    int cx = floor_q8_cell(x_q8);
    int cy = floor_q8_cell(y_q8);
    if (map_at_cell(active_chunk, cx, cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 - PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 + PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 - PLAYER_RADIUS_Q8))) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 + PLAYER_RADIUS_Q8))) return 0;
    if (dynamic_blocked(x_q8, y_q8)) return 0;
    return 1;
}

static void load_dynamic_blockers(unsigned short active_chunk) {
    int active_chunk_x = active_chunk % DOOM_CHUNK_COLS;
    int active_chunk_y = active_chunk / DOOM_CHUNK_COLS;
    dynamic_blocker_count = 0;
    for (int radius = 0; radius <= 1 && dynamic_blocker_count < DOOM_CHUNK_MAX_ACTIVE_THINGS; radius++) {
        for (int dy = -radius; dy <= radius && dynamic_blocker_count < DOOM_CHUNK_MAX_ACTIVE_THINGS; dy++) {
            for (int dx = -radius; dx <= radius && dynamic_blocker_count < DOOM_CHUNK_MAX_ACTIVE_THINGS; dx++) {
                int chunk_x;
                int chunk_y;
                unsigned short chunk;
                unsigned short first;
                unsigned char count;
                if (radius && abs(dx) < radius && abs(dy) < radius) continue;
                chunk_x = active_chunk_x + dx;
                chunk_y = active_chunk_y + dy;
                if (chunk_x < 0 || chunk_y < 0 || chunk_x >= DOOM_CHUNK_COLS || chunk_y >= DOOM_CHUNK_ROWS) continue;
                chunk = (unsigned short)(chunk_y * DOOM_CHUNK_COLS + chunk_x);
                first = g_chunk_thing_first[chunk];
                count = g_chunk_thing_count[chunk];
                for (unsigned char n = 0; n < count && dynamic_blocker_count < DOOM_CHUNK_MAX_ACTIVE_THINGS; n++) {
                    const NgChunkThing *thing = &g_chunk_things[first + n];
                    if (thing->thing_class != 1 && thing->thing_class != 2) continue;
                    dynamic_blockers[dynamic_blocker_count].x_q8 = thing->x_q8 + dx * SIMPLE_MAP_W * 256;
                    dynamic_blockers[dynamic_blocker_count].y_q8 = thing->y_q8 + dy * SIMPLE_MAP_H * 256;
                    dynamic_blocker_count++;
                }
            }
        }
    }
}

static int mul_q8(int a, int b) {
    return (a * b) / 256;
}

static int global_grid_w(void) {
    return DOOM_CHUNK_COLS * SIMPLE_MAP_W;
}

static int global_grid_h(void) {
    return DOOM_CHUNK_ROWS * SIMPLE_MAP_H;
}

static int chunk_global_x_q8(unsigned short active_chunk, int x_q8) {
    int chunk_x = active_chunk % DOOM_CHUNK_COLS;
    return chunk_x * SIMPLE_MAP_W * 256 + x_q8;
}

static int chunk_global_y_q8(unsigned short active_chunk, int y_q8) {
    int chunk_y = active_chunk / DOOM_CHUNK_COLS;
    return chunk_y * SIMPLE_MAP_H * 256 + y_q8;
}

static int global_cell_blocked(int x, int y) {
    unsigned short chunk;
    unsigned short cell;
    int width = global_grid_w();
    int height = global_grid_h();
    if (x < 0 || y < 0 || x >= width || y >= height) return 1;
    chunk = (unsigned short)((y / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (x / SIMPLE_MAP_W));
    cell = (unsigned short)((y % SIMPLE_MAP_H) * SIMPLE_MAP_W + (x % SIMPLE_MAP_W));
    if (g_chunk_door_cell[chunk][cell] >= 2) return 0;
    if (g_chunk_lift_cell[chunk][cell]) return 0;
    return g_chunk_solid[chunk][cell] ? 1 : 0;
}

static unsigned char global_cell_door_id(int x, int y) {
    unsigned short chunk;
    unsigned short cell;
    int width = global_grid_w();
    int height = global_grid_h();
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    chunk = (unsigned short)((y / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (x / SIMPLE_MAP_W));
    cell = (unsigned short)((y % SIMPLE_MAP_H) * SIMPLE_MAP_W + (x % SIMPLE_MAP_W));
    return g_chunk_door_cell[chunk][cell];
}

static unsigned char global_cell_lift_id(int x, int y) {
    unsigned short chunk;
    unsigned short cell;
    int width = global_grid_w();
    int height = global_grid_h();
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    chunk = (unsigned short)((y / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (x / SIMPLE_MAP_W));
    cell = (unsigned short)((y % SIMPLE_MAP_H) * SIMPLE_MAP_W + (x % SIMPLE_MAP_W));
    return g_chunk_lift_cell[chunk][cell];
}

static unsigned short active_chunk_for_global_cell(int x, int y, int *local_x, int *local_y) {
    int width = global_grid_w();
    int height = global_grid_h();
    if (x < 0 || y < 0 || x >= width || y >= height) return 0xFFFF;
    *local_x = x % SIMPLE_MAP_W;
    *local_y = y % SIMPLE_MAP_H;
    return (unsigned short)((y / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (x / SIMPLE_MAP_W));
}

static int validate_opened_interactive_cell_passability(void) {
    int checked = 0;
    for (unsigned short door_index = 0; door_index < DOOM_CHUNK_DOOR_COUNT; door_index++) {
        int local_x;
        int local_y;
        unsigned short active_chunk = active_chunk_for_global_cell(g_chunk_doors[door_index].x, g_chunk_doors[door_index].y, &local_x, &local_y);
        if (active_chunk == 0xFFFF) continue;
        for (unsigned short i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
        if (!map_at_cell(active_chunk, local_x, local_y)) {
            fprintf(stderr, "RIPDOOM render probe failed: closed chunk door cell was passable door=%u cell=(%u,%u)\n", (unsigned)door_index, g_chunk_doors[door_index].x, g_chunk_doors[door_index].y);
            return 0;
        }
        g_chunk_door_open[door_index] = 1;
        if (map_at_cell(active_chunk, local_x, local_y)) {
            fprintf(stderr, "RIPDOOM render probe failed: opened chunk door cell stayed blocked door=%u cell=(%u,%u)\n", (unsigned)door_index, g_chunk_doors[door_index].x, g_chunk_doors[door_index].y);
            return 0;
        }
        g_chunk_door_open[door_index] = 0;
        checked++;
        break;
    }

    for (unsigned short ref_index = 0; ref_index < DOOM_CHUNK_LIFT_CELL_REF_COUNT; ref_index++) {
        int lift_x = g_chunk_lift_cells[ref_index] % DOOM_CHUNK_GRID_W;
        int lift_y = g_chunk_lift_cells[ref_index] / DOOM_CHUNK_GRID_W;
        unsigned char lift_id = global_cell_lift_id(lift_x, lift_y);
        int local_x;
        int local_y;
        unsigned short active_chunk;
        if (!lift_id) continue;
        active_chunk = active_chunk_for_global_cell(lift_x, lift_y, &local_x, &local_y);
        if (active_chunk == 0xFFFF) continue;
        for (unsigned short i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
        if (!map_at_cell(active_chunk, local_x, local_y)) {
            fprintf(stderr, "RIPDOOM render probe failed: closed chunk lift cell was passable cell=(%d,%d) lift=%u\n", lift_x, lift_y, (unsigned)lift_id - 1);
            return 0;
        }
        g_chunk_lift_open[lift_id - 1] = 1;
        if (map_at_cell(active_chunk, local_x, local_y)) {
            fprintf(stderr, "RIPDOOM render probe failed: opened chunk lift cell stayed blocked cell=(%d,%d) lift=%u\n", lift_x, lift_y, (unsigned)lift_id - 1);
            return 0;
        }
        g_chunk_lift_open[lift_id - 1] = 0;
        checked++;
        break;
    }

    return checked > 0;
}

static short cell_center_doom_x(int grid_x) {
    return (short)(DOOM_CHUNK_ORIGIN_X + grid_x * DOOM_CHUNK_CELL_DOOM_UNITS + DOOM_CHUNK_CELL_DOOM_UNITS / 2);
}

static short cell_center_doom_y(int grid_y) {
    return (short)(DOOM_CHUNK_ORIGIN_Y - grid_y * DOOM_CHUNK_CELL_DOOM_UNITS - DOOM_CHUNK_CELL_DOOM_UNITS / 2);
}

static int validate_opened_door_ray_skip(void) {
    static const signed char dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    if (DOOM_CHUNK_DOOR_COUNT <= 0) return 1;
    for (unsigned short door_index = 0; door_index < DOOM_CHUNK_DOOR_COUNT; door_index++) {
        int door_x = g_chunk_doors[door_index].x;
        int door_y = g_chunk_doors[door_index].y;
        unsigned char door_id = global_cell_door_id(door_x, door_y);
        if (door_id < 2) continue;
        for (int d = 0; d < 4; d++) {
            int source_x = door_x + dirs[d][0];
            int source_y = door_y + dirs[d][1];
            short ray_x;
            short ray_y;
            short source_doom_x;
            short source_doom_y;
            NgRipRayHit closed_hit;
            NgRipRayHit open_hit;
            int open_has_hit;
            if (global_cell_blocked(source_x, source_y)) continue;
            if (global_cell_door_id(source_x, source_y) >= 2) continue;
            ray_x = (short)((door_x - source_x) * 256);
            ray_y = (short)((source_y - door_y) * 256);
            source_doom_x = cell_center_doom_x(source_x);
            source_doom_y = cell_center_doom_y(source_y);
            for (unsigned short i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
            if (!ripdoom_cast_local_ray(source_doom_x, source_doom_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &closed_hit)) continue;
            if (!(closed_hit.flags & NG_RIP_SEG_DOOR)) continue;
            g_chunk_door_open[door_id - 2] = 1;
            open_has_hit = ripdoom_cast_local_ray(source_doom_x, source_doom_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &open_hit);
            g_chunk_door_open[door_id - 2] = 0;
            if (open_has_hit && open_hit.seg == closed_hit.seg && open_hit.distance_q8 <= closed_hit.distance_q8 + 16) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: opened chunk door still blocks ray door=%u cell=(%d,%d) source=(%d,%d) seg=%u dist=%u\n",
                    (unsigned)door_index,
                    door_x,
                    door_y,
                    source_x,
                    source_y,
                    closed_hit.seg,
                    closed_hit.distance_q8
                );
                return 0;
            }
            return 1;
        }
    }
    fprintf(stderr, "RIPDOOM render probe failed: no chunk door could be ray-validated\n");
    return 0;
}

static int validate_opened_lift_ray_skip(void) {
    static const signed char dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    if (DOOM_CHUNK_LIFT_COUNT <= 0) return 1;
    for (unsigned short ref_index = 0; ref_index < DOOM_CHUNK_LIFT_CELL_REF_COUNT; ref_index++) {
        int lift_x = g_chunk_lift_cells[ref_index] % DOOM_CHUNK_GRID_W;
        int lift_y = g_chunk_lift_cells[ref_index] / DOOM_CHUNK_GRID_W;
        unsigned char lift_id = global_cell_lift_id(lift_x, lift_y);
        if (!lift_id) continue;
        for (int d = 0; d < 4; d++) {
            int source_x = lift_x + dirs[d][0];
            int source_y = lift_y + dirs[d][1];
            short ray_x;
            short ray_y;
            short source_doom_x;
            short source_doom_y;
            NgRipRayHit closed_hit;
            NgRipRayHit open_hit;
            int open_has_hit;
            if (global_cell_blocked(source_x, source_y)) continue;
            if (global_cell_door_id(source_x, source_y) >= 2) continue;
            if (global_cell_lift_id(source_x, source_y)) continue;
            ray_x = (short)((lift_x - source_x) * 256);
            ray_y = (short)((source_y - lift_y) * 256);
            source_doom_x = cell_center_doom_x(source_x);
            source_doom_y = cell_center_doom_y(source_y);
            for (unsigned short i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
            if (!ripdoom_cast_local_ray(source_doom_x, source_doom_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &closed_hit)) continue;
            if (closed_hit.flags & NG_RIP_SEG_ONE_SIDED) continue;
            if (!(closed_hit.flags & (NG_RIP_SEG_LOWER | NG_RIP_SEG_UPPER | NG_RIP_SEG_MID | NG_RIP_SEG_DOOR))) continue;
            g_chunk_lift_open[lift_id - 1] = 1;
            open_has_hit = ripdoom_cast_local_ray(source_doom_x, source_doom_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &open_hit);
            g_chunk_lift_open[lift_id - 1] = 0;
            if (open_has_hit && open_hit.seg == closed_hit.seg && open_hit.distance_q8 <= closed_hit.distance_q8 + 16) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: opened chunk lift still blocks ray cell=(%d,%d) source=(%d,%d) seg=%u dist=%u flags=0x%x\n",
                    lift_x,
                    lift_y,
                    source_x,
                    source_y,
                    closed_hit.seg,
                    closed_hit.distance_q8,
                    closed_hit.flags
                );
                return 0;
            }
            return 1;
        }
    }
    fprintf(stderr, "RIPDOOM render probe failed: no chunk lift could be ray-validated\n");
    return 0;
}

static int build_route_to_exit(int *out_path, int max_path) {
    static const signed char dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int width = global_grid_w();
    int height = global_grid_h();
    int cell_count = width * height;
    int start_cell;
    int target_cell = -1;
    int head = 0;
    int tail = 0;
    int path_len = 0;

    if (cell_count > MAX_ROUTE_CELLS || max_path <= 0 || DOOM_CHUNK_EXIT_COUNT <= 0) {
        fprintf(stderr, "RIPDOOM route probe failed: invalid route bounds cells=%d max=%d exits=%d\n", cell_count, MAX_ROUTE_CELLS, DOOM_CHUNK_EXIT_COUNT);
        return 0;
    }
    for (int i = 0; i < cell_count; i++) route_prev[i] = -2;

    start_cell = (DOOM_CHUNK_START_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H * width
        + (DOOM_CHUNK_START_Y_Q8 >> 8) * width
        + (DOOM_CHUNK_START_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W
        + (DOOM_CHUNK_START_X_Q8 >> 8);
    if (start_cell < 0 || start_cell >= cell_count || global_cell_blocked(start_cell % width, start_cell / width)) {
        fprintf(stderr, "RIPDOOM route probe failed: start blocked/out of bounds cell=%d pos=(%d,%d)\n", start_cell, start_cell % width, start_cell / width);
        return 0;
    }

    for (unsigned short i = 0; i < DOOM_CHUNK_EXIT_COUNT; i++) {
        int x = g_chunk_exits[i].x_q8 >> 8;
        int y = g_chunk_exits[i].y_q8 >> 8;
        if (x >= 0 && y >= 0 && x < width && y < height) {
            target_cell = y * width + x;
            break;
        }
    }
    if (target_cell < 0) {
        fprintf(stderr, "RIPDOOM route probe failed: no valid exit target\n");
        return 0;
    }

    route_prev[start_cell] = -1;
    route_queue[tail++] = start_cell;
    while (head < tail) {
        int cell = route_queue[head++];
        int x = cell % width;
        int y = cell / width;
        if (cell == target_cell) break;
        for (int d = 0; d < 4; d++) {
            int nx = x + dirs[d][0];
            int ny = y + dirs[d][1];
            int next;
            if (global_cell_blocked(nx, ny)) continue;
            next = ny * width + nx;
            if (route_prev[next] != -2) continue;
            route_prev[next] = cell;
            route_queue[tail++] = next;
        }
    }
    if (route_prev[target_cell] == -2) {
        fprintf(stderr, "RIPDOOM route probe failed: no route start=(%d,%d) exit=(%d,%d) visited=%d\n", start_cell % width, start_cell / width, target_cell % width, target_cell / width, tail);
        return 0;
    }

    for (int cell = target_cell; cell >= 0 && path_len < max_path; cell = route_prev[cell]) {
        route_path[path_len++] = cell;
        if (route_prev[cell] == -1) break;
    }
    for (int i = 0; i < path_len; i++) out_path[i] = route_path[path_len - 1 - i];
    return path_len;
}

static void route_view_for_index(const int *path, int path_len, int index, short *view_x, short *view_y) {
    int width = global_grid_w();
    int prev = index > 3 ? index - 3 : 0;
    int next = index + 3 < path_len ? index + 3 : path_len - 1;
    int dx = (path[next] % width) - (path[prev] % width);
    int dy = (path[next] / width) - (path[prev] / width);
    if (abs(dx) >= abs(dy) && dx) {
        *view_x = dx < 0 ? -256 : 256;
        *view_y = 0;
    } else if (dy) {
        *view_x = 0;
        *view_y = dy < 0 ? 256 : -256;
    } else {
        *view_x = (short)FIX(DOOM_CHUNK_START_DIR_X);
        *view_y = (short)FIX(-DOOM_CHUNK_START_DIR_Y);
    }
}

static int validate_route_waypoints(int start_rip_x, int start_rip_y, long start_global_x_q8, long start_global_y_q8, int *out_waypoints, int *out_steps) {
    int route[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
    int path_len = build_route_to_exit(route, MAX_ROUTE_CELLS);
    int sampled = 0;
    if (path_len <= 1) return 0;
    *out_steps = path_len - 1;

    for (int sample = 1; sample <= ROUTE_WAYPOINTS; sample++) {
        int index = (sample * (path_len - 1)) / (ROUTE_WAYPOINTS + 1);
        int width = global_grid_w();
        int cell = route[index];
        int global_x_q8 = (cell % width) * 256 + 128;
        int global_y_q8 = (cell / width) * 256 + 128;
        unsigned short chunk = (unsigned short)((cell / width / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (cell % width / SIMPLE_MAP_W));
        int local_x_q8 = global_x_q8 - (chunk % DOOM_CHUNK_COLS) * SIMPLE_MAP_W * 256;
        int local_y_q8 = global_y_q8 - (chunk / DOOM_CHUNK_COLS) * SIMPLE_MAP_H * 256;
        int rip_x;
        int rip_y;
        short waypoint_view_x;
        short waypoint_view_y;
        unsigned int waypoint_min = 0xffff;
        unsigned int waypoint_max = 0;
        int waypoint_first = -1;
        int waypoint_hits;
        ripdoom_pose_from_chunk_q8(chunk, local_x_q8, local_y_q8, start_global_x_q8, start_global_y_q8, start_rip_x, start_rip_y, &rip_x, &rip_y);
        route_view_for_index(route, path_len, index, &waypoint_view_x, &waypoint_view_y);
        waypoint_hits = sample_view(rip_x, rip_y, waypoint_view_x, waypoint_view_y, &waypoint_min, &waypoint_max, &waypoint_first);
        if (waypoint_hits < ROUTE_MIN_HITS) {
            fprintf(
                stderr,
                "RIPDOOM render probe failed: route waypoint %d/%d cell=(%d,%d) hits=%d/80 first=%d dist=%u..%u rip=(%d,%d) view=(%d,%d)\n",
                sample,
                ROUTE_WAYPOINTS,
                cell % width,
                cell / width,
                waypoint_hits,
                waypoint_first,
                waypoint_min,
                waypoint_max,
                rip_x,
                rip_y,
                waypoint_view_x,
                waypoint_view_y
            );
            return 0;
        }
        sampled++;
    }
    *out_waypoints = sampled;
    return sampled == ROUTE_WAYPOINTS;
}

static void ripdoom_pose_from_chunk_q8(
    unsigned short active_chunk,
    int local_x_q8,
    int local_y_q8,
    long start_global_x_q8,
    long start_global_y_q8,
    int start_rip_x,
    int start_rip_y,
    int *rip_x,
    int *rip_y
) {
    long global_x_q8 = chunk_global_x_q8(active_chunk, local_x_q8);
    long global_y_q8 = chunk_global_y_q8(active_chunk, local_y_q8);
    long dx = ((global_x_q8 - start_global_x_q8) * DOOM_CHUNK_CELL_DOOM_UNITS) >> 8;
    long dy = ((global_y_q8 - start_global_y_q8) * DOOM_CHUNK_CELL_DOOM_UNITS) >> 8;
    *rip_x = (int)(start_rip_x + dx);
    *rip_y = (int)(start_rip_y - dy);
}

static int simulate_forward_movement(unsigned short *active_chunk, int *x_q8, int *y_q8, int *moved_ticks, int *progress_q8) {
    unsigned short chunk = *active_chunk;
    int x = *x_q8;
    int y = *y_q8;
    int start_global_x = chunk_global_x_q8(chunk, x);
    int start_global_y = chunk_global_y_q8(chunk, y);
    int dir_x_q8 = (int)(DOOM_CHUNK_START_DIR_X * 256.0);
    int dir_y_q8 = (int)(DOOM_CHUNK_START_DIR_Y * 256.0);
    int dx_q8 = mul_q8(dir_x_q8, MOVE_SPEED_Q8);
    int dy_q8 = mul_q8(dir_y_q8, MOVE_SPEED_Q8);

    *moved_ticks = 0;
    *progress_q8 = 0;
    load_dynamic_blockers(chunk);
    if (!can_occupy(chunk, x, y)) return 0;
    if (dx_q8 == 0 && dy_q8 == 0) return 0;

    for (int tick = 0; tick < MOVEMENT_RENDER_TICKS; tick++) {
        int old_x = x;
        int old_y = y;
        if (can_occupy(chunk, x + dx_q8, y)) x += dx_q8;
        if (can_occupy(chunk, x, y + dy_q8)) y += dy_q8;
        if (x != old_x || y != old_y) (*moved_ticks)++;
        {
            NgChunkStreamState state = ng_chunk_stream_update(x, y, chunk);
            if (state.changed) {
                chunk = state.chunk;
                x += state.shift_x_q8;
                y += state.shift_y_q8;
                load_dynamic_blockers(chunk);
                if (!can_occupy(chunk, x, y)) return 0;
            }
        }
    }

    {
        int final_global_x = chunk_global_x_q8(chunk, x);
        int final_global_y = chunk_global_y_q8(chunk, y);
        int progress_x = final_global_x - start_global_x;
        int progress_y = final_global_y - start_global_y;
        *progress_q8 = mul_q8(progress_x, dir_x_q8) + mul_q8(progress_y, dir_y_q8);
    }
    *active_chunk = chunk;
    *x_q8 = x;
    *y_q8 = y;
    return *progress_q8 >= MIN_RENDER_MOVE_PROGRESS_Q8;
}
#endif

static int sample_view(int start_x, int start_y, short view_x, short view_y, unsigned int *out_min, unsigned int *out_max, int *out_first) {
    return sample_view_columns(start_x, start_y, view_x, view_y, out_min, out_max, out_first, NULL, NULL);
}

static int sample_view_columns(
    int start_x,
    int start_y,
    short view_x,
    short view_y,
    unsigned int *out_min,
    unsigned int *out_max,
    int *out_first,
    unsigned short *out_dist,
    unsigned short *out_seg
) {
    enum { COLUMNS = 80, PLANE_Q8 = 169 };
    short plane_x = (short)((-(int)view_y * PLANE_Q8) >> 8);
    short plane_y = (short)(((int)view_x * PLANE_Q8) >> 8);
    int hits = 0;
    *out_min = 0xffff;
    *out_max = 0;
    *out_first = -1;

    for (int column = 0; column < COLUMNS; column++) {
        int camera_q8 = ((2 * 256 * column) / (COLUMNS - 1)) - 256;
        short ray_x = (short)(view_x + (((int)plane_x * camera_q8) >> 8));
        short ray_y = (short)(view_y + (((int)plane_y * camera_q8) >> 8));
        NgRipRayHit hit;
        if (out_dist) out_dist[column] = 0xffff;
        if (out_seg) out_seg[column] = 0xffff;
        if (!ripdoom_cast_local_ray((short)start_x, (short)start_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &hit)) {
            continue;
        }
        if (*out_first < 0) *out_first = column;
        hits++;
        if (hit.distance_q8 < *out_min) *out_min = hit.distance_q8;
        if (hit.distance_q8 > *out_max) *out_max = hit.distance_q8;
        if (out_dist) out_dist[column] = hit.distance_q8 > 0xfffe ? 0xfffe : (unsigned short)hit.distance_q8;
        if (out_seg) out_seg[column] = hit.seg;
    }
    return hits;
}

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
static int count_changed_render_columns(
    const unsigned short *before_dist,
    const unsigned short *before_seg,
    const unsigned short *after_dist,
    const unsigned short *after_seg,
    unsigned int *out_total_delta
) {
    enum { COLUMNS = 80 };
    int changed = 0;
    unsigned int total_delta = 0;

    for (int column = 0; column < COLUMNS; column++) {
        int before_hit = before_dist[column] != 0xffff;
        int after_hit = after_dist[column] != 0xffff;
        unsigned int delta = 0;
        if (before_hit != after_hit) {
            changed++;
            total_delta += 1024;
            continue;
        }
        if (!before_hit) continue;
        delta = before_dist[column] > after_dist[column]
            ? before_dist[column] - after_dist[column]
            : after_dist[column] - before_dist[column];
        total_delta += delta;
        if (before_seg[column] != after_seg[column] || delta >= MIN_RENDER_COLUMN_DELTA_Q8) changed++;
    }

    *out_total_delta = total_delta;
    return changed;
}
#endif

int main(void) {
    enum { COLUMNS = 80 };
    int start_x = 0;
    int start_y = 0;
    int forward_x = 0;
    int forward_y = 0;
    int have_forward = 0;
    int start_angle = 0;
    short view_x = 256;
    short view_y = 0;
    int player_seen = 0;
    int hits = 0;
    int first_hit_column = -1;
    unsigned int min_dist = 0xffff;
    unsigned int max_dist = 0;
    const char *mode = "player";

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    {
        long chunk_x = DOOM_CHUNK_START_CHUNK % DOOM_CHUNK_COLS;
        long chunk_y = DOOM_CHUNK_START_CHUNK / DOOM_CHUNK_COLS;
        long global_x_q8 = chunk_x * DOOM_CHUNK_SIZE * 256L + DOOM_CHUNK_START_X_Q8;
        long global_y_q8 = chunk_y * DOOM_CHUNK_SIZE * 256L + DOOM_CHUNK_START_Y_Q8;
        start_x = (int)(DOOM_CHUNK_ORIGIN_X + ((global_x_q8 * DOOM_CHUNK_CELL_DOOM_UNITS + 128) >> 8));
        start_y = (int)(DOOM_CHUNK_ORIGIN_Y - ((global_y_q8 * DOOM_CHUNK_CELL_DOOM_UNITS + 128) >> 8));
        start_angle = DOOM_CHUNK_START_ANGLE;
        view_x = (short)FIX(DOOM_CHUNK_START_DIR_X);
        view_y = (short)FIX(-DOOM_CHUNK_START_DIR_Y);
        forward_x = start_x + (int)(DOOM_CHUNK_START_DIR_X * DOOM_CHUNK_CELL_DOOM_UNITS * 4.0);
        forward_y = start_y + (int)(-DOOM_CHUNK_START_DIR_Y * DOOM_CHUNK_CELL_DOOM_UNITS * 4.0);
        have_forward = 1;
        player_seen = 1;
        mode = "chunk";
    }
#else
    for (int i = 0; i < NG_RIP_THING_COUNT; i++) {
        if (g_rip_things[i].type == 1) {
            start_x = g_rip_things[i].x;
            start_y = g_rip_things[i].y;
            start_angle = g_rip_things[i].angle;
            player_seen = 1;
            break;
        }
    }
    switch (start_angle) {
    case 0: view_x = 256; view_y = 0; break;
    case 90: view_x = 0; view_y = 256; break;
    case 180: view_x = -256; view_y = 0; break;
    case 270: view_x = 0; view_y = -256; break;
    default: view_x = 256; view_y = 0; break;
    }
#endif
    if (!player_seen) {
        fprintf(stderr, "RIPDOOM render probe failed: no player start thing\n");
        return 1;
    }

    hits = sample_view(start_x, start_y, view_x, view_y, &min_dist, &max_dist, &first_hit_column);
    if (hits < COLUMNS / 2) {
        static const short cardinal[4][2] = {{256, 0}, {0, 256}, {-256, 0}, {0, -256}};
        int best_hits = hits;
        int best_first = first_hit_column;
        unsigned int best_min = min_dist;
        unsigned int best_max = max_dist;
        for (int i = 0; i < 4; i++) {
            unsigned int candidate_min;
            unsigned int candidate_max;
            int candidate_first;
            int candidate_hits = sample_view(start_x, start_y, cardinal[i][0], cardinal[i][1], &candidate_min, &candidate_max, &candidate_first);
            if (candidate_hits > best_hits) {
                best_hits = candidate_hits;
                best_first = candidate_first;
                best_min = candidate_min;
                best_max = candidate_max;
                mode = "cardinal";
                have_forward = 0;
            }
        }
        hits = best_hits;
        first_hit_column = best_first;
        min_dist = best_min;
        max_dist = best_max;
    }

    if (hits < COLUMNS / 2) {
        fprintf(stderr, "RIPDOOM render probe failed: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u\n", start_angle, mode, hits, COLUMNS, first_hit_column, min_dist, max_dist);
        return 1;
    }

    if (have_forward) {
        unsigned int forward_min = 0xffff;
        unsigned int forward_max = 0;
        int forward_first = -1;
        int forward_hits = sample_view(forward_x, forward_y, view_x, view_y, &forward_min, &forward_max, &forward_first);
        if (forward_hits < COLUMNS / 2) {
            fprintf(
                stderr,
                "RIPDOOM render probe failed: forward mode=%s hits=%d/%d first=%d dist=%u..%u start=(%d,%d) forward=(%d,%d)\n",
                mode,
                forward_hits,
                COLUMNS,
                forward_first,
                forward_min,
                forward_max,
                start_x,
                start_y,
                forward_x,
                forward_y
            );
            return 1;
        }
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
        {
            unsigned short moved_chunk = DOOM_CHUNK_START_CHUNK;
            int moved_x_q8 = DOOM_CHUNK_START_X_Q8;
            int moved_y_q8 = DOOM_CHUNK_START_Y_Q8;
            int moved_ticks = 0;
            int progress_q8 = 0;
            int moved_rip_x = start_x;
            int moved_rip_y = start_y;
            unsigned int moved_min = 0xffff;
            unsigned int moved_max = 0;
            int moved_first = -1;
            int moved_hits;
            unsigned int start_columns_min = 0xffff;
            unsigned int start_columns_max = 0;
            int start_columns_first = -1;
            int start_columns_hits;
            unsigned short start_dist_columns[COLUMNS];
            unsigned short start_seg_columns[COLUMNS];
            unsigned short moved_dist_columns[COLUMNS];
            unsigned short moved_seg_columns[COLUMNS];
            int changed_columns = 0;
            unsigned int total_column_delta = 0;
            int route_waypoints = 0;
            int route_steps = 0;
            int door_skip = 0;
            int lift_skip = 0;
            int interactive_pass = 0;
            long start_global_x_q8 = chunk_global_x_q8(DOOM_CHUNK_START_CHUNK, DOOM_CHUNK_START_X_Q8);
            long start_global_y_q8 = chunk_global_y_q8(DOOM_CHUNK_START_CHUNK, DOOM_CHUNK_START_Y_Q8);

            if (!simulate_forward_movement(&moved_chunk, &moved_x_q8, &moved_y_q8, &moved_ticks, &progress_q8)) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: accepted movement did not advance enough moved_ticks=%d progress_q8=%d chunk=%u local=(%d,%d)\n",
                    moved_ticks,
                    progress_q8,
                    moved_chunk,
                    moved_x_q8,
                    moved_y_q8
                );
                return 1;
            }
            ripdoom_pose_from_chunk_q8(
                moved_chunk,
                moved_x_q8,
                moved_y_q8,
                start_global_x_q8,
                start_global_y_q8,
                start_x,
                start_y,
                &moved_rip_x,
                &moved_rip_y
            );
            if (moved_rip_x == start_x && moved_rip_y == start_y) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: accepted movement kept the RIPDOOM pose fixed at (%d,%d)\n",
                    start_x,
                    start_y
                );
                return 1;
            }
            start_columns_hits = sample_view_columns(
                start_x,
                start_y,
                view_x,
                view_y,
                &start_columns_min,
                &start_columns_max,
                &start_columns_first,
                start_dist_columns,
                start_seg_columns
            );
            if (start_columns_hits < COLUMNS / 2) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: start column sample degraded hits=%d/%d first=%d dist=%u..%u\n",
                    start_columns_hits,
                    COLUMNS,
                    start_columns_first,
                    start_columns_min,
                    start_columns_max
                );
                return 1;
            }
            moved_hits = sample_view_columns(
                moved_rip_x,
                moved_rip_y,
                view_x,
                view_y,
                &moved_min,
                &moved_max,
                &moved_first,
                moved_dist_columns,
                moved_seg_columns
            );
            if (moved_hits < COLUMNS / 2) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: moved pose hits=%d/%d first=%d dist=%u..%u chunk=%u local=(%d,%d) rip=(%d,%d)\n",
                    moved_hits,
                    COLUMNS,
                    moved_first,
                    moved_min,
                    moved_max,
                    moved_chunk,
                    moved_x_q8,
                    moved_y_q8,
                    moved_rip_x,
                    moved_rip_y
                );
                return 1;
            }
            changed_columns = count_changed_render_columns(
                start_dist_columns,
                start_seg_columns,
                moved_dist_columns,
                moved_seg_columns,
                &total_column_delta
            );
            if (changed_columns < MIN_RENDER_CHANGED_COLUMNS || total_column_delta < MIN_RENDER_TOTAL_DELTA_Q8) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: accepted movement barely changed render columns changed=%d/%d total_delta=%u progress_q8=%d start_dist=%u..%u moved_dist=%u..%u\n",
                    changed_columns,
                    COLUMNS,
                    total_column_delta,
                    progress_q8,
                    start_columns_min,
                    start_columns_max,
                    moved_min,
                    moved_max
                );
                return 1;
            }
            if (!validate_route_waypoints(start_x, start_y, start_global_x_q8, start_global_y_q8, &route_waypoints, &route_steps)) {
                fprintf(
                    stderr,
                    "RIPDOOM render probe failed: route waypoint validation failed route_steps=%d sampled=%d\n",
                    route_steps,
                    route_waypoints
                );
                return 1;
            }
            door_skip = validate_opened_door_ray_skip();
            if (!door_skip) return 1;
            lift_skip = validate_opened_lift_ray_skip();
            if (!lift_skip) return 1;
            interactive_pass = validate_opened_interactive_cell_passability();
            if (!interactive_pass) return 1;
            printf(
                "RIPDOOM render probe OK: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u forward_hits=%d/%d forward_dist=%u..%u moved_hits=%d/%d moved_dist=%u..%u moved_changed_cols=%d moved_total_delta=%u moved_progress_q8=%d moved_ticks=%d route_waypoints=%d route_steps=%d door_skip=%d lift_skip=%d interactive_pass=%d\n",
                start_angle,
                mode,
                hits,
                COLUMNS,
                first_hit_column,
                min_dist,
                max_dist,
                forward_hits,
                COLUMNS,
                forward_min,
                forward_max,
                moved_hits,
                COLUMNS,
                moved_min,
                moved_max,
                changed_columns,
                total_column_delta,
                progress_q8,
                moved_ticks,
                route_waypoints,
                route_steps,
                door_skip,
                lift_skip,
                interactive_pass
            );
            return 0;
        }
#endif
        printf(
            "RIPDOOM render probe OK: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u forward_hits=%d/%d forward_dist=%u..%u\n",
            start_angle,
            mode,
            hits,
            COLUMNS,
            first_hit_column,
            min_dist,
            max_dist,
            forward_hits,
            COLUMNS,
            forward_min,
            forward_max
        );
        return 0;
    }

    printf("RIPDOOM render probe OK: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u\n", start_angle, mode, hits, COLUMNS, first_hit_column, min_dist, max_dist);
    return 0;
}
