# Auto-generated config file for ngdevkit-examples
# This file only holds configuration for all the build rules
# It expects ngdevkit binary to be in your PATH
# Re-generate with ./configure

# Local ngdevkit install extracted under .tools/ by the repo setup.
TOOLS_PREFIX?=$(CURDIR)/.tools/ngdevkit-local/usr
TOOL_BINDIR=$(TOOLS_PREFIX)/bin
Z80_BINDIR=$(TOOLS_PREFIX)/z80-neogeo-ihx/bin

# ngdevkit dependencies
PKGCONFIG=/usr/bin/pkg-config
PYTHON=/usr/bin/python3
ZIP=/usr/bin/zip

# ngdevkit toolchain
M68KAR=$(TOOL_BINDIR)/m68k-neogeo-elf-ar
M68KAS=$(TOOL_BINDIR)/m68k-neogeo-elf-as
M68KGCC=$(TOOL_BINDIR)/m68k-neogeo-elf-gcc
M68KGXX=$(TOOL_BINDIR)/m68k-neogeo-elf-g++
M68KLD=$(TOOL_BINDIR)/m68k-neogeo-elf-ld
M68KOBJCOPY=$(TOOL_BINDIR)/m68k-neogeo-elf-objcopy
M68KRANLIB=$(TOOL_BINDIR)/m68k-neogeo-elf-ranlib
Z80SDAR=$(Z80_BINDIR)/sdar
Z80SDAS=$(Z80_BINDIR)/sdasz80
Z80SDCC=$(Z80_BINDIR)/sdcc
Z80SDLD=$(Z80_BINDIR)/sdldz80
Z80SDOBJCOPY=$(Z80_BINDIR)/sdobjcopy
Z80SDRANLIB=$(Z80_BINDIR)/sdranlib

# ngdevkit tools
PALTOOL=$(TOOL_BINDIR)/paltool.py
TILETOOL=$(TOOL_BINDIR)/tiletool.py
ADPCMTOOL=$(TOOL_BINDIR)/adpcmtool.py
VROMTOOL=$(TOOL_BINDIR)/vromtool.py
FURTOOL=$(TOOL_BINDIR)/furtool.py
NSSTOOL=$(TOOL_BINDIR)/nsstool.py
SOUNDTOOL=$(TOOL_BINDIR)/soundtool.py

# ngdevkit build flags
NGCFLAGS=-I$(TOOLS_PREFIX)/m68k-neogeo-elf/include
NGLDFLAGS=-L$(TOOLS_PREFIX)/m68k-neogeo-elf/lib -specs ngdevkit -lngdevkit
NGLIBDIR=$(TOOLS_PREFIX)/m68k-neogeo-elf/lib
NGZ80INCLUDEDIR=$(TOOLS_PREFIX)/z80-neogeo-ihx/include
NGZ80LIBDIR=$(TOOLS_PREFIX)/z80-neogeo-ihx/lib
# this variable must be resolved here as it is used as a base
# directory for dependencies (nullsound, nullbios)
NGSHAREDIR=$(TOOLS_PREFIX)/share/ngdevkit

# additional dependencies
CONVERT=/usr/bin/convert

# any additional config or dependencies can be added below
SOX=/usr/bin/sox
RSYNC=/usr/bin/rsync
GNGEO=$(TOOL_BINDIR)/ngdevkit-gngeo
GNGEO_P1CONTROL=A=K122,B=K120,C=K97,D=K115,START=K49,COIN=K51,UP=K82,DOWN=K81,LEFT=K80,RIGHT=K79,MENU=K27

# GnGeo config
GNGEO_GLSL=no
GNGEO_SHADER_PATH=$(TOOLS_PREFIX)/share/ngdevkit-gngeo
GNGEO_DATAFILE=$(GNGEO_SHADER_PATH)/gngeo_data.zip
GLSL_SHADER_PATH=$(TOOLS_PREFIX)/share/ngdevkit-gngeo
SHADER_PATH=$(TOOLS_PREFIX)/share/ngdevkit-gngeo
SHADER=noop.glslp

# OS-specific
ENABLE_MSYS2=no
ENABLE_MINGW=no
GNGEO_INSTALL_PATH=
