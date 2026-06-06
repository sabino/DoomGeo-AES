#include <stdio.h>

#include "ripdoom_runtime.h"

int main(void) {
    int player_seen = 0;
    int player_sector = -1;
    int player_subsector = -1;
    int player_block_x = -1;
    int player_block_y = -1;
    int player_block_lines = 0;
    int player_local_lines = 0;
    int player_local_segs = 0;
    int player_ray_hits = 0;
    unsigned short player_first_ray_dist = 0;
    int sector_hits = 0;
    int block_hits = 0;
    unsigned short local_lines[64];
    unsigned short local_segs[96];

    for (int i = 0; i < NG_RIP_THING_COUNT; i++) {
        const NgRipThing *thing = &g_rip_things[i];
        int sector = ripdoom_point_sector(thing->x, thing->y);
        int bx = -1;
        int by = -1;
        if (sector >= 0) sector_hits++;
        if (ripdoom_blockmap_cell(thing->x, thing->y, &bx, &by)) block_hits++;
        if (thing->type == 1 && !player_seen) {
            player_seen = 1;
            player_subsector = ripdoom_point_subsector(thing->x, thing->y);
            player_sector = sector;
            player_block_x = bx;
            player_block_y = by;
            player_block_lines = ripdoom_blockmap_line_count(bx, by);
            player_local_lines = ripdoom_collect_local_lines(thing->x, thing->y, 1, local_lines, 64);
            player_local_segs = ripdoom_collect_local_segs(thing->x, thing->y, 1, local_segs, 96);
            {
                static const short dirs[4][2] = {{256, 0}, {0, 256}, {-256, 0}, {0, -256}};
                for (int d = 0; d < 4; d++) {
                    NgRipRayHit hit;
                    if (ripdoom_cast_local_ray(thing->x, thing->y, dirs[d][0], dirs[d][1], 6, &hit)) {
                        if (!player_first_ray_dist) player_first_ray_dist = hit.distance_q8;
                        player_ray_hits++;
                    }
                }
            }
        }
    }

    if (!player_seen) {
        fprintf(stderr, "RIPDOOM runtime probe failed: no player start thing\n");
        return 1;
    }
    if (player_subsector < 0 || player_sector < 0) {
        fprintf(stderr, "RIPDOOM runtime probe failed: player subsector=%d sector=%d\n", player_subsector, player_sector);
        return 1;
    }
    if (player_block_x < 0 || player_block_y < 0) {
        fprintf(stderr, "RIPDOOM runtime probe failed: player block=(%d,%d) lines=%d\n", player_block_x, player_block_y, player_block_lines);
        return 1;
    }
    if (player_local_lines <= 0 || player_local_segs <= 0) {
        fprintf(stderr, "RIPDOOM runtime probe failed: local_lines=%d local_segs=%d\n", player_local_lines, player_local_segs);
        return 1;
    }
    if (player_ray_hits <= 0 || player_first_ray_dist == 0) {
        fprintf(stderr, "RIPDOOM runtime probe failed: ray_hits=%d first_ray_dist=%u\n", player_ray_hits, player_first_ray_dist);
        return 1;
    }
    if (sector_hits < NG_RIP_THING_COUNT / 2 || block_hits < NG_RIP_THING_COUNT / 2) {
        fprintf(stderr, "RIPDOOM runtime probe failed: sector_hits=%d block_hits=%d things=%d\n", sector_hits, block_hits, NG_RIP_THING_COUNT);
        return 1;
    }

    printf(
        "RIPDOOM runtime probe OK: player subsector=%d sector=%d block=(%d,%d) block_lines=%d local_lines=%d local_segs=%d ray_hits=%d first_ray_dist=%u thing_sectors=%d/%d thing_blocks=%d/%d\n",
        player_subsector,
        player_sector,
        player_block_x,
        player_block_y,
        player_block_lines,
        player_local_lines,
        player_local_segs,
        player_ray_hits,
        player_first_ray_dist,
        sector_hits,
        NG_RIP_THING_COUNT,
        block_hits,
        NG_RIP_THING_COUNT
    );
    return 0;
}
