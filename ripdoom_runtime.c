/* ripdoom_runtime.c - query real Doom geometry converted by RIPDOOM-lite. */
#include "ripdoom_runtime.h"

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
        if (line >= 0 && line < NG_RIP_LINE_COUNT) count++;
    }
    return count;
}
