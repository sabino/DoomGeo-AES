#include <stdio.h>
#include <stdlib.h>

#include "ripdoom_runtime.h"
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#include "doom_chunks_generated.h"
#include "chunk_stream.h"
#endif

#define FBITS 8
#define FONE 256
#define FIX(v) ((int)((v) * FONE))
#ifndef DOOM_RIPDOOM_RENDER_BLOCK_RADIUS
#define DOOM_RIPDOOM_RENDER_BLOCK_RADIUS 8
#endif

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
enum {
    PLAYER_RADIUS_Q8 = 51,
    DYNAMIC_BLOCK_RANGE_Q8 = 104,
    MOVE_SPEED_Q8 = 31,
    MOVEMENT_RENDER_TICKS = 70,
    MIN_RENDER_MOVE_PROGRESS_Q8 = 256
};

typedef struct DynamicBlocker {
    int x_q8;
    int y_q8;
} DynamicBlocker;

static DynamicBlocker dynamic_blockers[DOOM_CHUNK_MAX_ACTIVE_THINGS ? DOOM_CHUNK_MAX_ACTIVE_THINGS : 1];
static int dynamic_blocker_count = 0;

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
    if (g_chunk_door_cell[chunk][cell] >= 2) return 1;
    if (g_chunk_lift_cell[chunk][cell]) return 1;
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

static int chunk_global_x_q8(unsigned short active_chunk, int x_q8) {
    int chunk_x = active_chunk % DOOM_CHUNK_COLS;
    return chunk_x * SIMPLE_MAP_W * 256 + x_q8;
}

static int chunk_global_y_q8(unsigned short active_chunk, int y_q8) {
    int chunk_y = active_chunk / DOOM_CHUNK_COLS;
    return chunk_y * SIMPLE_MAP_H * 256 + y_q8;
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
        if (!ripdoom_cast_local_ray((short)start_x, (short)start_y, ray_x, ray_y, DOOM_RIPDOOM_RENDER_BLOCK_RADIUS, &hit)) {
            continue;
        }
        if (*out_first < 0) *out_first = column;
        hits++;
        if (hit.distance_q8 < *out_min) *out_min = hit.distance_q8;
        if (hit.distance_q8 > *out_max) *out_max = hit.distance_q8;
    }
    return hits;
}

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
            moved_hits = sample_view(moved_rip_x, moved_rip_y, view_x, view_y, &moved_min, &moved_max, &moved_first);
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
            printf(
                "RIPDOOM render probe OK: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u forward_hits=%d/%d forward_dist=%u..%u moved_hits=%d/%d moved_dist=%u..%u moved_progress_q8=%d moved_ticks=%d\n",
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
                progress_q8,
                moved_ticks
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
