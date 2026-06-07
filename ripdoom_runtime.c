/* ripdoom_runtime.c - query real Doom geometry converted by RIPDOOM-lite. */
#include "ripdoom_runtime.h"

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
#include "doom_chunks_generated.h"
extern unsigned char g_chunk_door_open[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];
extern unsigned char g_chunk_lift_open[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];
#endif

enum { RIPDOOM_BLOCKMAP_SHIFT = 7 };

int ripdoom_point_side(short x, short y, const NgRipNode *node) {
    long dx;
    long dy;
    long left;
    long right;
    if (!node) return 0;

    if (node->dx == 0) {
        if (x <= node->x) return node->dy > 0;
        return node->dy < 0;
    }
    if (node->dy == 0) {
        if (y <= node->y) return node->dx < 0;
        return node->dx > 0;
    }

    dx = (long)x - node->x;
    dy = (long)y - node->y;
    left = (long)node->dy * dx;
    right = dy * (long)node->dx;
    return right < left ? 0 : 1;
}

int ripdoom_point_subsector(short x, short y) {
    unsigned short node_index;
    unsigned short guard;
    if (NG_RIP_SUBSECTOR_COUNT == 0) return -1;
    if (NG_RIP_NODE_COUNT == 0) return 0;

    node_index = (unsigned short)(NG_RIP_NODE_COUNT - 1);
    for (guard = 0; guard <= NG_RIP_NODE_COUNT; guard++) {
        const NgRipNode *node = &g_rip_nodes[node_index];
        unsigned short child = node->child[ripdoom_point_side(x, y, node) ? 1 : 0];
        unsigned short child_index = (unsigned short)(child & 0x7FFF);
        if (child & 0x8000) {
            if (child_index >= NG_RIP_SUBSECTOR_COUNT) return -1;
            return (int)child_index;
        }
        if (child_index >= NG_RIP_NODE_COUNT) return -1;
        node_index = child_index;
    }
    return -1;
}

int ripdoom_point_sector(short x, short y) {
    int subsector = ripdoom_point_subsector(x, y);
    int sector;
    if (subsector < 0 || subsector >= NG_RIP_SUBSECTOR_COUNT) return -1;
    sector = g_rip_subsectors[subsector].sector;
    if (sector < 0 || sector >= NG_RIP_SECTOR_COUNT) return -1;
    return sector;
}

int ripdoom_blockmap_cell(short x, short y, int *block_x, int *block_y) {
    long rel_x = (long)x - NG_RIP_BLOCKMAP_ORIGIN_X;
    long rel_y = (long)y - NG_RIP_BLOCKMAP_ORIGIN_Y;
    int bx;
    int by;
    if (rel_x < 0 || rel_y < 0) return 0;
    bx = (int)(rel_x >> RIPDOOM_BLOCKMAP_SHIFT);
    by = (int)(rel_y >> RIPDOOM_BLOCKMAP_SHIFT);
    if (bx < 0 || by < 0 || bx >= NG_RIP_BLOCKMAP_W || by >= NG_RIP_BLOCKMAP_H) return 0;
    if (block_x) *block_x = bx;
    if (block_y) *block_y = by;
    return 1;
}

int ripdoom_blockmap_line_count(int block_x, int block_y) {
    return ripdoom_blockmap_lines(block_x, block_y, 0, 0);
}

static int ripdoom_append_unique(unsigned short value, unsigned short *out_values, int count, int max_values) {
    int i;
    int stored = count < max_values ? count : max_values;
    for (i = 0; i < stored; i++) {
        if (out_values[i] == value) return count;
    }
    if (count < max_values) out_values[count] = value;
    return count + 1;
}

static int ripdoom_div_q8(long numerator, long denominator) {
    if (numerator <= 0 || denominator <= 0) return 0;
    while (numerator > 0x007fffffL) {
        numerator >>= 1;
        denominator >>= 1;
        if (denominator <= 0) return 0x7fffffff;
    }
    return (int)((numerator << 8) / denominator);
}

