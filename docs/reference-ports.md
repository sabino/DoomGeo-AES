# Reference Port Notes

These repositories are useful research material for DoomGeo-AES, but they are
not drop-in solutions for Neo Geo hardware. The current branch does not copy
their code.

## doomgeneric

Repository: https://github.com/ozkl/doomgeneric

Useful ideas:

- Keep the platform boundary tiny.
- Separate init, frame/tick, time, sleep, input, and optional sound hooks.
- Make ports provide just the host integration while Doom owns game timing.
- Keep WAD ownership explicit; the user still needs game data.

Why it cannot be copied directly:

- The model assumes a normal `DG_ScreenBuffer` that the platform displays.
- DoomGeo-AES has no framebuffer and cannot upload arbitrary per-frame pixels.
- The Neo Geo path must pre-bake graphics and update sprite control blocks
  instead.

Potential adaptation:

- Add a tiny internal platform-ish layer for Neo Geo services: input polling,
  ticks, palette effects, and frame presentation.
- Use the small API style as inspiration for tests/tools, not runtime pixel
  rendering.

License note:

- `doomgeneric` ships with GPL-2.0 license text and Doom-derived source. Any
  code import would need explicit GPL-compatible handling and attribution.

## PureDOOM

Repository: https://github.com/Daivuk/PureDOOM

Useful ideas:

- Single-file/callback-oriented presentation is excellent for documenting port
  boundaries.
- The README clearly separates features, TODOs, usage, video, input, sound, and
  music.
- The DOOM license/provenance block is visible in the project documentation.
- Its callback shape is a useful reference for future test harnesses or host
  tools.

Why it cannot be copied directly:

- It still exposes Doom's framebuffer/audio model.
- It is designed for devices that can receive a framebuffer and output it.
- DoomGeo-AES must avoid runtime patch composition and pixel drawing.

Potential adaptation:

- Mirror its documentation style: explicit features, explicit TODOs, explicit
  license/provenance, and clear platform responsibilities.
- Consider callback-style host tools for native comparison/capture work.

License note:

- `PureDOOM` ships with GPL-2.0 license text and a historical Doom source
  license notice. Treat exact code copying as a separate licensing decision.

## id Software DOOM Source

Repository: https://github.com/id-Software/DOOM

Useful ideas:

- The original README is still relevant: Doom's map/render pipeline can be
  improved by a front-to-back BSP walk, and line-of-sight/movement can use
  better spatial tests.
- The original architecture explains why exact Doom behavior needs more than a
  Wolfenstein-like grid raycaster.

Neo Geo implication:

- The generated `SEGS`, `SSECTORS`, `NODES`, `REJECT`, and `BLOCKMAP` arrays are
  valuable because they give future work a path beyond the coarse grid.
- The next serious fidelity jump should evaluate a Neo Geo-compatible BSP/seg
  renderer rather than only adding more wall texture classes.

License note:

- The official source release is GPL-2.0. Doom game assets/WADs remain separate
  and are not redistributed by this repo.
