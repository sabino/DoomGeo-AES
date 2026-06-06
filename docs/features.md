# Feature Inventory

This document describes what the current `doom-neogeo-port` branch actually
contains. It is intentionally separate from the README so the landing page stays
readable.

## Converted Doom Data

- Reads Doom-format WAD map lumps at build time: `THINGS`, `LINEDEFS`,
  `SIDEDEFS`, `VERTEXES`, `SECTORS`, `SEGS`, `SSECTORS`, `NODES`, `REJECT`, and
  `BLOCKMAP`.
- Emits a compact runtime grid plus texture class, texture phase, damaging
  sector, secret sector, floor/ceiling height, door, exit, and thing data.
- Keeps richer generated map arrays for future work, but the runtime path uses
  Neo Geo-friendly fixed-size structures instead of a generic WAD directory.
- Defaults to E1M1 and supports changing `DOOM_MAP`, `DOOM_MAP_WIDTH`,
  `DOOM_MAP_HEIGHT`, `DOOM_MAP_DETAIL_CULL`, `DOOM_RENDER_DETAIL_CULL`,
  `DOOM_MAP_READABILITY_CLEANUP`, and `DOOM_SKILL_MASK` at build time. The
  default skill mask is `4`, matching hard/Ultra-Violence THING placement; use
  `1` for easy or `2` for medium placement. The default grid remains centered
  but is now `48x36`, deliberately cutting the runtime map area to one quarter
  of the earlier `96x72` conversion. The default map pass uses
  `DOOM_MAP_DETAIL_CULL=0.5`: low enough to keep short architectural lines such
  as E1M1's start-room columns and window frame, but still scale-aware so very
  tiny solid-line noise can be removed before it becomes false full-height
  obstacles in the sprite-strip raycaster. Generated visual lines use the separate
  `DOOM_RENDER_DETAIL_CULL=1.5` default, preserving larger room-edge and pillar
  cues without requiring those lines to stay as blocking collision cells. The
  defaults were chosen from strict Episode 1 route checks and E1M1 converter
  metrics; higher-resolution builds can still override the map size for
  comparison work.
- `DOOM_SIMPLE_MAP=1` switches to the simplified NGRayEx-style runtime shape:
  an active `16x16` map, 80 wall columns, full-height grid DDA walls, no
  generated WAD render-line refinement, and full-screen baked floor/ceiling
  plane tiles. The active page can be authored or loaded from WAD-derived
  `16x16` chunks while HUD/assets/thing metadata continue to come from the
  offline conversion path.
- `make chunk-map` runs `tools/doom_chunk_convert.py`, which converts the WAD
  map at a fixed cell scale and emits generated `16x16` chunk pages plus
  `doom_chunks_preview.txt`. The default chunk scale is 64 Doom units per cell:
  E1M1 becomes a 5x3 set of pages, but the Neo Geo runtime still loads only one
  `16x16` page into the original NGRayEx-style 80-column renderer. `make
  chunk-route-check` verifies the generated chunk start-to-exit route, treating
  generated doors and lift cells as interactive pass-through cells, and also
  runs a key-aware route pass so locked door cells require a reachable matching
  key first. `make chunk-movement-check` mirrors the runtime player-radius
  collision and chunk-stream update enough to prove the generated start can
  walk forward instead of spawning wedged against a wall. `make
  chunk-visibility-check` verifies generated monster/pickup/weapon coverage
  and the generated maximum active 3x3 chunk-window thing count. Chunked runtime
  actor arrays use that active-window cap instead of the full converted map's
  thing count, so E1M1 keeps persistent state for all things but scans only the
  currently streamable actor set.
- Chunk start placement still prefers an open centered cell in the player's
  source sector, but now also scores exact forward visual clearance against WAD
  linedefs. This keeps the E1M1 playable start from being pushed into a coarse
  cell that looks open in the `16x16` grid while the RIPDOOM wall renderer is
  actually too close to the front wall.
- The RIPDOOM wall renderer now maps chunk/grid movement directly back to WAD
  coordinates with the expected Y inversion. `ripdoom-render-check` compiles
  with the same chunk/RIPDOOM flags as the ROM and samples both the generated
  chunk start and a short forward pose for E1M1, so movement cannot silently
  rotate the real-Doom geometry out from under the simple grid collision.
  In chunked builds it also simulates accepted forward movement through the
  `16x16` collision/streaming rules and verifies the moved RIPDOOM pose still
  renders after the same 70-tick movement span used by `chunk-movement-check`,
  then compares per-column wall distance/seg samples so the harness catches
  regressions where the weapon bobs but the rendered wall state appears fixed
  in place.
- RIPDOOM ray casting keeps the fast local blockmap search first, then falls
  back to sampling blockmap cells along a missed ray. This fills long corridor
  or chunk-edge columns without raising the local line/seg caps for every
  column.
- Chunk conversion exposes `DOOM_CHUNK_START_LOCAL_X`,
  `DOOM_CHUNK_START_LOCAL_Y`, and `DOOM_CHUNK_KEEP_WAD_START_OFFSET` through
  the Makefile. The playable E1M1 chunk ROM keeps the WAD start aligned to the
  center of its active `16x16` room page, which gives more visual clearance from
  the start wall without changing the sprite-strip/RIPDOOM-lite renderer.
- `make chunk-playable-rom` builds the current manual E1M1 chunk/RIPDOOM-lite
  ROM with `16x16` chunks, 32 Doom units per cell, skipped intro, and normal
  player input. `make chunk-movement-test-rom` uses the same scale but replaces
  input with a scripted forward walk, so it is only for regression testing.
- `make chunk-playable-debug-rom` uses the same playable build but enables
  frame and input debug overlays. The top-left rows show raw input, local
  chunk X/Y, active chunk, global chunk-grid X/Y, and the last actual movement
  flag, which distinguishes stale ROMs or blocked movement from valid chunk
  streaming where local coordinates recenter.
- `make ripdoom-map ripdoom-check ripdoom-runtime-check ripdoom-render-check` runs the RIPDOOM-lite geometry converter.
  This path preserves Doom's real BSP/seg/subsector/blockmap data in compact C
  tables with SNES-style hard caps, semantic wall-span flags, and a generated
  linedef-to-seg index for local renderer candidate collection. It also
  validates the C query layer for point-to-sector, blockmap lookup, local
  line/seg collection, nearest local ray hits, and E1M1 start-view ray coverage.
  `DOOM_RIPDOOM_RENDER=1` enables an experimental sprite-strip wall renderer
  backed by these local WAD-native ray hits while leaving the existing
  collision/gameplay path intact.
- In simple-map play, pickups are given a foreground/readability bias and a
  larger minimum projected size so ammo, armor, health, and weapons remain
  visible among monsters and barrels.
