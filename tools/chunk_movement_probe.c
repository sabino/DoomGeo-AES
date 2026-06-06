#include <stdio.h>
#include <stdlib.h>

#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#include "doom_chunks_generated.h"
#include "doom_map_generated.h"
#include "chunk_stream.h"

enum {
    PLAYER_RADIUS_Q8 = 51,
    DYNAMIC_BLOCK_RANGE_Q8 = 104,
    MOVE_SPEED_Q8 = 31,
    FORWARD_TICKS = 70,
    MIN_FORWARD_PROGRESS_Q8 = 512,
    MAX_ROUTE_CELLS = DOOM_CHUNK_COUNT * DOOM_CHUNK_CELLS
};

typedef struct DynamicBlocker {
    int x_q8;
    int y_q8;
} DynamicBlocker;

static DynamicBlocker dynamic_blockers[NG_RUNTIME_THING_COUNT ? NG_RUNTIME_THING_COUNT : 1];
static int dynamic_blocker_count = 0;
static unsigned char g_chunk_door_open[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
static unsigned char g_chunk_lift_open[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];
static int route_prev[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
static int route_queue[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
static int route_path[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];

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
    if (chunk == 0xFFFF) return 1;
    cell = (unsigned short)(local_y * SIMPLE_MAP_W + local_x);
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

static int can_occupy_static(unsigned short active_chunk, int x_q8, int y_q8) {
    int cx = floor_q8_cell(x_q8);
    int cy = floor_q8_cell(y_q8);
    if (map_at_cell(active_chunk, cx, cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 - PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 + PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 - PLAYER_RADIUS_Q8))) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 + PLAYER_RADIUS_Q8))) return 0;
    return 1;
}

