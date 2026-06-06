# Copyright (c) 2015-2025 Damien Ciabrini
# This file is part of ngdevkit
#
# ngdevkit is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# ngdevkit is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with ngdevkit.  If not, see <http://www.gnu.org/licenses/>.



all: cart bios

# These are various variables that you might want to customize
# based on your liking or your requirements.
# location of generated and compiled content
BUILDDIR=build
# all directories that contain source to be compiled
SRCDIRS=
# default build flags, can be overriden per target
CFLAGS=-I$(BUILDDIR) -std=c99 -fomit-frame-pointer -Os -g
LDFLAGS=
Z80FLAGS=
Z80LDFLAGS=

# Build-time Doom WAD conversion. The generated header is intentionally kept
# in BUILDDIR so cart builds can swap maps without touching committed sources.
FREEDOOM_VERSION=0.13.0
FREEDOOM_ZIP=.tools/assets/freedoom-$(FREEDOOM_VERSION).zip
FREEDOOM_URL=https://github.com/freedoom/freedoom/releases/download/v$(FREEDOOM_VERSION)/freedoom-$(FREEDOOM_VERSION).zip
DOOM_SHAREWARE_ZIP=.tools/assets/doom1.wad.zip
DOOM_SHAREWARE_URL=https://www.libsdl.org/projects/doom/data/doom1.wad.zip
DOOM_IWAD?=$(DOOM_SHAREWARE_ZIP)
DOOM_MAP?=E1M1
DOOM_SIMPLE_MAP?=0
ifeq ($(DOOM_SIMPLE_MAP),1)
DOOM_MAP_WIDTH?=16
DOOM_MAP_HEIGHT?=16
else
DOOM_MAP_WIDTH?=48
DOOM_MAP_HEIGHT?=36
endif
DOOM_MAP_DETAIL_CULL?=0.5
DOOM_RENDER_DETAIL_CULL?=1.5
DOOM_MAP_READABILITY_CLEANUP?=1
DOOM_SKILL_MASK?=4
DOOM_WALL_TEXTURE?=STARTAN3
DOOM_DETAIL?=quality
DOOM_FRAME_STATS?=0
DOOM_SKIP_INTRO?=0
DOOM_WALL_UPLOAD_COLUMNS?=
DOOM_WALL_UPLOAD_OVERRUN_COLUMNS?=
EPISODE_MAPS?=E1M1 E1M2 E1M3 E1M4 E1M5 E1M6 E1M7 E1M8 E1M9
EPISODE_MAP?=E1M1
DOOM_MAP_HEADER=$(BUILDDIR)/doom_map_generated.h
DOOM_MAP_SOURCE=$(BUILDDIR)/doom_map_generated.c
DOOM_MAP_OBJECT=$(BUILDDIR)/doom_map_generated.o
DOOM_CHUNK_HEADER=$(BUILDDIR)/doom_chunks_generated.h
DOOM_CHUNK_SOURCE=$(BUILDDIR)/doom_chunks_generated.c
DOOM_CHUNK_OBJECT=
DOOM_CHUNK_DEP=
DOOM_CHUNK_PREVIEW=$(BUILDDIR)/doom_chunks_preview.txt
DOOM_CHUNK_SIZE?=16
DOOM_CHUNK_CELL_UNITS?=128
DOOM_ASSETS_HEADER=$(BUILDDIR)/doom_assets_generated.h
DOOM_ASSETS_SOURCE=$(BUILDDIR)/doom_assets_generated.c
DOOM_ASSETS_OBJECT=$(BUILDDIR)/doom_assets_generated.o
DOOM_MAP_CLEANUP_ARGS=$(if $(filter 1 yes true,$(DOOM_MAP_READABILITY_CLEANUP)),--readability-cleanup,)
GFX_SIMPLE_MAP_ARG=$(if $(filter 1 yes true,$(DOOM_SIMPLE_MAP)),--simple-map,)
GFX_HEADER=$(BUILDDIR)/doom_gfx_generated.h
GFX_ROM_DIR?=rom
GFX_STAMP=$(GFX_ROM_DIR)/.generated-gfx
CUSTOM_GENERATE_TARGETS+=doom-assets