- Generated map tables are split into `doom_map_generated.h` and
  `doom_map_generated.c` for Makefile builds so large arrays are compiled once
  instead of duplicated by every file that includes the generated header.
- The generated header exposes the current map code and Episode 1 next-map
  metadata. Exit records also carry a compact destination derived from the Doom
  line special, so E1M3's secret exit can report E1M9 while the normal exit
  reports E1M4. The intro menu shows the compiled map instead of a hard-coded
  E1M1 label, and the completion overlay can show the reached exit's next
  standalone map code.
- Pickup/key runtime positions preserve the original WAD fractional placement
  when possible. If the coarse wall grid makes the exact WAD point solid, the
  converter moves the pickup into the nearest open cell but clamps it toward
  the original point instead of snapping it to the cell center. This keeps E1M2
  key/weapon placements closer to native Doom while staying collectible.
- Supported runtime things can also reopen their own coarse cell when the cell
  was only closed by rasterization overlap. This preserves original player and
  thing placement without punching general-purpose holes through high ledges.
- If a supported runtime thing lands in a sealed coarse-grid pocket, the
  converter opens one adjacent escape cell that already borders open floor.
  This keeps monsters/pickups from being stranded by the lower-resolution map
  without broadly erasing nearby walls.
- Two-sided lines are passable only when their vertical opening fits Doom's
  player height and their floor delta is within the configured step height.
  Small stairs remain walkable; tall platforms and ledges stay blocking.

## Rendering

- The default `DOOM_DETAIL=quality` renderer uses 40 wall-column sprites over
  the 320-pixel playfield, giving 8-pixel logical columns backed by 16-pixel
  Neo Geo strips. This is the normal readable-navigation mode; `balanced` and
  `speed` remain lower-cost stress tiers, while the heavier 64-column
  `clarity` tier remains available for visual comparison.
- Each frame casts fixed-point DDA rays, computes projected wall height, refines
  visual hits against compact WAD-derived render lines indexed by the hit cell,
  and writes Neo Geo sprite shrink/position data.
- Wall projection uses a taller Doom-biased scale than the raw playfield height,
  so converted rooms spend less of the view on floor/ceiling backdrop and more
  on readable wall structure.
- The render-line broadphase is done offline by the converter. The E1M1 build
  currently emits 456 visual render lines and 1310 cell references; the runtime
  checks only the compact line candidates indexed by the traversed/hit cell
  instead of scanning the whole render-line table for every wall column.
- In balanced/speed tiers, solid grid-cell hits skip the extra solid-line
  refinement pass. The default quality tier keeps solid-line refinement for
  better close-wall phase/orientation readability. Open-cell portal/lower/upper
  span hits still run, so visible sector transitions keep their Doom-like cues
  without paying the full per-column line-intersection cost on every solid wall
  in the stress tiers.
- In addition to solid linedefs, the converter now emits selected two-sided
  lower, upper, and mid-texture visual lines. Lower/upper spans are side-owned
  from the WAD sidedef that should see them, so a stair lip or upper window cap
  is not considered from the opposite sector. A single Doom linedef can now emit
  both lower and upper visual spans when adjacent floor and ceiling heights both
  differ. The runtime can draw one top- or bottom-aligned partial wall span per
  column when that span projects large enough to be readable. Open-cell spans
  are collected while the ray keeps walking to the farther solid wall, then the
  span replaces the wall column only when its projected height is large enough
  to carry the view. This keeps window/opening views from collapsing into dark
  horizontal fences while still showing nearby ledge/step cues. Converted
  lower/upper spans are capped so large Doom sector-height deltas do not become
  fake full-height walls in the one-span-per-column approximation.
- Generated lower/upper span metadata uses WAD texture fallbacks when a sidedef
  omits the explicit upper/lower texture, so sector-height transitions still
  get a visible cue in the sprite-strip renderer.
- The renderer caches each column's ray vector, DDA reciprocal deltas, and step
  signs for the current angle/FOV, rebuilding that cache only when the view
  direction changes. Movement-only frames reuse those values before running
  DDA, avoiding two fixed-point multiplies and two reciprocal lookups per wall
  column.
- The default renderer spends more active playfield sprites on walls: 20
  backdrop strips, 40 wall columns, seven 4-strip world things, and seven weapon
  strips fit inside the first 95 active sprites. Alternate build tiers are
  available for different tradeoffs: `DOOM_DETAIL=clarity` uses 64 wall columns
  and one visible world thing, `balanced` uses 32 columns and nine things, and
  `speed` uses 20 columns and eleven things.
- Wall textures are precomposed offline from Doom wall patches into Neo Geo
  tile strips. In the quality default and clarity mode, the wall,
  alternate-wall, and door atlases use 32 texture-phase columns for closer-range
  readability; the lower-cost balanced/speed tiers keep the older 16-column
  atlases. The current preferred wall texture is
  `STARTAN3`, with alternate atlases for common E1M1 walls and `BIGDOOR2`
  doors.
- Each baked wall strip samples the narrow source texture band represented by
  that phase instead of repeating a single source texel across the 16-pixel
  Neo Geo tile. The runtime still draws one sprite strip per wall column, but
  close doors and panels retain more horizontal texture detail after hardware
  shrink.
- Floor and ceiling default to compact pre-baked perspective tile caches selected
  by player direction and camera-lateral coarse position. The runtime wraps
  columns incrementally so strafing moves the planes while keeping the tile bank
  inside the hardware-safe range. `DOOM_FLAT_PLANES=1` switches back to static
  solid planes for debugging.
- Sector floor/liquid preview is deliberately local. The current floor is a
  whole-row Neo Geo backdrop palette, so distant hazards no longer recolor the
  whole room through coarse-grid openings before the player reaches them.
- Wall projection is intentionally a little taller than the original NGRayEx
  baseline. `DOOM_WALL_PROJECTION_NUM` / `DOOM_WALL_PROJECTION_DEN` tune the
  projection constant at compile time, keeping more of the view occupied by
  walls and less by the approximate floor/ceiling backdrop without changing the
  sprite-strip architecture.
- The converter also emits a compact per-cell sector floor visual class and
  light band derived from `SECTORS` floor flat names, specials, and light
  levels. The runtime uses those generated cells to tint floor/ceiling palettes
  when the player enters water-like, damaging nukage/slime/lava, blood, or
  darker/brighter sectors. This keeps sector identity visible without runtime
  WAD parsing or extra floor-casting work.
