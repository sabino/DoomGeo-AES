# Feature Inventory

This document describes what the current `doom-neogeo-port` branch actually
contains. It is intentionally separate from the README so the landing page stays
readable.

## Converted Doom Data

- Reads Doom-format WAD map lumps at build time: `THINGS`, `LINEDEFS`,
  `SIDEDEFS`, `VERTEXES`, `SECTORS`, `SEGS`, `SSECTORS`, `NODES`, `REJECT`, and
  `BLOCKMAP`.
- Emits a compact runtime grid plus texture class, texture phase, damaging
  sector, secret sector, door, exit, and thing data.
- Keeps richer generated map arrays for future work, but the runtime path uses
  Neo Geo-friendly fixed-size structures instead of a generic WAD directory.
- Defaults to E1M1 and supports changing `DOOM_MAP`, `DOOM_MAP_WIDTH`,
  `DOOM_MAP_HEIGHT`, and `DOOM_SKILL_MASK` at build time. The default skill
  mask is `4`, matching hard/Ultra-Violence THING placement; use `1` for easy
  or `2` for medium placement.

## Rendering

- 40 wall-column sprites cover the 320-pixel screen in 8-pixel logical columns
  backed by 16-pixel Neo Geo strips.
- Each frame casts fixed-point DDA rays, computes projected wall height, refines
  visual hits against compact WAD-derived render lines indexed by the hit cell,
  and writes Neo Geo sprite shrink/position data.
- The render-line broadphase is done offline by the converter. The E1M1 build
  currently emits 325 visual render lines and 857 cell references; the runtime
  checks at most 7 line candidates in a hit cell instead of scanning the whole
  render-line table for every wall column.
- This renderer spends more sprite budget on wall fidelity than the older
  20-column pass while holding seven visible world-thing slots for monsters,
  pickups, projectiles, corpses, and weapon sprites under the Neo Geo scanline
  limit.
- Wall textures are precomposed offline from Doom wall patches into Neo Geo
  tile strips. The current preferred wall texture is `STARTAN3`, with alternate
  atlases for common E1M1 walls and `BIGDOOR2` doors.
- Floor and ceiling use compact pre-baked perspective tile caches selected by
  player direction and coarse position. The runtime wraps those tile columns
  incrementally over several frames so movement reads less static without
  spending one full vblank on plane uploads. This is a compromise, not true Doom
  span rendering; the cache is deliberately kept small so monster and pickup
  sprite tiles stay inside the visible Neo Geo C-ROM tile range.
- Floor flats keep the WAD texture pattern but normalize green-dominant palette
  entries toward warm gray/brown before emitting Neo Geo tiles. This keeps E1M1
  closer to Doom's sober floor tone and avoids stray green speckles being read
  as a missing sprite or palette glitch.
- Depth palettes and directional shading give walls/planes distance cues without
  runtime pixel drawing.
- Point-blank wall fallback is limited to the primary wall atlas. Alternate
  walls and doors keep their own baked columns at close range, avoiding the
  wrong-palette block artifacts that could look like red/green missing sprites.

## HUD And UI

- Doom `STBAR` is baked into the HUD sprite strip area.
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
- The shareware WAD does not contain the original `PLSG`/`BFGG` weapon patches
  or `CELL`/`CELP` pickup sprites. Shareware builds therefore bake synthetic
  fallback psprite frames for plasma/BFG, while registered/commercial WAD builds
  can bake the exact art through the same C-ROM path.
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
- Visible thing selection uses one priority-ranked projection pass for
  monsters, barrels/explosions, collectible pickups, corpses, and spent pickups.
  This preserves the previous Doom-like visibility priority while avoiding the
  older five full scans of `NG_RUNTIME_THING_COUNT` every frame.
- Before projection and line-of-sight checks, thing selection now applies a
  conservative player-facing prefilter that rejects behind-camera and extremely
  distant candidates. This keeps normal E1M1 visible things intact while
  avoiding expensive projection work for objects that cannot contribute to the
  seven visible world-sprite slots.
- Ranged-attack warmup now tracks only the previous and current readable
  world-sprite slots. That replaces another per-frame scan across all converted
  runtime things with a bounded pass over the seven visible slots plus the
  previous seven-slot list.
- Combat damage and ranged attack paths reuse resolved runtime thing types
  after cheap coordinate, cooldown, and readability checks, reducing repeated
  per-frame classification work during active fights without changing damage,
  drops, or score behavior.
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
  objects outside the local collision area.
- Nearby pickup collection uses the same local map-cell prefilter before
  resolving thing type or exact q8 pickup distance. This keeps key, item,
  weapon, and dynamic-drop pickup behavior unchanged while avoiding most
  per-frame all-thing pickup checks.
- Per-frame thing timer maintenance skips entries with no active flash,
  attack, explosion, death-animation, or delayed-drop timers before touching
  the detailed state transitions.
- Weapon explosion, BFG cone, barrel splash, and sound-alert loops now reject
  dead entries and test cheap distance, readability, path, or forward-cone
  bounds before resolving runtime thing types or doing line-of-sight work. The
  final damage and wake-up thresholds are unchanged.
- Monster AI applies its active-range gate before resolving thing type, so
  distant converted things do not spend CPU on monster classification during
  movement ticks.
- Monster pathfinding caches the player map cell and avoids rebuilding the
  full BFS distance field while the player remains in that cell. Door openings
  and level restart still invalidate the cache so newly opened E1M1 routes are
  picked up immediately.
- Visible thing selection rejects behind-camera and far-away things before
  resolving runtime type or render bucket, reducing per-frame sprite candidate
  work while preserving the same final projection and priority rules.
- `tools/smoke_gameplay.sh` chains the verified enemy visibility, key-door,
  weapon shortcut, death/drop, and powerup screenshot passes for a broad local
  playable-feature regression check.
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
  offscreen, or fully clipped sprites no longer consume one of the seven visible
  world-sprite slots, so the renderer keeps scanning for the next visible
  monster, pickup, projectile, corpse, or drop.
- Thing selection keeps an oversized sorted candidate buffer behind those seven
  visible slots. Edge-clipped or missing-art candidates can fail without
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
- Doors are converted from Doom linedefs, can require keys, and open in grouped
  cells.
- Door use first checks cells touching the player's collision body, then falls
  back to the forward use trace and nearby view cone. This makes large
  converted door groups easier to open when the player is already rubbing the
  door edge or slightly off-center.
- Exits freeze the level and show compact kill/item/secret completion
  percentage rows before restart, computed from the converted map's runtime
  monsters, pickups, and secret cells.
- Damaging floor cells apply periodic damage through the same hurt/armor path as
  combat, unless the radiation-suit timer is active.
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
