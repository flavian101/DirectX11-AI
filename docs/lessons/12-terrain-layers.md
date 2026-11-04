# Lesson 12 - Terrain layers from height and slope

## Understand

**One texture everywhere looks like wallpaper.** Real landscapes follow rules: soil cannot cling to a cliff, so steep slopes show bare rock; the high ground is cold, so it carries snow; the strip next to water is sand. If the shader knows each pixel's *height* and *slope*, it can pick a layer by rule instead of an artist painting every area.

## Visualize

**Turn on "Show layer weights" in the terrain panel.** The terrain turns into a map: red is rock, green is snow, blue is shore sand. This is the whole idea made visible: each pixel is a mix of layers, and the weights add up to a colour recipe.

## Why it is built this way

**Slope is `1 - normal.y`.** A flat surface has a normal pointing straight up (`y = 1`, slope 0); a vertical wall has `y = 0` (slope 1). A 45-degree slope has `y = cos 45 = 0.71`, so slope 0.29. The default "rock starts at 0.25" means: from roughly 40 degrees upward.

**Weights come from `smoothstep`.** `smoothstep(a, b, x)` is 0 below `a`, 1 above `b`, and an S-curve between. `slopeFade` and `shoreWidth` are simply the width of that blend zone. The layers are then stacked with `lerp`, and the order is the priority: sand first, rock over it, snow on top. Snow is multiplied by `1 - rockW` so it never lands on a cliff.

**Noise breaks up the edges.** Without it every layer boundary is a smooth contour line, obviously computed. `ValueNoise` (now in `noise.hlsli`, shared with the water foam) jitters the height and slope we test, so edges wander.

**Triplanar mapping for the rock.** The mesh UVs are a flat top-down projection. On a steep cliff many world units of height map to almost no UV change, so the texture smears into vertical streaks. Triplanar mapping samples the texture three times, projected along X, Y and Z, and blends the samples by how much the surface faces each axis (`pow(abs(normal), 4)` sharpens the choice). It costs three samples, and only the rock layer pays it.

**One texture, several looks.** You have one terrain texture, so the layers are tinted variants: rock is desaturated and darker, sand and snow keep the texture's light and dark detail but take a new colour. The system is ready for real textures later. The tint colours are picked in sRGB and converted to linear (lesson 6) before upload.

**Its own material block.** All of this lives in `TerrainMaterial` (`b4`), using lesson 11's pattern.

## Other ways, and the trade-offs

**Painted splat maps.** An RGBA texture stores four layer weights per area, painted by an artist. Total control, but needs a paint tool and a texture the size of the terrain. A common combination is rules for the default and painted overrides on top.

**Texture arrays with height-based blending.** Each layer has its own albedo, normal and roughness, and the blend uses each texture's height detail so pebbles poke through sand instead of fading uniformly. The next visual step up.

**Biplanar mapping** (two projections instead of three) is cheaper, and **stochastic sampling** hides tiling repetition.

## Predict, then check

1. What slope value does a 45-degree cliff have?
2. Why is snow multiplied by `1 - rockW`?
3. What would you see on a steep face if the rock were sampled with the ordinary UVs?

**Answers.** (1) About 0.29, because `1 - cos 45` is `1 - 0.707`. (2) Snow cannot stay on steep faces, so rock must win there. (3) Vertical streaks: the texture stretches along the face because the UVs only change with horizontal position.