- The floor palette selector samples a few wall-stopped view rays plus a small
  forward cone ahead of the player, then lets visible higher-priority sector
  classes bias the active flat-plane tint. Preview sectors tint the far floor
  gradient rows first, keeping the near floor neutral while making nukage,
  slime, lava, blood, and water readable before contact, including E1M1's
  start-window hazard view. Cone candidates must have a coarse visible path
  from the player before they can bias the palette; the scan is palette-only
  and does not change collision or ray hits.
- Sector floor/ceiling palette preview sampling is cached by coarse player
  position and view vector. Straight movement inside the same coarse pose
  bucket skips the forward ray/cone preview work, while liquid pulse sectors
  still advance their low-cost palette phase.
- Water, blood, and hazardous liquid classes also apply a slow four-phase
  palette pulse to the already-baked floor gradients. This is a low-cost
  substitute for Doom's animated flats that keeps liquid sectors visibly active
  without runtime pixel drawing or extra floor sprites.
- Floor flats keep the WAD texture pattern but normalize green-dominant palette
  entries toward warm gray/brown before emitting Neo Geo tiles. This keeps E1M1
  closer to Doom's sober floor tone and avoids stray green speckles being read
  as a missing sprite or palette glitch.
- Depth palettes and directional shading give walls/planes distance cues without
  runtime pixel drawing. The wall depth budget is capped to fit the primary
  wall, door, and seven alternate wall palette ranges inside the Neo Geo's
  256 palette slots; GnGeo movement benches now reject invalid palette writes.
- Point-blank wall fallback is limited to the primary wall atlas. Alternate
  walls and doors keep their own baked columns at close range, avoiding the
  wrong-palette block artifacts that could look like red/green missing sprites.

## HUD And UI

- Doom `STBAR` is baked into the HUD sprite strip area.
- Normal ROMs now boot to a small fix-layer menu state flow instead of the old
  one-line intro prompt. It has Start, skill/build info, compiled-map/next-map
  info, and options placeholder pages. Focused verification ROMs still define
  `DOOM_SKIP_INTRO` and boot directly into their scenario.
- The normal intro/menu backdrop uses the WAD `TITLEPIC` converted offline into
  Neo Geo sprite tiles and a generated palette. Those sprites are menu-only and
  are hidden before the game initializes the playfield renderer.
- Doom status face frames are baked into a dedicated face bank and switch by
  health, turn direction, pain, evil grin, and death state.
- Large red Doom-style `STTNUM` digits are rendered on the fix layer for ammo,
  health, frags/kills, and armor.
- The large status numbers are positioned over the AMMO, HEALTH, FRAG, and
  ARMOR fields with the Doom red-number palette.
- The right-side ammo reserve counters use the WAD `STYSNUM` small digits with
  a second dark sprite pass offset behind the yellow foreground, matching the
  original status-bar shadow treatment more closely while staying in the sprite
  HUD path.
- Weapon indicators now use a Doom-style two-row ARMS layout for pistol through
  BFG weapon numbers 2-7 instead of the old continuous debug strip. The ARMS
  fix-layer rows are kept inside the metal status bar, away from the weapon
  and enemy playfield sprites. Key indicators and short messages (`KEY`,
  `AMMO`, `MED`, `ARM`, `DOR`, `SEC`, `EXIT`, `DEAD`) use fix-layer glyphs.
- Locked-door and key pickup messages draw a compact `KEY` label followed by
  the required or collected key color glyph, so the E1M2 red keycard path can be
  verified visually instead of only through generated data.
- Owned HUD keys use the baked Doom keycard patches from the WAD, shrunken into
  the status-bar key cells with a shared WAD-derived blue/red/yellow HUD key
  palette instead of coarse placeholder glyphs or borrowed world-sprite
  palettes. Transparent patch pixels stay transparent in the generated Neo Geo
  tiles, so the cells do not fill with a solid placeholder color.
- The crosshair marker has been cleared from the current runtime path.

## Weapons And Combat

- Implemented runtime weapons: fist, pistol, shotgun, chaingun, rocket
  launcher, plasma rifle, BFG, and chainsaw.
- Weapon psprites are gated by generated WAD asset coverage. The default
  shareware build masks missing plasma/BFG psprites instead of drawing
  placeholder guns; explicit Freedoom builds exercise the full redistributable
  plasma/BFG path.
- Pistol/chaingun use compact hitscan-style targeting.
- Plasma rifle spends cells rapidly and launches the baked small fireball strip
  as a visible forward projectile. A direct projectile hit applies compact
  single-target damage with an impact burst; if the one-projectile runtime slot
  is already busy, it falls back to the older visible-target damage path.
- Fist/chainsaw use a close-range rendered-slot melee check matched to the
  runtime monster distance scale, so they can hit visible nearby targets without
  reaching through walls or off-screen things.
- Shotgun uses readable rendered slots for spread damage and can hit multiple
  visible targets. Its pump/reload psprite frames are centered in the offline
  Neo Geo weapon canvas so the WAD offsets do not push the hand/gun art into a
  left-edge-only strip during the fire cycle.
- Rocket launcher tracks rockets separately and applies compact splash damage.
- BFG spends 40 cells, launches a visible forward projectile using the current
  baked fireball sprite path, then detonates when it hits a wall, crosses a
  shootable thing, or times out. The detonation applies heavy damage to readable
  rendered targets plus lighter bounded tracer damage against line-of-sight
  monsters in the player's forward cone.
- Hitscan and melee targeting use the same readable-slot test as monster
  attacks, so edge-clipped or metadata-only sprite projections are not treated
  as valid direct combat targets.
- Empty weapon fire shows an ammo message and can auto-select a weapon with
  usable ammo.
- The C-button weapon cycle now prefers weapons that are both owned and ready
  to fire, then falls back to owned empty/melee weapons only if needed.
- C+D-pad provides fast weapon shortcuts without opening the minimap: Up
  selects shotgun, Right selects chaingun, Down selects rocket launcher,
  Down+Right selects plasma rifle, Down+Left selects BFG, and Left cycles
  close/basic weapons (chainsaw, fist, pistol). If a direct heavy shortcut is
  unavailable, it falls back through the heavy weapon group. Movement directions
  are masked during these combos so weapon selection does not shove the player
  around. Shortcuts work both when the direction is already held before C and
  when C is held first before pressing a direction.
- Weapon pickups and manual weapon changes briefly flash the selected Doom slot
  number in the center fix-layer message area, giving immediate feedback for C
  cycling and C+D-pad shortcuts without opening the minimap.