static unsigned char ripdoom_span_height_from_delta(int delta) {
    int height;
    if (delta < 0) delta = -delta;
    if (delta <= 0) return 0;
    height = (delta * 128 + 63) / 128;
    if (height < 1) height = 1;
    if (height > 128) height = 128;
    return (unsigned char)height;
}

static void ripdoom_seg_span(const NgRipSeg *seg, unsigned char *span, unsigned char *span_height) {
    enum { MIN_OCCLUDING_SPAN_HEIGHT = 48 };
    *span = 0;
    *span_height = 0;
    if (!seg) return;
    if (seg->flags & NG_RIP_SEG_DOOR) return;
    if (!(seg->flags & NG_RIP_SEG_TWO_SIDED)) return;
    if (seg->front_sector < 0 || seg->back_sector < 0) return;
    if (seg->front_sector >= NG_RIP_SECTOR_COUNT || seg->back_sector >= NG_RIP_SECTOR_COUNT) return;
    if ((seg->flags & NG_RIP_SEG_LOWER) && seg->lower_texture) {
        int delta = (int)g_rip_sectors[seg->back_sector].floor_height - (int)g_rip_sectors[seg->front_sector].floor_height;
        unsigned char height = ripdoom_span_height_from_delta(delta);
        if (height >= MIN_OCCLUDING_SPAN_HEIGHT) {
            *span = 1;
            *span_height = height;
            return;
        }
    }
    if ((seg->flags & NG_RIP_SEG_UPPER) && seg->upper_texture) {
        int delta = (int)g_rip_sectors[seg->front_sector].ceiling_height - (int)g_rip_sectors[seg->back_sector].ceiling_height;
        unsigned char height = ripdoom_span_height_from_delta(delta);
        if (height >= MIN_OCCLUDING_SPAN_HEIGHT) {
            *span = 2;
            *span_height = height;
        }
    }
}

#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
static int ripdoom_floor_div(long value, int divisor) {
    if (value >= 0) return (int)(value / divisor);
    return -(int)((-value + divisor - 1) / divisor);
}

static int ripdoom_chunk_door_open_at(int grid_x, int grid_y) {
    unsigned short chunk;
    unsigned short cell;
    unsigned char door_id;
    if (grid_x < 0 || grid_y < 0 || grid_x >= DOOM_CHUNK_GRID_W || grid_y >= DOOM_CHUNK_GRID_H) return 0;
    chunk = (unsigned short)((grid_y / DOOM_CHUNK_SIZE) * DOOM_CHUNK_COLS + (grid_x / DOOM_CHUNK_SIZE));
    cell = (unsigned short)((grid_y % DOOM_CHUNK_SIZE) * DOOM_CHUNK_SIZE + (grid_x % DOOM_CHUNK_SIZE));
    door_id = g_chunk_door_cell[chunk][cell];
    return door_id >= 2 && g_chunk_door_open[door_id - 2];
}

static int ripdoom_chunk_lift_open_at(int grid_x, int grid_y) {
    unsigned short chunk;
    unsigned short cell;
    unsigned char lift_id;
    if (grid_x < 0 || grid_y < 0 || grid_x >= DOOM_CHUNK_GRID_W || grid_y >= DOOM_CHUNK_GRID_H) return 0;
    chunk = (unsigned short)((grid_y / DOOM_CHUNK_SIZE) * DOOM_CHUNK_COLS + (grid_x / DOOM_CHUNK_SIZE));
    cell = (unsigned short)((grid_y % DOOM_CHUNK_SIZE) * DOOM_CHUNK_SIZE + (grid_x % DOOM_CHUNK_SIZE));
    lift_id = g_chunk_lift_cell[chunk][cell];
    return lift_id && g_chunk_lift_open[lift_id - 1];
}

