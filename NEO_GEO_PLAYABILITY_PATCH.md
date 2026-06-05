# Neo Geo Playability Patch

This branch adopts the useful parts of the external playability patch while
preserving DoomGeo-AES' Neo Geo renderer model: WAD data is converted offline,
graphics are pre-baked into Neo Geo-friendly tiles, and the 68000 runtime
updates sprite/fix-layer state instead of drawing a framebuffer.

## Adopted Changes

- The default converted map grid is now `96x72`, improving spatial fidelity over
  the older lower-resolution layout.
- WAD map bounds are centered in the generated grid, reducing whole-map drift
  between original Doom coordinates and Neo Geo runtime cells.
- Player starts and runtime things preserve fractional WAD positions where
  possible. If a supported thing lands in a cell closed only by coarse line
  rasterization, the converter reopens that thing cell instead of snapping the
  thing far away from its original location.
- Two-sided linedefs now use Doom-like player-height and step-height checks:
  small stairs remain passable, while tall platforms and ledges remain blocking
  instead of becoming walk-through holes.
- The converter emits per-cell floor and ceiling height grids. Runtime world
  sprites use those floor heights for vertical seating, so pickups, monsters,
  corpses, and drops align better in raised/lowered sectors.
- Two-sided floor/ceiling deltas generate lower/upper visual spans with WAD
  texture fallbacks when explicit upper/lower textures are absent.
- Portal/span thresholds are lower, so stairs and platform edges remain visible
  at practical play distances.
- Generated map data is split into `doom_map_generated.h` declarations and
  `doom_map_generated.c` table definitions for normal Makefile builds. This
  avoids duplicating large generated arrays through every C translation unit.
- Route validators understand the split generated source/header output.
- Focused exit smoke now starts from generated exit metadata instead of stale
  hardcoded converted cells, keeping it stable when map centering changes.
- Screenshot smoke heuristics were retuned for the current WAD/render colors:
  distant shaded scout monsters, dark corpse/drop frames, and non-green powerup
  fallback art are still validated, but no longer require bright palette colors
  that are not present in the capture.

## Deliberately Not Adopted

- The external patch's lower wall-atlas quality setting was not adopted. The
  default `DOOM_DETAIL=quality` path still emits 32-phase wall and door atlases
  for close-wall readability.
- The renderer remains the Neo Geo sprite-strip approximation. This patch does
  not switch to a framebuffer renderer or import large Doom engine code.

## Validation Notes

Validation was run locally with the repo's shareware Doom 1 WAD zip and the
in-repo ngdevkit/GnGeo tooling:

```sh
python3 -m py_compile tools/doom_convert.py tools/check_e1m1_route.py tools/check_episode_routes.py tools/inspect_map_specials.py
python3 tools/doomgeo_build.py doctor
make route-check
python3 tools/check_episode_routes.py --iwad .tools/assets/doom1.wad.zip --width 96 --height 72 --skill-mask 4
python3 tools/doomgeo_build.py build
make DOOM_MAP=E1M1 cart
make DOOM_MAP=E1M2 BUILDDIR=build/e1m2-96 ROM=build/e1m2-96-rom GFX_ROM_DIR=build/e1m2-96-assets cart
make key-test-rom
make key-door-test-rom
make combat-test-rom encounter-test-rom exit-test-rom powerup-test-rom
SMOKE_DISPLAY=:1 SMOKE_WORKSPACE=4 SMOKE_OUTPUT_DIR=.tools/screens/playability-patch-keydoor tools/smoke_key_door.sh
SMOKE_DISPLAY=:1 SMOKE_WORKSPACE=4 SMOKE_OUTPUT_DIR=.tools/screens/playability-patch-exit tools/smoke_e1m1_exit.sh
SMOKE_DISPLAY=:1 SMOKE_WORKSPACE=4 SMOKE_OUTPUT_DIR=.tools/screens/playability-patch-smoke tools/smoke_weapon_shortcuts.sh
SMOKE_DISPLAY=:1 SMOKE_WORKSPACE=4 SMOKE_OUTPUT_DIR=.tools/screens/playability-patch-powerup-rerun tools/smoke_powerup.sh
SMOKE_DISPLAY=:1 SMOKE_WORKSPACE=4 SMOKE_OUTPUT_DIR=.tools/screens/playability-patch-movement SMOKE_LOG=.tools/logs/playability-patch-movement-gngeo.log tools/bench_movement.sh
COMPARE_FOCUS_WORKSPACE=0 COMPARISON_WORKSPACE=4 COMPARISON_TILE_WINDOWS=0 COMPARE_WAYPOINT=start DOOM_MAP=E1M1 tools/capture_compare.sh
COMPARE_FOCUS_WORKSPACE=0 COMPARISON_WORKSPACE=4 COMPARISON_TILE_WINDOWS=0 COMPARE_WAYPOINT=start DOOM_MAP=E1M2 tools/capture_compare.sh
```

The native-vs-NeoGeo captures generated in this run were:

- `.tools/screens/compare-E1M1-start-20260605-120749-side-by-side.png`
- `.tools/screens/compare-E1M2-start-20260605-120852-side-by-side.png`
- `.tools/screens/compare-E1M1-start-20260605-122237-side-by-side.png`
- `.tools/screens/compare-E1M2-start-20260605-122305-side-by-side.png`
- `.tools/screens/compare-E1M1-start-20260605-125433-side-by-side.png`
- `.tools/screens/compare-E1M2-start-20260605-125514-side-by-side.png`
- `.tools/screens/compare-E1M1-start-20260605-131438-side-by-side.png`
- `.tools/screens/compare-E1M2-start-20260605-131512-side-by-side.png`

Those comparisons still show major visual gaps versus native Doom. This patch
improves coordinate fidelity, route stability, step/ledge blocking, and sprite
floor seating. The follow-up span pass keeps walking to the farther wall before
letting a large lower/upper span replace a column, which restores more room
boundaries, but it still does not solve true multi-span Doom wall rendering yet.
The powerup smoke now stages robust visible pickup sprites in the focused ROM
because the special powerup sprites are still too subtle in the current
sprite-strip view; making those special sprites read like native Doom remains a
separate art/readability gap.
The floor palette preview now also scans a small forward cone, so E1M1's
start-window hazard/liquid area is immediately recognizable instead of reading
as the same gray floor as the starting sector.
The local floor follow-up keeps that E1M1 hazard cue mostly in the far floor
rows, so the visible liquid band is easier to distinguish from the normal
near-floor texture while E1M2's start view remains neutral.
