/* chunk_stream.h - shared 16x16 chunk window transition helper */
#ifndef CHUNK_STREAM_H_INCLUDED
#define CHUNK_STREAM_H_INCLUDED

#define NG_CHUNK_STREAM_PAGE_W_Q8 (SIMPLE_MAP_W * 256)
#define NG_CHUNK_STREAM_PAGE_H_Q8 (SIMPLE_MAP_H * 256)

typedef struct NgChunkStreamState {
    unsigned short chunk;
    int local_x_q8;
    int local_y_q8;
    short shift_x_q8;
    short shift_y_q8;
    unsigned char changed;
} NgChunkStreamState;

static inline NgChunkStreamState ng_chunk_stream_update(int px_q8, int py_q8, unsigned short active_chunk) {
    int chunk_x = active_chunk % DOOM_CHUNK_COLS;
    int chunk_y = active_chunk / DOOM_CHUNK_COLS;
    int new_chunk_x = chunk_x;
    int new_chunk_y = chunk_y;
    short shift_x = 0;
    short shift_y = 0;
    NgChunkStreamState state;

    while (px_q8 < 0 && new_chunk_x > 0) {
        new_chunk_x--;
        px_q8 += NG_CHUNK_STREAM_PAGE_W_Q8;
        shift_x = (short)(shift_x + NG_CHUNK_STREAM_PAGE_W_Q8);
    }
    while (px_q8 >= NG_CHUNK_STREAM_PAGE_W_Q8 && new_chunk_x + 1 < DOOM_CHUNK_COLS) {
        new_chunk_x++;
        px_q8 -= NG_CHUNK_STREAM_PAGE_W_Q8;
        shift_x = (short)(shift_x - NG_CHUNK_STREAM_PAGE_W_Q8);
    }
    while (py_q8 < 0 && new_chunk_y > 0) {
        new_chunk_y--;
        py_q8 += NG_CHUNK_STREAM_PAGE_H_Q8;
        shift_y = (short)(shift_y + NG_CHUNK_STREAM_PAGE_H_Q8);
    }
    while (py_q8 >= NG_CHUNK_STREAM_PAGE_H_Q8 && new_chunk_y + 1 < DOOM_CHUNK_ROWS) {
        new_chunk_y++;
        py_q8 -= NG_CHUNK_STREAM_PAGE_H_Q8;
        shift_y = (short)(shift_y - NG_CHUNK_STREAM_PAGE_H_Q8);
    }

    state.chunk = (unsigned short)(new_chunk_y * DOOM_CHUNK_COLS + new_chunk_x);
    state.local_x_q8 = px_q8;
    state.local_y_q8 = py_q8;
    state.shift_x_q8 = shift_x;
    state.shift_y_q8 = shift_y;
    state.changed = (unsigned char)(state.chunk != active_chunk);
    return state;
}

#endif /* CHUNK_STREAM_H_INCLUDED */