- Muzzle, hurt, and impact feedback use palette flashes and projected explosion
  sprites. Weapon muzzle flash now uses a restrained warm additive palette so
  shotgun and pistol frames brighten without flattening into a solid yellow
  silhouette. Hurt feedback tints the active Doom playfield and weapon palettes
  instead of flashing the raw Neo Geo backdrop, avoiding full-height red edge
  leaks if sprite coverage drops out during busy scenes. Monster fireballs now
  spawn the same impact burst when they hit a wall or the player instead of
  disappearing silently. Projectile movement is evaluated after the current
  frame's world-sprite selection, so monster fireballs cannot keep advancing
  from stale source visibility after the source leaves the rendered slot set.
- The rocket launcher now launches a visible forward projectile using the
  current baked fireball strip placeholder. It detonates into the existing
  compact splash-damage path when it hits a wall, crosses a shootable thing, or
  times out; if the one-projectile runtime slot is already busy, it falls back
  to the older instant splash path.

## Things, Pickups, And Enemies

- Runtime things include common E1M1 pickups, keys, bullet/shell/rocket/cell
  ammo, armor, health, backpack, standard Doom powerups, weapons, barrels,
  monsters, projectiles, corpses, and explosions.
- Converted pickups keep sub-cell placement after coarse-grid correction, so
  keys and weapons no longer drift as far from their original WAD locations when
  a nearby wall line occupies the raw grid cell.
- Visible world sprites are seated against the generated floor height for their
  current cell instead of always assuming the player's floor. Pickups, monsters,
  corpses, drops, projectiles, and impacts therefore align better in sectors
  with raised/lowered floors.
- Pickup sprites receive a small runtime lift so floor items remain visible in
  the wall-heavy sprite-strip view. The focused powerup smoke uses robust
  visible pickup sprites for the screenshot oracle; special powerup sprites are
  still a known readability gap.
- Visible thing selection uses one priority-ranked projection pass for
  monsters, barrels/explosions, collectible pickups, corpses, and spent pickups.
  This preserves the previous Doom-like visibility priority while avoiding the
  older five full scans of `NG_RUNTIME_THING_COUNT` every frame.
- Empty world-sprite slots remember that they are already hidden, so normal
  gameplay no longer rewrites every unused enemy/pickup sprite control block on
  every frame. The logical slot state is still cleared when a candidate fails to
  render, but repeated hidden slots stop spending VRAM writes.
- The converter emits compact runtime class/info bytes for every supported
  thing, so monster, threat, pickup, corpse, shootable, and render eligibility
  tests can use generated metadata instead of repeated type-switch scans.
- Before projection and line-of-sight checks, thing selection now applies a
  conservative player-facing prefilter that rejects behind-camera and extremely
  distant candidates. This keeps normal E1M1 visible things intact while
  avoiding expensive projection work for objects that cannot contribute to the
  configured visible world-sprite slots.
- Ranged-attack warmup now tracks only the previous and current readable
  world-sprite slots. That replaces another per-frame scan across all converted
  runtime things with a bounded pass over the configured visible slots plus the
  previous-slot list.
- Combat damage and ranged attack paths reuse resolved runtime thing types
  after cheap coordinate, cooldown, and readability checks, reducing repeated
  per-frame classification work during active fights without changing damage,
  drops, or score behavior.
- Monster damage reads the player position once per frame, reuses the current
  visible-slot projection for close melee checks, and uses that same cached
  player point for ranged line-of-sight checks instead of re-projecting visible
  monsters during the damage pass.
- Sound-alert and monster-AI passes reject dead, already-awake, flashing, and
  unreadable-slot entries before distance math or type classification, keeping
  periodic E1M1 monster work focused on things that can actually wake or move.
- Close monster melee now uses the same bounded readable-slot list instead of
  scanning every converted runtime thing. It still requires close world
  distance, projection, screen position, and line-of-sight before applying
  damage.
- Player projectile hit tests and monster movement occupancy now use coarse
  map-cell rejection before exact q8 distance and type checks. This preserves
  the exact hit/separation thresholds while avoiding most all-thing work for
  objects outside the local collision area. Player projectile hit range and
  coarse-cell span are derived once at spawn time and reused for every active
  projectile frame, and projectile/monster-spacing candidates cache their q8
  coordinates for both coarse-cell and exact-range checks. Monster movement
  occupancy reuses the runtime shootable predicate after the coarse cell reject
  instead of resolving a full type and rechecking monster/barrel classes for
  each nearby candidate.
- Projectile updates split player-owned target scans from monster-owned player
  collision checks, avoiding the opposite ownership path each active projectile
  frame. Monster projectile spawn also reuses the player point already loaded
  by the ranged-damage pass.
- Nearby pickup collection uses the same local map-cell prefilter before
  resolving thing type or exact q8 pickup distance. This keeps key, item,
  weapon, and dynamic-drop pickup behavior unchanged while avoiding most
  per-frame all-thing pickup checks. Static pickup and dynamic-drop scans cache
  each candidate q8 coordinate once for both the coarse-cell and exact-radius
  tests.
- Per-frame thing timer maintenance is bounded to the shootable candidate list
  and then skips entries with no active flash, attack, explosion,
  death-animation, or delayed-drop timers before touching detailed state
  transitions.
- Weapon explosion, BFG cone, barrel splash, and sound-alert loops now reject
  dead entries and test cheap distance, readability, path, or forward-cone
  bounds before resolving runtime thing types or doing line-of-sight work. The
  final damage and wake-up thresholds are unchanged.
- Line-of-sight checks compare converted map cells before doing absolute
  distance setup, then reuse cached absolute dx/dy values for the sampled step
  count. Same-cell checks still avoid sampled wall-step work for close combat
  and projection fallback cases that cannot cross a wall cell.
- Monster AI applies its active-range gate before resolving thing type, so
  distant converted things do not spend CPU on monster classification during
  movement ticks.
- Monster pathfinding caches the player map cell and avoids rebuilding the
  full BFS distance field while the player remains in that cell. Ordinary
  player-cell changes reuse the previous field for a few monster-AI ticks before
  rebuilding, which avoids clearing/filling the 76x54 E1M1 path grid on every
  step. Door openings and level restart still invalidate the cache so newly
  opened E1M1 routes are picked up immediately.
- Visible thing selection rejects behind-camera and far-away things before
  resolving runtime type or render bucket, reducing per-frame sprite candidate
  work while preserving the same final projection and priority rules. The
  precheck short-circuits behind-camera and range rejects before doing the
  side-of-camera multiply used for far-offscreen rejection.
- A conservative side-of-camera band now rejects far-offscreen world sprites
  before runtime type lookup and exact projection. The exact projector and
  screen bounds still decide everything near the view, so visible E1M1
  monsters, drops, and pickups keep their normal priority path.
- Sprite fallback projection reuses the player/view state already loaded for the
  candidate pass, and fallback line-of-sight checks use that same cached player
  point instead of querying the raycaster again per candidate.