static int ripdoom_chunk_door_open_near(short doom_x, short doom_y, int along_x, int along_y) {
    int grid_x = ripdoom_floor_div((long)doom_x - DOOM_CHUNK_ORIGIN_X, DOOM_CHUNK_CELL_DOOM_UNITS);
    int grid_y = ripdoom_floor_div((long)DOOM_CHUNK_ORIGIN_Y - doom_y, DOOM_CHUNK_CELL_DOOM_UNITS);
    int dx = 0;
    int dy = 0;
    if (along_x < 0) along_x = -along_x;
    if (along_y < 0) along_y = -along_y;
    if (along_x >= along_y) {
        dy = 1;
    } else {
        dx = 1;
    }
    if (ripdoom_chunk_door_open_at(grid_x, grid_y)) return 1;
    if (ripdoom_chunk_door_open_at(grid_x + dx, grid_y + dy)) return 1;
    if (ripdoom_chunk_door_open_at(grid_x - dx, grid_y - dy)) return 1;
    if (ripdoom_chunk_door_open_at(grid_x + dy, grid_y + dx)) return 1;
    if (ripdoom_chunk_door_open_at(grid_x - dy, grid_y - dx)) return 1;
    return 0;
}

static int ripdoom_chunk_lift_open_near(short doom_x, short doom_y, int along_x, int along_y) {
    int grid_x = ripdoom_floor_div((long)doom_x - DOOM_CHUNK_ORIGIN_X, DOOM_CHUNK_CELL_DOOM_UNITS);
    int grid_y = ripdoom_floor_div((long)DOOM_CHUNK_ORIGIN_Y - doom_y, DOOM_CHUNK_CELL_DOOM_UNITS);
    int dx = 0;
    int dy = 0;
    if (along_x < 0) along_x = -along_x;
    if (along_y < 0) along_y = -along_y;
    if (along_x >= along_y) {
        dy = 1;
    } else {
        dx = 1;
    }
    if (ripdoom_chunk_lift_open_at(grid_x, grid_y)) return 1;
    if (ripdoom_chunk_lift_open_at(grid_x + dx, grid_y + dy)) return 1;
    if (ripdoom_chunk_lift_open_at(grid_x - dx, grid_y - dy)) return 1;
    if (ripdoom_chunk_lift_open_at(grid_x + dy, grid_y + dx)) return 1;
    if (ripdoom_chunk_lift_open_at(grid_x - dy, grid_y - dx)) return 1;
    return 0;
}

static int ripdoom_chunk_door_seg_open(const NgRipSeg *seg, const NgRipVertex *v1, const NgRipVertex *v2) {
    short mid_x;
    short mid_y;
    if (!(seg->flags & NG_RIP_SEG_DOOR)) return 0;
    mid_x = (short)(((long)v1->x + v2->x) / 2);
    mid_y = (short)(((long)v1->y + v2->y) / 2);
    return ripdoom_chunk_door_open_near(mid_x, mid_y, (int)v2->x - v1->x, (int)v2->y - v1->y);
}

static int ripdoom_chunk_lift_seg_open(const NgRipSeg *seg, const NgRipVertex *v1, const NgRipVertex *v2) {
    short mid_x;
    short mid_y;
    if (seg->flags & NG_RIP_SEG_ONE_SIDED) return 0;
    if (!(seg->flags & (NG_RIP_SEG_LOWER | NG_RIP_SEG_UPPER | NG_RIP_SEG_MID | NG_RIP_SEG_DOOR))) return 0;
    mid_x = (short)(((long)v1->x + v2->x) / 2);
    mid_y = (short)(((long)v1->y + v2->y) / 2);
    return ripdoom_chunk_lift_open_near(mid_x, mid_y, (int)v2->x - v1->x, (int)v2->y - v1->y);
}
#endif

