# Lesson 29 - Terrain texturing, the way shipped engines do it: bake the weights, read few textures

## Understand

**Lesson 17 worked out "how much rock is here?" in the pixel shader, for every pixel, every frame.** It read the mesh normal, a noise function and three smoothsteps, then read EVERY layer's textures (18 reads, 20 with the colour map) whether the layer was visible or not. A meadow pixel paid as much as a cliff pixel.

**Shipped engines split that job in two.** Weights ("what is the ground made of here?") are *data*, worked out once: Unreal caches the landscape's layer blend in a runtime virtual texture, Frostbite computes material masks from height, slope and painted maps, and most editors export a splat map. The pixel shader then reads the weights and spends its time only on the layers that are present.

**Three problems with the old way, one fix each.**
1. *Cost.* Everything above, per pixel. Now: one weight read, and only present layers are read.
2. *The look moved with the level of detail.* The old slope came from the interpolated MESH normal, and a coarser chunk interpolates over bigger triangles, so a layer border shifted when a chunk changed level. The weights are now baked from the heightfield, which has no levels of detail.
3. *The colour map* (`Diffuse Map.png`) was made for one imported heightmap: it stopped matching the ground after the first sculpt, it cost two reads of a 2048 x 2048 texture even at influence 0 (and a 21 MB PNG to decode at start-up), and "divide by its average" is not something other engines do. **It is removed.** The idea behind it, large-scale colour variation to hide tiling, is real (engines use a low-resolution tint map, or noise at a second scale), but it should be a small map made FOR the terrain, not a picture that came with a heightmap. If you want it back, it belongs next to the splat map.

## Visualize

**Turn on "Show layer weights"** (red = rock, green = snow, blue = sand): that is now a direct view of the splat texture. Drag "Slope start" and watch the borders move: the map is re-baked (a few milliseconds). Sculpt a hill: only the block under the brush is re-baked and uploaded.

## Why it is built this way

**One texel per grid cell.** `TerrainSplat.cpp` runs the lesson 12 rules over the heightfield (average of the four corner normals, the cell's height, two octaves of noise read in the terrain's own space) and writes four bytes that add up to exactly 255. Pure C++ with a unit test (`tests/terrain_splat_test.cpp`) that checks the weights, the rules, and that re-baking only the block a brush touched gives the same bytes as re-baking everything.

**Packed textures.** The twelve files are unchanged, but the GPU gets two arrays: albedo + height, and normal XY + occlusion + roughness (the normal's Z is rebuilt: a unit vector). Two reads per layer instead of three, and the height that blending needs rides along with the colour.

**Skipping layers needs `SampleGrad`.** `Sample` works out which mip to use from how the texture coordinate changes between neighbouring pixels, so it must run for the whole 2 x 2 quad together. Inside an `if` that is not guaranteed, so the shader works out the derivatives once (`ddx(p)`, `ddy(p)`) and passes them to `SampleGrad`.

**Biplanar rock.** Triplanar reads three projections; the one facing away contributes least. Pick the two best-facing axes (Inigo Quilez's *biplanar mapping*): 4 reads instead of 9 for rock.

**Normals without tangent frames.** The old code built a tilted frame per layer (`Flatten`, two `rsqrt`s each). Now the three flat layers blend their tangent normals first and tilt into world space once with the "whiteout" formula (add the tangent tilt to the surface normal's components on the texture's two axes).

**Height blend, multiplicative.** Each layer's height is multiplied by its weight, the tallest layer leads, and layers within `transition` of it stay (the form Unity's terrain uses). The old code ADDED height to weight and needed `saturate(w * 10)` to stop a zero-weight layer popping in; multiplying makes that impossible: no weight, no height.

**Reads per pixel (before -> after).** Meadow: 20 -> 3. Cliff: 20 -> 5. Meadow/cliff border: 20 -> 7. Worst case, all four layers: 20 -> 11.

## Other ways, and the trade-offs

**Painted layers.** A painted layer is a second set of weights that overrides the baked ones before the upload; the shader never changes. This is roadmap item 3. **Runtime virtual texturing** goes one step further: composite albedo, normal and roughness into a cache so the main pass reads one texture; big, and worth it for large worlds. **Block compression** (BC7 for albedo/height and normal/roughness, made offline) would cut texture bandwidth about 4x; it needs a DDS import step. **Two tilings per layer** (near and far) hides repetition at the price of reads. **Cheaper reflection pass:** the water's mirror draws the terrain with this same shader.

## Predict, then check

1. A pixel is 100 % grass. How many layer textures does the shader read? 2. You raise the terrain by 60 world units in the editor. Which rule inputs changed, and what happens? 3. Why must the splat weights add up to EXACTLY 255?

**Answers.** (1) Two (albedo + height, normal + occlusion + roughness) plus the one weight read. (2) The rules use WORLD height, so everything is re-baked; moving sideways changes nothing. (3) The shader treats them as fractions of 1: if they added up to less, the ground would be darker; more, brighter.
