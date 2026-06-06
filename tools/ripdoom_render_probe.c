#include <stdio.h>

#include "ripdoom_runtime.h"

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
        if (!ripdoom_cast_local_ray((short)start_x, (short)start_y, ray_x, ray_y, 8, &hit)) {
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
    int start_angle = 0;
    short view_x = 256;
    short view_y = 0;
    int player_seen = 0;
    int hits = 0;
    int first_hit_column = -1;
    unsigned int min_dist = 0xffff;
    unsigned int max_dist = 0;
    const char *mode = "player";

    for (int i = 0; i < NG_RIP_THING_COUNT; i++) {
        if (g_rip_things[i].type == 1) {
            start_x = g_rip_things[i].x;
            start_y = g_rip_things[i].y;
            start_angle = g_rip_things[i].angle;
            player_seen = 1;
            break;
        }
    }
    if (!player_seen) {
        fprintf(stderr, "RIPDOOM render probe failed: no player start thing\n");
        return 1;
    }

    switch (start_angle) {
    case 0: view_x = 256; view_y = 0; break;
    case 90: view_x = 0; view_y = 256; break;
    case 180: view_x = -256; view_y = 0; break;
    case 270: view_x = 0; view_y = -256; break;
    default: view_x = 256; view_y = 0; break;
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

    printf("RIPDOOM render probe OK: angle=%d mode=%s hits=%d/%d first=%d dist=%u..%u\n", start_angle, mode, hits, COLUMNS, first_hit_column, min_dist, max_dist);
    return 0;
}
