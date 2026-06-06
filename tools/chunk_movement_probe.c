#include <stdio.h>

#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#include "doom_chunks_generated.h"
#include "chunk_stream.h"

enum {
    PLAYER_RADIUS_Q8 = 51,
    MOVE_SPEED_Q8 = 31,
    FORWARD_TICKS = 70,
    MIN_FORWARD_PROGRESS_Q8 = 512
};

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

static int can_occupy(unsigned short active_chunk, int x_q8, int y_q8) {
    int cx = floor_q8_cell(x_q8);
    int cy = floor_q8_cell(y_q8);
    if (map_at_cell(active_chunk, cx, cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 - PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, floor_q8_cell(x_q8 + PLAYER_RADIUS_Q8), cy)) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 - PLAYER_RADIUS_Q8))) return 0;
    if (map_at_cell(active_chunk, cx, floor_q8_cell(y_q8 + PLAYER_RADIUS_Q8))) return 0;
    return 1;
}

static int mul_q8(int a, int b) {
    return (a * b) / 256;
}

int main(void) {
    unsigned short active_chunk = DOOM_CHUNK_START_CHUNK;
    int x_q8 = DOOM_CHUNK_START_X_Q8;
    int y_q8 = DOOM_CHUNK_START_Y_Q8;
    const int start_x_q8 = x_q8;
    const int start_y_q8 = y_q8;
    const int dir_x_q8 = (int)(DOOM_CHUNK_START_DIR_X * 256.0);
    const int dir_y_q8 = (int)(DOOM_CHUNK_START_DIR_Y * 256.0);
    const int dx_q8 = mul_q8(dir_x_q8, MOVE_SPEED_Q8);
    const int dy_q8 = mul_q8(dir_y_q8, MOVE_SPEED_Q8);
    int moved_ticks = 0;
    int transitions = 0;

    if (!can_occupy(active_chunk, x_q8, y_q8)) {
        fprintf(
            stderr,
            "%s movement failed: start chunk=%u local=(%d,%d) is blocked for player radius\n",
            DOOM_CHUNK_MAP_NAME,
            active_chunk,
            x_q8,
            y_q8
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
        int progress_x = x_q8 - start_x_q8;
        int progress_y = y_q8 - start_y_q8;
        int forward_progress = mul_q8(progress_x, dir_x_q8) + mul_q8(progress_y, dir_y_q8);
        if (forward_progress < MIN_FORWARD_PROGRESS_Q8) {
            fprintf(
                stderr,
                "%s movement failed: forward progress %d q8 after %d ticks, moved_ticks=%d chunk=%u local=(%d,%d)\n",
                DOOM_CHUNK_MAP_NAME,
                forward_progress,
                FORWARD_TICKS,
                moved_ticks,
                active_chunk,
                x_q8,
                y_q8
            );
            return 1;
        }
        printf(
            "%s chunk movement OK: start_chunk=%u final_chunk=%u progress_q8=%d moved_ticks=%d transitions=%d start=(%d,%d) final=(%d,%d)\n",
            DOOM_CHUNK_MAP_NAME,
            DOOM_CHUNK_START_CHUNK,
            active_chunk,
            forward_progress,
            moved_ticks,
            transitions,
            start_x_q8,
            start_y_q8,
            x_q8,
            y_q8
        );
    }
    return 0;
}