ifeq ($(DOOM_DETAIL),clarity)
DOOM_DETAIL_DEFINE=-DDOOM_DETAIL_CLARITY=1
else ifeq ($(DOOM_DETAIL),quality)
DOOM_DETAIL_DEFINE=-DDOOM_DETAIL_QUALITY=1
else ifeq ($(DOOM_DETAIL),balanced)
DOOM_DETAIL_DEFINE=-DDOOM_DETAIL_BALANCED=1
else ifeq ($(DOOM_DETAIL),speed)
DOOM_DETAIL_DEFINE=-DDOOM_DETAIL_SPEED=1
else
$(error DOOM_DETAIL must be clarity, quality, balanced, or speed)
endif
override CFLAGS += $(DOOM_DETAIL_DEFINE)
ifeq ($(DOOM_FRAME_STATS),1)
override CFLAGS += -DDOOM_FRAME_STATS=1
endif
ifeq ($(DOOM_SKIP_INTRO),1)
override CFLAGS += -DDOOM_SKIP_INTRO=1
endif
ifeq ($(DOOM_SIMPLE_MAP),1)
override CFLAGS += -DDOOM_SIMPLE_MAP=1 -DDOOM_CHUNKED_SIMPLE_MAP=1
DOOM_CHUNK_OBJECT=$(BUILDDIR)/doom_chunks_generated.o
DOOM_CHUNK_DEP=$(DOOM_CHUNK_HEADER)
endif
ifneq ($(strip $(DOOM_WALL_UPLOAD_COLUMNS)),)
override CFLAGS += -DWALL_TILE_UPLOAD_COLUMNS_PER_FRAME=$(DOOM_WALL_UPLOAD_COLUMNS)
endif
ifneq ($(strip $(DOOM_WALL_UPLOAD_OVERRUN_COLUMNS)),)
override CFLAGS += -DWALL_TILE_UPLOAD_COLUMNS_OVERRUN=$(DOOM_WALL_UPLOAD_OVERRUN_COLUMNS)
endif
ifneq ($(strip $(DOOM_BG_SCROLL_COLUMNS)),)
override CFLAGS += -DBG_SCROLL_COLUMNS_PER_FRAME=$(DOOM_BG_SCROLL_COLUMNS)
endif
ifneq ($(strip $(DOOM_BG_SCROLL_OVERRUN_COLUMNS)),)
override CFLAGS += -DBG_SCROLL_COLUMNS_OVERRUN=$(DOOM_BG_SCROLL_OVERRUN_COLUMNS)
endif
ifneq ($(strip $(DOOM_ADAPTIVE_LINE_REFINEMENT)),)
override CFLAGS += -DDOOM_ADAPTIVE_LINE_REFINEMENT=$(DOOM_ADAPTIVE_LINE_REFINEMENT)
endif
ifneq ($(strip $(DOOM_MOVING_LINE_REFINEMENT_CELLS)),)
override CFLAGS += -DDOOM_MOVING_LINE_REFINEMENT_CELLS=$(DOOM_MOVING_LINE_REFINEMENT_CELLS)
endif
ifneq ($(strip $(DOOM_OVERRUN_LINE_REFINEMENT_CELLS)),)
override CFLAGS += -DDOOM_OVERRUN_LINE_REFINEMENT_CELLS=$(DOOM_OVERRUN_LINE_REFINEMENT_CELLS)
endif

# This is an autoconf-generated configuration for your environment
# (ngdevkit path, OS-specific configs...)
include config.mk

# This defines the layout of your game cartridge
# You can customize it to match your requirements
include rom.mk

# All the generic build targets (68k, Z80, assets, run)
include build.mk

# Some default targets for running your project via emulators
include emu.mk



# program ROM: your main program
# Add your dependencies below to compile your sources into an ELF binary
# that is used as the content of the program ROM (referenced by symbol PROM1)
# 
# Note: build rules (%.c -> %.o -> %.elf) are defined in Makefile.build
ELF=$(BUILDDIR)/rom.elf
$(ELF):	$(BUILDDIR)/main.o $(BUILDDIR)/raycast.o $(DOOM_MAP_OBJECT) $(DOOM_CHUNK_OBJECT) $(DOOM_ASSETS_OBJECT)
$(PROM1): $(ELF)

$(BUILDDIR)/main.o: config.h hw.h raycast.h map.h simple_map.h $(DOOM_MAP_HEADER) $(DOOM_CHUNK_DEP) $(DOOM_ASSETS_HEADER) $(GFX_HEADER)
$(BUILDDIR)/raycast.o: config.h hw.h raycast.h map.h simple_map.h $(DOOM_MAP_HEADER) $(DOOM_ASSETS_HEADER)
$(DOOM_MAP_OBJECT): $(DOOM_MAP_SOURCE) $(DOOM_MAP_HEADER)
	$(M68KGCC) $(NGCFLAGS) $(CFLAGS) -c $(DOOM_MAP_SOURCE) -o $@
