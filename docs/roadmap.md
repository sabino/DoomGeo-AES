# Roadmap

This is the practical next-work list. It tracks what is visibly wrong or missing
after the current documentation pass.

## Immediate Visual Fixes

- Keep checking HUD number and ammo-counter alignment against fresh screenshots
  whenever status-bar assets or weapon placement change.
- Keep the current safer near-wall clamp unless the WAD render-line refinement
  proves a better close-wall strategy visually.
- Recheck weapon vertical placement after the HUD number pass so the gun and
  status face do not visually fight each other.
- Keep checking shotgun fire and pump frames against combat smoke captures;
  the weapon frame is centered, but palette and smoke timing changes can still
  affect how readable the fired frame feels.
- Update screenshots whenever the HUD or weapon placement changes.

## Gameplay And Assets

- Keep hardware-checking the direct C+D-pad and diagonal weapon shortcuts with
  `tools/smoke_weapon_shortcuts.sh`, then tune the mapping if diagonals are
  awkward on the target controls.
- Replace the current synthetic shareware plasma/BFG fallback psprites with
  exact art by testing a registered/commercial WAD path.
- Improve BFG/plasma fidelity: BFG trace behavior, better projectile art when a
  registered/commercial WAD provides exact assets, and visible pickup sprites
  when the source WAD provides them.
- Complete enemy sprite coverage beyond the current A/B eight-way walk rotation
  groups plus partial rotated attack/pain coverage: full death rotations,
  registered/Doom II reaction rotations, and more faithful thing placement
  still need work.
- Continue tuning line-of-sight, wall-depth fallback, and encounter placement
  now that monster tiles fit inside the visible C-ROM tile range and world
  sprites use a more stable floor-baseline anchor. Normal builds now preserve
  converted WAD monster placement; use
  `DOOM_REVEAL_HIDDEN_MONSTERS` only as an explicit debug aid.
- Add exact sprites for any registered-only powerup frames that are not present
  in the shareware WAD, then consider HUD countdown/status feedback beyond the
  current timed-powerup palette tint.

## Rendering Fidelity

- Tune the first cached perspective floor/ceiling pass: reduce noise, improve
  movement phase choices, and keep uploads inside the Neo Geo sprite/vblank
  budget.
- Extend the current WAD render-line refinement into a true higher-fidelity
  path using generated seg/node data.
- Add sector-aware upper/lower wall spans, two-sided windows, and multiple
  clipped spans; these are the main remaining reasons the native Doom E1M1
  start view does not match exactly even though the same map and player start
  are loaded.
- Experiment with diagonal wall or multi-span approximations within Neo Geo
  sprite limits.
- Profile wall, plane, thing, and HUD update costs before increasing sprite or
  map complexity.

## Map And Performance

- Keep refining minimap responsiveness after the incremental open/close pass:
  redraw only changed cells where possible.
- Reduce runtime work in monster selection/projection.
- Add a repeatable screenshot smoke test to CI for the Linux ROM build.
- Convert more maps once E1M1 visual and gameplay fundamentals are stable.

## Audio And Flow

- Add YM2610/Z80 sound effect playback.
- Convert Doom sound lumps to Neo Geo-friendly sample data.
- Decide whether music should be simplified YM2610 arrangements, sample-based
  approximations, or skipped until the visual/gameplay port is stronger.
- Add menus, intermission flow, and more complete restart/progression behavior.
