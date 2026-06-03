# Architecture Notes

DoomGeo-AES uses the original Neo Geo raycaster idea from the video reference:
do not draw pixels at runtime. Instead, prepare graphics in C-ROM format and let
the video chip scale vertical sprite strips.

## Runtime Shape

- The 68000 keeps the player, doors, pickups, monsters, projectiles, HUD state,
  and palette timers in normal RAM.
- The wall renderer casts one fixed-point DDA ray per screen column and updates
  sprite control blocks.
- Background planes, walls, weapon strips, visible things, and HUD each have
  reserved sprite ranges so the project can reason about the 96-sprites-per-
  scanline limit.
- The fix layer is used for minimap, status digits, key/weapon indicators, and
  short gameplay messages because it always draws over sprites.

## Build-Time WAD Conversion

`tools/doom_convert.py` is the map bridge. It reads the WAD on the host machine
and emits generated C headers/sources under `build/`:

- Coarse grid collision/render map.
- Per-cell wall texture class and texture phase.
- Door/exit trigger tables.
- Damage and secret bit grids.
- Runtime thing list with supported Doom thing types.
- Full compact arrays for vertices, linedefs, sidedefs, sectors, segs,
  subsectors, nodes, reject, and blockmap for future higher-fidelity work.

The ROM does not load a WAD at runtime.

## Graphics Conversion

`tools/gen_gfx.py` creates Neo Geo graphics ROM data directly:

- Doom wall textures are precomposed from `TEXTURE1`/`PNAMES`/patches into tile
  strip atlases.
- Doom flats are sampled into tile banks and perspective plane caches.
- Doom status bar, face frames, weapon psprites, pickups, monsters, corpses,
  projectiles, and effects are pre-baked into C-ROM tiles and palettes.
- Weapon/fire frames and sprite scale levels are generated offline so the 68000
  does not compose Doom patches during play.
- The perspective plane cache is intentionally compact. Earlier multi-phase
  plane caches pushed monster tiles past the practical C-ROM tile index range,
  making enemies invisible even though their sprite slots were active.

## Why Not Exact Doom Yet

The Neo Geo has no normal framebuffer and the 68000 cannot read C-ROM texture
pixels. That makes classic Doom's column/span renderer a poor direct fit. The
current runtime accepts several compromises:

- Grid/coarse wall representation instead of arbitrary wall segments.
- One projected wall height per column instead of multiple clipped subsector
  spans.
- Pre-baked floor/ceiling tile views instead of true per-pixel floor casting.
- A limited number of visible world-sprite slots for monsters/pickups/projectiles.
  The current runtime uses 40 wall columns so five 4-strip world things can fit
  alongside the backdrop and weapon under the 96-sprites-per-scanline limit.
- Thing projection samples neighboring wall columns before culling, and slots
  that do not draw any strips are treated as non-visible so hidden/off-screen
  monsters cannot drive melee or ranged damage.
- Monster chase uses a periodically refreshed grid distance field from the
  player, which is cheaper and more reliable on the converted map than asking
  each monster to solve local wall avoidance independently.

The generated full map data is kept so later work can experiment with more
Doom-like traversal without redoing the WAD parsing layer.
