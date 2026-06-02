/* config.h - screen geometry, sprite-slot layout, and raycaster tunables. */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- screen ---------------------------------------------------------- */
#define SCRW     320
#define SCRH     224
#define HUD_H    32
#define GAME_H   (SCRH - HUD_H)
#define HORIZON  (GAME_H / 2)        

/* ---- column resolution ----------------------------------------------
 * NUM_COLS wall columns, each COLW pixels wide. COLW must divide SCRW.
 * Sprite horizontal width = HSHRINK+1, so HSHRINK = COLW-1.
 */
#define NUM_COLS 64                 /* 64 walls + 20 backdrop = 84/line       */
#define COLW     (SCRW / NUM_COLS)  /* 5 px                                  */
#define HSHRINK  (COLW - 1)        
 
#define BG_SPLIT  (BG_WIN / 2)

#define WALL_WIN 15                 /* tiles in the wall sprite window       */
#define WALLH    GAME_H             /* projection scale: wall height @ dist 1 */
#define MAX_H    GAME_H             /* clamp so top>=0 (avoids Y-wrap bug)    */

/* ---- sprite slot assignment -----------------------------------------
 * Priority: lower index = back, higher = front ("1 is in the back").
 * So the floor/ceiling backdrop sprites sit behind the wall slices.
 * Sprite #0 is unusable on this hardware.
 */
#define BG_BASE   1                
#define BG_COUNT  (SCRW / 16)       
#define BG_WIN    (GAME_H / 16)     
#define WALL_BASE (BG_BASE + BG_COUNT)   
#define HUD_BASE  (WALL_BASE + NUM_COLS)
#define HUD_COUNT (SCRW / 16)
#define HUD_WIN   (HUD_H / 16)
#define SPR_TOTAL 381               

/* ---- C-ROM tile numbers (see tools/gen_gfx.py) ----------------------- */
#define TILE_BLANK 0
#define TILE_BRICK 1                /* mipmapped Doom wall texture tile      */
#define TILE_SOLID 2                /* all pixels = palette index 1          */
#define TILE_WALL_ATLAS_BASE 3
#define TILE_WALL_ATLAS_COLS 16
#define TILE_WALL_ATLAS_ROWS WALL_WIN
#define TILE_HUD_BASE (TILE_WALL_ATLAS_BASE + TILE_WALL_ATLAS_COLS * TILE_WALL_ATLAS_ROWS)
#define TILE_HUD_COLS 20
#define TILE_HUD_ROWS 2
#define BG_PHASES 16
#define TILE_CEILING_BASE (TILE_HUD_BASE + TILE_HUD_COLS * TILE_HUD_ROWS)
#define TILE_BG_PHASE_TILES (BG_COUNT * BG_SPLIT)
#define TILE_FLOOR_BASE (TILE_CEILING_BASE + BG_PHASES * TILE_BG_PHASE_TILES)
#define TILE_SPRITE_CACHE_BASE (TILE_FLOOR_BASE + BG_PHASES * TILE_BG_PHASE_TILES)

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
#define PAL_HUD       7

 
#define DEPTH_BANDS    14
#define PAL_DEPTH_BASE 8            /* lit: 8..13, dark: 14..19              */

/* ---- movement feel --------------------------------------------------- */
#define MOVE_SPEED 0.12             
#define ROT_COS    0.99452        
#define ROT_SIN    0.10453        

#endif /* CONFIG_H */
