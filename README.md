# NGRayEx

A real-time, first-person raycaster demonstration for the SNK Neo Geo AES, written
in C.

This was made purely for research purposes to understand the complexities of rendering realtime "3D"
on the Neo Geo. The code is unoptimized and could be built to run much faster. 

<img width="960" height="672" alt="Current Neo Geo Doom prototype with Doom wall and door textures, shaded floor and ceiling, pistol HUD, pickups, and live counters" src="docs/screenshots/doom-neogeo-current.png" />

## How it works

Every frame, for each of 64 screen columns:

1. Cast a ray through a 2D grid map until it hits a wall.
2. Measure the perpendicular distance and turn it into a slice height
3. Write a vertical-shrink value, a Y position, and a palette into the sprite
   control block for that column's sprite.

The video chip then scales each precomposed Doom wall-texture column to the
computed height. Floor and ceiling use a sprite-backed approximation of Doom
floor casting: the converter still reads the player-start Doom flats, but now
bakes each plane into 16 view-direction buckets, 2x2 movement phases, and 6x20
screen-tile perspective caches. Runtime only chooses the current bucket from
the raycaster's direction and half-cell player phase, so each 16x16 backdrop
tile already contains perspective-sampled flat pixels instead of a raw flat
block. The baked sampler uses a denser Doom-flat texel scale, keeping the
planes tied to the wall perspective while making the near floor read as a
receding textured surface without adding more per-scanline sprites.
The wall path now carries a compact per-cell texture-class grid alongside the
solid map. Normal walls still keep the preferred `STARTAN3` atlas, common
`BROWNGRN`, `BROWN1`, and `SUPPORT2` E1M1 linedefs can select their own
precomposed atlases, and converted closed-door cells use `BIGDOOR2`; each atlas
has its own distance-shaded palette range. The converter also emits a compact
per-cell texture phase from Doom sidedef offsets and distance along each solid
linedef, so wall columns no longer all restart at the same coarse grid-cell
edge. Doom pistol frames and the status-face set are baked with Doom patch
offsets so the weapon sits at the same screen anchor as the original psprite
path while the bottom 32-pixel `STBAR` remains a separate HUD surface.
The weapon bake lifts the cropped psprite window inside the Neo Geo strip chain,
keeping the pistol hand and barrel visible above the status bar instead of
letting the lower rows disappear into the HUD edge.
The pistol animates when B is pressed; walking and strafing nudge the strips
with a small hardware-position bob so movement feels less static without adding
any sprite slots. Real shots also pulse the weapon palette for a few frames,
briefly brighten the wall/floor/ceiling depth palettes, and successful pickups
add a short warm bonus flash, giving muzzle and item feedback through cheap
palette updates while the project still uses the null sound driver. The converter emits a compact grid-space runtime list from WAD `THINGS`;
the renderer projects up to three visible monster candidates with the same camera
math as the wall renderer while staying at the Neo Geo's
96-sprites-per-scanline ceiling in the worst case. Active monster projectiles
reserve the first visible world-sprite slot so incoming fire stays readable,
then visible monsters/barrels are selected before useful pickups, and corpses
only use slots left after collectible gameplay-critical objects; full or
otherwise unusable pickups are fallback candidates after corpses;
candidates are ranked by distance and screen relevance each frame, and tiny
candidates hidden under the pistol overlay are skipped. The converter only emits
monster thing types that currently have pre-scaled sprite frames (`POSS`,
`SPOS`, `TROO`, `SARG`, `BOSS`) and live palettes, and the live monster entries
now bake Doom `A/B` frames so the 68000 can alternate poses without composing
sprites or adding visible sprite slots. Unsupported later-Doom IDs do not
silently fall back to the wrong enemy art. The pistol clears
the currently rendered target set as the initial combat proof of concept. The
optional minimap is drawn on the fix (text) layer, which always composites over
sprites.

All arithmetic is 16.16 . Rotation uses constant cos/sin multiplies. The whole
renderer writes only a few control words per column per frame; the expensive
pixel work is offloaded to the scaler hardware.
 
## Controls

| Input               | Action            |
|---------------------|-------------------|
| D-pad Up / Down     | Move forward/back |
| D-pad Left / Right  | Turn              |
| Hold A + Left/Right | Strafe            |
| B                   | Fire weapon       |
| C                   | Toggle minimap    |
| D                   | Use facing door   |
| Hold A + D          | Toggle weapon     |
| D after DEAD/EXIT   | Restart level     |

