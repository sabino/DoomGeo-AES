/* ripdoom_runtime.h - compact Doom BSP/blockmap query helpers. */
#ifndef RIPDOOM_RUNTIME_H
#define RIPDOOM_RUNTIME_H

#include "doom_ripdoom_generated.h"

int ripdoom_point_side(short x, short y, const NgRipNode *node);
int ripdoom_point_subsector(short x, short y);
int ripdoom_point_sector(short x, short y);
int ripdoom_blockmap_cell(short x, short y, int *block_x, int *block_y);
int ripdoom_blockmap_line_count(int block_x, int block_y);

#endif /* RIPDOOM_RUNTIME_H */