static void ripdoom_consider_seg_ray(
    unsigned short seg_index,
    short x,
    short y,
    short dir_x_q8,
    short dir_y_q8,
    int min_t_q8,
    int *best_seg,
    int *best_t_q8,
    int *best_u_q8,
    int *best_side
) {
    const NgRipSeg *seg;
    const NgRipVertex *v1;
    const NgRipVertex *v2;
    long seg_x;
    long seg_y;
    long rel_x;
    long rel_y;
    long denom;
    long num_t;
    long num_u;
    int t_q8;
    int u_q8;

    if (seg_index >= NG_RIP_SEG_COUNT) return;
    seg = &g_rip_segs[seg_index];
    if (!(seg->flags & (NG_RIP_SEG_SOLID | NG_RIP_SEG_DOOR | NG_RIP_SEG_LOWER | NG_RIP_SEG_UPPER | NG_RIP_SEG_MID))) return;
    if (seg->v1 >= NG_RIP_VERTEX_COUNT || seg->v2 >= NG_RIP_VERTEX_COUNT) return;
    v1 = &g_rip_vertices[seg->v1];
    v2 = &g_rip_vertices[seg->v2];
#if DOOM_SIMPLE_MAP && DOOM_CHUNKED_SIMPLE_MAP
    if (ripdoom_chunk_door_seg_open(seg, v1, v2)) return;
    if (ripdoom_chunk_lift_seg_open(seg, v1, v2)) return;
#endif
    seg_x = (long)v2->x - v1->x;
    seg_y = (long)v2->y - v1->y;
    denom = (long)dir_x_q8 * seg_y - (long)dir_y_q8 * seg_x;
    if (denom == 0) return;
    rel_x = (long)v1->x - x;
    rel_y = (long)v1->y - y;
    num_t = rel_x * seg_y - rel_y * seg_x;
    num_u = rel_x * (long)dir_y_q8 - rel_y * (long)dir_x_q8;
    if (denom < 0) {
        denom = -denom;
        num_t = -num_t;
        num_u = -num_u;
    }
    if (num_t <= 0 || num_u < 0 || num_u > denom) return;
    t_q8 = ripdoom_div_q8(num_t, denom);
    if (t_q8 < min_t_q8 || t_q8 >= *best_t_q8) return;
    u_q8 = ripdoom_div_q8(num_u, denom);
    *best_seg = seg_index;
    *best_t_q8 = t_q8;
    *best_u_q8 = u_q8;
    *best_side = ((seg_x < 0 ? -seg_x : seg_x) > (seg_y < 0 ? -seg_y : seg_y)) ? 1 : 0;
}

static void ripdoom_cast_sampled_blocks(
    short x,
    short y,
    short dir_x_q8,
    short dir_y_q8,
    int block_radius,
    int min_t_q8,
    int *best_seg,
    int *best_t_q8,
    int *best_u_q8,
    int *best_side
) {
    /* Fallback for long/open rays that leave the small local block square. */
    enum { SAMPLE_STEP = 64, MAX_SAMPLE_STEPS = 96 };
    int max_steps = block_radius * 4;
    int last_bx = -1;
    int last_by = -1;
    int step;
    if (max_steps < 8) max_steps = 8;
    if (max_steps > MAX_SAMPLE_STEPS) max_steps = MAX_SAMPLE_STEPS;

    for (step = 0; step <= max_steps; step++) {
        int dist = step * SAMPLE_STEP;
        short sx = (short)(x + (((long)dir_x_q8 * dist) >> 8));
        short sy = (short)(y + (((long)dir_y_q8 * dist) >> 8));
        int bx;
        int by;
        int cell;
        int offset;
        if (*best_seg >= 0 && dist > ((*best_t_q8 >> 8) + (1 << RIPDOOM_BLOCKMAP_SHIFT))) break;
        if (!ripdoom_blockmap_cell(sx, sy, &bx, &by)) continue;
        if (bx == last_bx && by == last_by) continue;
        last_bx = bx;
        last_by = by;
        cell = by * NG_RIP_BLOCKMAP_W + bx;
        offset = g_rip_blockmap_words[4 + cell];
        if (offset < 0 || offset >= NG_RIP_BLOCKMAP_WORD_COUNT) continue;
        if (g_rip_blockmap_words[offset] == 0) offset++;
        while (offset < NG_RIP_BLOCKMAP_WORD_COUNT && g_rip_blockmap_words[offset] != -1) {
            int line = g_rip_blockmap_words[offset++];
            if (line >= 0 && line < NG_RIP_LINE_COUNT) {
                const NgRipLineSegSpan *span = &g_rip_line_seg_spans[line];
                int span_index;
                if ((int)span->firstseg + (int)span->numsegs > NG_RIP_LINE_SEG_INDEX_COUNT) continue;
                for (span_index = 0; span_index < span->numsegs; span_index++) {
                    unsigned short seg = g_rip_line_seg_indices[span->firstseg + span_index];
                    ripdoom_consider_seg_ray(seg, x, y, dir_x_q8, dir_y_q8, min_t_q8, best_seg, best_t_q8, best_u_q8, best_side);
                }
            }
        }
    }
}