$(BUILDDIR)/doom_chunks_generated.o: $(DOOM_CHUNK_SOURCE) $(DOOM_CHUNK_HEADER)
	$(M68KGCC) $(NGCFLAGS) $(CFLAGS) -c $(DOOM_CHUNK_SOURCE) -o $@
$(DOOM_ASSETS_OBJECT): $(DOOM_ASSETS_SOURCE) $(DOOM_ASSETS_HEADER)
	$(M68KGCC) $(NGCFLAGS) $(CFLAGS) -c $(DOOM_ASSETS_SOURCE) -o $@

FACE_TEST_ROM=$(BUILDDIR)/face-test-rom
FACE_TEST_ASSET_ROM=$(BUILDDIR)/face-test-assets
FACE_TEST_GFX_STAMP=$(FACE_TEST_ASSET_ROM)/.generated-gfx
FACE_TEST_ELF=$(BUILDDIR)/face_test.elf
FACE_TEST_PROM=$(FACE_TEST_ROM)/202-p1.p1
FACE_TEST_CART=$(FACE_TEST_ROM)/$(GAMEROM).zip

$(BUILDDIR)/face_test.o: $(GFX_HEADER)
$(FACE_TEST_ELF): $(BUILDDIR)/face_test.o
	$(M68KGCC) -o $@ $^ $(NGLDFLAGS) $(LDFLAGS)

$(FACE_TEST_ROM):
	mkdir -p $@

$(FACE_TEST_ASSET_ROM):
	mkdir -p $@

$(FACE_TEST_GFX_STAMP): tools/gen_gfx.py tools/doom_convert.py config.h $(DOOM_MAP_HEADER) $(DOOM_IWAD) | $(FACE_TEST_ASSET_ROM)
	$(PYTHON) tools/gen_gfx.py --iwad $(DOOM_IWAD) --map $(DOOM_MAP) --wall-texture $(DOOM_WALL_TEXTURE) --detail $(DOOM_DETAIL) --face-tune-grid --out-dir $(FACE_TEST_ASSET_ROM)
	touch $@

$(FACE_TEST_PROM): $(FACE_TEST_ELF) | $(FACE_TEST_ROM)
	$(M68KOBJCOPY) -O binary -S -R .text2 --gap-fill 0xff --pad-to $(PROMSIZE) $< $@ && dd if=$@ of=$@ conv=notrunc,swab status=none

$(FACE_TEST_ROM)/202-c1.c1: $(FACE_TEST_GFX_STAMP) | $(FACE_TEST_ROM)
	cp $(FACE_TEST_ASSET_ROM)/c1.bin $@
$(FACE_TEST_ROM)/202-c2.c2: $(FACE_TEST_GFX_STAMP) | $(FACE_TEST_ROM)
	cp $(FACE_TEST_ASSET_ROM)/c2.bin $@
$(FACE_TEST_ROM)/202-s1.s1: $(FACE_TEST_GFX_STAMP) | $(FACE_TEST_ROM)
	cp $(FACE_TEST_ASSET_ROM)/s1.bin $@
$(FACE_TEST_ROM)/202-m1.m1: $(FACE_TEST_GFX_STAMP) | $(FACE_TEST_ROM)
	cp $(FACE_TEST_ASSET_ROM)/m1.bin $@
$(FACE_TEST_ROM)/202-v1.v1: $(FACE_TEST_GFX_STAMP) | $(FACE_TEST_ROM)
	cp $(FACE_TEST_ASSET_ROM)/v1.bin $@

$(FACE_TEST_ROM)/neogeo.zip: $(ROM)/neogeo.zip | $(FACE_TEST_ROM)
	cp $< $@

$(FACE_TEST_CART): $(FACE_TEST_PROM) $(FACE_TEST_ROM)/202-c1.c1 $(FACE_TEST_ROM)/202-c2.c2 $(FACE_TEST_ROM)/202-s1.s1 $(FACE_TEST_ROM)/202-m1.m1 $(FACE_TEST_ROM)/202-v1.v1 $(FACE_TEST_ROM)/neogeo.zip
	cd $(FACE_TEST_ROM) && for i in `ls -1 | grep -v -e \.bin -e \.zip`; do ln -nsf $$i $${i%.*}.bin; done; \
	printf "===\nhttps://github.com/dciabrin/ngdevkit\n===" | zip -qz $(GAMEROM).zip `ls -1 | grep -v -e \.zip`

