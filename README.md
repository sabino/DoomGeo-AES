# NGRayEx

A real-time, first-person raycaster demonstration for the SNK Neo Geo AES, written
in C.

This was made purely for research purposes to understand the complexities of rendering realtime "3D"
on the Neo Geo. The code is unoptimized and could be built to run much faster. 

<img width="1280" height="729" alt="image" src="https://github.com/user-attachments/assets/b7616e2a-4fb5-4afa-b1b9-46d4edaf23b0" />

## How it works

Every frame, for each of 80 screen columns:

1. Cast a ray through a 2D grid map until it hits a wall.
2. Measure the perpendicular distance and turn it into a slice height
3. Write a vertical-shrink value, a Y position, and a palette into the sprite
   control block for that column's sprite.

The video chip then scales each column's brick-texture sprite to the computed
height. Floor and ceiling are a static backdrop of full-width sprites sitting
behind the walls (lower sprite indices draw first = further back). A HUD
minimap is drawn on the fix (text) layer, which always composites over sprites.

All arithmetic is 16.16 . Rotation uses constant cos/sin multiplies. The whole
renderer writes only a few control words per column per frame; the expensive
pixel work is offloaded to the scaler hardware.
 
## Controls

| Input               | Action            |
|---------------------|-------------------|
| D-pad Up / Down     | Move forward/back |
| D-pad Left / Right  | Turn              |
| Hold A + Left/Right | Strafe            |
| C                   | Toggle minimap    |

## Building

The `doom-neogeo-port` branch expects a local ngdevkit/GnGeo install under
`.tools/ngdevkit-local`; `.tools/` is ignored by git so the toolchain and WAD
downloads stay repo-local without being committed.
 

```sh
# graphics + sound ROMs (self-contained tile encoder)
python3 tools/gen_gfx.py

# compile, convert Freedoom E1M1, and assemble the cartridge
make

# run with the local GnGeo and known-good keyboard mapping
SDL_VIDEODRIVER=x11 make gngeo
```

`tools/gen_gfx.py` emits the C/S/M/V ROMs directly in the Neo Geo's planar
format, so the only ngdevkit dependency is the m68k toolchain. See the comments
at the top of each tool for details.

`tools/doom_convert.py` reads a Doom-format WAD at build time and emits a compact
grid header for the Neo Geo raycaster. By default, `make` downloads Freedoom
0.13.0 into `.tools/assets/` and converts `E1M1`:

```sh
make DOOM_MAP=E1M2
make DOOM_MAP=E1M1 DOOM_MAP_WIDTH=38 DOOM_MAP_HEIGHT=27
```

This is intentionally build-time WAD compatibility: the cartridge contains the
converted map data, not a runtime WAD loader.
 
You must supply your own Neo Geo BIOS — it is copyrighted and not included.

I've only tested this with [gngeo](https://github.com/dciabrin/gngeo) It may not render correctly on real hardware
 
## Acknowledgements

Built against the [ngdevkit](https://github.com/dciabrin/ngdevkit) toolchain.
Hardware details cross-referenced from the
[Neo Geo Development Wiki](https://wiki.neogeodev.org).  
