# DoomGeo-MVS Build and Release Plan

This file is the committed plan tracked by `doomgeo-plan`. It is intentionally
separate from the build helper so one standalone binary can build/package while
the other only accompanies and tracks the plan.

## Checklist

- [x] Add a plan-only CLI that reads and updates this markdown checklist.
- [x] Add a build CLI with doctor, build, package, install-tools, and uninstall commands.
- [x] Package both CLIs as standalone Linux and Windows binaries in GitHub Actions.
- [x] Build the Neo Geo ROM in GitHub Actions on Ubuntu 24.04.
- [x] Add a GitHub Pages bundle that plays the ROM through a WebAssembly/asm.js browser emulator frontend.
- [x] Add a separate 68000 ASM ROM build and expose it from GitHub Pages.
- [x] Add a fully native Windows/MSYS2 ROM build job after validating ngdevkit UCRT64 in CI.
- [ ] Add signed release uploads for tagged builds.
- [ ] Add a smoke-run screenshot capture job for the Linux ROM build.
- [x] Decide whether the final user-facing build helper should install ngdevkit through MSYS2, WSL, Docker, or all three on Windows.

## Evidence

- Linux ROM builds are expected to produce `build/rom/puzzledp.zip`.
- ASM ROM builds are expected to produce `build/asm-rom/puzzledp.zip` from
  `asm/doomgeo_asm.S`.
- Windows/MSYS2 ROM builds are expected to produce the same `puzzledp.zip`
  through the UCRT64 ngdevkit packages.
- Standalone helper builds are expected to produce `doomgeo-build` and
  `doomgeo-plan` artifacts for Linux, plus `.exe` variants for Windows.
- The Pages bundle is expected to publish `index.html`, `asm.html`,
  `rom/puzzledp.zip`, `rom/asm/puzzledp.zip`, and `rom/neogeo.zip`.
- Repo-local installs are removable with `doomgeo-build uninstall`; `--all`
  also removes cached WAD/package downloads under `.tools`.
- The installer decision is MSYS2 UCRT64 for native Windows builds, with WSL
  delegation kept as a fallback when the helper is launched from normal Windows.
