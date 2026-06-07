#include <stdio.h>

#define SIMPLE_MAP_W 16
#define SIMPLE_MAP_H 16
#include "doom_chunks_generated.h"
#include "chunk_stream.h"

static int expect_state(
    const char *name,
    NgChunkStreamState state,
    unsigned short chunk,
    int local_x_q8,
    int local_y_q8,
    short shift_x_q8,
    short shift_y_q8,
    unsigned char changed
) {
    if (state.chunk == chunk
        && state.local_x_q8 == local_x_q8
        && state.local_y_q8 == local_y_q8
        && state.shift_x_q8 == shift_x_q8
        && state.shift_y_q8 == shift_y_q8
        && state.changed == changed) {
        return 0;
    }
    fprintf(
        stderr,
        "%s failed: chunk=%u local=(%d,%d) shift=(%d,%d) changed=%u, "
        "expected chunk=%u local=(%d,%d) shift=(%d,%d) changed=%u\n",
        name,
        state.chunk,
        state.local_x_q8,
        state.local_y_q8,
        state.shift_x_q8,
        state.shift_y_q8,
        state.changed,
        chunk,
        local_x_q8,
        local_y_q8,
        shift_x_q8,
        shift_y_q8,
        changed
    );
    return 1;
}

int main(void) {
    enum {
        PAGE_W = NG_CHUNK_STREAM_PAGE_W_Q8,
        PAGE_H = NG_CHUNK_STREAM_PAGE_H_Q8
    };
    const unsigned short start_chunk = DOOM_CHUNK_START_CHUNK;
    const unsigned short start_x = start_chunk % DOOM_CHUNK_COLS;
    const unsigned short start_y = start_chunk / DOOM_CHUNK_COLS;
    int failures = 0;

    if (PAGE_W != 16 * 256 || PAGE_H != 16 * 256) {
        fprintf(stderr, "unexpected chunk page size: %d x %d\n", PAGE_W, PAGE_H);
        return 1;
    }
    if (start_x == 0 || start_x + 1 >= DOOM_CHUNK_COLS || start_y == 0 || start_y + 1 >= DOOM_CHUNK_ROWS) {
        fprintf(stderr, "start chunk %u is not surrounded enough for stream probe\n", start_chunk);
        return 1;
    }

    failures += expect_state(
        "inside",
        ng_chunk_stream_update(DOOM_CHUNK_START_X_Q8, DOOM_CHUNK_START_Y_Q8, start_chunk),
        start_chunk,
        DOOM_CHUNK_START_X_Q8,
        DOOM_CHUNK_START_Y_Q8,
        0,
        0,
        0
    );
    failures += expect_state(
        "north",
        ng_chunk_stream_update(DOOM_CHUNK_START_X_Q8, -1, start_chunk),
        (unsigned short)((start_y - 1) * DOOM_CHUNK_COLS + start_x),
        DOOM_CHUNK_START_X_Q8,
        PAGE_H - 1,
        0,
        PAGE_H,
        1
    );
    failures += expect_state(
        "south",
        ng_chunk_stream_update(DOOM_CHUNK_START_X_Q8, PAGE_H, start_chunk),
        (unsigned short)((start_y + 1) * DOOM_CHUNK_COLS + start_x),
        DOOM_CHUNK_START_X_Q8,
        0,
        0,
        -PAGE_H,
        1
    );
    failures += expect_state(
        "west",
        ng_chunk_stream_update(-1, DOOM_CHUNK_START_Y_Q8, start_chunk),
        (unsigned short)(start_y * DOOM_CHUNK_COLS + start_x - 1),
        PAGE_W - 1,
        DOOM_CHUNK_START_Y_Q8,
        PAGE_W,
        0,
        1
    );
    failures += expect_state(
        "east",
        ng_chunk_stream_update(PAGE_W, DOOM_CHUNK_START_Y_Q8, start_chunk),
        (unsigned short)(start_y * DOOM_CHUNK_COLS + start_x + 1),
        0,
        DOOM_CHUNK_START_Y_Q8,
        -PAGE_W,
        0,
        1
    );
    failures += expect_state(
        "two-pages-south",
        ng_chunk_stream_update(DOOM_CHUNK_START_X_Q8, PAGE_H * 2 + 123, start_chunk),
        (unsigned short)((start_y + 2) * DOOM_CHUNK_COLS + start_x),
        DOOM_CHUNK_START_X_Q8,
        123,
        0,
        (short)(-PAGE_H * 2),
        1
    );

    if (failures) return 1;
    printf(
        "%s chunk stream OK: start_chunk=%u page=%dx%d start=(%d,%d)\n",
        DOOM_CHUNK_MAP_NAME,
        start_chunk,
        PAGE_W,
        PAGE_H,
        DOOM_CHUNK_START_X_Q8,
        DOOM_CHUNK_START_Y_Q8
    );
    return 0;
}
