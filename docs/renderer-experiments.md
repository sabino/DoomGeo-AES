# Renderer Experiments

This branch uses named renderer experiments for small ROMs that test one idea
at a time. The default `make cart` path remains the stable simple-map showcase.

## Commands

List available experiments:

```sh
make experiment-list
```

Build one experiment:

```sh
make experiment-cart EXPERIMENT=ripdoom_flat DOOM_IWAD=/home/sabino/Downloads/Doom1.WAD
```

Run its configured host probes, when available:

```sh
make experiment-probes EXPERIMENT=ripdoom_flat DOOM_IWAD=/home/sabino/Downloads/Doom1.WAD
```

Build and launch it in GnGeo:

```sh
make experiment-gngeo EXPERIMENT=ripdoom_flat DOOM_IWAD=/home/sabino/Downloads/Doom1.WAD SCALE_WIN=3
```

Each experiment writes its own artifacts under:

```text
build/experiments/<experiment>/
```

The generated `manifest.txt` captures the description, build flags, ROM path,
asset path, and configured probes for that run.

Renderer experiments currently force full wall texture-chain refresh with
`DOOM_WALL_UPLOAD_COLUMNS=255` and `DOOM_WALL_UPLOAD_OVERRUN_COLUMNS=255`.
This is deliberate: it removes delayed SCB1 tile updates as a variable when
testing whether geometry, projection, or texture selection is causing a visual
artifact. Once an experiment looks correct, reduce those budgets to find the
real hardware/performance tradeoff.

## Current Experiments

`texture_columns`

Generated STARTAN3 texture columns on the stable simple-map raycaster. Use this
as the baseline for texture readability, column density, and classic floor
motion before adding real WAD geometry.

`ripdoom_flat`

Actual E1M1 RIPDOOM geometry with one wall layer and flat floor/ceiling
backdrop. Use this to judge whether full WAD geometry, generated textures, and
basic horizontal ray visibility feel coherent before any foreground
span composition.

`ripdoom_solid20`

Actual E1M1 RIPDOOM geometry through the 20-column multifloor sprite budget,
with foreground spans disabled. Use this to isolate the cost and look of the
v1 Neo Geo sprite budget before layering upper/lower wall bands.

`ripdoom_spans`

Actual E1M1 RIPDOOM geometry with three wall layers and upper/lower
foreground spans. Use this only after `ripdoom_flat` and `ripdoom_solid20`
look acceptable; this is where span sorting, clipping, and backdrop hiding are
tested.

## Rule

Renderer experiments should stay narrow. Add a new named experiment when a
concept needs different projection, plane, span, or sprite-budget behavior
instead of folding every idea into the same ROM.
