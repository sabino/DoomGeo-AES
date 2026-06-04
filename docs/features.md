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
- Defaults to E1M1 and supports changing `DOOM_MAP`, `DOOM_MAP_WIDTH`, and
  `DOOM_MAP_HEIGHT` at build time.

## Rendering

- 20 wall-column sprites cover the 320-pixel screen in 16-pixel strips.
- This fallback renderer deliberately spends less sprite budget on wall detail
  so monsters, pickups, projectiles, corpses, and weapon sprites remain visible
  under the Neo Geo scanline limit.
- Each frame casts fixed-point rays, computes projected wall height, and writes
  Neo Geo sprite shrink/position data.
- Wall textures are precomposed offline from Doom wall patches into Neo Geo
  tile strips. The current preferred wall texture is `STARTAN3`, with alternate
  atlases for common E1M1 walls and `BIGDOOR2` doors.
- Floor and ceiling use compact pre-baked perspective tile caches selected by
  player direction. This is a compromise, not true Doom span rendering; the
  cache is deliberately kept small so monster and pickup sprite tiles stay
  inside the visible Neo Geo C-ROM tile range.
- Depth palettes and directional shading give walls/planes distance cues without
  runtime pixel drawing.

## HUD And UI

- Doom `STBAR` is baked into the HUD sprite strip area.
- Doom status face frames are baked into a dedicated face bank and switch by
  health, turn direction, pain, evil grin, and death state.
- Large red Doom-style `STTNUM` digits are rendered on the fix layer for ammo,
  health, frags/kills, and armor.
- The large status numbers are positioned over the AMMO, HEALTH, FRAG, and
  ARMOR fields with the Doom red-number palette.
- Weapon indicators now use a Doom-style two-row ARMS layout for pistol through
  BFG weapon numbers 2-7 instead of the old continuous debug strip. Key
  indicators and short messages (`KEY`, `AMMO`, `MED`, `ARM`, `DOR`, `SEC`,
  `EXIT`, `DEAD`) use fix-layer glyphs.
- Locked-door messages include the required key color glyph, so the E1M2 red
  keycard path can be verified visually instead of only through generated data.
- Owned HUD keys use the baked Doom keycard patches from the WAD, shrunken into
  the status-bar key cells with the matching WAD-derived blue/red/yellow
  palettes instead of coarse placeholder glyphs.
- The crosshair marker has been cleared from the current runtime path.

## Weapons And Combat

- Implemented runtime weapons: fist, pistol, shotgun, chaingun, rocket
  launcher, plasma rifle, BFG, and chainsaw.
- The shareware WAD does not contain the original `PLSG`/`BFGG` weapon patches
  or `CELL`/`CELP` pickup sprites. Shareware builds therefore keep placeholder
  psprite frames for plasma/BFG, while registered/commercial WAD builds can
  bake the real art through the same C-ROM path.
- Pistol/chaingun use compact hitscan-style targeting.
- Plasma rifle uses rapid cell-ammo visible-target damage.
- Fist/chainsaw use close-range rendered-slot melee checks, so they only hit
  visible nearby targets.
- Shotgun uses spread damage and can hit multiple visible targets.
- Rocket launcher tracks rockets separately and applies compact splash damage.
- BFG spends 40 cells, launches a visible forward projectile using the current
  baked fireball sprite path, then detonates into heavy damage on visible
  rendered targets.
- Empty weapon fire shows an ammo message and can auto-select a weapon with
  usable ammo.
- The C-button weapon cycle now prefers weapons that are both owned and ready
  to fire, then falls back to owned empty/melee weapons only if needed.
- C+D-pad provides direct weapon-group shortcuts without opening the minimap:
  Up selects the shotgun group, Right selects chaingun, Down cycles heavy
  weapons (rocket launcher, plasma, BFG), and Left cycles close/basic weapons
  (chainsaw, fist, pistol). Movement directions are masked during these combos
  so weapon selection does not shove the player around.
- Muzzle and impact feedback use palette flashes and projected explosion
  sprites. Monster fireballs now spawn the same impact burst when they hit a
  wall or the player instead of disappearing silently.

## Things, Pickups, And Enemies

- Runtime things include common E1M1 pickups, keys, ammo, cell ammo, armor,
  health, backpack, weapons, barrels, monsters, projectiles, corpses, and
  explosions.
- The default ROM starts on shareware `E1M1`; `make key-test-rom` and
  `make key-test-gngeo` build shareware `E1M2` into an isolated output tree so
  the real red keycard and red locked-door path can be verified without
  changing the default map.
- `make combat-test-rom` and `make combat-test-gngeo` build an isolated E1M1
  enemy-combat verification ROM. It compiles with `DOOM_COMBAT_TEST`, places a
  visible imp in front of the player, and equips the shotgun while leaving the
  normal cart path unchanged.
