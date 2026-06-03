# Roadmap

This is the practical next-work list. It tracks what is visibly wrong or missing
after the current documentation pass.

## Immediate Visual Fixes

- Align the red HUD numbers over the Doom AMMO, HEALTH, FRAG, and ARMOR fields
  while keeping the current Doom-style font.
- Keep the current safer near-wall clamp unless a better close-wall strategy is
  proven visually.
- Recheck weapon vertical placement after the HUD number pass so the gun and
  status face do not visually fight each other.
- Update screenshots whenever the HUD or weapon placement changes.

## Gameplay And Assets

- Add missing weapons: fist, chainsaw, plasma rifle, and BFG.
- Expand weapon selection and ammo rules to cover the full Doom weapon set.
- Complete enemy sprite coverage, rotations, animation states, pain/death
  frames, and more faithful thing placement.
- Add more pickup/effect frames where the current sprite set still uses limited
  frame coverage.

## Rendering Fidelity

- Improve floor and ceiling rendering so movement reads more like Doom and less
  like a static plane approximation.
- Investigate a higher-fidelity wall path using the generated seg/node data.
- Experiment with diagonal wall or multi-span approximations within Neo Geo
  sprite limits.
- Profile wall, plane, thing, and HUD update costs before increasing sprite or
  map complexity.

## Map And Performance

- Make the minimap feel instant: avoid full redraws where marker/cell updates
  are enough, and cache static fix-layer cells aggressively.
- Reduce runtime work in monster selection/projection.
- Add a repeatable screenshot smoke test to CI for the Linux ROM build.
- Convert more maps once E1M1 visual and gameplay fundamentals are stable.

## Audio And Flow

- Add YM2610/Z80 sound effect playback.
- Convert Doom sound lumps to Neo Geo-friendly sample data.
- Decide whether music should be simplified YM2610 arrangements, sample-based
  approximations, or skipped until the visual/gameplay port is stronger.
- Add menus, intermission flow, and more complete restart/progression behavior.