- Rendered monster angle-frame selection also reuses the candidate pass player
  point, avoiding another raycaster position query for each visible monster.
  Slot rendering caches the resolved type plus monster/shootable flags, so
  bounded visible-slot targeting, melee, ranged-readiness, and ranged-damage
  passes do not re-resolve the same type before the next render refresh.
- Readable, attackable, and ranged-attackable world-sprite slot flags are
  cached when each slot is rendered. Targeting, melee, monster ranged attacks,
  projectile ownership, and AI visibility checks reuse those flags instead of
  recomputing screen clipping and center bounds several times per frame.
- The world-sprite selector no longer clears its candidate buffer or visible
  slot ids before immediately overwriting or hiding them. This removes redundant
  per-frame writes from the normal E1M1 render path while preserving the same
  slot cleanup at the end of the pass.
- HUD status numbers, key slots, and ammo reserve counters now update only
  when their displayed values change. The animated face still updates every
  frame, but quiet E1M1 traversal no longer rewrites the static HUD sprites and
  small counter digits every vblank. The arms/weapon status strip also reuses
  the status bits already computed for the change check when it redraws.
- `tools/smoke_gameplay.sh` chains the verified enemy visibility, key-door,
  weapon shortcut, death/drop, and powerup screenshot passes for a broad local
  playable-feature regression check.
- `make route-check` statically verifies the generated E1M1 start-to-exit
  route against `build/doom_map_generated.h`, including whether completion
  depends on generated door cells.
- `make chunk-route-check` statically verifies the generated `16x16` chunk map
  route against `build/doom_chunks_generated.h` and
  `build/doom_chunks_generated.c`. It now checks both the all-interactive route
  and a key-aware route where keycard/skull things must be reachable before
  matching locked door specials. A stricter state route keeps generated lift
  cells closed until a reachable lift trigger opens them, so converted
  elevators/platforms cannot silently become always-open route shortcuts.
  Accepted routes are also sampled at cell centers and cell-boundary midpoints
  with the same q8 player radius used by the runtime collision checks. The
  accepted state route is also replayed through the shared `16x16` chunk
  streaming convention, so start-to-exit validation catches page-wrap mistakes
  before they become runtime movement traps. If the
  exact WAD player start falls on a
  coarse-grid wall, chunk conversion moves it to the nearest open cell and
  opens the minimum number of coarse wall cells needed to preserve a playable
  start-to-exit route.
- `make chunk-stream-check` compiles a host probe against the generated chunk
  header and the same inline streaming helper used by the 68000 runtime. It
  verifies that local player coordinates wrap by full `16x16` q8 pages when the
  active chunk changes, guarding against tiny-shift chunk drift.
- `make chunk-movement-check` compiles a host probe against the generated chunk
  header/source and runs the same start pose, player-radius solid checks, and
  chunk-stream wrapping convention used by the runtime. It fails if the player
  cannot occupy the generated start or cannot make useful forward progress.
- `make episode-route-report` converts shareware `E1M1` through `E1M9` into
  isolated generated headers and reports whether each map has a coarse-grid
  start-to-exit route. `make episode-route-check` runs the same pass in strict
  mode. The default grid now routes E1M1-E1M7 and E1M9, while E1M8 reports the
  supported boss-death exit because it has no linedef exit.
- `make episode-map-rom EPISODE_MAP=E1M3` and `make episode-map-gngeo
  EPISODE_MAP=E1M3` build or launch a standalone ROM for a specific Episode 1
  map under `build/episode-roms/`. `make episode-roms` loops through E1M1-E1M9
  and builds one standalone ROM output per map.
- The default ROM starts on shareware `E1M1`; `make key-test-rom` and
  `make key-test-gngeo` build shareware `E1M2` into an isolated output tree so
  the real red keycard and red locked-door path can be verified without
  changing the default map.
- `make key-door-test-rom` and `make key-door-test-gngeo` build a focused E1M2
  key/door verification ROM. It starts beside the real red locked-door group,
  faces the door, hides unrelated things, and places a real WAD red keycard in
  front of the player so pickup and locked-door behavior can be tested quickly.
- `tools/smoke_key_door.sh` automates that focused ROM through missing-key,
  key pickup, and successful door-open stages, producing comparison screenshots
  under `.tools/screens/latest`. It now runs a focused PNG checker that verifies
  key-message evidence, post-pickup HUD state, the opened-door visual state, and
  a frame after walking through the opened doorway.
- `make combat-test-rom` and `make combat-test-gngeo` build an isolated E1M1
  enemy-combat verification ROM. It compiles with `DOOM_COMBAT_TEST`, places a
  visible imp in front of the player, and equips the shotgun while leaving the
  normal cart path unchanged.
- `tools/smoke_combat_interaction.sh` drives the combat ROM through a shotgun
  fire interaction and captures before/fire/death frames, proving that a visible
  enemy can be targeted, damaged, killed, and left as visible feedback.
- `make encounter-test-rom` and `make encounter-test-gngeo` build a focused
  real-map E1M1 encounter ROM. It starts near an existing converted shotgun guy
  at its WAD-derived placement, wakes only that runtime thing, and verifies that
  real-map monster projection and pistol interaction are readable without moving
  or inventing the monster.
- `tools/smoke_e1m1_encounter.sh` captures the initial focused E1M1 encounter
  frame and a fired frame under `.tools/screens/latest`, giving a regression
  check for real converted monster visibility in the normal E1M1 data.
- `make scout-test-rom` and `make scout-test-gngeo` build a first-contact E1M1
  scout ROM. It keeps the first reachable shotgun guys and pickups at their
  converted WAD positions, but starts from a normal-route waypoint looking into
  that encounter space so early-route visibility can be checked without faking
  monster placement.
- `tools/smoke_e1m1_scout.sh` captures that scout viewpoint and a pistol-fire
  frame, extending the real-map evidence beyond the closer encounter ROM.
- `tools/capture_compare.sh` can now capture named native-vs-NeoGeo comparison
  waypoints under `.tools/screens/`: `start`, `e1m1-start`, `e1m2-start`,
  `e1m1-encounter`, `e1m1-scout`, and `e1m2-keydoor`. The start waypoints use
  native and Neo Geo map spawns; non-start route waypoints now drive both
  engines with the same timed input script from the map spawn by default, with
  native Doom holding its speed modifier during forward movement.
  `COMPARE_NATIVE_MOVE_MODIFIER=` disables that speed modifier, and
  `COMPARE_NATIVE_ROUTE_*` / `COMPARE_NEO_ROUTE_*` can override individual
  route timings when matching exact views for visual investigation.
  `COMPARE_ROUTE_MODE=focused` keeps the older focused Neo Geo verification ROM
  visual registers when that is the useful evidence.