face-test-rom: $(FACE_TEST_CART)

face-test-gngeo: $(FACE_TEST_CART)
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i $(FACE_TEST_ROM) $(GAMEROM)

HUD_TEST_ROM=$(BUILDDIR)/hud-test-rom
HUD_TEST_ELF=$(BUILDDIR)/hud_test.elf
HUD_TEST_PROM=$(HUD_TEST_ROM)/202-p1.p1
HUD_TEST_CART=$(HUD_TEST_ROM)/$(GAMEROM).zip

$(BUILDDIR)/hud_test.o: $(GFX_HEADER)
$(HUD_TEST_ELF): $(BUILDDIR)/hud_test.o
	$(M68KGCC) -o $@ $^ $(NGLDFLAGS) $(LDFLAGS)

$(HUD_TEST_ROM):
	mkdir -p $@

$(HUD_TEST_PROM): $(HUD_TEST_ELF) | $(HUD_TEST_ROM)
	$(M68KOBJCOPY) -O binary -S -R .text2 --gap-fill 0xff --pad-to $(PROMSIZE) $< $@ && dd if=$@ of=$@ conv=notrunc,swab status=none

$(HUD_TEST_ROM)/202-c1.c1: $(GFX_STAMP) | $(HUD_TEST_ROM)
	cp rom/c1.bin $@
$(HUD_TEST_ROM)/202-c2.c2: $(GFX_STAMP) | $(HUD_TEST_ROM)
	cp rom/c2.bin $@
$(HUD_TEST_ROM)/202-s1.s1: $(GFX_STAMP) | $(HUD_TEST_ROM)
	cp rom/s1.bin $@
$(HUD_TEST_ROM)/202-m1.m1: $(GFX_STAMP) | $(HUD_TEST_ROM)
	cp rom/m1.bin $@
$(HUD_TEST_ROM)/202-v1.v1: $(GFX_STAMP) | $(HUD_TEST_ROM)
	cp rom/v1.bin $@

$(HUD_TEST_ROM)/neogeo.zip: $(ROM)/neogeo.zip | $(HUD_TEST_ROM)
	cp $< $@

$(HUD_TEST_CART): $(HUD_TEST_PROM) $(HUD_TEST_ROM)/202-c1.c1 $(HUD_TEST_ROM)/202-c2.c2 $(HUD_TEST_ROM)/202-s1.s1 $(HUD_TEST_ROM)/202-m1.m1 $(HUD_TEST_ROM)/202-v1.v1 $(HUD_TEST_ROM)/neogeo.zip
	cd $(HUD_TEST_ROM) && for i in `ls -1 | grep -v -e \.bin -e \.zip`; do ln -nsf $$i $${i%.*}.bin; done; \
	printf "===\nhttps://github.com/dciabrin/ngdevkit\n===" | zip -qz $(GAMEROM).zip `ls -1 | grep -v -e \.zip`

hud-test-rom: $(HUD_TEST_CART)

hud-test-gngeo: $(HUD_TEST_CART)
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i $(HUD_TEST_ROM) $(GAMEROM)

key-test-rom:
	$(MAKE) cart DOOM_MAP=E1M2 BUILDDIR=build/key-test ROM=build/key-test-rom GFX_ROM_DIR=build/key-test-assets

key-test-gngeo:
	$(MAKE) key-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/key-test-rom $(GAMEROM)

key-door-test-rom:
	$(MAKE) cart DOOM_MAP=E1M2 BUILDDIR=build/key-door-test ROM=build/key-door-test-rom GFX_ROM_DIR=build/key-door-test-assets CFLAGS="-Ibuild/key-door-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_KEY_DOOR_TEST"

key-door-test-gngeo:
	$(MAKE) key-door-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/key-door-test-rom $(GAMEROM)

chunk-key-door-test-rom:
	$(MAKE) cart DOOM_MAP=E1M2 DOOM_SIMPLE_MAP=1 DOOM_SKIP_INTRO=1 BUILDDIR=build/chunk-key-door-test ROM=build/chunk-key-door-test-rom GFX_ROM_DIR=build/chunk-key-door-test-assets CFLAGS="-Ibuild/chunk-key-door-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_KEY_DOOR_TEST"

