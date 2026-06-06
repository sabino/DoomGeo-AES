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
  coordinates with span kind, height, texture, and side-ownership metadata for
  runtime hit refinement. A single Doom linedef can emit both lower and upper
  visual spans when both adjacent sector floors and ceilings differ.
- Per-cell render-line index tables, generated from the same raster cells as
  the collision grid. On E1M1 this reduces wall-hit refinement from scanning
  hundreds of render lines per column to checking only local cell candidates.
- Door/exit trigger tables. Exit records include the raw Doom line special plus
  the generated Episode/map destination so normal and secret exits can diverge
  without runtime WAD parsing.
- Damage and secret bit grids.
- Per-cell sector floor visual class, light band, floor height, and ceiling
  height, derived from sector flat names, specials, light levels, and sector
  heights for low-cost runtime palette cues and sprite floor seating.
- Runtime thing list with supported Doom thing types.
- Runtime thing class/info bytes for monster, threat, pickup, corpse,
  shootable, and render predicates, so the 68000 can test generated metadata
  instead of repeatedly classifying Doom type numbers.
- Full compact arrays for vertices, linedefs, sidedefs, sectors, segs,
  subsectors, nodes, reject, and blockmap for future higher-fidelity work.
- BSP vertices and nodes transformed into the raycaster's grid/q8 coordinate
  space, so a future visible-seg renderer can traverse Doom nodes without doing
  WAD-to-grid coordinate conversion on the 68000. `make bsp-asset-check`
  verifies the generated counts, bounds, partition vectors, and child indices
  for the selected map.
- Centered WAD-to-grid bounds. The default conversion is `48x36`, and generated
  starts/things preserve sub-cell WAD positions where possible instead of
  drifting to one anchored corner of the converted grid.
- Scale-aware map simplification. The normal build now halves the older
  `96x72` grid to a `48x36` runtime map and uses
  `DOOM_MAP_DETAIL_CULL=0.5`, opens isolated coarse-grid wall specks, and
  prunes short dead-end wall tails. This reduces the collision, floor, light,
  secret, lift, and damage grids by 75% while keeping strict Episode 1 route
  coverage with the shareware WAD. The lower default cull is deliberate: the
  E1M1 start room depends on short solid linedefs for its columns and framed
  window, and dropping those lines makes the first view collapse into an
  over-open courtyard. Visual WAD-line metadata is generated with the separate
  `DOOM_RENDER_DETAIL_CULL=1.5` default, so the runtime can still draw larger
  Doom room-edge and pillar cues without keeping every minor line as a blocking
  collision cell. `DOOM_MAP_DETAIL_CULL`,
  `DOOM_RENDER_DETAIL_CULL`, and `DOOM_MAP_READABILITY_CLEANUP` can be
  overridden for exact-conversion or higher-resolution experiments.
- Simple/chunk-shaped renderer baseline. `DOOM_SIMPLE_MAP=1` keeps the
  NGRayEx-style active `16x16` map and pure grid DDA columns, while Doom
  sprites, weapons, HUD, and baked floor/ceiling assets still come from the
  normal offline asset path. The active page can be authored or loaded from
  compact WAD-derived chunk data around the player instead of scaling the whole
  WAD map into one large runtime grid.
- `tools/doom_chunk_convert.py` is the first build-time version of that chunk
  pass. It defaults to 64 Doom units per cell, emits `16x16` chunk pages with
  wall, texture-class, floor visual, damage, light, floor/ceiling height, and
  chunk-local thing metadata, and writes an ASCII preview for inspection. The
  runtime streams one generated page at a time into the existing simple-map
  renderer.
- RIPDOOM-lite geometry path. `tools/doom_ripdoom_convert.py` is the next
  converter track inspired by the SNES Doom tooling model: keep the authored
  simple map as the playable baseline, but separately emit compact WAD-native
  geometry tables for `VERTEXES`, `LINEDEFS`, `SIDEDEFS`, `SECTORS`, `SEGS`,
  `SSECTORS`, `NODES`, `REJECT`, `BLOCKMAP`, and `THINGS`. Generated segs carry
  preclassified flags for one-sided/two-sided walls, solid/passable spans,
  doors, lower/upper wall deltas, and mid textures. The converter also emits a
  linedef-to-seg index so runtime code can turn nearby blockmap lines into a
  small seg candidate list without scanning the whole map. This data is
  validated by `make ripdoom-check`. `ripdoom_runtime.c` adds the first
  runtime-facing query helpers for BSP point-to-subsector/sector lookup,
  blockmap cell line counts, local line/seg collection, and nearest local ray
  hits; `make ripdoom-runtime-check` compiles those helpers as a host probe,
  and `make ripdoom-render-check` verifies start-view, moved-view,
  per-column movement delta, route waypoint, and opened chunk-door ray
  coverage. With `DOOM_RIPDOOM_RENDER=1`, the sprite-strip wall renderer can
  use those local ray hits for an experimental WAD-native wall view while
  preserving the existing simple-map collision/gameplay path. Opened generated
  chunk doors and lifts are also skipped by the RIPDOOM ray candidate path, so
  a door or platform cell that becomes passable no longer remains a static
  visual blocker in the sprite-strip renderer. This render mode is not the
  default gameplay path yet.
