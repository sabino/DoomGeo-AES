    .text
    .align 2
    .globl main
    .type main, @function

REG_P1CNT    = 0x300000
REG_WATCHDOG = 0x300001
REG_VRAMADDR = 0x3c0000
REG_VRAMRW   = 0x3c0002
REG_VRAMMOD  = 0x3c0004
REG_LSPCMODE = 0x3c0006
PALRAM       = 0x400000
VRAM_SCB1    = 0x0000
VRAM_FIX     = 0x7000
VRAM_SCB2    = 0x8000
VRAM_SCB3    = 0x8200
VRAM_SCB4    = 0x8400

PAL_FIX      = 1
PAL_WALL     = 2
PAL_PLAYER   = 3
PAL_MARKER   = 4

main:
    jsr init_palettes
    jsr clear_fix
    jsr draw_hud_text
    jsr init_scene_sprites

.loop:
    jsr wait_vblank
    move.b #0,REG_WATCHDOG
    jsr read_controls
    jsr update_player_sprite
    bra.s .loop

init_palettes:
    lea PALRAM,%a0
    moveq #0,%d0
    move.w #80-1,%d1
.clear_pal:
    move.w %d0,(%a0)+
    dbra %d1,.clear_pal

    lea PALRAM + PAL_FIX*32,%a0
    move.w #0x0000,(%a0)+
    move.w #0x7fff,(%a0)+
    move.w #0x4f00,(%a0)+
    move.w #0x20f0,(%a0)+
    move.w #0x100f,(%a0)+
    move.w #0x7ff0,(%a0)+
    move.w #0x6aaa,(%a0)+
    move.w #0x7555,(%a0)+

    lea PALRAM + PAL_WALL*32,%a0
    move.w #0x0000,(%a0)+
    move.w #0x1111,(%a0)+
    move.w #0x2444,(%a0)+
    move.w #0x4666,(%a0)+
    move.w #0x6aaa,(%a0)+

    lea PALRAM + PAL_PLAYER*32,%a0
    move.w #0x0000,(%a0)+
    move.w #0x1111,(%a0)+
    move.w #0x4b20,(%a0)+
    move.w #0x5e41,(%a0)+
    move.w #0x6f73,(%a0)+
    move.w #0x7fff,(%a0)+
    move.w #0x2773,(%a0)+
    move.w #0x0555,(%a0)+
    move.w #0x4000,(%a0)+
    move.w #0x4b20,(%a0)+
    move.w #0x5e41,(%a0)+
    move.w #0x7fff,(%a0)+
    move.w #0x2444,(%a0)+

    lea PALRAM + PAL_MARKER*32,%a0
    move.w #0x0000,(%a0)+
    move.w #0x4f00,(%a0)+
    move.w #0x20f0,(%a0)+
    move.w #0x100f,(%a0)+
    move.w #0x7fff,(%a0)+
    rts

clear_fix:
    move.w #VRAM_FIX,REG_VRAMADDR
    move.w #1,REG_VRAMMOD
    moveq #0,%d0
    move.w #1280-1,%d1
.clear_loop:
    move.w %d0,REG_VRAMRW
    dbra %d1,.clear_loop
    rts

draw_hud_text:
    lea title,%a0
    moveq #10,%d0
    moveq #3,%d1
    jsr draw_string
    lea subtitle,%a0
    moveq #5,%d0
    moveq #5,%d1
    jsr draw_string
    lea controls,%a0
    moveq #6,%d0
    moveq #25,%d1
    jsr draw_string
    lea page_note,%a0
    moveq #4,%d0
    moveq #27,%d1
    jsr draw_string
    rts

draw_string:
    move.w %d0,%d2
    lsl.w #5,%d2
    add.w %d1,%d2
    add.w #VRAM_FIX,%d2
    move.w %d2,REG_VRAMADDR
    move.w #32,REG_VRAMMOD
.char_loop:
    moveq #0,%d3
    move.b (%a0)+,%d3
    beq.s .done
    or.w #(PAL_FIX << 12),%d3
    move.w %d3,REG_VRAMRW
    bra.s .char_loop
.done:
    rts

init_scene_sprites:
    moveq #1,%d7
    move.w #40,%d6