chunk-key-door-test-gngeo:
	$(MAKE) chunk-key-door-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/chunk-key-door-test-rom $(GAMEROM)

combat-test-rom:
	$(MAKE) cart BUILDDIR=build/combat-test ROM=build/combat-test-rom GFX_ROM_DIR=build/combat-test-assets CFLAGS="-Ibuild/combat-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_COMBAT_TEST"

combat-test-gngeo:
	$(MAKE) combat-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/combat-test-rom $(GAMEROM)

encounter-test-rom:
	$(MAKE) cart BUILDDIR=build/encounter-test ROM=build/encounter-test-rom GFX_ROM_DIR=build/encounter-test-assets CFLAGS="-Ibuild/encounter-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_E1M1_ENCOUNTER_TEST"

encounter-test-gngeo:
	$(MAKE) encounter-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/encounter-test-rom $(GAMEROM)

scout-test-rom:
	$(MAKE) cart BUILDDIR=build/scout-test ROM=build/scout-test-rom GFX_ROM_DIR=build/scout-test-assets CFLAGS="-Ibuild/scout-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_E1M1_SCOUT_TEST"

scout-test-gngeo:
	$(MAKE) scout-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/scout-test-rom $(GAMEROM)

exit-test-rom:
	$(MAKE) cart BUILDDIR=build/exit-test ROM=build/exit-test-rom GFX_ROM_DIR=build/exit-test-assets CFLAGS="-Ibuild/exit-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_E1M1_EXIT_TEST"

exit-test-gngeo:
	$(MAKE) exit-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/exit-test-rom $(GAMEROM)

e1m8-boss-test-rom:
	$(MAKE) cart DOOM_MAP=E1M8 DOOM_DETAIL=quality BUILDDIR=build/e1m8-boss-test ROM=build/e1m8-boss-test-rom GFX_ROM_DIR=build/e1m8-boss-test-assets CFLAGS="-Ibuild/e1m8-boss-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_E1M8_BOSS_TEST"

e1m8-boss-test-gngeo:
	$(MAKE) e1m8-boss-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/e1m8-boss-test-rom $(GAMEROM)

episode-map-rom:
	$(MAKE) cart DOOM_MAP=$(EPISODE_MAP) BUILDDIR=build/episode-roms/$(EPISODE_MAP) ROM=build/episode-roms/$(EPISODE_MAP)-rom GFX_ROM_DIR=build/episode-roms/$(EPISODE_MAP)-assets

episode-map-gngeo:
	$(MAKE) episode-map-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/episode-roms/$(EPISODE_MAP)-rom $(GAMEROM)

episode-roms:
	@for map in $(EPISODE_MAPS); do \
		echo "== $$map =="; \
		$(MAKE) episode-map-rom EPISODE_MAP=$$map || exit $$?; \
	done

hidden-attack-test-rom:
	$(MAKE) cart BUILDDIR=build/hidden-attack-test ROM=build/hidden-attack-test-rom GFX_ROM_DIR=build/hidden-attack-test-assets CFLAGS="-Ibuild/hidden-attack-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_HIDDEN_ATTACK_TEST"

hidden-attack-test-gngeo:
	$(MAKE) hidden-attack-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/hidden-attack-test-rom $(GAMEROM)

melee-test-rom:
	$(MAKE) cart BUILDDIR=build/melee-test ROM=build/melee-test-rom GFX_ROM_DIR=build/melee-test-assets CFLAGS="-Ibuild/melee-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_MELEE_TEST"

melee-test-gngeo:
	$(MAKE) melee-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/melee-test-rom $(GAMEROM)

monster-gallery-rom:
	$(MAKE) cart DOOM_DETAIL=quality BUILDDIR=build/monster-gallery ROM=build/monster-gallery-rom GFX_ROM_DIR=build/monster-gallery-assets CFLAGS="-Ibuild/monster-gallery -std=c99 -fomit-frame-pointer -Os -g -DDOOM_MONSTER_GALLERY_TEST"

monster-gallery-gngeo:
	$(MAKE) monster-gallery-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/monster-gallery-rom $(GAMEROM)

arsenal-test-rom:
	$(MAKE) cart DOOM_DETAIL=quality BUILDDIR=build/arsenal-test ROM=build/arsenal-test-rom GFX_ROM_DIR=build/arsenal-test-assets CFLAGS="-Ibuild/arsenal-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_ARSENAL_TEST"

