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
 *
 * The tiers follow the Doom8088-style detail trade. Clarity spends the sprite
 * budget on walls first because navigation readability is the main bottleneck.
 */
#if !defined(DOOM_DETAIL_CLARITY) && !defined(DOOM_DETAIL_QUALITY) && !defined(DOOM_DETAIL_BALANCED) && !defined(DOOM_DETAIL_SPEED)
#define DOOM_DETAIL_BALANCED 1
#endif

#if defined(DOOM_DETAIL_CLARITY) && (defined(DOOM_DETAIL_QUALITY) || defined(DOOM_DETAIL_BALANCED) || defined(DOOM_DETAIL_SPEED))
#error "Select exactly one Doom detail tier"
#endif
#if defined(DOOM_DETAIL_QUALITY) && (defined(DOOM_DETAIL_BALANCED) || defined(DOOM_DETAIL_SPEED))
#error "Select exactly one Doom detail tier"
#endif
#if defined(DOOM_DETAIL_BALANCED) && defined(DOOM_DETAIL_SPEED)
#error "Select exactly one Doom detail tier"
#endif

#if defined(DOOM_DETAIL_CLARITY)
#define NUM_COLS 64                 /* 5px wall strips: readable navigation */
#elif defined(DOOM_DETAIL_BALANCED)
#define NUM_COLS 32                 /* 10px wall strips: frees eight sprites */
#elif defined(DOOM_DETAIL_SPEED)
#define NUM_COLS 20                 /* 16px wall strips: fastest stable tier */
#else
#define NUM_COLS 40                 /* 8px wall strips: closest to original NGRayEx smoothness */
#endif

#if (SCRW % NUM_COLS) != 0
#error "NUM_COLS must divide SCRW"
#endif

#define COLW     (SCRW / NUM_COLS)
#define HSHRINK  (COLW - 1)        
 
#define BG_SPLIT  (BG_WIN / 2)

#define WALL_WIN 15                 /* tiles in the wall sprite window       */
#define WALLH    GAME_H             /* projection scale: wall height @ dist 1 */
#define MAX_H    GAME_H             /* clamp so top>=0 (avoids Y-wrap bug)    */
#define DOOM_RENDER_LINES 1         /* visual ray hits use WAD-derived lines  */
#ifndef DOOM_SOLID_LINE_REFINEMENT
#if defined(DOOM_DETAIL_CLARITY) || defined(DOOM_DETAIL_QUALITY)
#define DOOM_SOLID_LINE_REFINEMENT 1
#else
#define DOOM_SOLID_LINE_REFINEMENT 0
#endif
#endif

#ifndef WALL_TILE_UPLOAD_COLUMNS_PER_FRAME
#if defined(DOOM_DETAIL_CLARITY)
#define WALL_TILE_UPLOAD_COLUMNS_PER_FRAME 16
#elif defined(DOOM_DETAIL_QUALITY)
#define WALL_TILE_UPLOAD_COLUMNS_PER_FRAME 12
#else
#define WALL_TILE_UPLOAD_COLUMNS_PER_FRAME 8
#endif
#endif

/* ---- sprite slot assignment -----------------------------------------
 * Priority: lower index = back on Neo Geo sprite evaluation. One world thing
 * sits behind the gun, so centered enemies are still visible but the weapon
 * correctly overlays them. HUD lives on non-gameplay rows.
 * Sprite #0 is unusable on this hardware.
 */
#define BG_BASE   1                
#define BG_COUNT  (SCRW / 16)       
#define BG_WIN    (GAME_H / 16)     
#define WALL_BASE (BG_BASE + BG_COUNT)   
/*
 * Runtime sprite budget on active playfield scanlines depends on DOOM_DETAIL.
 * The default clarity tier uses 20 backdrop + 64 wall columns + 4 thing strips
 * + 7 weapon strips = 95. Neo Geo evaluates 96 sprites per scanline, so every
 * tier keeps the weapon in front while staying inside the practical budget.
 */