int ripdoom_blockmap_lines(int block_x, int block_y, unsigned short *out_lines, int max_lines) {
    int cell;
    int offset;
    int count = 0;
    if (block_x < 0 || block_y < 0 || block_x >= NG_RIP_BLOCKMAP_W || block_y >= NG_RIP_BLOCKMAP_H) return 0;
    cell = block_y * NG_RIP_BLOCKMAP_W + block_x;
    offset = g_rip_blockmap_words[4 + cell];
    if (offset < 0 || offset >= NG_RIP_BLOCKMAP_WORD_COUNT) return 0;
    if (g_rip_blockmap_words[offset] == 0) offset++;
    while (offset < NG_RIP_BLOCKMAP_WORD_COUNT && g_rip_blockmap_words[offset] != -1) {
        int line = g_rip_blockmap_words[offset++];
        if (line >= 0 && line < NG_RIP_LINE_COUNT) {
            if (out_lines && count < max_lines) out_lines[count] = (unsigned short)line;
            count++;
        }
    }
    return count;
}

int ripdoom_collect_local_lines(short x, short y, int block_radius, unsigned short *out_lines, int max_lines) {
    int center_x;
    int center_y;
    int count = 0;
    int by;
    if (!out_lines || max_lines <= 0) return 0;
    if (block_radius < 0) block_radius = 0;
    if (!ripdoom_blockmap_cell(x, y, &center_x, &center_y)) return 0;

    for (by = center_y - block_radius; by <= center_y + block_radius; by++) {
        int bx;
        for (bx = center_x - block_radius; bx <= center_x + block_radius; bx++) {
            int cell;
            int offset;
            if (bx < 0 || by < 0 || bx >= NG_RIP_BLOCKMAP_W || by >= NG_RIP_BLOCKMAP_H) continue;
            cell = by * NG_RIP_BLOCKMAP_W + bx;
            offset = g_rip_blockmap_words[4 + cell];
            if (offset < 0 || offset >= NG_RIP_BLOCKMAP_WORD_COUNT) continue;
            if (g_rip_blockmap_words[offset] == 0) offset++;
            while (offset < NG_RIP_BLOCKMAP_WORD_COUNT && g_rip_blockmap_words[offset] != -1) {
                int line = g_rip_blockmap_words[offset++];
                if (line >= 0 && line < NG_RIP_LINE_COUNT) {
                    count = ripdoom_append_unique((unsigned short)line, out_lines, count, max_lines);
                }
            }
        }
    }
    return count > max_lines ? max_lines : count;
}