arsenal-test-gngeo:
	$(MAKE) arsenal-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/arsenal-test-rom $(GAMEROM)

death-test-rom:
	$(MAKE) cart DOOM_DETAIL=quality BUILDDIR=build/death-test ROM=build/death-test-rom GFX_ROM_DIR=build/death-test-assets CFLAGS="-Ibuild/death-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_DEATH_TEST"

death-test-gngeo:
	$(MAKE) death-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/death-test-rom $(GAMEROM)

powerup-test-rom:
	$(MAKE) cart DOOM_DETAIL=quality BUILDDIR=build/powerup-test ROM=build/powerup-test-rom GFX_ROM_DIR=build/powerup-test-assets CFLAGS="-Ibuild/powerup-test -std=c99 -fomit-frame-pointer -Os -g -DDOOM_POWERUP_TEST"

powerup-test-gngeo:
	$(MAKE) powerup-test-rom
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i build/powerup-test-rom $(GAMEROM)

ASM_ROM=$(BUILDDIR)/asm-rom
ASM_ASSET_ROM=$(BUILDDIR)/asm-assets
ASM_GFX_STAMP=$(ASM_ASSET_ROM)/.generated-gfx
ASM_ELF=$(BUILDDIR)/asm/doomgeo_asm.elf
ASM_PROM=$(ASM_ROM)/202-p1.p1
ASM_CART=$(ASM_ROM)/$(GAMEROM).zip
ASM_SOUND_DRIVER=$(NGSHAREDIR)/nullsound_driver.ihx

$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	mkdir -p $(dir $@)
	$(M68KGCC) $(NGCFLAGS) $(CFLAGS) -c $< -o $@

$(ASM_ELF): $(BUILDDIR)/asm/doomgeo_asm.o
	$(M68KGCC) -o $@ $^ $(NGLDFLAGS) $(LDFLAGS)

$(ASM_ROM):
	mkdir -p $@

$(ASM_ASSET_ROM):
	mkdir -p $@

$(ASM_GFX_STAMP): tools/gen_asm_gfx.py | $(ASM_ASSET_ROM)
	$(PYTHON) tools/gen_asm_gfx.py --out-dir $(ASM_ASSET_ROM)
	touch $@

$(ASM_PROM): $(ASM_ELF) | $(ASM_ROM)
	$(M68KOBJCOPY) -O binary -S -R .text2 --gap-fill 0xff --pad-to $(PROMSIZE) $< $@ && dd if=$@ of=$@ conv=notrunc,swab status=none

$(ASM_ROM)/202-c1.c1: $(ASM_GFX_STAMP) | $(ASM_ROM)
	cp $(ASM_ASSET_ROM)/c1.bin $@
$(ASM_ROM)/202-c2.c2: $(ASM_GFX_STAMP) | $(ASM_ROM)
	cp $(ASM_ASSET_ROM)/c2.bin $@
$(ASM_ROM)/202-s1.s1: $(ASM_GFX_STAMP) | $(ASM_ROM)
	cp $(ASM_ASSET_ROM)/s1.bin $@
$(ASM_ROM)/202-m1.m1: $(ASM_SOUND_DRIVER) | $(ASM_ROM)
	$(Z80SDOBJCOPY) -I ihex -O binary $< $@ --pad-to $(MROMSIZE)
$(ASM_ROM)/202-v1.v1: $(ASM_GFX_STAMP) | $(ASM_ROM)
	cp $(ASM_ASSET_ROM)/v1.bin $@

$(ASM_ROM)/neogeo.zip: $(ROM)/neogeo.zip | $(ASM_ROM)
	cp $< $@

$(ASM_CART): $(ASM_PROM) $(ASM_ROM)/202-c1.c1 $(ASM_ROM)/202-c2.c2 $(ASM_ROM)/202-s1.s1 $(ASM_ROM)/202-m1.m1 $(ASM_ROM)/202-v1.v1 $(ASM_ROM)/neogeo.zip
	cd $(ASM_ROM) && for i in `ls -1 | grep -v -e \.bin -e \.zip`; do ln -nsf $$i $${i%.*}.bin; done; \
	printf "===\nhttps://github.com/dciabrin/ngdevkit\n===" | zip -qz $(GAMEROM).zip `ls -1 | grep -v -e \.zip`

asm-rom: $(ASM_CART)

