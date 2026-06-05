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
- Compact visual render-line rows derived from solid Doom linedefs plus selected
  two-sided lower, upper, and mid-texture linedefs, stored in generated map
  coordinates and one-span metadata for runtime hit refinement.
- Per-cell render-line index tables, generated from the same raster cells as
  the collision grid. On E1M1 this reduces wall-hit refinement from scanning
  hundreds of render lines per column to checking only local cell candidates.
- Door/exit trigger tables. Exit records include the raw Doom line special plus
  the generated Episode/map destination so normal and secret exits can diverge
  without runtime WAD parsing.
- Damage and secret bit grids.
- Per-cell sector floor visual class and light band, derived from sector flat
  names, specials, and light levels for low-cost runtime palette cues.
- Runtime thing list with supported Doom thing types.
- Runtime thing class/info bytes for monster, threat, pickup, corpse,
  shootable, and render predicates, so the 68000 can test generated metadata
  instead of repeatedly classifying Doom type numbers.
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
- Wall strip tiles are still selected by compact texture phase at runtime, but
  each offline tile now preserves the horizontal source band for that phase
  instead of flattening it to one repeated texel column.
- The graphics converter follows the same `DOOM_DETAIL` tier as the C build:
  clarity mode emits 32-phase wall/door atlases and a four-direction plane
  cache, while quality/balanced/speed keep the 16-phase wall atlases and
  16-direction plane cache. The runtime defaults to the cached perspective plane
  upload path; `DOOM_FLAT_PLANES=1` enables static solid planes for debugging.
- Doom flats are sampled into tile banks and perspective plane caches at build
  time. Normal runtime floor identity remains a palette-level cue over those
  cached planes. The player cell and a few wall-stopped forward view samples choose a
  representative visible sector class, so hazards/liquids can read before
  contact without introducing runtime floor casting. Liquid classes add a slow
  palette pulse over the same floor gradients instead of animating flat pixels.
- The floor/ceiling path intentionally follows the same performance trade seen
  in the Super FX Doom source: spend active runtime budget on wall visibility and
  control response, while floors remain a cheap solid/palette/pre-baked cue
  rather than a per-pixel texture-mapped span renderer.
- The normal menu title backdrop is also WAD-derived: `TITLEPIC` is converted
  offline into a small sprite-tile block and palette, then drawn only while the
  fix-layer menu is active.
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
- Weapon psprites use real WAD patches only. Builds emit a `WEAPON_ASSET_MASK`
  from the selected IWAD and the runtime refuses to grant/select weapons whose
  psprite art is missing. The default shareware build masks missing plasma/BFG;
  explicit Freedoom builds provide the full redistributable weapon-art path.
- The perspective plane cache is intentionally compact. Earlier multi-phase
  plane caches pushed monster tiles past the practical C-ROM tile index range,
  making enemies invisible even though their sprite slots were active.

## Why Not Exact Doom Yet

The Neo Geo has no normal framebuffer and the 68000 cannot read C-ROM texture
pixels. That makes classic Doom's column/span renderer a poor direct fit. The
current runtime accepts several compromises:

- Grid/coarse collision representation with per-cell visual render-line
  refinement instead of a full BSP/seg traversal.
- At most one projected wall or large two-sided partial span per column instead
  of true multiple clipped subsector spans. Small open-cell spans are allowed to
  pass through so openings can show the farther space, but the renderer still
  cannot draw a near lower/upper span and a far wall in the same column.
- Pre-baked floor/ceiling tile views instead of true per-pixel floor casting.
- A limited number of visible world-sprite slots for monsters/pickups/projectiles.
  The default quality runtime uses a 40-column wall pass with seven visible
  world things so walls, backdrop, weapon, and HUD stay inside the practical
  96-sprites-per-scanline limit. `DOOM_DETAIL=clarity` keeps the heavier
  64-column visual comparison path, while `balanced` and `speed` progressively
  spend fewer sprites on walls and more on visible world things.
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
