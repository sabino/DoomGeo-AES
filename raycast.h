/* raycast.h */
#ifndef RAYCAST_H
#define RAYCAST_H

#include "hw.h"

void rc_init(void);          /* set player start, init shadow buffers       */
void rc_input(u8 pressed);   /* pressed = active-HIGH P1 bits (already inv.) */
void rc_invalidate_view(void);
void rc_render(void);        /* run DDA for every column -> shadow buffers   */
void rc_blit(void);          /* push shadow buffers to VRAM (call in vblank) */
void rc_player_cell(int *cx, int *cy);
void rc_player_q8(int *x_q8, int *y_q8);
void rc_view_q8(int *dir_x, int *dir_y, int *plane_x, int *plane_y);
int rc_project_point(int world_x_q8, int world_y_q8, int *screen_x, int *height, int *dist_q8);

#endif /* RAYCAST_H */
