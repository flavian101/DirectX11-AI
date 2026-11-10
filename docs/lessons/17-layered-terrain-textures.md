# Lesson 17 - Layered terrain: texture blending with normals and more

> Lesson 29 replaces how the weights are computed (baked once, not per pixel) and how many textures are read. The ideas here (weights add up to 1, height-based blending, projections) still hold.

## Understand

**Lesson 12 blended colours; this lesson blends materials.** Before, rock, sand and snow were the *same* texture, darkened or tinted. Now each layer has its own albedo, normal map and ORM map, so rock has cracks, sand has grain and snow is smooth. The *rules* that decide where each layer appears (height, slope, noise) did not change.

**Each pixel is a recipe of four layers.** The weights always add up to 1. Every kind of map (colour, normal, roughness, occlusion) is mixed with those same four weights, so a pixel that is 70 % grass and 30 % rock gets 70 / 30 of everything.

## Visualize

**Turn on "Show layer weights", then turn it off.** Red, green and blue show where rock, snow and sand take over. Now set "Normal strength" to 0 and back to 1: the lighting on grass and rock changes character. Set "Height blend" to 0 and 1 and look at a sand / rock boundary: one fades like a double exposure, the other lets the rock bumps poke through.

## Why it is built this way

**A texture array is one texture with slices.** Four layers times three kinds of map would need twelve shader slots. A `Texture2DArray` holds all four layers of one kind in a single slot; the shader reads it with `float3(u, v, slice)`. All slices must share one size, so `TextureArray` resizes any odd file to match the first one it loads.

**Missing layers become neutral, again.** Like lesson 16, a layer without a file is filled with the neutral value of its map: white colour, flat normal, medium roughness. The tint colours then decide how it looks, and the editor shows how many files were found.

**The weights are the old lerp chain, expanded.** `lerp(lerp(lerp(grass, sand, s), rock, r), snow, n)` multiplies out to four weights: grass `(1-s)(1-r)(1-n)`, sand `s(1-r)(1-n)`, rock `r(1-n)`, snow `n`. Same look, but now each weight can be used for more than colour.

**Which projection each layer uses.** Grass, sand and snow lie on flat ground, so one projection from above is enough: `uv = world xz * tiling`. Rock sits on cliffs, where that smears, so it uses three projections (triplanar) and blends them by how much the surface faces each axis. That costs nine samples for rock against three for each other layer.

**Terrain tangents are not (1, 0, 0).** For a texture projected from above, U grows along world X and V along world Z. On a slope the surface is tilted, so those directions must be *flattened onto the surface*: `axis - N * dot(N, axis)`. That tilts with the ground. We build the frame in the shader from the projection, so no tangent is stored in the vertex (`Vertex::tangent` from lesson 16 stays unused for terrain). Triplanar rock does this once per projection, then blends the three world normals.

**Height-based blending.** A plain blend fades two textures like a double exposure. Instead, each layer's weight gets its own height added (the normal map's alpha channel), the highest layer leads, and only layers within a margin of it survive. Where two layers meet, the taller bumps win. `Height blend` mixes between the plain and the sharpened weights, and a layer with almost no weight is multiplied down so it cannot pop in.

**The colour map still has a job.** `Diffuse Map.png` covers the whole terrain once. Dividing it by its average colour (the smallest mip level is that average) gives a gentle variation around 1 that multiplies the layers. That is the "colour options" slider: 0 shows pure layer textures, 1 gives full large-scale colour variation. Snow ignores it so it stays clean.

**Tints multiply.** Each layer's tint is multiplied by its texture. The stand-in sand is nearly grey, so the sand tint supplies the yellow. With a real sand photo you would usually set the tint to white.

**Roughness and occlusion are blended too.** They feed the same Blinn-Phong stand-in as lesson 16, through the shared `RoughnessToShininess` function. The IBL patch swaps the lighting model; the maps stay.

## Other ways, and the trade-offs

**Painted splat maps.** An artist paints the four weights into an RGBA texture; the rules become a default. Full control, plus a paint tool and big textures. **Skipping unused layers.** Branch on weight (with `SampleGrad`) so a grass pixel never reads rock. Fewer samples, more complex shader.

**Virtual texturing** streams unique texture detail for the whole world. Powerful and a big project. **Stochastic or hex-tiled sampling** hides the repetition that tiling creates.

## Predict, then check

1. On a 45-degree slope facing +X, which way does the projected "V" direction (world Z) point after flattening?
2. With "Height blend" at 0, what do the four weights equal?
3. Why does `macroVariation` divide by the average colour?

**Answers.** (1) Still +Z: Z is perpendicular to the slope's tilt (which is in the X-Y plane), so flattening leaves it alone. U (world X) is the one that tilts. (2) Exactly the expanded lerp-chain weights, so the result matches lesson 12's mix. (3) Without it the colour map would multiply everything by its raw brightness and darken the whole terrain; dividing makes it a variation around 1.