- Smoke and comparison captures default to workspace 4 and targeted X11 key
  events so they do not steal focus while the user is working. Direct i3/sway
  tiling remains opt-in (`SMOKE_TILE_WINDOWS=1` or
  `COMPARISON_TILE_WINDOWS=1`) because `ngdevkit-gngeo` currently crashes when
  the window manager resizes it out of floating mode.
- `tools/inspect_map_specials.py --map E1M2` audits linedef and sector specials
  from the same WAD conversion path. It makes unsupported Doom mechanics such
  as lifts/platforms visible before they are mistaken for only renderer bugs.
- The wall atlas keeps seven alternate texture banks but now spends two of
  those banks on high-coverage Episode 1 textures (`SLADWALL` and `COMPTALL`)
  instead of lower-impact slots. The converter maps related stone, tech,
  computer, light, and brown variants into those existing classes so E1M2 walls
  retain more Doom identity without adding runtime WAD parsing or more wall
  sprites.
- `make exit-test-rom` and `make exit-test-gngeo` build a focused E1M1 exit
  completion ROM. It starts two converted cells left of the real generated
  E1M1 exit trigger, and `tools/smoke_e1m1_exit.sh` walks into that trigger,
  captures the completed frame, and checks the `EXIT` plus kill/item/secret
  percentage overlay.
- `make e1m8-boss-test-rom` and `make e1m8-boss-test-gngeo` build a focused
  E1M8 boss-exit verification ROM. It stages the two real E1M8 Baron things in
  front of the player with low HP, and `tools/smoke_e1m8_boss_exit.sh` fires
  once, captures the completed frame, and checks the normal `EXIT` overlay.
- `make hidden-attack-test-rom` and `make hidden-attack-test-gngeo` build a
  readable-slot regression ROM. It places an awake shotgun guy outside the
  readable view and freezes its movement, so the HUD health value must stay
  unchanged unless hidden or offscreen enemies can still damage the player.
- `tools/smoke_hidden_attack.sh` captures that ROM at boot and again after a
  short wait. The delayed frame should show no readable monster and unchanged
  health, proving combat pressure is tied to visible world-sprite slots.
- `tools/smoke_enemy_visibility.sh` runs the combat, real-map encounter, scout,
  hidden-attack, and monster-gallery screenshot passes so renderer or AI changes
  can refresh the full visible-enemy evidence set with one command. It also
  runs a lightweight PNG sanity checker so blank/error captures fail fast.
- `make melee-test-rom` and `make melee-test-gngeo` build an isolated close-
  combat verification ROM. It compiles with `DOOM_MELEE_TEST`, equips the
  chainsaw, and places a visible imp inside the corrected player melee range.
- `make monster-gallery-rom` and `make monster-gallery-gngeo` build an isolated
  E1M1 living-enemy gallery. It compiles with `DOOM_MONSTER_GALLERY_TEST` and
  places shareware former human, shotgun guy, imp, demon, Baron, and barrel
  sprites into one view so sprite selection, palettes, orientation, and the
  floor-baseline anchor can be checked together.
- `make arsenal-test-rom` and `make arsenal-test-gngeo` build an isolated E1M1
  weapons/keycard HUD verification ROM. It compiles with `DOOM_ARSENAL_TEST`,
  grants all implemented weapons, backpack ammo caps, all three keycards, blue
  armor, and the same visible test imp while leaving the normal cart path
  unchanged.
- `make death-test-rom` and `make death-test-gngeo` build an isolated E1M1
  corpse/drop verification ROM. It compiles with `DOOM_DEATH_TEST`, places
  shareware Doom corpse sprites and a dropped shotgun in front of the player,
  and leaves the normal cart path unchanged.
- `tools/smoke_death_drop.sh` captures that death/drop ROM through the same
  GnGeo screenshot path used by the other visual regression helpers and now
  verifies corpse-colored pixels plus central dropped-weapon evidence.
- `make powerup-test-rom` and `make powerup-test-gngeo` build an isolated E1M1
  powerup verification ROM. It compiles with `DOOM_POWERUP_TEST`, places the
  currently supported powerup pickups and a separate visible imp in front of the
  player, starts with a short light-amplification palette tint so timed-powerup
  feedback can be inspected in screenshots, and leaves the normal cart path
  unchanged.
- `tools/smoke_powerup.sh` captures that ROM and verifies powerup-colored
  pickup evidence, the visible imp, and the expected status bar.
- The E1M2 key-test conversion currently includes one red keycard and a six-cell
  red locked-door group, matching the intended isolated keycard test path.
- Weapon pickups include shotgun, chaingun, rocket launcher, chainsaw, plasma
  rifle, and BFG thing IDs. The converter now preserves map chainsaws, and
  shell boxes use their own thing ID and WAD sprite when present. Pickup sprites
  use exact WAD frames when present; missing shareware-only frames are skipped
  instead of breaking the build.
- Pickups update live ammo/health/armor/key/weapon state and remain in the map
  if the player cannot use them yet.
- Green and blue armor pickups update both armor value and armor class. Blue
  armor can still be collected as a class upgrade when the player has weaker
  armor, matching the damage-reduction model instead of checking only the
  numeric armor value.
- Core Doom powerups now survive conversion and have runtime effects:
  invulnerability blocks player damage, berserk raises health to at least 100
  and strengthens fist damage, partial invisibility makes readable ranged
  monster attacks miss intermittently, radiation suits suppress damaging-floor
  ticks, computer maps mark the automap pickup as collected, and light goggles
  are accepted as a timed pickup. Active timed powerups now get a low-priority
  playfield/weapon palette tint that resumes after hurt, pickup, and muzzle
  flashes. Shareware builds bake the powerup sprites whose lumps are present and
  use safe fallback pickup sprites for missing registered-only frames.
- Former humans, shotgun guys, imps, demons/spectres, Hell Knights, Barons,
  cacodemons, lost souls, Doom II heavy weapon dudes, revenants, mancubi,
  arachnotrons, arch-viles, pain elementals, cyberdemons, spider masterminds,
  Wolfenstein SS enemies, and barrels have partial gameplay support. Registered,
  commercial, and Doom II monsters are accepted from WAD maps when those sprite
  lumps are present; lost souls and several Doom II monsters still use
  simplified movement/attack behavior until their dedicated rules are ported.
- Supported Doom, registered, and Doom II monsters now bake A/B walk frames
  across the available Doom angle groups when the source WAD contains those
  sprite lumps. Doom combined mirror lumps such as `TROOA2A8` are expanded
  offline into both normal and mirrored Neo Geo sprite strips, and runtime
  selection can choose all eight Doom rotation buckets from the coarse
  monster-facing vector instead of collapsing enemies onto the front/one-side
  frames.
