/* ripdoom_runtime.h - compact Doom BSP/blockmap query helpers. */
#ifndef RIPDOOM_RUNTIME_H
#define RIPDOOM_RUNTIME_H

#include "doom_ripdoom_generated.h"

typedef struct NgRipRayHit {
    unsigned short seg;
    unsigned short linedef;
    short sector;
    unsigned short flags;
    unsigned short texture;
    unsigned short distance_q8;
    unsigned char texture_kind;
    unsigned char tex_u;
    unsigned char side;
} NgRipRayHit;

int ripdoom_point_side(short x, short y, const NgRipNode *node);
int ripdoom_point_subsector(short x, short y);
int ripdoom_point_sector(short x, short y);
int ripdoom_blockmap_cell(short x, short y, int *block_x, int *block_y);
int ripdoom_blockmap_line_count(int block_x, int block_y);
int ripdoom_blockmap_lines(int block_x, int block_y, unsigned short *out_lines, int max_lines);
int ripdoom_collect_local_lines(short x, short y, int block_radius, unsigned short *out_lines, int max_lines);
int ripdoom_collect_local_segs(short x, short y, int block_radius, unsigned short *out_segs, int max_segs);
int ripdoom_cast_local_ray(short x, short y, short dir_x_q8, short dir_y_q8, int block_radius, NgRipRayHit *out_hit);

#endif /* RIPDOOM_RUNTIME_H */