asm-gngeo: $(ASM_CART)
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i $(ASM_ROM) $(GAMEROM)

smoke-screenshot:
	tools/smoke_capture.sh

route-check: $(DOOM_MAP_HEADER) $(DOOM_MAP_SOURCE)
	$(PYTHON) tools/check_e1m1_route.py --header $(DOOM_MAP_HEADER) --source $(DOOM_MAP_SOURCE)

episode-route-report: $(DOOM_IWAD)
	$(PYTHON) tools/check_episode_routes.py --iwad $(DOOM_IWAD) --width $(DOOM_MAP_WIDTH) --height $(DOOM_MAP_HEIGHT) --detail-cull $(DOOM_MAP_DETAIL_CULL) --render-detail-cull $(DOOM_RENDER_DETAIL_CULL) $(DOOM_MAP_CLEANUP_ARGS) --skill-mask $(DOOM_SKILL_MASK)

episode-route-check: $(DOOM_IWAD)
	$(PYTHON) tools/check_episode_routes.py --iwad $(DOOM_IWAD) --width $(DOOM_MAP_WIDTH) --height $(DOOM_MAP_HEIGHT) --detail-cull $(DOOM_MAP_DETAIL_CULL) --render-detail-cull $(DOOM_RENDER_DETAIL_CULL) $(DOOM_MAP_CLEANUP_ARGS) --skill-mask $(DOOM_SKILL_MASK) --strict

bsp-asset-check: doom-assets
	$(PYTHON) tools/check_bsp_assets.py --map-header $(DOOM_MAP_HEADER) --assets-header $(DOOM_ASSETS_HEADER) --assets-source $(DOOM_ASSETS_SOURCE)

.PHONY: face-test-rom face-test-gngeo hud-test-rom hud-test-gngeo key-test-rom key-test-gngeo key-door-test-rom key-door-test-gngeo chunk-key-door-test-rom chunk-key-door-test-gngeo combat-test-rom combat-test-gngeo encounter-test-rom encounter-test-gngeo scout-test-rom scout-test-gngeo exit-test-rom exit-test-gngeo e1m8-boss-test-rom e1m8-boss-test-gngeo episode-map-rom episode-map-gngeo episode-roms hidden-attack-test-rom hidden-attack-test-gngeo melee-test-rom melee-test-gngeo arsenal-test-rom arsenal-test-gngeo death-test-rom death-test-gngeo powerup-test-rom powerup-test-gngeo asm-rom asm-gngeo smoke-screenshot route-check episode-route-report episode-route-check bsp-asset-check chunk-map

$(FREEDOOM_ZIP):
	mkdir -p $(dir $@)
	curl -L --fail --output $@ $(FREEDOOM_URL)

$(DOOM_SHAREWARE_ZIP):
	mkdir -p $(dir $@)
	curl -L --fail --output $@ $(DOOM_SHAREWARE_URL)

$(DOOM_MAP_HEADER) $(DOOM_MAP_SOURCE): Makefile tools/doom_convert.py $(DOOM_IWAD) | $(BUILDDIR)
	$(PYTHON) tools/doom_convert.py --iwad $(DOOM_IWAD) --map $(DOOM_MAP) --skill-mask $(DOOM_SKILL_MASK) --width $(DOOM_MAP_WIDTH) --height $(DOOM_MAP_HEIGHT) --detail-cull $(DOOM_MAP_DETAIL_CULL) --render-detail-cull $(DOOM_RENDER_DETAIL_CULL) $(DOOM_MAP_CLEANUP_ARGS) --out $(DOOM_MAP_HEADER) --map-source $(DOOM_MAP_SOURCE) --assets-header $(DOOM_ASSETS_HEADER) --assets-source $(DOOM_ASSETS_SOURCE)

$(DOOM_CHUNK_HEADER) $(DOOM_CHUNK_SOURCE): Makefile tools/doom_chunk_convert.py tools/doom_convert.py $(DOOM_IWAD) | $(BUILDDIR)
	$(PYTHON) tools/doom_chunk_convert.py --iwad $(DOOM_IWAD) --map $(DOOM_MAP) --skill-mask $(DOOM_SKILL_MASK) --chunk-size $(DOOM_CHUNK_SIZE) --cell-units $(DOOM_CHUNK_CELL_UNITS) --out $(DOOM_CHUNK_HEADER) --chunk-source $(DOOM_CHUNK_SOURCE) --preview $(DOOM_CHUNK_PREVIEW)

