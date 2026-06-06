/* raycast.h */
#ifndef RAYCAST_H
#define RAYCAST_H

#include "hw.h"

void rc_init(void);          /* set player start, init shadow buffers       */
u8 rc_input(u8 pressed);     /* pressed = active-HIGH P1 bits; returns camera changed */
u8 rc_moved_last_input(void);
void rc_invalidate_view(void);
void rc_render(void);        /* run DDA for every column -> shadow buffers   */
void rc_set_frame_overrun(u8 overrun);
void rc_blit(void);          /* push shadow buffers to VRAM (call in vblank) */
void rc_player_cell(int *cx, int *cy);
void rc_player_q8(int *x_q8, int *y_q8);
void rc_dir_q8(int *dir_x, int *dir_y);
void rc_view_q8(int *dir_x, int *dir_y, int *plane_x, int *plane_y);
void rc_set_pose_q8(short x_q8, short y_q8, short dir_x_q8, short dir_y_q8);
void rc_shift_player_q8(short dx_q8, short dy_q8);
int rc_project_point(int world_x_q8, int world_y_q8, int *screen_x, int *height, int *dist_q8);
u8 rc_sprite_strip_visible(int left, int right, int dist_q8);
void rc_reserve_sprite_budget_for_screen_range(int left, int right);
u8 rc_background_column_hidden(u8 col);
u8 rc_dynamic_blocked_q8(short x_q8, short y_q8);

#endif /* RAYCAST_H */
