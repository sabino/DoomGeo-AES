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
 * Priority: lower index = back on Neo Geo sprite evaluation.  Weapon slots
 * stay immediately after the 84 backdrop+wall sprites so the gun is still in
 * the first 96 sprites on busy scanlines; HUD lives on non-gameplay rows.
 * Sprite #0 is unusable on this hardware.
 */
#define BG_BASE   1                
#define BG_COUNT  (SCRW / 16)       
#define BG_WIN    (GAME_H / 16)     
#define WALL_BASE (BG_BASE + BG_COUNT)   
#define WEAPON_BASE  (WALL_BASE + NUM_COLS) /* keep gun inside the first 96 sprites/line */
#define WEAPON_COUNT 7
#define WEAPON_WIN   8
#define WEAPON_Y_OFFSET 0
#define ENEMY_VISIBLE_COUNT 3
#define ENEMY_STRIPS 4
#define ENEMY_BASE   (WEAPON_BASE + WEAPON_COUNT)
#define ENEMY_COUNT  (ENEMY_VISIBLE_COUNT * ENEMY_STRIPS)
#define ENEMY_WIN    5
#define ENEMY_GROUND_LIFT 2         /* keeps prescaled Doom sprites seated on the raycast floor */
#define HUD_BASE  (ENEMY_BASE + ENEMY_COUNT)
#define HUD_COUNT (SCRW / 16)
#define HUD_WIN   (HUD_H / 16)
#define HUD_Y_OFFSET 1              /* Neo Geo sprite Y anchor correction for the bottom status bar */
#define SPR_TOTAL 381               

/* ---- C-ROM tile numbers (see tools/gen_gfx.py) ----------------------- */
#define TILE_BLANK 0
#define TILE_BRICK 1                /* mipmapped Doom wall texture tile      */
#define TILE_SOLID 2                /* all pixels = palette index 1          */
#define TILE_WALL_ATLAS_BASE 3
#define TILE_WALL_ATLAS_COLS 16
#define TILE_WALL_ATLAS_ROWS WALL_WIN
#define TILE_WALL_ATLAS_TILES (TILE_WALL_ATLAS_COLS * TILE_WALL_ATLAS_ROWS)
#define TILE_WALL_ALT_COUNT 7
#define TILE_WALL_ALT_ATLAS_BASE (TILE_WALL_ATLAS_BASE + TILE_WALL_ATLAS_TILES)
#define TILE_DOOR_ATLAS_BASE (TILE_WALL_ALT_ATLAS_BASE + TILE_WALL_ALT_COUNT * TILE_WALL_ATLAS_TILES)
#define TILE_FLAT_COLS 16
#define TILE_FLAT_ROWS 16
#define TILE_FLAT_TILES (TILE_FLAT_COLS * TILE_FLAT_ROWS)
#define TILE_PLANE_PERSPECTIVE_DIRS 16
#define TILE_PLANE_PERSPECTIVE_PHASES 2
#define TILE_PLANE_PERSPECTIVE_ROWS BG_SPLIT
#define TILE_PLANE_PERSPECTIVE_COLS BG_COUNT
#define TILE_PLANE_PERSPECTIVE_TILES (TILE_PLANE_PERSPECTIVE_DIRS * TILE_PLANE_PERSPECTIVE_PHASES * TILE_PLANE_PERSPECTIVE_PHASES * TILE_PLANE_PERSPECTIVE_ROWS * TILE_PLANE_PERSPECTIVE_COLS)
#define TILE_CEILING_FLAT_BASE (TILE_DOOR_ATLAS_BASE + TILE_WALL_ATLAS_TILES)
#define TILE_FLOOR_FLAT_BASE (TILE_CEILING_FLAT_BASE + TILE_FLAT_TILES)
#define TILE_HUD_BASE (TILE_FLOOR_FLAT_BASE + TILE_FLAT_TILES)
#define TILE_HUD_COLS 20
#define TILE_HUD_ROWS 2
#define TILE_HUD_FACE_BASE (TILE_HUD_BASE + TILE_HUD_COLS * TILE_HUD_ROWS)
#define TILE_HUD_FACE_COL 9
#define TILE_HUD_FACE_COLS 2
#define TILE_HUD_FACE_ROWS 2
#define TILE_HUD_FACE_FRAMES 36
#define TILE_HUD_FACE_FRAME_TILES (TILE_HUD_FACE_COLS * TILE_HUD_FACE_ROWS)
#define TILE_WEAPON_BASE (TILE_HUD_FACE_BASE + TILE_HUD_FACE_FRAMES * TILE_HUD_FACE_FRAME_TILES)
#define TILE_WEAPON_STRIPS 7
#define TILE_WEAPON_ROWS 8
#define TILE_WEAPON_FRAMES 12
#define TILE_WEAPON_FRAME_TILES (TILE_WEAPON_STRIPS * TILE_WEAPON_ROWS)
#define TILE_CEILING_PERSPECTIVE_BASE (TILE_WEAPON_BASE + TILE_WEAPON_FRAMES * TILE_WEAPON_FRAME_TILES)
#define TILE_FLOOR_PERSPECTIVE_BASE (TILE_CEILING_PERSPECTIVE_BASE + TILE_PLANE_PERSPECTIVE_TILES)
#define TILE_SPRITE_CACHE_BASE (TILE_FLOOR_PERSPECTIVE_BASE + TILE_PLANE_PERSPECTIVE_TILES)

/* ---- fix-layer (S-ROM) tile numbers --------------------------------- */
#define FIX_BLANK  0                /* transparent (all index 0)             */
#define FIX_SOLID  1                /* all index 15 -> opaque, palette picks color */
#define FIX_BIG_DIGIT_BASE 2        /* 2..41 = Doom STTNUM digits, 4 tiles each */
#define FIX_DIGIT_BASE 42           /* 42..51 = compact 8x8 message digits   */
#define FIX_AIM    52               /* tiny center aim marker                */
#define FIX_EXIT_BASE 53            /* 53..56 = EXIT message glyphs          */
#define FIX_DEAD_D 57               /* 57,58 plus EXIT's E draw DEAD         */
#define FIX_DEAD_A 58
#define FIX_KEY_BASE 59             /* 59..61 = B/R/Y key HUD glyphs         */
#define FIX_KEY_MSG_K 62            /* K for compact locked-door KEY message */
#define FIX_AMMO_M 63               /* M,O for compact empty-ammo AMMO msg   */
#define FIX_AMMO_O 64
#define FIX_SECRET_S 65             /* S,C for compact secret message        */
#define FIX_SECRET_C 66

 
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
#define PAL_CEILING_GRAD_BASE 50
#define PAL_FLOOR_GRAD_BASE   56
#define PAL_DOOR_DEPTH_BASE   62
#define PAL_WALL_ALT_DEPTH_BASE 90
#define PAL_WALL_ALT_DEPTH_STRIDE (DEPTH_BANDS * 2)
#define PAL_WEAPON    36
#define PAL_ENEMY_BASE 37

 
#define DEPTH_BANDS    14
#define PAL_DEPTH_BASE 8            /* lit: 8..13, dark: 14..19              */

/* ---- movement feel --------------------------------------------------- */
#define MOVE_SPEED 0.12             
#define ROT_COS    0.99863        
#define ROT_SIN    0.05234        

#endif /* CONFIG_H */
