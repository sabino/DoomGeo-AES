/* config.h - screen geometry, sprite-slot layout, and raycaster tunables. */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- screen ---------------------------------------------------------- */
#define SCRW     320
#define SCRH     224
#define HORIZON  (SCRH / 2)        

/* ---- column resolution ----------------------------------------------
 * NUM_COLS wall columns, each COLW pixels wide. COLW must divide SCRW.
 * Sprite horizontal width = HSHRINK+1, so HSHRINK = COLW-1.
 */
#define NUM_COLS 64                 /* 64 walls + 20 backdrop strips < 96/line */
#define COLW     (SCRW / NUM_COLS)  /* 4 px                                  */
#define HSHRINK  (COLW - 1)        
 
#define BG_SPLIT  7

#define WALL_WIN 15                 /* tiles in the wall sprite window       */
#define WALLH    224                /* projection scale: wall height @ dist 1 */
#define MAX_H    SCRH               /* clamp so top>=0 (avoids Y-wrap bug)    */

/* ---- sprite slot assignment -----------------------------------------
 * Priority: lower index = back, higher = front ("1 is in the back").
 * So the floor/ceiling backdrop sprites sit behind the wall slices.
 * Sprite #0 is unusable on this hardware.
 */
#define BG_BASE   1                
#define BG_COUNT  (SCRW / 16)       
#define BG_WIN    14               
#define WALL_BASE (BG_BASE + BG_COUNT)   
#define SPR_TOTAL 381               

/* ---- C-ROM tile numbers (see tools/gen_gfx.py) ----------------------- */
#define TILE_BLANK 0
#define TILE_BRICK 1                /* mipmapped Doom wall texture tile      */
#define TILE_SOLID 2                /* all pixels = palette index 1          */
#define TILE_WALL_ATLAS_BASE 3
#define TILE_WALL_ATLAS_COLS 16
#define TILE_WALL_ATLAS_ROWS WALL_WIN
#define TILE_SPRITE_CACHE_BASE 243

/* ---- fix-layer (S-ROM) tile numbers --------------------------------- */
#define FIX_BLANK  0                /* transparent (all index 0)             */
#define FIX_SOLID  1                /* all index 15 -> opaque, palette picks color */

 
#define MAP_FIX_COL 1             
#define MAP_FIX_ROW 1             

/* ---- palettes -------------------------------------------------------- */
#define PAL_WALL_LIT  1             /* N/S faces                             */
#define PAL_WALL_DARK 2             /* E/W faces (directional shading)       */
#define PAL_CEILING   3
#define PAL_FLOOR     4
#define PAL_MAP_WALL  5             /* minimap wall block (fix tile, idx 15) */
#define PAL_MAP_PLAYER 6            /* minimap player marker                 */

 
#define DEPTH_BANDS    14
#define PAL_DEPTH_BASE 8            /* lit: 8..13, dark: 14..19              */

/* ---- movement feel --------------------------------------------------- */
#define MOVE_SPEED 0.12             
#define ROT_COS    0.99452        
#define ROT_SIN    0.10453        

#endif /* CONFIG_H */
