/* raycast.h */
#ifndef RAYCAST_H
#define RAYCAST_H

#include "hw.h"

void rc_init(void);          /* set player start, init shadow buffers       */
void rc_input(u8 pressed);   /* pressed = active-HIGH P1 bits (already inv.) */
void rc_render(void);        /* run DDA for every column -> shadow buffers   */
void rc_blit(void);          /* push shadow buffers to VRAM (call in vblank) */
void rc_player_cell(int *cx, int *cy);

#endif /* RAYCAST_H */