Turning is tuned deliberately slower than the original raycaster demo so the
projected Doom targets can be lined up with keyboard or arcade-stick input.
Player movement keeps the original axis-separated slide feel, but collision now
tests a small Doom-like body radius instead of only the player's center cell, so
corners and closed doors behave less like thin grid lines.
The converter preserves the Doom player start depth on cardinal-facing starts
while using the nearest safe lateral grid center, which keeps the opening view
closer to the WAD without putting the player against coarse converted walls.

The wall and sprite projection heights use the raycaster's reciprocal lookup
table instead of doing a 64-bit divide for each projected column.

Runtime WAD things now include common Doom pickups as well as monsters. Pickups
share the three projected world-sprite slots to preserve the Neo Geo scanline
budget, disappear when touched, and update live fix-layer health, ammo, and
armor counters over the Doom status bar using large Doom `STTNUM` digit art
quantized into the `STBAR` palette instead of debug-green minimap colors.
The status bar also overlays compact `1`-`4` weapon indicators in the Doom
`ARMS` area: unowned slots stay dim, owned slots use the HUD palette, and the
active weapon is highlighted so weapon cycling is readable during play.
The center face is now generated from Doom's straight, left/right turn,
`STFOUCH`, `STFEVL`, and death face windows in C-ROM. It swaps by health band,
cycles the straight face variants, glances while turning, briefly shows the
matching pain face when damage lands, and uses the evil grin when a weapon is
picked up. Weapon frames start after that dedicated face block and both runtime
frame setters clamp to their own generated banks, so the gun cannot accidentally
read HUD face tiles or vice versa. The base `STBAR` rows and the dedicated face
crop are corrected for the Neo Geo sprite-chain row order, keeping the Doom
labels and face readable while leaving the already-aligned weapon strip path
alone.
The pistol crop keeps the original Doom patch offsets inside an eight-row Neo
Geo strip window aligned to the playfield bottom, preserving the lower hand
pixels before the status bar masks the final edge.
Clips, shells, and rockets are tracked separately. Bullet, shell, and rocket
pools now use compact Doom-like caps, and
pickups remain in the map instead of disappearing when the matching resource is
already full, but the renderer gives currently collectible pickups first claim
on scarce world-sprite slots. Shotgun guys now drop a shotgun pickup, and collecting one equips
Doom's shotgun frames, adds shells, and makes B fire a wider spread shot that
can hit a second visible target for reduced damage. Chaingun pickups are also
converted from Doom thing type `2002`, rendered with the `MGUN` pickup sprite,
and equip a third weapon slot that uses the `CHGG` weapon frames for a faster
held-fire bullet stream. Rocket launchers now convert from thing type `2003`
with `LAUN` pickup art, use `MISG` weapon frames, track rockets separately from
bullets and shells, and apply compact splash damage around the auto-aimed
target or a traced wall impact when no visible target is selected. Trying to fire empty flashes
a compact fix-layer `AMMO` message instead of failing silently, and firing an
empty selected weapon first auto-selects another owned weapon with usable ammo.
Backpacks now convert from Doom thing type `8`, use `BPAK` art, double the
compact ammo caps, and grant a small ammo/shell/rocket refill; thing type
`2048` now uses the correct `AMMO` box art instead of backpack art.
Supercharge pickups now convert from Doom thing type `2013`, use `SOUL` art,
and raise health toward 200 through the existing status-face and health path.
The weapon psprite bake now uses the same eight-row top position as the runtime
Neo Geo sprite chain, so the original Doom hand/gun patches land in the intended
screen-space window instead of being clipped into an unreadable center blob.
Close visible monsters apply a first-pass contact-damage tick with Doom-like armor absorption:
green armor absorbs roughly one third of incoming damage and blue armor absorbs
roughly half.
Pickups briefly flash compact center feedback using existing fix glyphs:
`KEY`, `AMMO`, `MED`, `ARM`, or weapon digits `2`/`3`/`4`.
Former humans and shotgun guys still apply compact hitscan-style ranged damage,
while imps and Barons now launch projected Doom fireball sprites (`BAL1`/`BAL7`)
that travel toward the player and deal damage on impact. This gives the player
visible pressure to move, aim, and use doors instead of only avoiding contact.
Each live monster also keeps a tiny attack cooldown: waking, getting hit, melee
swipes, hitscan attacks, and projectile launches all pace their next attack
independently, so combat pressure is readable instead of every visible enemy
sharing one abrupt global damage rhythm.
Combat uses a compact line-of-sight
sample against the converted map, so closed doors and walls block player shots,
shotgun spread targets, monster ranged damage, and monster chase wake-up. Damage
briefly tints the playfield red by swapping palettes during vblank, giving clear
feedback without spending additional sprite slots. Firing a real shot now also
wakes nearby monsters with a compact sound-alert pass, so rooms escalate more
like Doom instead of only reacting to the single enemy currently under the
crosshair. Pistol, shotgun, chaingun, and rocket hits also reserve the existing
projectile/effect slot for a short Doom `BEXP` puff, giving misses,
wall shots, and impacts visible feedback without increasing the scanline sprite
budget.
Monsters now keep compact per-thing hit points, so pistol shots damage targets
over multiple hits instead of deleting every visible enemy immediately; the
pistol uses a Doom-like visible-target autoaim and damages the visible monster
closest to the crosshair instead of damaging every visible target at once.
Pistol, shotgun, and rocket target selection now rejects projected candidates
outside the actual playfield, so weapons cannot silently choose an off-screen
monster just because the map line-of-sight trace still reaches it.
Surviving monsters flash briefly when hit, making shots readable without
spending extra sprite slots; the same short timer also pauses their chase and
attack logic, creating a compact Doom-like pain reaction. Former humans briefly
show their corpse frame before becoming clip pickups, and shotgun guys do the
same before becoming shotgun pickups, reusing the existing projected pickup
path. Other killed monsters now turn into projected Doom corpse frames
(`TROOR0`, `SARGN0`, `BOSSO0`, etc.) so fights leave a readable battlefield
state instead of simply deleting every dead thing.
Kills still add a small capped internal score for combat bookkeeping, but the
visible status bar keeps Doom's ammo, health, face, armor, and key fields
instead of drawing a custom score over the face slot. Explosive barrel thing
type `2035` is converted from the WAD, rendered with the `BAR1` sprite, and can
be shot to apply compact radius damage to nearby monsters, barrels, and the
player; detonated barrels briefly swap to a precomposed `BEXP` sprite before
disappearing. A tiny fix-layer center marker gives the player a stable aim point
without spending any sprite slots.
Runtime things now have a small mutable position layer, letting monsters take
throttled chase steps toward the player while still using the compact converted
WAD data for type, flags, and initial placement. Monsters now keep a compact
awake bit after seeing or being hit by the player, so they continue pursuit
around corners instead of stopping the moment line of sight is broken. Chase
movement also tries the alternate axis when the preferred step is blocked, and
keeps a small separation radius between live monsters. This reduces stuck or
stacked enemies and makes the projected world-sprite slots more readable. When
health reaches zero, movement, firing, and active projectiles
stop and the fix layer shows a compact `DEAD` message; pressing D resets the
player, doors, pickups, monsters, and HUD for another run. Restart also clears
the button-edge latches used by fire, doors, weapon toggle, minimap, and
restart so stale held inputs do not leak into the next run.