- `make arsenal-test-rom` and `make arsenal-test-gngeo` build an isolated E1M1
  weapons/keycard HUD verification ROM. It compiles with `DOOM_ARSENAL_TEST`,
  grants all implemented weapons, backpack ammo caps, all three keycards, blue
  armor, and the same visible test imp while leaving the normal cart path
  unchanged.
- The E1M2 key-test conversion currently includes one red keycard and a six-cell
  red locked-door group, matching the intended isolated keycard test path.
- Weapon pickups include shotgun, chaingun, rocket launcher, chainsaw, plasma
  rifle, and BFG thing IDs. Pickup sprites use exact WAD frames when present;
  missing shareware-only frames are skipped instead of breaking the build.
- Pickups update live ammo/health/armor/key/weapon state and remain in the map
  if the player cannot use them yet.
- Former humans, shotgun guys, imps, demons/spectres, Hell Knights, Barons,
  cacodemons, lost souls, Doom II heavy weapon dudes, revenants, mancubi,
  arachnotrons, arch-viles, pain elementals, cyberdemons, spider masterminds,
  Wolfenstein SS enemies, and barrels have partial gameplay support. Registered,
  commercial, and Doom II monsters are accepted from WAD maps when those sprite
  lumps are present; lost souls and several Doom II monsters still use
  simplified movement/attack behavior until their dedicated rules are ported.
- Supported Doom, registered, and Doom II monsters now bake A/B walk frames
  across the available Doom angle groups when the source WAD contains those
  sprite lumps. Runtime sprite selection tracks a coarse monster-facing vector
  so visible enemies are no longer locked to a single front-facing frame.
- The same monsters also bake front-facing attack frames where the WAD provides
  them. A short runtime attack timer swaps the visible sprite to those frames
  when a monster fires, throws a projectile, or bites.
- Front-facing pain frames are baked for the supported monsters and selected
  during the existing hit-flash window, so weapon impacts read as a sprite
  reaction rather than only a palette flash.
- Monsters keep health, awake state, pain flash/pause, attack cooldown, and a
  mutable position layer. Awakened monsters follow a coarse player distance
  field so they can move around converted E1M1 walls instead of getting stuck
  on direct-line chase.
- Hitscan, projectile, and melee monsters can damage the player only from
  readable on-screen world-sprite slots, so hidden/off-screen things do not
  attack through the Neo Geo sprite fallback path. Player weapon hits also
  target rendered enemies only, keeping combat feedback tied to visible sprites.
  Imps, Hell Knights, cacodemons, Barons, and placeholder Doom II projectile
  monsters can launch visible fireball sprites; those fireballs are dropped if
  their source monster leaves the readable rendered slot set before impact.
- Visible monsters are clamped to a minimum projected sprite size before
  drawing, which keeps distant shooters readable under the reduced 20-column
  wall pass instead of letting them collapse into near-invisible sprite strips.
- Thing projection falls back to a q8 player/view-vector projection after a
  successful map line-of-sight check, which avoids false sprite culling from the
  coarse wall depth buffer.
- Runtime sprite lookup now refuses to draw missing sprite definitions instead
  of falling back to the first baked enemy, which prevents wrong-looking
  monsters or corrupt placeholder sprites when a WAD lacks optional art.
- Thing slots are advanced only after a sprite actually renders. Missing,
  offscreen, or fully clipped sprites no longer consume one of the eight visible
  world-sprite slots, so the renderer keeps scanning for the next visible
  monster, pickup, projectile, corpse, or drop.
- Monster visibility tests now count readable on-screen slots, not merely
  projected-but-edge-clipped sprite metadata. This keeps hidden-monster reveal
  and attack gating tied to sprites the player can actually see.
- Barrels explode and can apply radius damage.
- Killed monsters show a short three-step Doom death sequence before settling
  into corpse frames or drops where implemented. Hell Knights and cacodemons now
  keep visible corpse/death feedback when their registered/commercial sprite
  lumps are available.
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
  combat.
- Secret cells can be discovered once and increment the secret count.
- The minimap uses the Neo Geo fix layer and overlays walls, player, threats,
  pickups, closed doors, and exits.
- Opening the minimap redraws the 38x23 fix-layer view incrementally in small
  frame budgets while repainting the player marker, so the map appears without
  the old one-frame stall.
- Death and exit transitions close the minimap before drawing their compact
  fix-layer messages, avoiding stale map cells behind terminal status text.

## Build And Packaging

- Local Linux build uses ngdevkit under `.tools/ngdevkit-local` by default.
- Windows builds are supported through MSYS2 UCRT64.
- GitHub Actions builds Linux ROM, Windows ROM, standalone helper binaries, a
  Pages-playable FBNeo package, and a separate 68000 ASM ROM.
- Browser packages are public `doomgeo-aes.zip` / `doomgeo-aes-asm.zip` files,
  while the internal FBNeo chip names/CRCs match the `puzzledp` driver.