#if defined(DOOM_DETAIL_CLARITY)
#define ENEMY_VISIBLE_COUNT 1       /* 20 backdrop + 64 walls + 4 thing + 7 weapon = 95 */
#elif defined(DOOM_DETAIL_BALANCED)
#define ENEMY_VISIBLE_COUNT 9
#elif defined(DOOM_DETAIL_SPEED)
#define ENEMY_VISIBLE_COUNT 11
#else
#define ENEMY_VISIBLE_COUNT 7
#endif

#define ENEMY_STRIPS 4
#define ENEMY_BASE   (WALL_BASE + NUM_COLS)
#define ENEMY_COUNT  (ENEMY_VISIBLE_COUNT * ENEMY_STRIPS)
#define ENEMY_WIN    5
#define ENEMY_GROUND_LIFT 2         /* keeps prescaled Doom sprites seated on the raycast floor */
#define WEAPON_BASE  (ENEMY_BASE + ENEMY_COUNT) /* keep gun inside the first 96 sprites/line */
#define WEAPON_COUNT 7
#define WEAPON_WIN   8
#define WEAPON_Y_OFFSET 0
#define DOOM_PLAYFIELD_SPRITE_COUNT (BG_COUNT + NUM_COLS + ENEMY_COUNT + WEAPON_COUNT)
#define DOOM_PLAYFIELD_SCANLINE_LIMIT 95
#if DOOM_PLAYFIELD_SPRITE_COUNT > DOOM_PLAYFIELD_SCANLINE_LIMIT
#error "Active playfield sprites exceed the Neo Geo scanline budget"
#endif
#if (WEAPON_BASE + WEAPON_COUNT - 1) > DOOM_PLAYFIELD_SCANLINE_LIMIT
#error "Weapon sprites must stay inside the first 95 active playfield sprites"
#endif

#define HUD_BASE  (WEAPON_BASE + WEAPON_COUNT)
#define HUD_COUNT (SCRW / 16)
#define HUD_WIN   (HUD_H / 16)
#define HUD_Y_OFFSET 0              /* STBAR starts exactly at the playfield edge */
#define HUD_VALUE_BASE 300
#define HUD_VALUE_COUNT 11
#define HUD_KEY_BASE 312
#define HUD_KEY_COUNT 3
#define HUD_COUNTER_SHADOW_BASE 320
#define HUD_COUNTER_SHADOW_COUNT 24
#define HUD_COUNTER_BASE (HUD_COUNTER_SHADOW_BASE + HUD_COUNTER_SHADOW_COUNT)
#define HUD_COUNTER_COUNT 24
#define SPR_TOTAL 381               