The converter also preserves Doom door and exit linedefs as compact runtime
trigger lists, and keycard/skull pickups set compact blue/red/yellow inventory
bits. The status bar shows compact `B`, `R`, and `Y` key indicators that brighten
when the matching key is collected. Pressing D traces forward from the player
and opens the first converted door cell in front of the view, with a short
forgiving fallback for slightly off-center doors. Keyed door specials require
the matching key, and connected cells from the same converted door open as a
single group. Opened doors affect both movement and raycasting through the
shared `map_at()` path. Opened doors flash a compact `DOR` center message;
trying a facing keyed door without the matching key flashes `KEY`, so door
interactions have readable feedback. Reaching the
converted E1M1 exit cell now
raises a fix-layer `EXIT` message with compact `K`, `I`, and `S` stat rows for
kills, collected pickups, and found secrets, then freezes player control,
monster movement, monster damage, and active projectiles until D restarts the level. This keeps
level progression behavior in the ROM without keeping generic WAD
directory/lump metadata in the cartridge.
Converted damaging sector specials now emit a tiny floor-damage grid, so E1M1
nukage cells periodically hurt the player through the same armor, hurt flash,
and status-face path as monster damage.
Secret sectors also emit a compact bit grid. Entering a secret cell for the
first time flashes a fix-layer `SEC` message, giving the map a basic Doom-style
exploration reward without storing generic WAD metadata at runtime.

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

The script launches native Doom with the same `DOOM_IWAD` used by the ROM
conversion, launches GnGeo with the current ROM, and writes native/Neo
Geo/side-by-side screenshots under `.tools/screens/`.
 
You must supply your own Neo Geo BIOS — it is copyrighted and not included.

I've only tested this with [gngeo](https://github.com/dciabrin/gngeo) It may not render correctly on real hardware
 
## Acknowledgements

Built against the [ngdevkit](https://github.com/dciabrin/ngdevkit) toolchain.
Hardware details cross-referenced from the
[Neo Geo Development Wiki](https://wiki.neogeodev.org).  