int ripdoom_collect_local_segs(short x, short y, int block_radius, unsigned short *out_segs, int max_segs) {
    enum { LOCAL_LINE_LIMIT = 96 };
    unsigned short lines[LOCAL_LINE_LIMIT];
    int line_count;
    int seg_count = 0;
    int line_index;
    if (!out_segs || max_segs <= 0) return 0;
    line_count = ripdoom_collect_local_lines(x, y, block_radius, lines, LOCAL_LINE_LIMIT);
    for (line_index = 0; line_index < line_count; line_index++) {
        const NgRipLineSegSpan *span;
        int span_index;
        unsigned short line = lines[line_index];
        if (line >= NG_RIP_LINE_COUNT) continue;
        span = &g_rip_line_seg_spans[line];
        if ((int)span->firstseg + (int)span->numsegs > NG_RIP_LINE_SEG_INDEX_COUNT) continue;
        for (span_index = 0; span_index < span->numsegs; span_index++) {
            unsigned short seg = g_rip_line_seg_indices[span->firstseg + span_index];
            if (seg < NG_RIP_SEG_COUNT) {
                seg_count = ripdoom_append_unique(seg, out_segs, seg_count, max_segs);
            }
        }
    }
    return seg_count > max_segs ? max_segs : seg_count;
}

int ripdoom_cast_local_ray_after(short x, short y, short dir_x_q8, short dir_y_q8, int block_radius, unsigned short min_distance_q8, NgRipRayHit *out_hit) {
    enum { LOCAL_SEG_LIMIT = 128 };
    unsigned short local_segs[LOCAL_SEG_LIMIT];
    int local_count;
    int best_seg = -1;
    int best_t_q8 = 0x7fffffff;
    int best_u_q8 = 0;
    int best_side = 0;
    int i;
    int min_t_q8 = min_distance_q8 < 24 ? 24 : min_distance_q8;
    if (!out_hit || (dir_x_q8 == 0 && dir_y_q8 == 0)) return 0;
    local_count = ripdoom_collect_local_segs(x, y, block_radius, local_segs, LOCAL_SEG_LIMIT);

    for (i = 0; i < local_count; i++) {
        unsigned short seg_index = local_segs[i];
        ripdoom_consider_seg_ray(seg_index, x, y, dir_x_q8, dir_y_q8, min_t_q8, &best_seg, &best_t_q8, &best_u_q8, &best_side);
    }

    if (best_seg < 0) {
        ripdoom_cast_sampled_blocks(x, y, dir_x_q8, dir_y_q8, block_radius, min_t_q8, &best_seg, &best_t_q8, &best_u_q8, &best_side);
    }
    if (best_seg < 0) return 0;
    {
        const NgRipSeg *seg = &g_rip_segs[best_seg];
        unsigned short texture = seg->mid_texture;
        unsigned char texture_kind = seg->mid_kind;
        unsigned char span = 0;
        unsigned char span_height = 0;
        if ((seg->flags & NG_RIP_SEG_LOWER) && seg->lower_texture) texture = seg->lower_texture;
        if ((seg->flags & NG_RIP_SEG_LOWER) && seg->lower_texture) texture_kind = seg->lower_kind;
        else if ((seg->flags & NG_RIP_SEG_UPPER) && seg->upper_texture) {
            texture = seg->upper_texture;
            texture_kind = seg->upper_kind;
        }
        ripdoom_seg_span(seg, &span, &span_height);
        out_hit->seg = (unsigned short)best_seg;
        out_hit->linedef = (unsigned short)seg->linedef;
        out_hit->sector = seg->front_sector;
        out_hit->flags = seg->flags;
        out_hit->texture = texture;
        out_hit->distance_q8 = best_t_q8 > 0xffff ? 0xffff : (unsigned short)best_t_q8;
        out_hit->texture_kind = texture_kind;
        out_hit->tex_u = (unsigned char)(best_u_q8 > 255 ? 255 : best_u_q8);
        out_hit->side = (unsigned char)best_side;
        out_hit->span = span;
        out_hit->span_height = span_height;
    }
    return 1;
}

int ripdoom_cast_local_ray(short x, short y, short dir_x_q8, short dir_y_q8, int block_radius, NgRipRayHit *out_hit) {
    return ripdoom_cast_local_ray_after(x, y, dir_x_q8, dir_y_q8, block_radius, 24, out_hit);
}
