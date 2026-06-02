# NGRayEx

A real-time, first-person raycaster demonstration for the SNK Neo Geo AES, written
in C.

This was made purely for research purposes to understand the complexities of rendering realtime "3D"
on the Neo Geo. The code is unoptimized and could be built to run much faster. 

<img width="960" height="672" alt="Current Neo Geo Doom prototype with textured floor and ceiling, pistol HUD, and an imp target" src="docs/screenshots/doom-neogeo-current.png" />

## How it works

Every frame, for each of 64 screen columns:

1. Cast a ray through a 2D grid map until it hits a wall.
2. Measure the perpendicular distance and turn it into a slice height
3. Write a vertical-shrink value, a Y position, and a palette into the sprite
   control block for that column's sprite.

The video chip then scales each precomposed Doom wall-texture column to the
computed height. Floor and ceiling use Doom flat textures selected from the
player-start sector and packed into preprojected sprite-strip phase banks; the
68000 swaps backdrop tile IDs as the player moves so the planes scroll without
a framebuffer span renderer. Doom pistol frames are rendered as a centered
sprite-strip overlay above the bottom 32-pixel `STBAR` status bar and animate
when B is pressed. A first Doom monster sprite is projected from world space
using the same camera math as the wall renderer, with the pistol clearing the
visible target as the initial combat proof of concept. The optional minimap is
drawn on the fix (text) layer, which always composites over sprites.

All arithmetic is 16.16 . Rotation uses constant cos/sin multiplies. The whole
renderer writes only a few control words per column per frame; the expensive
pixel work is offloaded to the scaler hardware.
 
## Controls

| Input               | Action            |
|---------------------|-------------------|
| D-pad Up / Down     | Move forward/back |
| D-pad Left / Right  | Turn              |
| Hold A + Left/Right | Strafe            |
| B                   | Fire pistol       |
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

For visual regression work, run the native Doom comparison capture:

```sh
DOOM_MAP=E1M1 tools/capture_compare.sh
```

The script launches native Doom with the same Freedoom WAD, launches GnGeo with
the current ROM, and writes native/Neo Geo/side-by-side screenshots under
`.tools/screens/`.
 
You must supply your own Neo Geo BIOS — it is copyrighted and not included.

I've only tested this with [gngeo](https://github.com/dciabrin/gngeo) It may not render correctly on real hardware
 
## Acknowledgements

Built against the [ngdevkit](https://github.com/dciabrin/ngdevkit) toolchain.
Hardware details cross-referenced from the
[Neo Geo Development Wiki](https://wiki.neogeodev.org).  
