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

- 40 wall-column sprites cover the 320-pixel screen in 8-pixel strips.
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
- Weapon indicators, key indicators, and short messages (`KEY`, `AMMO`, `MED`,
  `ARM`, `DOR`, `SEC`, `EXIT`, `DEAD`) use fix-layer glyphs.
- The crosshair marker has been cleared from the current runtime path.

## Weapons And Combat

- Implemented weapons: pistol, shotgun, chaingun, and rocket launcher.
- Pistol/chaingun use compact hitscan-style targeting.
- Shotgun uses spread damage and can hit multiple visible targets.
- Rocket launcher tracks rockets separately and applies compact splash damage.
- Empty weapon fire shows an ammo message and can auto-select a weapon with
  usable ammo.
- Muzzle and impact feedback use palette flashes and projected explosion sprites.

## Things, Pickups, And Enemies

- Runtime things include common E1M1 pickups, keys, ammo, armor, health,
  backpack, weapons, barrels, monsters, projectiles, corpses, and explosions.
- Pickups update live ammo/health/armor/key/weapon state and remain in the map
  if the player cannot use them yet.
- Former humans, shotgun guys, imps, demons/spectres, Barons, and barrels have
  partial gameplay support.
- Monsters keep health, awake state, pain flash/pause, attack cooldown, and a
  mutable position layer. Awakened monsters follow a coarse player distance
  field so they can move around converted E1M1 walls instead of getting stuck
  on direct-line chase.
- Hitscan, projectile, and melee monsters can damage the player only from
  rendered world-sprite slots, so hidden/off-screen things do not attack through
  the Neo Geo sprite fallback path. Imps/Barons can launch visible fireball
  sprites.
- Thing projection falls back to a q8 player/view-vector projection after a
  successful map line-of-sight check, which avoids false sprite culling from the
  coarse 40-column wall depth buffer.
- Barrels explode and can apply radius damage.
- Killed monsters leave corpse frames or drops where implemented.

## Map And Level Flow

- Movement uses fixed-point position/direction with a Doom-like body radius.
- Doors are converted from Doom linedefs, can require keys, and open in grouped
  cells.
- Exits freeze the level and show compact kill/item/secret stat rows before
  restart.
- Damaging floor cells apply periodic damage through the same hurt/armor path as
  combat.
- Secret cells can be discovered once and increment the secret count.
- The minimap uses the Neo Geo fix layer and overlays walls, player, threats,
  pickups, closed doors, and exits.

## Build And Packaging

- Local Linux build uses ngdevkit under `.tools/ngdevkit-local` by default.
- Windows builds are supported through MSYS2 UCRT64.
- GitHub Actions builds Linux ROM, Windows ROM, standalone helper binaries, a
  Pages-playable FBNeo package, and a separate 68000 ASM ROM.
- Browser packages are public `doomgeo-aes.zip` / `doomgeo-aes-asm.zip` files,
  while the internal FBNeo chip names/CRCs match the `puzzledp` driver.