.wall_loop:
    move.w %d7,%d0
    moveq #1,%d1
    moveq #11,%d2
    moveq #PAL_WALL,%d3
    jsr fill_sprite_tiles
    move.w %d7,%d0
    move.w #0x0fff,%d1
    jsr write_scb2
    move.w %d7,%d0
    move.w #0xd00b,%d1
    jsr write_scb3
    move.w %d7,%d0
    move.w %d6,%d1
    jsr write_scb4_x
    add.w #24,%d6
    addq.w #1,%d7
    cmp.w #11,%d7
    blt.s .wall_loop

    moveq #15,%d0
    moveq #3,%d1
    moveq #1,%d2
    moveq #PAL_MARKER,%d3
    jsr fill_sprite_tiles
    moveq #15,%d0
    move.w #0x0fff,%d1
    jsr write_scb2
    moveq #15,%d0
    move.w #0xc003,%d1
    jsr write_scb3
    moveq #15,%d0
    move.w #152,%d1
    jsr write_scb4_x

    moveq #20,%d0
    moveq #2,%d1
    moveq #2,%d2
    moveq #PAL_PLAYER,%d3
    jsr fill_sprite_tiles
    moveq #20,%d0
    move.w #0x0fff,%d1
    jsr write_scb2
    rts

fill_sprite_tiles:
    movem.l %d0-%d4/%a0,-(%sp)
    lsl.w #6,%d0
    add.w #VRAM_SCB1,%d0
    move.w %d0,REG_VRAMADDR
    move.w #1,REG_VRAMMOD
    lsl.w #8,%d3
    move.w %d2,%d4
    subq.w #1,%d4
.tile_loop:
    move.w %d1,REG_VRAMRW
    move.w %d3,REG_VRAMRW
    dbra %d4,.tile_loop
    movem.l (%sp)+,%d0-%d4/%a0
    rts

write_scb2:
    add.w #VRAM_SCB2,%d0
    move.w %d0,REG_VRAMADDR
    move.w %d1,REG_VRAMRW
    rts

write_scb3:
    add.w #VRAM_SCB3,%d0
    move.w %d0,REG_VRAMADDR
    move.w %d1,REG_VRAMRW
    rts

write_scb4_x:
    add.w #VRAM_SCB4,%d0
    move.w %d0,REG_VRAMADDR
    lsl.w #7,%d1
    move.w %d1,REG_VRAMRW
    rts

read_controls:
    move.b REG_P1CNT,%d0
    btst #0,%d0
    bne.s .no_up
    subq.w #2,player_y
.no_up:
    btst #1,%d0
    bne.s .no_down
    addq.w #2,player_y
.no_down:
    btst #2,%d0
    bne.s .no_left
    subq.w #2,player_x
.no_left:
    btst #3,%d0
    bne.s .no_right
    addq.w #2,player_x
.no_right:
    rts

update_player_sprite:
    move.w player_y,%d1
    cmp.w #48,%d1
    bge.s .y_min_ok
    move.w #48,%d1
.y_min_ok:
    cmp.w #176,%d1
    ble.s .y_max_ok
    move.w #176,%d1
.y_max_ok:
    move.w %d1,player_y
    move.w #496,%d2
    sub.w %d1,%d2
    and.w #0x01ff,%d2
    lsl.w #7,%d2
    or.w #2,%d2
    moveq #20,%d0
    move.w %d2,%d1
    jsr write_scb3

    move.w player_x,%d1
    cmp.w #24,%d1
    bge.s .x_min_ok
    move.w #24,%d1
.x_min_ok:
    cmp.w #280,%d1
    ble.s .x_max_ok
    move.w #280,%d1
.x_max_ok:
    move.w %d1,player_x
    moveq #20,%d0
    jsr write_scb4_x
    rts

wait_vblank:
.leave_old_vblank:
    move.w REG_LSPCMODE,%d0
    btst #15,%d0
    beq.s .leave_old_vblank
.wait_next_vblank:
    move.w REG_LSPCMODE,%d0
    btst #15,%d0
    bne.s .wait_next_vblank
    rts

    .data
    .align 2
player_x:
    .word 152
player_y:
    .word 128
title:
    .asciz "DOOMGEO ASM BUILD"
subtitle:
    .asciz "68000 ONLY MAIN LOOP - NO C GAME CODE"
controls:
    .asciz "ARROWS MOVE THE GREEN MARINE SPRITE"
page_note:
    .asciz "THIS ROM IS BUILT FOR GITHUB PAGES ASM.HTML"
