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
- Improve BFG/plasma fidelity under IWADs that actually provide those psprites:
  BFG trace behavior, projectile tuning, and visible pickup sprites when the
  source WAD provides them.
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

- Use the generated per-cell floor/ceiling heights for more than sprite
  seating. The current renderer now knows the heights, but it still does not
  draw true Doom floor/ceiling clipping or stacked multi-span sector geometry.
- Continue tuning the cached perspective floor/ceiling pass. The stable default
  keeps one phase per direction because multi-phase floor banks exceeded the
  practical sprite tile index range; any future forward-motion cue needs a
  smaller tile layout, fewer plane rows/directions, or a non-tile-index-heavy
  approximation.
- Extend the current WAD render-line refinement into a true higher-fidelity
  path using the generated grid/q8 BSP vertex/node data. The converter now emits
  and verifies that data; the runtime still needs the front-to-back visible-seg
  owner pass that feeds the existing sprite-strip buffers.
- Extend the new one-span two-sided wall approximation into real multiple
  clipped spans for windows, ledges, and upper/lower sector transitions; these
  are still a main reason the native Doom E1M1 start view does not match exactly
  even though the same map and player start are loaded.
- Keep checking native-vs-NeoGeo waypoints after converter changes. The centered
  `48x36` map, route-validated `DOOM_MAP_DETAIL_CULL=2.0` collision grid, and
  `DOOM_RENDER_DETAIL_CULL=1.5` visual-line pass deliberately reduce runtime
  map complexity while retaining larger Doom room-edge cues. Current E1M1/E1M2
  start comparisons still show large visual differences from native Doom. The
  converter now tags lower/upper spans with sidedef ownership, so the next major
  renderer gap is drawing more than one visible sector/span in a column instead
  of replacing the far wall with one selected partial span.
- Experiment with diagonal wall or multi-span approximations within Neo Geo
  sprite limits.
- Profile wall, plane, thing, and HUD update costs before increasing sprite or
  map complexity.

## Map And Performance

- Keep refining minimap responsiveness after the incremental open/close pass:
  redraw only changed cells where possible.
- Keep `make episode-route-check` green as the Episode 1 conversion baseline:
  E1M1-E1M7 and E1M9 route through the generated grid, and E1M8 uses the
  boss-death completion path.
- Reduce runtime work in monster selection/projection.
- Add a repeatable screenshot smoke test to CI for the Linux ROM build.
- Convert more maps once E1M1 visual and gameplay fundamentals are stable.

## Audio And Flow

- Add YM2610/Z80 sound effect playback.
- Convert Doom sound lumps to Neo Geo-friendly sample data.
- Decide whether music should be simplified YM2610 arrangements, sample-based
  approximations, or skipped until the visual/gameplay port is stronger.
- Replace the current standalone-map progression prompt with a true multi-map
  episode/intermission flow once multiple generated maps can live in one cart.