chunk-map: $(DOOM_CHUNK_HEADER) $(DOOM_CHUNK_SOURCE)

$(DOOM_ASSETS_HEADER) $(DOOM_ASSETS_SOURCE): $(DOOM_MAP_HEADER)

doom-assets: $(DOOM_MAP_HEADER) $(DOOM_MAP_SOURCE) $(DOOM_ASSETS_HEADER) $(DOOM_ASSETS_SOURCE)



# fixed tiles ROM: all your 8x8 pixel tiles for the fixed layer
# Add your dependencies below to convert images to fixed tile binary data
# and pack it into the fixed tile ROM (referenced by symbol SROM1)
# 
# By default, this makefile creates a tileset ROM with small and tall
# latin characters for printing ASCII string, and tiles that are
# displayed during the attract mode.
# Note: build rules (%.gif -> %.fix) are defined in Makefile.build
$(SROM1): $(GFX_ROM_DIR)/s1.bin



# sprite ROM: all your 16x16 pixel tiles for sprites
# Add your dependencies below to convert images to fixed sprite binary data
# and pack it into two separate sprite ROMs that hold odd and even bits of
# color information (referenced by symbols CROM1 and CROM2)
#
# By default, this makefile creates CROMs with tiles for displaying a ngdevkit
# logo during the attract mode.
# Note: build rules (%.gif -> %.c<1,2>) are defined in Makefile.build
$(CROM1): $(GFX_ROM_DIR)/c1.bin
$(CROM2): $(GFX_ROM_DIR)/c2.bin

$(GFX_ROM_DIR)/c1.bin $(GFX_ROM_DIR)/c2.bin $(GFX_ROM_DIR)/s1.bin $(GFX_ROM_DIR)/m1.bin $(GFX_ROM_DIR)/v1.bin: $(GFX_STAMP)
	@test -f $@

$(GFX_STAMP): tools/gen_gfx.py tools/doom_convert.py config.h $(DOOM_MAP_HEADER) $(DOOM_IWAD) | $(BUILDDIR) $(GFX_ROM_DIR)
	$(PYTHON) tools/gen_gfx.py --iwad $(DOOM_IWAD) --map $(DOOM_MAP) --wall-texture $(DOOM_WALL_TEXTURE) --detail $(DOOM_DETAIL) $(GFX_SIMPLE_MAP_ARG) --palette-header $(GFX_HEADER) --out-dir $(GFX_ROM_DIR)
	touch $@

$(GFX_ROM_DIR):
	mkdir -p $@

$(GFX_HEADER): $(GFX_STAMP)
	@test -f $@



# sound driver ROM: nullsound + your configured sound commands
# Add your dependencies below to compile your z80 sources into a HEX binary
# that is used as the sound driver ROM (referenced by symbol MROM1)
#
# By default, this makefile uses the empty sound driver provided by ngdevkit
# Note: build rules (%.s -> %.o -> %.ihx) are defined in Makefile.build
SOUND_DRIVER=$(NGSHAREDIR)/nullsound_driver.ihx
$(MROM1): $(SOUND_DRIVER)



# sound FX ROM: all your ADPCM samples
# Add your dependencies below to convert your samples into suitable
# ADPCM data and pack it into the sound ROM (referenced by symbol VROM1)
#
# Music and SFX assets can be managed with <> rather to generate
# the dependencies automatically.
# By default, no sample is used, so the VROM is empty
# Note: build rules (%.wav-> %.adpcm<a,b>) are defined in Makefile.build



# One-time asset preprocessing
# Various assets may have to be processed before they can be used to
# build your ROM, for example:
#   . the ngdevkit assets must be converted to sprite and text tiles
#   . converting your SFX assets from .mp3 into .wav so they can be
#     processed by ngdevkit tools
# To handle these pre-processing requirements, `make` automatically
# runs into all subdirectories under ./setup/, so you get a chance
# to preprocess what you need before anything is built.
#
# Other assets must be processed only once to generate source files
# that gets built into your ROM, for example:
#   . converting your musics from .fur to z80 code and data files
#   . generating make dependencies to pack gfx or sound assets into
#     the right ROM bank based on your input assets
# Those actions can be achieved by creating custom make targets
# that get invoked automatically when added to the variable below.



# a default `clean` target removes .o .elf and .ihx from the builddir
# a default `distclean` target removes the build directory entirely
# you can customize clean up by adding dependencies to those targets
