# Lesson 30 - Painting layers

## Understand

**Rules answer "what is this ground made of?" from height and slope.** Lesson 29 bakes those answers into a splat map: four weights per texel (grass, rock, sand, snow) that add up to 1, which the shader reads. An artist wants to overrule the rules: a sand path here, a rock patch there. Painting does not replace the rules. It adds to them.

**The painted map.** Each texel gets four more numbers: how much of each layer was *painted* there. Their sum is the texel's **coverage** `c`: 0 means untouched, 1 means fully painted. The final weights are

```
final = baked * (1 - c) + painted
```

**Why that adds up to 1.** The baked weights add up to 1, so `baked * (1 - c)` adds up to `1 - c`. The painted weights add up to `c`. Together: `(1 - c) + c = 1`. Untouched ground keeps the rules, fully painted ground ignores them, and erasing lowers `c` until the rules come back.

**The shader never changes.** Lesson 29 already said it: painting is a second set of weights that overrides the baked ones *before the upload*. `ApplyPaint` runs right after `BakeSplat`, on the same block of texels, and the existing upload sends it to the GPU.

## Visualize

**Try it.** Open the editor, press `4` (or the *Brush* button), choose *Paint layers*, pick *Sand* and drag on the terrain. The ring is blue for paint, yellow for shape. Hold Shift to erase while you drag, or tick *Erase*. Turn on *Show layer weights* (lesson 29) to see exactly what the shader receives.

**Mix it with sculpting.** Paint a path, then switch back to *Shape* and sculpt a hill across it. The paint stays. Change a rule slider: painted ground keeps its look, unpainted ground follows the new rule. Undo and redo each stroke.

**Save and look at the file.** Next to the scene file appears `yourscene.paint.png`. Open it in an image viewer: red is grass, green is rock, blue is sand, alpha is snow, and every painted pixel is a tiny bit of your stroke.

## Why it is built this way

**One texel per cell, like the splat.** The painted map has the same grid as the baked one, so the overlay is texel-for-texel with no resampling. At the default size that is about half a metre per texel.

**Floats in memory, bytes on disk.** A gentle brush adds a tiny amount per frame, and a byte would round that to zero and stall. So the painted weights are floats while you work, and only the final splat and the saved PNG are 8-bit.

**The update rule.** For every texel under the brush, work out `t = weight * strength * 6 * seconds` (the brush weight is the same smoothstep fade as the sculpt brush). Painting moves each channel towards 1 for the chosen layer and 0 for the others by `t`. Erasing moves all four towards 0. Both keep every channel at or above 0 and the sum at or below 1. A value that falls under 0.0001 snaps to exactly 0, so an erased texel really becomes unpainted.

**The re-bake problem.** `BakeSplat` overwrites its texels with the rules. If a sculpt step or a slider re-bakes a block that holds paint and nothing puts the paint back, the paint silently vanishes. The fix is to have exactly one place do it: `RefreshSplat` bakes the block, then calls `ApplyPaint` on that same block, then uploads. Paint steps, sculpt steps and rule changes all go through it. The test checks it the hard way: after 150 random paint and sculpt steps, the live splat must equal "bake everything, then lay all the paint on" byte for byte. Skipping the overlay after a sculpt step makes that test fail.

**Undo is the same machinery as sculpting.** The stroke recorder remembers the first value of each float it changes, and the patch holds `before` and `after`. A patch index is `texel * 4 + channel`. The block that undo reports is exactly what gets re-uploaded, and the test checks it is the exact bounding box of what changed: too small would leave stale texels on screen.

**Saving.** The paint map is written as an 8-bit RGBA PNG by the same encoder as the height PNG (colour type 6 instead of 0). 8 bits is plenty for blend weights. On loading, if rounding makes a texel's bytes add up to more than 1, they are scaled back, so coverage never exceeds 1. If the image does not match the grid, it is refused with a warning.

**What locks, and why.** The painted map belongs to a grid. A different mesh resolution, or a heightmap of a different shape, is a different grid, and the paint would be lost. So while the terrain is painted, *Mesh resolution* and the heightmap are locked. Noise is not locked by painting (noise changes heights, the grid stays), but it still is by sculpting. *Clear painting* unlocks, and Undo brings the paint back.

## What this step does not do

**Painted edges follow the texel grid.** The texture is filtered when drawn, so edges are soft, but a very sharp painted line cannot be thinner than a texel.

**Four layers only.** The weights live in the four channels of an RGBA image. More layers means a second map and a shader change.

**One brush size for both targets.** Radius, strength and falloff are shared between Shape and Paint layers.

**Stale files.** *Clear painting* followed by a save leaves the old `.paint.png` on disk, unused. Nothing is deleted without being asked.

## Other ways, and the trade-offs

**A paint map sharper than the grid.** A 2048 x 2048 map on a 256-cell terrain paints finer detail, but it is its own texture with its own binding, and the shader must sample both.

**Biome ids instead of weights.** Store "which biome" per texel and let rules turn that into weights. Smaller and better for scatter later, but you lose per-layer control. This map can grow into that.

**Decals.** Paint into a screen-space decal instead of the ground. Cheap for a few marks, awkward for large areas and for saving.

## Predict, then check

1. A texel has painted weights (0, 0, 0.5, 0) and baked weights (0.6, 0.4, 0, 0). What are the final weights?
2. Why must `RefreshSplat` call `ApplyPaint` after `BakeSplat`?
3. A fresh texel, brush weight 1, strength 0.5, one step of 0.1 s: how much of the layer is painted? And after a second identical step?
4. Why are Mesh resolution and the heightmap locked while painted, but Noise is not?
5. Why floats in memory, but bytes on disk?

**Answers.** (1) Coverage is 0.5, so `baked * 0.5 + painted` = (0.3, 0.2, 0.5, 0): it adds up to 1. (2) The bake overwrote those texels with the rules; without the overlay, a sculpt step near paint would erase it. (3) `t = 0.5 * 6 * 0.1 = 0.3`, so 0.3; the second step gives `0.3 + (1 - 0.3) * 0.3 = 0.51`. (4) Resolution and a differently shaped heightmap change the grid, so the paint would not fit it; noise only changes heights. (5) Small steps need float precision to accumulate; 8 bits is enough for the finished weights.

## Rebuild cold

**Before reading `ApplyPaint` again**, write it for one texel from the formula in "Understand": clamp the coverage to 1, blend, then call `QuantiseSplat` so the bytes still add up to 255. Then build `TerrainPaintTests`: `TestOverlay` and `TestIncrementalEqualsFull` grade you.

## The tests

**`TerrainPaintTests`** covers the brush (including a stretched grid), undo patches and their exact rectangles, the overlay (every texel adds up to 255, fully painted and half painted cases, untouched texels exactly as baked), incremental versus full, erasing restoring the rules exactly, and saving. **`Png16Tests`** now also reads back 8-bit RGBA files. Beyond the tests, the editor panel was run in a headless ImGui with simulated clicks on every new widget, including the push/pop pattern from the last bug fix.
