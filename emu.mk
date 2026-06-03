# Emulator targets
#
# regular case: pass the rompath to the emulator

SCALE_WIN=3
SCALE_FULL?=5

MAME?=mame
MAME_RES_WIN=$(shell echo $$(($(SCALE_WIN)*320))x$$(($(SCALE_WIN)*224)))

GNGEO?=ngdevkit-gngeo

ifeq ($(GNGEO_GLSL), yes)
SHADEROPTS=-b glsl
ifneq ($(ENABLE_MINGW),yes)
ifneq ($(SHADER_PATH),)
SHADEROPTS+= --shaderpath="$(SHADER_PATH)"
endif
endif
ifneq ($(SHADER),)
SHADEROPTS+= --shader="$(SHADER)"
endif
else
SHADEROPTS=
endif

ifneq ($(ENABLE_MINGW),yes)
$(call export_path,gngeo)
gngeo:
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --scale $(SCALE_WIN) --no-resize -i $(ROM) $(GAMEROM)

$(call export_path,gngeo-fullscreen)
gngeo-fullscreen:
	$(GNGEO) --datafile="$(GNGEO_DATAFILE)" --p1control="$(GNGEO_P1CONTROL)" $(SHADEROPTS) $(EXTRAOPTS) --screen320 --fullscreen --scale $(SCALE_FULL) --no-resize -i $(ROM) $(GAMEROM)

mame:
	$(MAME) -w -resolution $(MAME_RES_WIN) -noautosave -skip_gameinfo -rp $(ROM) $(GAMEROM)

mame-fullscreen:
	$(MAME) -noautosave -skip_gameinfo -rp $(ROM) $(GAMEROM)

else
# MinGW: GnGeo is a native app, so instead of passing path to the
# Linux filesystem, we copy the $(ROM) in the GnGeo directory
# we also copy any shader config that might be in use

ifneq ($(SHADER),)
ifneq ($(SHADER),noop.glslp)
ifneq ($(GLSL_SHADER_PATH),)
$(GNGEO_INSTALL_PATH)/shaders/$(SHADER): $(GLSL_SHADER_PATH)
	$(RSYNC) -a $</ $(GNGEO_INSTALL_PATH)/shaders/
endif
endif
endif

gngeo:
	cp $(CART) $(BIOS) $(GNGEO_INSTALL_PATH)/roms && (cd $(GNGEO_INSTALL_PATH) && $(GNGEO) $(SHADEROPTS) $(EXTRAOPTS) --scale $(SCALE_WIN) --no-resize $(GAMEROM))

gngeo-fullscreen:
	cp $(CART) $(BIOS) $(GNGEO_INSTALL_PATH)/roms && (cd $(GNGEO_INSTALL_PATH) && $(GNGEO) $(SHADEROPTS) $(EXTRAOPTS) --fullscreen --scale $(SCALE_FULL) --no-resize $(GAMEROM))

mame:
	cp $(CART) $(BIOS) $(GNGEO_INSTALL_PATH)/roms && ($(MAME) -w -resolution $(MAME_RES_WIN) -noautosave -skip_gameinfo -rp $(ROM) $(GAMEROM))

mame-fullscreen:
	cp $(CART) $(BIOS) $(GNGEO_INSTALL_PATH)/roms && ($(MAME) -noautosave -skip_gameinfo -rp $(ROM) $(GAMEROM))

endif

.PHONY: gngeo gngeo-fullscreen