static void load_dynamic_blockers(unsigned short active_chunk) {
    int active_chunk_x = active_chunk % DOOM_CHUNK_COLS;
    int active_chunk_y = active_chunk / DOOM_CHUNK_COLS;
    dynamic_blocker_count = 0;
    for (int radius = 0; radius <= 1 && dynamic_blocker_count < NG_RUNTIME_THING_COUNT; radius++) {
        for (int dy = -radius; dy <= radius && dynamic_blocker_count < NG_RUNTIME_THING_COUNT; dy++) {
            for (int dx = -radius; dx <= radius && dynamic_blocker_count < NG_RUNTIME_THING_COUNT; dx++) {
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
                for (unsigned char n = 0; n < count && dynamic_blocker_count < NG_RUNTIME_THING_COUNT; n++) {
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

static int chunk_global_x_q8(unsigned short active_chunk, int x_q8) {
    int chunk_x = active_chunk % DOOM_CHUNK_COLS;
    return chunk_x * SIMPLE_MAP_W * 256 + x_q8;
}

static int chunk_global_y_q8(unsigned short active_chunk, int y_q8) {
    int chunk_y = active_chunk / DOOM_CHUNK_COLS;
    return chunk_y * SIMPLE_MAP_H * 256 + y_q8;
}

static int global_grid_w(void) {
    return DOOM_CHUNK_COLS * SIMPLE_MAP_W;
}

static int global_grid_h(void) {
    return DOOM_CHUNK_ROWS * SIMPLE_MAP_H;
}

static int global_cell_blocked_for_route(int x, int y) {
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

static void open_route_cell(int global_x, int global_y, int *opened_doors, int *opened_lifts) {
    unsigned short chunk;
    unsigned short cell;
    unsigned char door_id;
    unsigned char lift_id;
    int width = global_grid_w();
    int height = global_grid_h();
    if (global_x < 0 || global_y < 0 || global_x >= width || global_y >= height) return;
    chunk = (unsigned short)((global_y / SIMPLE_MAP_H) * DOOM_CHUNK_COLS + (global_x / SIMPLE_MAP_W));
    cell = (unsigned short)((global_y % SIMPLE_MAP_H) * SIMPLE_MAP_W + (global_x % SIMPLE_MAP_W));
    door_id = g_chunk_door_cell[chunk][cell];
    if (door_id >= 2 && !g_chunk_door_open[door_id - 2]) {
        g_chunk_door_open[door_id - 2] = 1;
        (*opened_doors)++;
    }
    lift_id = g_chunk_lift_cell[chunk][cell];
    if (lift_id && !g_chunk_lift_open[lift_id - 1]) {
        g_chunk_lift_open[lift_id - 1] = 1;
        (*opened_lifts)++;
    }
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

    if (cell_count > MAX_ROUTE_CELLS || max_path <= 0 || DOOM_CHUNK_EXIT_COUNT <= 0) return 0;
    for (int i = 0; i < cell_count; i++) route_prev[i] = -2;

    start_cell = (DOOM_CHUNK_START_CHUNK / DOOM_CHUNK_COLS) * SIMPLE_MAP_H * width
        + (DOOM_CHUNK_START_Y_Q8 >> 8) * width
        + (DOOM_CHUNK_START_CHUNK % DOOM_CHUNK_COLS) * SIMPLE_MAP_W
        + (DOOM_CHUNK_START_X_Q8 >> 8);
    if (start_cell < 0 || start_cell >= cell_count || global_cell_blocked_for_route(start_cell % width, start_cell / width)) return 0;

    for (unsigned short i = 0; i < DOOM_CHUNK_EXIT_COUNT; i++) {
        int x = g_chunk_exits[i].x_q8 >> 8;
        int y = g_chunk_exits[i].y_q8 >> 8;
        if (x >= 0 && y >= 0 && x < width && y < height) {
            target_cell = y * width + x;
            break;
        }
    }
    if (target_cell < 0) return 0;

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
            if (global_cell_blocked_for_route(nx, ny)) continue;
            next = ny * width + nx;
            if (route_prev[next] != -2) continue;
            route_prev[next] = cell;
            route_queue[tail++] = next;
        }
    }
    if (route_prev[target_cell] == -2) return 0;

    for (int cell = target_cell; cell >= 0 && path_len < max_path; cell = route_prev[cell]) {
        route_path[path_len++] = cell;
        if (route_prev[cell] == -1) break;
    }
    for (int i = 0; i < path_len; i++) out_path[i] = route_path[path_len - 1 - i];
    return path_len;
}

static int validate_route_movement(
    int *out_steps,
    int *out_transitions,
    int *out_opened_doors,
    int *out_opened_lifts
) {
    int route[MAX_ROUTE_CELLS ? MAX_ROUTE_CELLS : 1];
    int path_len = build_route_to_exit(route, MAX_ROUTE_CELLS);
    unsigned short active_chunk = DOOM_CHUNK_START_CHUNK;
    int x_q8 = DOOM_CHUNK_START_X_Q8;
    int y_q8 = DOOM_CHUNK_START_Y_Q8;
    int width = global_grid_w();

    *out_steps = path_len > 0 ? path_len - 1 : 0;
    *out_transitions = 0;
    *out_opened_doors = 0;
    *out_opened_lifts = 0;
    if (path_len <= 1) return 0;

    for (unsigned short i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
    for (unsigned short i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
    if (!can_occupy_static(active_chunk, x_q8, y_q8)) return 0;

    for (int index = 1; index < path_len; index++) {
        int prev_cell = route[index - 1];
        int cell = route[index];
        int prev_x = prev_cell % width;
        int prev_y = prev_cell / width;
        int cell_x = cell % width;
        int cell_y = cell / width;
        int local_x_q8;
        int local_y_q8;
        int mid_x_q8;
        int mid_y_q8;
        NgChunkStreamState state;

        if (abs(cell_x - prev_x) + abs(cell_y - prev_y) != 1) return 0;
        open_route_cell(cell_x, cell_y, out_opened_doors, out_opened_lifts);

        local_x_q8 = (cell_x - (active_chunk % DOOM_CHUNK_COLS) * SIMPLE_MAP_W) * 256 + 128;
        local_y_q8 = (cell_y - (active_chunk / DOOM_CHUNK_COLS) * SIMPLE_MAP_H) * 256 + 128;
        mid_x_q8 = (x_q8 + local_x_q8) / 2;
        mid_y_q8 = (y_q8 + local_y_q8) / 2;
        if (!can_occupy_static(active_chunk, mid_x_q8, mid_y_q8)) return 0;
        if (!can_occupy_static(active_chunk, local_x_q8, local_y_q8)) return 0;

        x_q8 = local_x_q8;
        y_q8 = local_y_q8;
        state = ng_chunk_stream_update(x_q8, y_q8, active_chunk);
        if (state.changed) {
            active_chunk = state.chunk;
            x_q8 += state.shift_x_q8;
            y_q8 += state.shift_y_q8;
            (*out_transitions)++;
            if (!can_occupy_static(active_chunk, x_q8, y_q8)) return 0;
        }
    }
    return 1;
}

int main(void) {
    unsigned short active_chunk = DOOM_CHUNK_START_CHUNK;
    int x_q8 = DOOM_CHUNK_START_X_Q8;
    int y_q8 = DOOM_CHUNK_START_Y_Q8;
    const int start_global_x_q8 = chunk_global_x_q8(active_chunk, x_q8);
    const int start_global_y_q8 = chunk_global_y_q8(active_chunk, y_q8);
    const int dir_x_q8 = (int)(DOOM_CHUNK_START_DIR_X * 256.0);
    const int dir_y_q8 = (int)(DOOM_CHUNK_START_DIR_Y * 256.0);
    const int dx_q8 = mul_q8(dir_x_q8, MOVE_SPEED_Q8);
    const int dy_q8 = mul_q8(dir_y_q8, MOVE_SPEED_Q8);
    int moved_ticks = 0;
    int transitions = 0;
    int route_steps = 0;
    int route_transitions = 0;
    int route_opened_doors = 0;
    int route_opened_lifts = 0;

    for (unsigned short i = 0; i < DOOM_CHUNK_DOOR_COUNT; i++) g_chunk_door_open[i] = 0;
    for (unsigned short i = 0; i < DOOM_CHUNK_LIFT_COUNT; i++) g_chunk_lift_open[i] = 0;
    load_dynamic_blockers(active_chunk);
    if (!can_occupy(active_chunk, x_q8, y_q8)) {
        fprintf(
            stderr,
            "%s movement failed: start chunk=%u local=(%d,%d) is blocked for player radius/dynamic blockers=%d\n",
            DOOM_CHUNK_MAP_NAME,
            active_chunk,
            x_q8,
            y_q8,
            dynamic_blocker_count
        );
        return 1;
    }
    if (dx_q8 == 0 && dy_q8 == 0) {
        fprintf(stderr, "%s movement failed: generated start direction is zero\n", DOOM_CHUNK_MAP_NAME);
        return 1;
    }

    for (int tick = 0; tick < FORWARD_TICKS; tick++) {
        int old_x = x_q8;
        int old_y = y_q8;
        if (can_occupy(active_chunk, x_q8 + dx_q8, y_q8)) x_q8 += dx_q8;
        if (can_occupy(active_chunk, x_q8, y_q8 + dy_q8)) y_q8 += dy_q8;
        if (x_q8 != old_x || y_q8 != old_y) moved_ticks++;
        {
            NgChunkStreamState state = ng_chunk_stream_update(x_q8, y_q8, active_chunk);
            if (state.changed) {
                active_chunk = state.chunk;
                x_q8 += state.shift_x_q8;
                y_q8 += state.shift_y_q8;
                transitions++;
                load_dynamic_blockers(active_chunk);
                if (!can_occupy(active_chunk, x_q8, y_q8)) {
                    fprintf(
                        stderr,
                        "%s movement failed: streamed into blocked position chunk=%u local=(%d,%d)\n",
                        DOOM_CHUNK_MAP_NAME,
                        active_chunk,
                        x_q8,
                        y_q8
                    );
                    return 1;
                }
            }
        }
    }

    {
        int final_global_x_q8 = chunk_global_x_q8(active_chunk, x_q8);
        int final_global_y_q8 = chunk_global_y_q8(active_chunk, y_q8);
        int progress_x = final_global_x_q8 - start_global_x_q8;
        int progress_y = final_global_y_q8 - start_global_y_q8;
        int forward_progress = mul_q8(progress_x, dir_x_q8) + mul_q8(progress_y, dir_y_q8);
        if (forward_progress < MIN_FORWARD_PROGRESS_Q8) {
            fprintf(
                stderr,
                "%s movement failed: forward progress %d q8 after %d ticks, moved_ticks=%d chunk=%u local=(%d,%d) global=(%d,%d)\n",
                DOOM_CHUNK_MAP_NAME,
                forward_progress,
                FORWARD_TICKS,
                moved_ticks,
                active_chunk,
                x_q8,
                y_q8,
                final_global_x_q8,
                final_global_y_q8
            );
            return 1;
        }
        if (!validate_route_movement(&route_steps, &route_transitions, &route_opened_doors, &route_opened_lifts)) {
            fprintf(
                stderr,
                "%s movement failed: generated chunk route traversal failed steps=%d transitions=%d opened_doors=%d opened_lifts=%d\n",
                DOOM_CHUNK_MAP_NAME,
                route_steps,
                route_transitions,
                route_opened_doors,
                route_opened_lifts
            );
            return 1;
        }
        printf(
            "%s chunk movement OK: start_chunk=%u final_chunk=%u progress_q8=%d moved_ticks=%d transitions=%d dynamic_blockers=%d route_steps=%d route_transitions=%d route_opened_doors=%d route_opened_lifts=%d start_global=(%d,%d) final_global=(%d,%d) final_local=(%d,%d)\n",
            DOOM_CHUNK_MAP_NAME,
            DOOM_CHUNK_START_CHUNK,
            active_chunk,
            forward_progress,
            moved_ticks,
            transitions,
            dynamic_blocker_count,
            route_steps,
            route_transitions,
            route_opened_doors,
            route_opened_lifts,
            start_global_x_q8,
            start_global_y_q8,
            final_global_x_q8,
            final_global_y_q8,
            x_q8,
            y_q8
        );
    }
    return 0;
}