- The same monsters also bake rotated attack and pain frames where the WAD
  provides them. Runtime prefers those state-specific rotation buckets during
  attack and hit-flash windows, then falls back to the older front-facing
  attack/pain buckets and finally to walking frames when a WAD has incomplete
  reaction art.
- Monsters keep health, awake state, pain flash/pause, attack cooldown, and a
  mutable position layer. Sleeping monsters now wake from line-of-sight or the
  weapon sound-alert path instead of waking through walls by proximity alone.
  Weapon sound alerts now reuse the player distance field, so a shot wakes
  reachable nearby monsters around converted E1M1 corners while preserving
  their WAD-derived positions. Awakened monsters follow that same coarse field
  so they can move around converted E1M1 walls instead of getting stuck on
  direct-line chase.
- Normal builds keep monsters at their converted WAD positions. The old helper
  that relocated hidden monsters near the player is now available only behind
  the explicit `DOOM_REVEAL_HIDDEN_MONSTERS` debug macro, so real-map play does
  not fake encounter placement just to fill a visible sprite slot.
- Hitscan, projectile, and melee monsters can damage the player only from
  readable on-screen world-sprite slots, so hidden/off-screen things do not
  attack through the Neo Geo sprite fallback path. Close melee still checks
  world distance and line-of-sight, but it now also requires the same readable
  slot proof as ranged attacks. Player weapon hits also target rendered enemies
  only, keeping combat feedback tied to visible sprites.
- Ranged monster attacks use a stricter readable-slot gate than player
  targeting: enough of the sprite must be clipped inside the screen, tall
  enough to read, and near the forward view before it can fire. Edge slices can
  still be drawn and shot, but they no longer start hitscan/projectile attacks.
- Ranged attacks also require several consecutive readable frames. Briefly
  clipping past a monster or seeing a one-frame projection is not enough for an
  instant hit; the enemy must remain readable before hitscan/projectile fire
  can begin. The warmup counter is updated from the readable slot list instead
  of rescanning every converted thing each frame.
  Monsters drawn through the coarse line-of-sight projection fallback are
  allowed to remain visible as feedback, but they cannot melee, shoot, or take
  direct weapon hits until normal wall-depth projection proves a readable slot.
  Imps, Hell Knights, cacodemons, Barons, and placeholder Doom II projectile
  monsters can launch visible fireball sprites; those fireballs are dropped if
  their source monster leaves the readable rendered slot set before impact.
  Once a readable monster launches a fireball, a close player impact always
  applies damage and hurt feedback instead of depending on whether the fireball
  still projects cleanly on the exact collision frame.
- Visible monsters are clamped to a minimum projected sprite size before
  drawing. Normal live monsters and fallback-projected feedback now stay in at
  least the 0.75 baked strip tier, which keeps distant shooters readable under
  the current wall pass instead of letting them collapse into near-invisible
  sprite strips.
- Monster, pickup, barrel, and corpse sprites preserve the source WAD patch
  `leftoffset`/`topoffset` while being baked into Neo Geo strips. Runtime
  placement applies those origins to a floor/reference baseline, keeping things
  centered and seated closer to Doom's source art while still allowing the
  weapon sprite to occlude centered close threats like Doom.
- Corpse sprites use a slightly higher runtime floor baseline than pickups and
  barrels, keeping small death/corpse frames readable above the status bar and
  weapon after a kill.
- Visible fireballs and impact bursts also keep a small minimum projected size,
  making projectile combat easier to read against dark wall and floor palettes.
- Thing projection falls back to a q8 player/view-vector projection after a
  successful map line-of-sight check, which avoids false sprite culling from the
  coarse wall depth buffer. Fallback-projected monsters use a stronger minimum
  size and must still be large/readable before they can make ranged attacks, so
  the game does not punish the player from tiny projection artifacts.
- Runtime sprite lookup now refuses to draw missing sprite definitions instead
  of falling back to the first baked enemy, which prevents wrong-looking
  monsters or corrupt placeholder sprites when a WAD lacks optional art.
- Thing slots are advanced only after a sprite actually renders. Missing,
  offscreen, or fully clipped sprites no longer consume one of the configured
  visible world-sprite slots, so the renderer keeps scanning for the next visible
  monster, pickup, projectile, corpse, or drop.
- Thing selection keeps an oversized sorted candidate buffer behind those
  configured visible slots. Edge-clipped or missing-art candidates can fail without
  starving later visible monsters in the same pass, which makes combat scenes
  less likely to contain an attacking-but-invisible enemy.
- Visible thing selection uses separate passes for projectiles, live monsters,
  barrels/explosions, collectible pickups, corpses, and noncollectible pickups.
  Live monsters therefore fill scarce world-sprite slots before shootable
  barrels or transient explosion sprites can claim them.
- Impact bursts are rendered only when a free world-sprite slot remains, so shot
  feedback cannot overwrite a selected monster, pickup, corpse, or drop slot.
- Monster visibility tests now count readable on-screen slots, not merely
  projected-but-edge-clipped sprite metadata. Attack gating is stricter than
  drawing so edge-clipped, metadata-only, or slot-starved monsters cannot hurt
  the player invisibly.
- Barrels explode and can apply radius damage.
- Killed monsters show a short three-step Doom death sequence before settling
  into corpse frames or drops where implemented. Shareware former humans,
  shotgun guys, imps, demons/spectres, and Barons now include the extra late
  death frame before the final corpse, so kills read closer to Doom's original
  animation cadence. Hell Knights and cacodemons keep visible corpse/death
  feedback when their registered/commercial sprite lumps are available.
- Former humans and shotgun guys now spawn separate dynamic clip/shotgun drops,
  so their corpse remains visible while the dropped pickup can still be
  collected. These dynamic drops do not increase the map item-completion
  counter because they are not part of the converted map's original pickup set.

## Map And Level Flow

- Movement uses fixed-point position/direction with a Doom-like body radius.
  Opposing inputs cancel cleanly, and forward-plus-strafe motion is combined into
  one normalized move step so diagonal walking does not run faster or spend a
  second collision pass.
- The main loop records whether it reached `wait_vblank_status()` after the
  vblank window had already started. The following active gameplay frame gives
  player movement/turning one extra capped input tick in that case, keeping
  walking responsiveness closer to real time after an over-budget frame without
  moving the whole game simulation twice.
- `tools/stress_movement.sh` boots the normal ROM, starts gameplay, and captures
  held forward, turn, and strafe poses. Use it alongside the route/key/powerup
  smokes when tuning renderer cost or movement feel.