/* ---- C-ROM tile numbers (see tools/gen_gfx.py) ----------------------- */
#define TILE_BLANK 0
#define TILE_BRICK 1                /* mipmapped Doom wall texture tile      */
#define TILE_SOLID 2                /* all pixels = palette index 1          */
#define TILE_WALL_ATLAS_BASE 3
#if defined(DOOM_DETAIL_CLARITY)
#define TILE_WALL_ATLAS_COLS 32     /* denser texture phase sampling for close walls */
#else
#define TILE_WALL_ATLAS_COLS 16
#endif
#define TILE_WALL_ATLAS_ROWS WALL_WIN
#define TILE_WALL_ATLAS_TILES (TILE_WALL_ATLAS_COLS * TILE_WALL_ATLAS_ROWS)
#define TILE_WALL_ALT_COUNT 7
#define TILE_WALL_ALT_ATLAS_BASE (TILE_WALL_ATLAS_BASE + TILE_WALL_ATLAS_TILES)
#define TILE_DOOR_ATLAS_BASE (TILE_WALL_ALT_ATLAS_BASE + TILE_WALL_ALT_COUNT * TILE_WALL_ATLAS_TILES)
#define TILE_FLAT_COLS 16
#define TILE_FLAT_ROWS 16
#define TILE_FLAT_TILES (TILE_FLAT_COLS * TILE_FLAT_ROWS)
#if defined(DOOM_DETAIL_CLARITY)
#define TILE_PLANE_PERSPECTIVE_DIRS 4
#else
#define TILE_PLANE_PERSPECTIVE_DIRS 16
#endif
#define TILE_PLANE_PERSPECTIVE_PHASES 1
#define TILE_PLANE_PERSPECTIVE_ROWS BG_SPLIT
#define TILE_PLANE_PERSPECTIVE_COLS BG_COUNT
#define TILE_PLANE_PERSPECTIVE_TILES (TILE_PLANE_PERSPECTIVE_DIRS * TILE_PLANE_PERSPECTIVE_PHASES * TILE_PLANE_PERSPECTIVE_PHASES * TILE_PLANE_PERSPECTIVE_ROWS * TILE_PLANE_PERSPECTIVE_COLS)
#define BG_SCROLL_COLUMNS_PER_FRAME 2
#ifndef DOOM_FLAT_PLANES
#define DOOM_FLAT_PLANES 0          /* pre-baked moving floor/ceiling cache */
#endif
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
#define TILE_WEAPON_FRAMES 28
#define TILE_WEAPON_FRAME_TILES (TILE_WEAPON_STRIPS * TILE_WEAPON_ROWS)
#define TILE_HUD_KEYCARD_BASE (TILE_WEAPON_BASE + TILE_WEAPON_FRAMES * TILE_WEAPON_FRAME_TILES)
#define TILE_HUD_KEYCARD_COUNT 3
#define TILE_HUD_DIGIT_BASE (TILE_HUD_KEYCARD_BASE + TILE_HUD_KEYCARD_COUNT)
#define TILE_HUD_DIGIT_COUNT 10
#define TILE_HUD_SMALL_DIGIT_BASE (TILE_HUD_DIGIT_BASE + TILE_HUD_DIGIT_COUNT)
#define TILE_HUD_SMALL_DIGIT_COUNT 10
#define TILE_CEILING_PERSPECTIVE_BASE (TILE_HUD_SMALL_DIGIT_BASE + TILE_HUD_SMALL_DIGIT_COUNT)
#define TILE_FLOOR_PERSPECTIVE_BASE (TILE_CEILING_PERSPECTIVE_BASE + TILE_PLANE_PERSPECTIVE_TILES)
#define TILE_TITLEPIC_BASE (TILE_FLOOR_PERSPECTIVE_BASE + TILE_PLANE_PERSPECTIVE_TILES)
#define TILE_TITLEPIC_COLS 20
#define TILE_TITLEPIC_ROWS 13
#define TILE_TITLEPIC_TILES (TILE_TITLEPIC_COLS * TILE_TITLEPIC_ROWS)
#define TILE_SPRITE_CACHE_BASE (TILE_TITLEPIC_BASE + TILE_TITLEPIC_TILES)

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
#define FIX_KEY_MSG_K 62            /* K,Y for compact locked-door KEY message */
#define FIX_KEY_MSG_Y 63
#define FIX_AMMO_M 64               /* M,O for compact empty-ammo AMMO msg   */
#define FIX_AMMO_O 65
#define FIX_SECRET_S 66             /* S,C for compact secret message        */
#define FIX_SECRET_C 67

 
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
#define PAL_TITLE     35
#define PAL_ENEMY_BASE 37
#define PAL_AMMO_COUNTER_SHADOW 45
#define PAL_HUD_KEY_BASE 46
#define PAL_AMMO_COUNTER 49

 
#define DEPTH_BANDS    14
#define PAL_DEPTH_BASE 8            /* lit: 8..13, dark: 14..19              */

/* ---- movement feel --------------------------------------------------- */
#define MOVE_SPEED 0.16
#define ROT_COS    0.99619
#define ROT_SIN    0.08716

#endif /* CONFIG_H */
