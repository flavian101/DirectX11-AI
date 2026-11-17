# Lesson 44 - Waves that scale with the water

## Understand

**The report.** A water body fit to a terrain of the maximum size had waves that died out and looked flat, even with Stormy selected.

**The cause was not a setting.** The vertex shader can only show a wave that the grid is fine enough to follow. It keeps a wave at full height when the wavelength is at least six grid spacings, and fades it to nothing at three (`WaveDisplacement` in `waves.hlsli`). The mesh has a fixed vertex budget (`WaterMesh.h`: about 450,000 vertices; `ChooseTileGrid` takes tiles away until it fits), so however the detail setting trades tile size against tile count, the whole body has about 670 cells along a side. A body of size S has cells of about S / 670 world units. At the maximum size, 2000, that is about 3 units: waves shorter than about 9 units are gone, and everything up to about 19 units is partly faded. The presets' waves run from about 3 to 48 units long, so a large part of the sea is in that range.

Here is the real Stormy preset on the real tile grid (the numbers come from the tests, not from a guess):

| body size | tiles | finest cell | wave height shown before | auto scale | wave height shown after | drawn amplitude |
|---|---|---|---|---|---|---|
| 100 | 8x8 | 0.39 | 100% | 1.00x | 100% | 0.60 |
| 250 | 8x8 | 0.98 | 93% | 1.00x | 93% | 0.60 |
| 500 | 12x12 | 1.30 | 85% | 1.00x | 85% | 0.60 |
| 1000 | 20x20 | 1.56 | 77% | 1.05x | 80% | 0.63 |
| 1500 | 20x20 | 2.34 | 55% | 1.58x | 80% | 0.95 |
| 2000 | 20x20 | 3.12 | 38% | 2.11x | 80% | 1.26 |

("Shown" is the share of the sea's wave height the vertex shader keeps, measured at the camera's closest distance.)

## Visualize

**Why not just spend more vertices?** The detail setting does not help: High has more cells per tile, but the budget allows fewer tiles, and the total is the same. Doubling the budget only makes the cells 30% finer, because it is a square: and it costs memory and build time on every body. A mesh that follows the camera (fine cells near you, coarse far away) would fix it properly; it is a bigger change and is on the roadmap.

**The idea: a big sea is made of big waves.** That is also true of real water: waves grow with the distance they have run. Scaling a sea by s multiplies every wavelength AND every height by s, so every wave keeps its steepness: the same sea, seen from further away. `WaterWaves::ScaledBy` does that; `ResolvableScale` finds the smallest s (1 or more) at which the mesh shows at least 80% of the wave height at its finest spacing, by bisection (the share only rises as the waves get longer). A body whose mesh already shows its sea is never touched.

**Where it lives.** `WaterWaves::VertexFade` is the shader's fade rule written down once (a test pins it to the shader's numbers). `Water::RebuildWaves` finds the automatic scale for the sea as the author set it, then `CurrentWaveParams` applies it on top; `Water::FinestCell` is the cell size that `UploadMaterial` already computed. The waves are rebuilt whenever the mesh is (a size change, the Scale tool, a new detail setting), because the cell size moves with it.

**Two controls.**
- **Scale** (a slider in Waves, 0.25x to 8x): the whole sea bigger or smaller, for artists. It is part of the sea the author dialled in.
- **Scale waves to the water's size** (a switch, on by default): the automatic part. When it is doing something, the inspector says so ("Drawn 2.1x bigger to fit this size") and explains why in a tooltip, so the amplitude you dialled in never silently differs from what you see. Performance shows the finest cell, so you can see the number behind it.

**Rivers are left alone.** A river's look comes from its current, and its grid is long and thin, not coarse.

## What this changes for existing scenes

A scene saved before this loads with a Scale of 1 and the automatic scale ON, so a big body's waves are no longer flattened. A body under about 1000 units looks the same as before. If you want the old look back, turn the switch off.

## What this does NOT do

**The sea is not the same in every detail.** A scaled sea is bigger, not the same sea: Stormy on a 2000-unit body has waves 7 to 91 units long with the tallest at about 1.7 units, instead of 3 to 43 units and 0.8. It looks like a bigger sea, which is what it is. If you want small waves on a big body, you need the mesh to follow the camera.

**Distance still fades waves.** The shader also coarsens its spacing with distance from the camera (the LOD), so far waves fade out by design. This lesson is about the near field, which was wrong at large sizes.

**The shading normal still sees every wave.** Only the geometry fades; lighting uses the full table, so small waves still show as shading. (Roadmap C will make what fades turn into roughness.)

## How it was checked

`tests/water_test.cpp`: the fade rule at its three anchor points; a scaled sea keeps every wave's steepness (spectrum and classic); coarser cells never need LESS scale; the scale found is the smallest that does the job; a hopeless case is capped, not infinite; and, on the real tile grid, a 2000-unit sea loses more than 40% of its height without the scale and shows at least 80% with it, while a 100-unit sea is untouched. `tests/water_settings_test.cpp`: the new settings clamp, belong to the Waves section (reset, the changed-mark, presets) and the real Calm, Choppy and Stormy presets at four sizes. Each of the three ways I could think of to get this wrong (the wrong fade rule, scaling the wrong way, scaling lengths but not heights) is caught by a failing test.

## Questions before you read the code

1. Why does the amplitude cancel out of `ShownShare` (what is it a share OF), and why does that make the bisection safe?
2. Why is the automatic scale found for the sea WITHOUT the automatic scale, and then applied?
3. Why does `SetEditorTransform` call `RebuildWaves` after `EnsureMesh` when the grid did not change?
4. A body at size 1000 gets a scale of 1.05x. Is that worth having? What would a minimum threshold cost?