- `tools/bench_movement.sh` runs the same movement stress path with GnGeo's FPS
  overlay enabled and longer holds. This gives a quick frame-pacing visual
  register under `.tools/screens/latest/movement-bench/` before and after
  renderer-cost changes.
- Movement bench builds an isolated `DOOM_FRAME_STATS=1` ROM by default. That
  ROM draws a compact green marker plus an `NN` fix-layer register in the playfield, where
  `NN` is the number of frames in the latest 64-frame window that reached
  `wait_vblank_status()` after vblank had already started. The checker rejects
  captures where the register is missing.
- The default quality wall-strip upload budget refreshes a bounded slice of the
  40 wall columns each frame, so texture/palette changes do not monopolize
  vblank. The overrun budget backs off when a frame reaches vblank late, and
  `DOOM_WALL_UPLOAD_COLUMNS` / `DOOM_WALL_UPLOAD_OVERRUN_COLUMNS` let movement
  benches test alternate budgets without hand-editing `CFLAGS`.
- Balanced movement frames skip portal-span refinement and tighten near-line
  refinement to a smaller radius, so held input spends less CPU time on WAD-line
  intersection scans in the lower-cost stress tier. The quality default keeps
  the richer solid-line path for readable close walls.
- The cached floor/ceiling updater refreshes 10 of its 20 backdrop columns per
  normal frame and four after a late frame, so turn/strafe plane changes settle
  quickly without runtime floor casting. `DOOM_BG_SCROLL_COLUMNS` and
  `DOOM_BG_SCROLL_OVERRUN_COLUMNS` expose that budget to the same movement bench
  path.
- Smoke and movement capture helpers accept `SMOKE_MAKE_ARGS`, which is passed
  to both the build and GnGeo run targets. This lets the same stress path test
  isolated builds such as `DOOM_DETAIL=speed BUILDDIR=build/speed-movement
  ROM=build/speed-movement-rom GFX_ROM_DIR=build/speed-movement-assets`;
  GnGeo still receives BIOS data through its configured `--datafile` path.
- Balanced rendering keeps the cheaper coarse wall path for distant solid walls
  but refines nearby solid hits against the converted WAD line metadata. The
  quality default goes further and enables solid-line refinement across the wall
  pass because navigation readability is the current bottleneck.
- Quality and clarity tiers also allow generated solid WAD lines to act as
  visual-only occluders while rays cross open coarse cells. This lets the
  default simplified collision grid stay playable while still drawing larger
  Doom pillars and room edges from the offline render-line table.
- Balanced mode tightens that near-line refinement radius while the player is
  actively moving and skips portal-span refinement on those moving frames.
  Standing frames restore the portal-span pass, and after a late frame the same
  reduced work is kept for one recovery frame. The default quality tier and
  clarity tier keep solid-line refinement for closer native-Doom still
  comparisons.
- Portal-span refinement now filters candidates to generated lower/upper span
  lines only, so a nearer solid render line in the same cell cannot hide a
  farther two-sided floor or ceiling transition. The ray also preserves the
  far-wall hit before deciding whether a portal span is visually dominant enough
  to replace that column.
- Solid WAD render lines now keep their front-sector height in the generated
  render metadata. Quality/clarity solid-line refinement uses that height to
  shrink low-room walls instead of projecting every one-sided wall as a full
  128-unit column, improving the E1M1 start-room read without changing collision
  or door cells.
- `DOOM_ADAPTIVE_LINE_REFINEMENT`,
  `DOOM_MOVING_LINE_REFINEMENT_CELLS`, `DOOM_MOVING_SPAN_REFINEMENT`, and
  `DOOM_OVERRUN_LINE_REFINEMENT_CELLS` can be passed through `SMOKE_MAKE_ARGS`
  when movement benches need to test a different CPU/fidelity balance.
- The converter flattens non-door two-sided sector transitions into narrow
  bridge cells after wall rasterization. This keeps Doom lift, stair, and ledge
  progression traversable in the Neo Geo port's 2D collision grid without
  making one-sided walls or locked door cells passable by default.
- Doors are converted from Doom linedefs, can require keys, and open in grouped
  cells.
- Door use first checks cells touching the player's collision body, then falls
  back to the forward use trace and nearby view cone. This makes large
  converted door groups easier to open when the player is already rubbing the
  door edge or slightly off-center.
- Exits freeze the level and show compact kill/item/secret completion
  percentage rows before restart, computed from the converted map's runtime
  monsters, pickups, and secret cells. When the reached exit's generated
  metadata names a next Episode 1 map, the overlay also draws that map code as
  the next standalone ROM to run. E1M3 normal and secret exits therefore point
  at different staged ROM targets even though the current cart still packages
  one generated map at a time.
- The menu can expose compiled-map and next-map metadata, but the current ROM
  still contains one heavy generated map/graphics set at a time. Full runtime
  multi-map loading remains a packaging/runtime-data milestone.
- Damaging floor cells apply periodic damage through the same hurt/armor path as
  combat, unless the radiation-suit timer is active. The generated floor visual
  class uses the same sector-special source for hazard tinting, and the runtime
  lookahead samples make visible damaging floors easier to read before they hurt
  the player.
- Secret cells can be discovered once and increment the secret count.
- The minimap uses the Neo Geo fix layer and overlays walls, player, threats,
  pickups, closed doors, and exits.
- Opening the minimap redraws the 38x23 fix-layer view incrementally in small
  frame budgets while repainting the player marker, so the map appears without
  the old one-frame stall. Normal A+C close now clears that same fix-layer
  region incrementally, reducing the return-to-gameplay spike after map use.
- Minimap overlay checks compare source-cell ranges for doors, exits, pickups,
  and threats instead of repeatedly converting every thing back into minimap
  coordinates for each drawn cell.
- Pickup and threat overlay checks also reject out-of-range source cells before
  resolving thing type, so opening/redrawing the map does less classification
  work on distant E1M1 objects.
- Moving monsters repaint their old and new minimap source cells while the map
  is open, so threat markers stay tied to real WAD/AI positions instead of
  leaving stale dots behind.
- Death and exit transitions still force-clear the minimap before drawing their
  compact fix-layer messages, avoiding stale map cells behind terminal status
  text.

## Build And Packaging

- Local Linux build uses ngdevkit under `.tools/ngdevkit-local` by default.
- Windows builds are supported through MSYS2 UCRT64.
- GitHub Actions builds Linux ROM, Windows ROM, standalone helper binaries, a
  Pages-playable FBNeo package, and a separate 68000 ASM ROM.
- Browser packages are public `doomgeo-aes.zip` / `doomgeo-aes-asm.zip` files,
  while the internal FBNeo chip names/CRCs match the `puzzledp` driver.