- Chunk movement validation mirrors the runtime stream contract. The
  `chunk-movement-check` host probe still verifies that the real start pose can
  move forward for the first scripted ticks, and now also follows a generated
  start-to-exit route across 16x16 pages while opening generated door/lift
  state and checking player-radius occupancy at each route cell and edge
  midpoint. This catches cases where the static route checker says a path
  exists but the runtime chunk window, door/lift state, or player body cannot
  actually traverse it.
- Doom-like two-sided opening tests. Small floor deltas stay passable, but
  openings lower than player height or taller than the configured step height
  remain blocking, which keeps high ledges/platform sides from becoming holes.
- Runtime thing placement has a narrow coarse-grid repair pass. When a
  supported thing cell has no cardinal open neighbor, the converter can open a
  single adjacent blocked cell that already touches open floor. This prevents
  isolated thing pockets caused by low-resolution line rasterization while
  preserving the surrounding map shape.

The ROM does not load a WAD at runtime.

Normal builds emit map declarations in `doom_map_generated.h` and the large
tables in `doom_map_generated.c`. Focused tools can still emit inline arrays
when no map source path is requested, but the Makefile path uses the split
source so large arrays are not duplicated across translation units.

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
  the quality default and clarity mode emit 32-phase wall/door atlases for
  close-wall readability, while balanced/speed keep the 16-phase wall atlases.
  Clarity also uses a four-direction plane cache; the other tiers keep the
  16-direction plane cache. The runtime defaults to the cached perspective plane
  upload path; `DOOM_FLAT_PLANES=1` enables static solid planes for debugging.
- Doom flats are sampled into tile banks and perspective plane caches at build
  time. Normal runtime floor identity remains a palette-level cue over those
  cached planes. Runtime column phase is driven from the camera plane rather
  than raw map X/Y, which keeps the cached floor/ceiling bank inside the
  hardware-safe tile range while avoiding runtime floor casting. The player cell
  and a few wall-stopped forward view samples choose a representative visible
  sector class, so nearby hazards/liquids can read before contact. The preview
  distance is capped because the Neo Geo floor is a whole-row palette cue; a
  distant sector should not recolor the entire current room through a coarse
  opening. Liquid classes add a slow palette pulse over the same floor gradients
  instead of animating flat pixels.
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
- The perspective plane cache is intentionally compact. Multi-phase plane
  experiments fit in C-ROM bytes but pushed floor/sprite references past the
  practical Neo Geo tile index range, producing invalid/black strips even though
  the ROM image still built.

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
  Converted lower/upper spans are capped before they reach the runtime, and a
  span must project as a large visible strip before it replaces the farther
  wall column. The ray keeps walking to a solid wall first, which avoids cases
  where a small sector-height cue hides the real room boundary.
- Pre-baked floor/ceiling tile views instead of true per-pixel floor casting.
- A limited number of visible world-sprite slots for monsters/pickups/projectiles.
  The default quality runtime uses a 40-column wall pass with seven visible
  world things so walls, backdrop, weapon, and HUD stay inside the practical
  96-sprites-per-scanline limit. `DOOM_DETAIL=balanced` and `DOOM_DETAIL=speed`
  spend fewer sprites on walls and more on visible world things, while
  `DOOM_DETAIL=clarity` is the heavier wall-readability comparison tier.
- Runtime wall projection still uses one sprite strip per wall column. In the
  balanced/speed tiers, solid grid-cell hits use the rasterized map wall
  directly and skip solid-line refinement; open-cell WAD render-line spans stay
  enabled for windows, lower walls, upper walls, and doors. The quality/clarity
  tiers keep solid-line refinement for closer native-Doom still comparisons.
  Refined solid-line hits also carry the front sector height, so low rooms such
  as the E1M1 start do not render every wall column as a full 128-unit slab.
- The converter now emits grid/q8 BSP node and vertex arrays beside the raw WAD
  geometry, but the active renderer has not yet switched to front-to-back
  visible-seg ownership. That is the next step toward replacing first-grid-wall
  ray ownership with Doom-like seg ownership while still filling the existing
  Neo Geo sprite-strip buffers.
- Wall strip geometry updates every dirty frame, but texture/palette SCB1
  rewrites are budgeted by `WALL_TILE_UPLOAD_COLUMNS_PER_FRAME`. This keeps
  controller response and wall height motion from stalling behind a full
  15-tile rewrite of every wall column while turning. The quality default now
  accepts a bounded texture-settle delay in exchange for 40 wall columns and
  32-phase wall atlases; the overrun path clamps the budget back down when
  `wait_vblank_status()` reports late frames.
- Wall depth shading uses 11 bands per side. With the primary wall, door, and
  seven alternate wall texture palettes, this keeps every wall-depth palette
  below Neo Geo palette index 256. Higher band counts overflow palette RAM and
  show up in GnGeo as `Invalid write` entries.
- Balanced mode also adapts WAD line refinement by motion state. Standing frames
  keep the normal portal-span pass and near solid-line correction, while moving
  frames skip portal-span refinement and use a tighter near-hit radius. Late-frame
  recovery keeps the same reduced span work for one frame. This keeps the runtime
  in the sprite-strip model while spending less 68000 time on line intersection
  scans exactly when input response matters most.
- The cached floor/ceiling backdrop has a separate `BG_SCROLL_COLUMNS_PER_FRAME`
  budget. Balanced defaults update 10 of 20 backdrop columns per normal frame
  and four after a late frame, keeping plane catch-up short without turning the
  planes into a runtime span renderer.
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
