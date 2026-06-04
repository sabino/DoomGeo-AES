# Architecture Notes

DoomGeo-AES uses the original Neo Geo raycaster idea from the video reference:
do not draw pixels at runtime. Instead, prepare graphics in C-ROM format and let
the video chip scale vertical sprite strips.

## Runtime Shape

- The 68000 keeps the player, doors, pickups, monsters, projectiles, HUD state,
  and palette timers in normal RAM.
- The wall renderer casts one fixed-point DDA ray per screen column, refines
  visual hits against compact WAD-derived render lines indexed by the hit cell,
  and updates sprite control blocks.
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
- Compact visual render-line rows derived from solid Doom linedefs, stored in
  generated map coordinates for runtime hit refinement.
- Per-cell render-line index tables, generated from the same raster cells as
  the collision grid. On E1M1 this reduces wall-hit refinement from scanning
  325 render lines per column to checking at most 7 lines in a hit cell, with
  about 1.7 lines on an average referenced cell.
- Door/exit trigger tables.
- Damage and secret bit grids.
- Runtime thing list with supported Doom thing types.
- Full compact arrays for vertices, linedefs, sidedefs, sectors, segs,
  subsectors, nodes, reject, and blockmap for future higher-fidelity work.

The ROM does not load a WAD at runtime.

The keycard verification ROM is built through recursive Make targets with
`DOOM_MAP=E1M2`, `BUILDDIR=build/key-test`, and `ROM=build/key-test-rom`,
keeping its generated map and object files separate from the default E1M1 build.
The target also copies the local `neogeo.zip` BIOS package into the isolated
ROM directory so `make key-test-gngeo` can boot that ROM directly.

## Graphics Conversion

`tools/gen_gfx.py` creates Neo Geo graphics ROM data directly:

- Doom wall textures are precomposed from `TEXTURE1`/`PNAMES`/patches into tile
  strip atlases.
- Doom flats are sampled into tile banks and perspective plane caches.
- Doom status bar, face frames, weapon psprites, pickups, monsters, corpses,
  projectiles, and effects are pre-baked into C-ROM tiles and palettes.
- HUD keycards are baked from the WAD keycard patches into their own compact
  tile/palette set instead of reusing pickup or enemy palettes at runtime.
- Weapon/fire frames and sprite scale levels are generated offline so the 68000
  does not compose Doom patches during play.
- Monster sprite definitions include a compact Doom angle bucket. The runtime
  stores one coarse facing vector per thing and picks from the baked rotation
  frames while keeping the same limited world-sprite slot count.
- Attack and pain frames use state-specific angle bucket ranges when the source
  WAD provides rotated reaction art. Runtime timers prefer those buckets, then
  fall back to the older front-facing attack/pain buckets and finally to walk
  frames without allocating extra world-sprite slots.
- Registered/commercial-only psprite lumps that are missing from shareware are
  replaced with synthetic fallback frames at build time, so the same runtime
  weapon table can be tested with visibly distinct shareware plasma/BFG weapons
  and then rebuilt with exact registered/commercial WAD art.
- The perspective plane cache is intentionally compact. Earlier multi-phase
  plane caches pushed monster tiles past the practical C-ROM tile index range,
  making enemies invisible even though their sprite slots were active.

## Why Not Exact Doom Yet

The Neo Geo has no normal framebuffer and the 68000 cannot read C-ROM texture
pixels. That makes classic Doom's column/span renderer a poor direct fit. The
current runtime accepts several compromises:

- Grid/coarse collision representation with per-cell visual render-line
  refinement instead of a full BSP/seg traversal.
- One projected wall height per column instead of multiple clipped subsector
  spans.
- Pre-baked floor/ceiling tile views instead of true per-pixel floor casting.
- A limited number of visible world-sprite slots for monsters/pickups/projectiles.
  The current runtime uses a 40-column wall pass with seven 4-strip world things
  so walls, backdrop, weapon, and HUD stay inside the practical
  96-sprites-per-scanline limit.
- Thing projection first samples neighboring wall columns before culling, then
  falls back to a q8 player/view-vector projection when map line-of-sight says
  the thing should be visible. Slots that do not draw any strips are treated as
  non-visible. Combat applies a stricter readable-slot gate so hidden,
  off-screen, or edge-only monsters cannot drive melee or ranged damage.
- Monster chase uses a periodically refreshed grid distance field from the
  player, which is cheaper and more reliable on the converted map than asking
  each monster to solve local wall avoidance independently. That same movement
  delta feeds the coarse facing vector for rotation-frame selection.

The generated full map data is kept so later work can experiment with more
Doom-like traversal without redoing the WAD parsing layer.
