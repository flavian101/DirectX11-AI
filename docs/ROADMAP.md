# Terrain, World, Water and Scatter: roadmap and source of truth

This is the **only** planning document for the terrain, the world, the water and the scatter system. It replaces
`terrain-world-design.md`, `terrain-world-roadmap.md`, `water-roadmap.md` and `scatter-ecosystem-design.md` (still readable in git
history at commit `f9e0401`, e.g. `git show f9e0401:docs/water-roadmap.md`). `docs/lessons/` is history: it explains how
things were built and is never updated to match the plan.

**Rules for this file**
1. A change that finishes, starts or reorders an item edits this file in the same commit.
2. Every item has an ID (`T2`, `W1`...). Code comments and commit messages refer to IDs, never to phase numbers.
3. An item is done only when its **acceptance** line holds and its tests exist in `tests/`.
4. Status: `[x]` done, `[~]` written and unit-tested but not yet checked in the editor, `[ ]` open, `[!]` known bug.
5. Facts about terrain and water were read from the code at commit `f9e0401`; facts about scatter and biomes (section 1 and M5) at commit `848a23f`.
   Nothing here was measured on a running build unless it says so.

---

## 1. Where we are

### Units and words
One world unit is one metre. A height is stored 0..1 and multiplied by `heightScale` (metres). The ground is a function of the world
position: **source -> shape -> edits -> rivers**.

| Word | Meaning |
|---|---|
| **Chunk** | 32 x 32 cells of mesh (`kTerrainChunkCells`). The unit of drawing: one draw, one bounding box, one LOD choice. |
| **Tile** | `chunksPerTile` x chunk = 8 x 32 = 256 cells; at the default 4 m per cell, 1024 m. The unit of data and streaming. Neighbours share an edge row. |
| **World** | A grid of tiles (`TerrainWorld`), endless (fractal) or bounded (picture). Only tiles near the camera are alive (max 256). |
| **Biome** | Overlapping weights on the ground (what lives here). Data, not geometry. Recipes drive scatter first, then ground layers and mood. **Not in the code yet**: today the four texture layers stand in for it (S4). |

Three layers sit on top of each other: **shape** (the heightfield), **surface** (layers, biome id), **things** (scatter, buildings).
Chunks and tiles cut across all three; ecosystems are never chunks.

### Terrain and world

| Area | State | Where |
|---|---|---|
| Heightfield, brushes (raise, lower, smooth, flatten), undo by patches | `[x]` single terrain | `Heightfield.*`, `HeightfieldEdit.*` |
| Chunks, 3 LOD levels, skirts, partial vertex refresh | `[x]` single terrain | `Terrain.cpp` (`BuildChunkVertices`, `RefreshChunks`) |
| Tiles, streaming, tile LOD (1, 1/4, 1/16, 1/64), apron for seamless normals | `[x]` | `WorldGrid.*`, `TerrainWorld.*` |
| Sources: fractal, picture (8/16-bit), bounded worlds | `[~]` | `WorldPicture.*`, `TerrainWorld.h` |
| Shape: levels, curve, invert, smoothing, bicubic, ridged detail noise in metres | `[~]` | `WorldShape.*` |
| Edits per tile (height deltas, 4 paint weights), `.worldedits` file, undo | `[~]` | `WorldEdits.*` |
| Migration of an old terrain into a world | `[~]` | `TerrainWorld::AdoptFrom` |
| Rivers carved into the world height function | `[x]` | lesson 51 |
| Layers: 4-layer splat, slope/height rules, biplanar rock, height blending | `[x]` | `TerrainSplat.*`, `terrain_ps.hlsl` |
| Reversed-Z, 10 km far plane; sun shadow box follows the camera (400 m) in a world | `[x]` | lessons 46, 49 |
| Sculpting on a world | `[~]` works, but **slow** (see T2) | `TerrainWorld::SculptStep` |
| `World::HeightAt(x, z)` for collision | `[ ]` | |
| Camera-relative rendering, cascaded shadows, tile building off the main thread | `[ ]` | |

Defaults worth knowing: world `heightScale` 240 m, `metresPerCell` 4 (allowed 0.25 to 32), `pictureWidth` 1024 m, `viewDistance`
5000 m. The single `Terrain` is limited to 40 to 1000 m wide and 1024 cells, heightScale 45 m. The single `Terrain` is the legacy
path: it is deleted at **M2** once the world covers every row of the table above.

### Water

| Area | State | Where |
|---|---|---|
| Gerstner waves, ripples, shoaling (waves shrink to 0 at the waterline) | `[x]` | `WaterWaves.h`, `waves.hlsli` |
| Shore depth map baked on the CPU from the terrains; foam, swash, refraction, Beer-Lambert absorption | `[x]` | `WaterShore.h`, `Water::RefreshShoreDepth` |
| Rivers: drawn curve, flow map, carve, found-downhill path, water level along the curve, only-the-river-is-water clipping | `[x]` | lessons 38 to 41, 45 |
| Selection, inspector (13 sections), presets, scene wind | `[x]` | lessons 42, 43 |
| Waves scale with a big body | `[x]` | lesson 44 |
| Body limits: size 2 to 2000 m, depth map capped near 131,072 texels (about 360 x 360), "no ground here" = 100 m deep | fact | `WaterSettings.h`, `WaterShore.h` |
| Water measures the highest ground of every **loaded** tile | `[x]` | `Water::RefreshShoreDepth` |

### Scatter and biomes

| Area | State | Where |
|---|---|---|
| Placement: a lattice of squares, one hashed candidate each, a vote of the ground, Matern thinning. A pure function of (rules, ground, square index) | `[x]` tested | `ScatterPlacement.*`, `scatter_placement_test.cpp` |
| Cells (32 m): regenerated when dirty, nearest first, within a time budget; per-cell frustum cull and 3 levels of detail; merged runs, one draw per variant and level; 32-byte GPU instance | `[x]` single terrain | `ScatterObject.cpp` |
| Variants (up to 8 per species): weight, layer bias, slope band, material overrides | `[x]` | lesson 36 |
| Shadows (distance cut, one level coarser) and a cheaper pass for the water mirror | `[x]` | `ScatterObject::Draw` |
| Ground: the **first `Terrain` only**. Nothing grows on a world's tiles | `[!]` **S12** | `Scene::Update` calls `Refresh(GetTerrain(), ...)` |
| Biomes | `[ ]` **S4**: `grep -ri biome src editor` finds nothing. `layerDensity[4]` and `layerBias[4]` (grass, rock, sand, snow) stand in | |
| Knows about water | `[ ]` **S13**: `ScatterGround` holds a heightfield, a splat map and a transform, nothing else | |

Sizes: `ScatterObject.cpp` is about 750 lines (the split is in S12); the scatter inspector is `editor/ScatterInspector.cpp` (299 lines, moved out of `EditorLayer.cpp` in S11).

---

## 2. Known problems (the work queue starts here)

| ID | Problem | Cause (read in the code) | Fix |
|---|---|---|---|
| T1 `[~]` | Cliffs look like stairs and vertical streaks; flat pads | A 2048 px picture over 1024 m is 0.5 m/px, sampled every 4 m: about 8 px per cell, **no prefilter**, so thin escarpments alias. `smoothing` defaults to 0. About 13% of the demo picture's pixels sit at the minimum value (flat floor). | T1 below |
| T2 `[!]` | Sculpting a world is slow | `SculptStep` marks tiles stale; `Stream()` builds a **new `Terrain` per touched tile per step** (heights for 257² + apron through the whole source chain, 64 chunks of vertices and index buffers, splat bake). The single-terrain path already patches chunks in place, but `Terrain::Sculpt` returns false for tiles. | T2 below |
| T3 `[ ]` | Every chunk builds its own identical index buffers (3 LODs plus skirts) | `BuildTerrainModel` | T3 below |
| W1 `[!]` | **Ocean is drawn under / through terrain that is below the shore level** | Not yet reproduced here (I could not run the engine). Ranked suspects in W1 below. | W1 below |
| W2 `[ ]` | The sea is one plane, at most 2000 m, with a coarse CPU depth map, and it has no idea about tiles that are not loaded | `Water`, `WaterShore::Bake` | W2 below |
| W3 `[ ]` | Rivers are one plane up to 2000 m (a found river is capped at 1500 m); no confluences or lake outlets | lesson 45 | W4 below |
| X1 `[ ]` | The bounded world is a floating slab: raw edges, dark void | no backdrop | X1 below |
| S12 `[!]` | Nothing grows on a world's tiles | Scatter has one grid over one `Terrain`'s extent, takes a `const Terrain*`, and tracks changes with that terrain's revision log | S12 below |
| S4 `[ ]` | No biomes: a texture layer is the only way to say "where" | `layerDensity[4]` and `layerBias[4]` are tied to the 4 splat channels; a forest and a swamp on the same grass cannot differ; more than 4 layers (L2) would break the rules | S4 below |
| S11 `[x]` | Dead and duplicate code in scatter | see S11 for the list, each item read in the code | S11 below |
| S13 `[ ]` | Scatter does not know about water | `ScatterGround` has no water input | S13 below |

---

## 3. Milestones

Order is by what you see and what unblocks the rest. Items inside a milestone can be done in any order unless a dependency is named.

### M1. Make what exists look and feel right

**T1. Prefilter the picture; show pixels per cell** `[~]` (lesson 54: written and unit-tested; **to check in the editor**: the demo picture's escarpments)
- `[x]` `PicturePixelsPerCell(metresPerCell, pictureWidth, imageWidth)`. The world inspector shows it under the picture controls and warns in orange above 2
  when the prefilter is off. (A world's pixel spacing is `width / (pixels - 1)`: pixel 0 and the last pixel sit on the two edges.)
- `[x]` A mip pyramid of the `HeightImage`, built once per picture (`BuildHeightPyramid`; each level is a low-pass of the one before, about half as wide, same area).
  `PictureHeightSource` reads one fixed level, `log2(pixelsPerCell)`, blended between the two levels around it. The level is fixed per world, so the height stays
  a pure function of the world position and tiles at every level of detail still agree.
- `[x]` New setting `HeightShapeSettings::prefilter` (default **on**; checkbox "Prefilter the picture"). It does nothing when a cell covers one pixel or less.
  A world migrated from an old terrain gets it **off**, so it still matches the old terrain sample for sample.
- The older `smoothing` setting (a blur in pixels) is unchanged and still useful for the steps of an 8-bit picture; prefilter handles thin features.
- Acceptance: `world_picture_test.cpp` `TestPrefilter`: a 1 px ridge at 8 px per cell changes by 0.062 over half a cell instead of 1.000; a footprint of one pixel
  or less equals the unfiltered source exactly; a step edge stays in range and only rises. Still open: the demo picture shows no stair terraces on its
  escarpments (editor). Opening an existing world, the shape changes a little because `prefilter` is on by default; saved edits are deltas and stay.

**T2. Sculpt a world without rebuilding tiles** `[ ]`
- Add `Terrain::PatchTile(rect, heightAt)`: write only the dirty rectangle with `Heightfield::SetSample`, then call the existing
  `RefreshChunks(rect)` and `RefreshSplat(...)`. The tile keeps its `Terrain` object.
- `TerrainWorld` keeps a dirty rectangle per tile. During a stroke tiles are patched, not rebuilt. A full rebuild happens when the stroke
  ends, when the tile's LOD changes, or when the world's settings change.
- Keep the apron correct when the rectangle touches a tile border (the neighbour's samples are read for normals).
- Add `QueryPerformanceCounter` timers around `GenerateTileHeights`, `BuildTerrainModel` and the splat bake, shown in the world inspector.
- Acceptance: no `scene.Add<Terrain>` during a stroke; a patched tile equals a freshly built tile sample for sample (same style as the
  200-random-strokes test of lesson 27, in `world_edits_test.cpp`); frame time during a stroke is reported by the timers.

**T3. Shared index buffers** `[ ]`
- Build one index set per (chunk size, LOD) and share it between all chunks of all tiles. Only vertex buffers differ per chunk.
- Acceptance: creating a tile creates vertex buffers only.

**W1. The ocean must not show where it should not** `[!]`
The rule (the **water-terrain contract**, section 5): where ground is below the sea level the surface is visible above the ground;
where ground is above sea level plus the wave margin there is no water pixel; where the world has no ground, no deep ocean appears over
land that exists in the world.
Suspects, most likely first. Each one has a check that tells it from the others:
1. **LOD mismatch.** The depth map is baked from the heightfield (true heights) but the screen shows the *coarser LOD mesh*. A coarse tile
   (1/4, 1/16, 1/64 of the cells) cuts across peaks and ridges, so a ridge that is above the sea in the heightfield can dip below the sea
   plane in the mesh. Check: set `viewDistance` small, or force every tile to LOD 0. If the artefact vanishes, this is it.
2. **"No ground" means 100 m deep.** `Bake` fills every texel with no loaded terrain with `kDeepWater`. Tiles that have not streamed in yet,
   tiles outside the bounded world, and the gaps while a tile is replaced all read as deep ocean. Check: select the water body and use
   Advanced > Debug > Shore depth; magenta or very blue where land will appear.
3. **Stale or coarse depth map.** The map is about 360 x 360 texels, rebuilt when the set of loaded tiles changes. `UpdateDryTiles` removes
   water tiles whose ground is above the level using that same map; a narrow valley under the level can be dropped (water missing) while a
   stale map elsewhere shows water that should be dry. Check: toggle the body's transform or sculpt to force a re-bake.
4. **Skirts.** The skirt under a tile edge hangs `max(1.5, 0.06 x heightScale)` m (14.4 m at 240 m) and is in the depth buffer the shader
   measures `thickness` against. Check: wireframe, and the water's foam band along tile seams.
- Fix: bake the depth map from the world's **ground function** (what tiles are built from: source -> shape -> edits -> rivers), not from
  whichever tile meshes happen to be loaded. That removes 2 and, with a conservative margin, 3. For 1, keep the heightfield and the mesh
  consistent near the shore: keep tiles whose bounds cross the sea level at LOD 0 or 1, or lower the coarse mesh's vertices at the shore
  (skirt-style, never above the true height).
- Acceptance: a pure-logic test `water_visibility_test.cpp` samples a grid of points and asserts the contract (below level -> water, above
  level + margin -> none, no ground inside the bounds -> not deep ocean). I cannot confirm the root cause without running the editor; step 1
  of the fix is to run the four checks above and record which one reproduces it here.

**X1. A world edge that does not look like a slab** `[ ]`
- Close the bounded world with skirts down to a floor below the lowest ground, or fade it into fog and a backdrop. Decide once and write it here.
- Acceptance: from the demo camera, no black void is visible below or beside the terrain.

### M2. Finish the world, then delete the single `Terrain`

The old terrain is deleted only when every row is `[x]`. Check each row against the current `Terrain` code before ticking it.

| Row | Needed | State |
|---|---|---|
| Heightmap picture | as a source | `[~]` |
| Noise on the heightmap | as detail noise in metres | `[~]` |
| Material and layer images | `customMaterial` on migrate | `[~]` |
| Migration of an old scene | *Add > Terrain world from this terrain* | `[~]` |
| Chunking and LOD | per tile, LOD across tile borders | `[x]` |
| Automatic texturing | per tile, world noise, no seams | `[x]` |
| Splat painting | per tile, seamless | `[~]` |
| Sculpt brushes | across tiles (and fast: T2) | `[~]` |
| Undo and redo | touched lattice points | `[~]` |
| **Collision and height queries** | `World::HeightAt(x, z)` across tiles | `[ ]` **C1** |
| **Water reads world ground** | via the ground function (W1) | `[~]` |
| **Scatter over every tile** | deterministic per tile, seed from the tile id | `[ ]` **C2 = S12** |
| Save and load | settings in the scene, edits in `.worldedits` | `[~]` |

- **C1** `World::HeightAt` (ground function, plus an exact tile raycast for picking). Acceptance: matches `Terrain::GetHeightAt` on a one-tile world.
- **C2** Scatter over every tile. The plan lives in **S12** only; this line is a pointer.
- **C3** Run every demo scene through migration, then delete `Scene::GetTerrain()` callers, `Terrain::CarveRiver`, and heightmap-only settings.
- **C4** Build tiles off the main thread; upload on the main thread with a per-frame budget.
- **C5** Camera-relative rendering (tile index + offset, subtract the camera before the GPU); cascaded shadow maps.
- **C6** Tile file format with a version number: header, R16 heights, RGBA8 splat, optional hole mask.

### M3. Terrain that looks like terrain

**G1. Generators as source filters** `[ ]`
- Ridged multifractal and domain-warped noise as sources; terracing as a *chosen* effect (not an accident); thermal and hydraulic erosion as a
  compute-shader pass over a tile (with its apron), producing height plus **flow and sediment maps**.
- Flow and sediment maps are reused for river paths (W4), swamp masks (W3) and texturing (L2).
- Acceptance: erosion is deterministic per tile and equal across tile borders (test with the apron).

**L1. Cliffs with bulging rock** `[ ]`
A heightmap holds one height per position, so rock that bulges is geometry, not height.
- Rock and cliff meshes scattered on steep slopes (a slope rule on a species; `ScatterObject` already exists, so a cliff species is data, not a new system; needs **S12** to work on a world), blended into the terrain material.
- Displacement (parallax or tessellation) on the near rock layer, using the height channel already packed in `diffHeight`.
- Break the slope threshold with noise so cliff edges are not straight lines.
- Acceptance: a slope above the rock threshold shows scattered rock meshes and relief on the rock layer in the editor.

**L2. Texturing** `[ ]`
- Splat weights from height, slope, moisture (flow map), and biome; more than 4 layers (a second map, or a texture array with per-tile layer ids).
- Detail normal and a macro-variation texture so tiling does not show.
- Hole mask per tile (caves, tunnel mouths, dock cut-outs).

**L3. Heights on the GPU** `[ ]` (only when the profiler asks for it)
- Heights in an `R32_FLOAT` (or `R16_UNORM`) texture, brush as a compute shader over the brush's bounding box, vertex shader displaces from
  the texture, normals from the texture. The CPU copy stays for picking, collision and undo.

### M4. Water that belongs to the world

**W2. The sea is a property of the world** `[ ]`
- `TerrainWorldSettings::seaLevel` (metres) and an enable flag. One sea per world, not a body you size by hand.
- Step 1 (cheap, depends on W1): keep the current plane, bake depth from the ground function, cover the world bounds.
- Step 2: a camera-following clipmap mesh (fine rings near, coarse far). This also fixes the open item "waves lose detail on a big body"
  and removes the 2000 m limit. Depth comes from a coarse always-resident height texture plus the fine tiles near the camera, read in the
  shader, so there is no CPU bake.
- Acceptance: the water-terrain contract (section 5) holds with tiles loading and unloading, and at the world edge.

**W3. Lakes and swamps** `[ ]`
- Lakes: find basins in the ground function (flood fill from a seed below a level), fit a body to the basin; never one big plane at one level.
- Swamps: the **swamp biome** is defined in **S4** (the rule lives there only). The water side is a **shallow-water** body type: water 0.1 to 0.5 m
  over the ground, calm waves, dark mud layer, ground fog. Reeds and dead trees are species of the swamp biome (S4), placed in shallow water by **S13**.
- Acceptance: a swamp biome painted or ruled on flat low ground shows standing water in many shallow patches, not a lake.

**W4. Rivers at world scale** `[ ]`
- A river is a list of per-tile segments that share the curve (this revisits the old "no ribbon mesh" decision, because the 2000 m plane cap
  no longer fits a world).
- Confluences, lake outlets, found rivers from flow accumulation (G1).
- **Waterfalls and drops** (old 20d): a point marked as a drop gives a height jump over a short run: falling sheet with its own shading,
  plunge-pool foam, mist (needs particles), a current that speeds up with the slope.

**W5. Shading and effects** `[ ]` (each stands alone, in this order)
1. **Shading v2**, a Classic / Physical switch: body colour as scattered light in a volume, per-channel absorption along the real line of
   sight, roughness from waves driving a GGX sun highlight, Fresnel, view-space refraction, shadows on water, caustics.
2. **Foam with memory**: a camera-centred ping-pong foam texture that decays and is advected by the flow map; replaces the analytic trail.
3. **Wakes and objects**: boats stamp foam and V-ripples; rocks in a river make standing waves and foam.
4. **Second light**: the moon (`dirDirection2`, `dirColor2`) on the water.
5. **Shore on land**: wet sand band, foam residue, darker wet rock.
6. **Reflection**: mip chain by roughness, edge and distance fade, mean-level mirror on a river that falls.
- Later: FFT ocean, bloom on the sun highlight, hover outline, ice and frozen rivers.

### M5. Scatter and ecosystems: revamp

**What stays** (read in the code, and tested): placement is a pure function of (rules, ground, square index), with random numbers from a hash of
the square, so any cell can be made alone, in any order, on any thread, and cells never show seams. The cell grid with per-cell culling and
levels of detail, the variant-major instance buffer with merged runs, and the 32-byte GPU instance also stay. Every item below changes what
feeds these parts or what wraps them, not how they work.

**What is wrong** (read at commit `848a23f`, none of it measured):
1. **One ground.** `Scene::Update` calls `ScatterObject::Refresh(const Terrain*, ...)` with the first terrain. The grid, the dirty tracking
   (`Terrain::GetSurfaceChangesSince`) and the one instance buffer per species all assume one terrain of a fixed size. A streaming world has none of that (**S12**).
2. **No biome.** The word is in the docs and not in the code. "Where may a pine grow" is answered by the four texture layers, so a forest and a
   swamp cannot differ on the same grass, and the rules are sized by `kSplatChannels` (**S4**).
3. **One rule, six files (fixed in S11).** A rule such as `clusterScale` is declared in `ScatterSpeciesSettings` and again in `ScatterRules`, copied in
   `MakeBaseRules`, clamped by borrowing in `ScatterSpeciesSettings::Clamp`, saved in `SceneJson.h` and drawn in `EditorLayer.cpp`. Adding a rule is a hunt (**S11**).
4. **Dead or duplicate (fixed in S11).** `ScatterObject::GenerateAllNow` has no caller (not the editor, not a test). `ScatterRules::modelHeight` (the species-level one)
   is never filled from the settings, so it is always 1.0. `ScatterSpeciesSettings::model` plus `variants` exist twice only so that lesson-31 scene
   files load (`EffectiveVariants`). (**S11**)
5. **Fragile (fixed in S11).** A change in the rules is detected with `memcmp` over `ScatterRules`. It works only because every member is 4 bytes; one `bool` added
   later makes it wrong without any warning (**S11**).
6. **Too much in two files (inspector moved in S11; `ScatterObject.cpp` is split in S12).** `ScatterObject.cpp` does five jobs (model cache, material loading, cell scheduling, upload, culling and drawing) and the
   inspector sits inside the 4805-line `EditorLayer.cpp` (**S12** and **S11**).
7. **Blind to water.** Nothing stops a species that likes the ground from growing under the sea or in a river bed (**S13**).

**Order:** S11, S12, S4, S13, S6, S5, S7. S9, S10 and S14 only when a profiler or a screenshot asks for them.

| ID | Item | Risk | State |
|---|---|---|---|
| S1 | Variants per species (mesh, materials, weight, layer bias, slope band) | low | `[x]` lesson 36 |
| S2 | Built-in rocks and a starter scatter | very low | `[x]` |
| S3 | ~~Groups with inherited defaults~~ | | dropped: a biome recipe (S4) lists its species, which is the same job. Revisit only if the species list gets long in practice |
| S11 | Clean-up before anything new (no behaviour change) | low | `[x]` lessons 52, 53; checked in the editor by the author |
| S12 | One ground interface and cells per tile (this is C2) | medium-high | `[ ]` |
| S4 | Biomes as data, rule-based | low-medium | `[ ]` |
| S13 | Water depth and exclusion masks (roads, buildings) | medium | `[ ]` |
| S6 | Painted `BiomeMap` (a second texture beside the splat map) | medium | `[ ]` |
| S5 | Density-mask brush (paint and erase per species) | medium | `[ ]` |
| S7 | Per-variant radius; share identical textures between variants | low-medium | `[ ]` |
| S8 | Biome transitions | none | free with S4 (overlapping weights); exclusion moved to S13 |
| S9 | Direct instance painting (hero objects) | high | `[ ]` |
| S10 | GPU-driven culling, texture-array variants | high | `[ ]` only if the profiler shows draw calls or CPU culling |
| S14 | Far distance: density fade or billboards beyond the last mesh level | medium | `[ ]` proposal, only if a screenshot or the profiler asks (today a species has 3 levels, then it is culled by `cullScreen`) |

**S11. Clean-up before anything new** `[x]` (lessons 52 and 53; the author applied it and reports that it works in the editor)
- `[x]` Delete `ScatterObject::GenerateAllNow`.
- `[x]` Delete the species-level `ScatterRules::modelHeight` and its `-1` fallback. A variant's height is then always the loaded mesh's (default 1.0 for a look that cannot be drawn).
- `[x]` Declare each rule once (`ScatterSpeciesRules`, `ScatterVariantBase`, both inherited by the settings and the rules): `ScatterSpeciesSettings` holds a `ScatterRules` instead of copying about 20 fields into a second struct. `MakeBaseRules` and the
  "borrow the rules to clamp" step in `ScatterSpeciesSettings::Clamp` go away. Same for variants.
- `[x]` Always store variants. The loader turns a lesson-31 file (a `"model"` and no `"variants"`) into a one-variant list once; then `model`, `EffectiveVariants()`
  and the **Several looks...** path go. Old scene files must still load (`TestOldSceneFilesLoad` stays).
- `[x]` Replace the `memcmp` on `ScatterRules` with `operator==` (field by field, guarded by `static_assert`s on the struct sizes; `TestRulesComparison`).
- `[x]` Move the scatter inspector out of `EditorLayer.cpp` into `editor/ScatterInspector.cpp` (the split of `ScatterObject.cpp` is done inside S12, which rewrites the same code).
- Acceptance: all existing scatter tests pass. `scatter_golden_test.cpp` hashes the instances of `ScatterSettings::Starter()` over a fixed ground; the hash was
  recorded **before** the first deletion (commit `848a23f`, g++) and must not change. Hashes are per compiler: run it once on the unchanged code on your machine and use your numbers if they differ (lesson 52). Adding a rule now touches four files, not six: the struct and `operator==` (`ScatterPlacement.h`, where a forgotten `operator==` stops the build), the clamp (`ScatterPlacement.cpp`), the JSON list (`SceneJson.h`) and the inspector (`ScatterInspector.cpp`).

**S12. One ground interface and cells per tile (C2)** `[ ]`
- `SampleScatterGround` is already the only door through which placement reads the ground. Replace `ScatterGround` (a heightfield pointer, a splat pointer and a
  transform) with a small interface: `Sample(x, z)` gives a `ScatterSample` (height, normal, layer weights; later biome weights and water depth).
  Two implementations: one `Terrain` (legacy, until M2 deletes it) and the world's ground function (decision 12).
- Cells belong to tiles. A tile that streams in makes its cells, a tile that unloads drops them, and a changed tile (sculpt, paint, river) marks only its own cells
  dirty, by the tile's revision. This replaces the one grid over one terrain's extent.
- Instance buffers per tile (or per small group of tiles) instead of one per species for everything. `maxInstancesPerSpecies` becomes a limit on the loaded
  area, so the cut falls by distance, not by buffer order.
- Split `ScatterObject.cpp` into model and material cache, cell scheduling and upload, and culling, level of detail and drawing.
- Acceptance (`scatter_ground_test.cpp`): on a one-tile world the instances equal those of a single `Terrain` with the same heights and layers; the cells on both
  sides of a tile border give the union (no seam, nothing doubled); streaming a tile out and in again gives the same instances. In the editor, a forest grows on
  streamed tiles and leaves with them. Frame time on a 3 x 3 world is read from the timers: not measured.

**S4. Biomes as data, rule-based** `[ ]`
- Decision 11 (section 4): a biome is a set of overlapping weights on the ground, separate from the texture layers. A texture says how the ground looks; a biome
  says what lives and happens there.
- `ScatterSample.biome[kMaxBiomes]` (start with 8), made from height, slope, layer weights, a low-frequency noise and, later, flow and moisture (G1), by a
  **biome recipe**: plain JSON, one entry per biome with ranges and a noise scale. A pure function, tested without Direct3D.
- A species lists `biomeDensity[b]`, and `density *= sum_b(sample.biome[b] * biomeDensity[b])`. Blending between biomes is free.
- `layerDensity` and the variants' `layerBias` are **removed** (decided by the author). A species uses `biomeDensity[b]`; a variant uses `biomeBias[b]`.
  The recipe may read the splat weights as one input; a species never does.
- Migration of old scenes: the built-in recipe has four ground biomes (Grass, Rock, Sand, Snow) whose weights equal the splat weights. The loader turns an
  old `layerDensity[k]` into the `biomeDensity` of the matching ground biome, so an old scene generates the same instances.
- Starter biomes: Forest, Meadow, Rocky, Swamp. The swamp rule (low altitude within a small height of the water level, low slope, high flow accumulation) is
  defined here; W3 uses it.
- A recipe may also name ground layers and mood (fog, ambient). Do the scatter part first; the rest when L2 and the Environment weather need it.
- A town is hand-placed objects on a flattened foundation: a *place*, not a biome.
- Acceptance (`biome_test.cpp`): weights are deterministic, equal across tile borders and sum to a stated total; a forest and a swamp on the same grass layer grow
  different species; a scene saved before biomes loads, is migrated, and generates the same instances as before (the S11 golden hash).

**S13. Water depth and exclusion** `[ ]`
- `ScatterSample.waterDepth`: metres of water over the ground, from the world's ground function, the sea level (W2) and river corridors (W4).
- A species gets `minWaterDepth` and `maxWaterDepth`. The default is dry ground only; reeds ask for 0 to 0.5 m.
- Exclusion masks (roads, buildings, foundations): rectangles or polygons that remove candidates. Still a pure function of position.
- Acceptance (`scatter_water_test.cpp`): with default rules nothing is placed where `waterDepth > 0`; contract 4 of section 5 holds with scatter on.

**S6. Painted `BiomeMap`** `[ ]`: a second texture beside the splat map (a PNG beside the world edits), painted with the existing paint tool; where painted it
overrides the recipe's weights. Undone by patch like paint. Acceptance: a stroke regenerates only the dirty cells; save and load round-trip.

**S5. Density-mask brush** `[ ]`: paint and erase per species, reusing the S6 storage, the paint tool, undo and dirty-cell regeneration.

**S7. Per-variant radius** `[ ]`: today there is one `minDistance` per species, so a huge boulder can stand closer to a neighbour than looks right. A per-variant
radius feeds the thinning. Identical textures are shared between variants.

Known limits left on purpose: the instanced draw has no GPU test; S10 and S14 wait for evidence.

---

## 4. Decisions that stay (change them only here, with a reason)

1. **Terrain is axis-aligned.** Rotation is locked; size comes from data (cells x metres per cell). Physics height-field colliders are axis-aligned too.
2. **Tiles share edge rows** (257 samples for 256 cells) so there is no seam and normals can read the neighbour. Tile LOD uses skirts to hide cracks.
3. **The ground is a function of the world position.** Everything that needs the ground (tiles, water, scatter, rivers) asks that function, not a loaded mesh.
4. **Edits are deltas over the source**, stored per tile, so changing the source does not destroy them. A sample belongs to one tile.
5. **Chunks are a performance tool; ecosystems are content.** Biomes are data the placement samples.
6. **Large worlds are camera-relative**: store tile + offset, subtract the camera before sending to the GPU (floats lose millimetres by 10 km).
7. **A heightmap cannot make overhangs.** Overhangs and bulging rock are meshes; holes are a mask.
8. **Heights are 16-bit or float everywhere**, never 8-bit.
9. **Width and height scale are independent and absolute.** Make a terrain 4x wider at the same height scale and every slope is 4x gentler.
10. **Placement stays a pure function; the instance stays 32 bytes** (scatter).
11. **A biome is data on the ground, separate from the texture layers.** Texture layers say how the ground looks, biome weights say what lives there. Once S4 is done, plants never read the texture layers directly; only the biome recipe may.
12. **Scatter reads the ground through one interface**, the world's ground function (decision 3), never a loaded mesh and never a `Terrain*` (S12).

---

## 5. The water-terrain contract

These must hold at all times and are what `water_visibility_test.cpp` checks.

1. **Below the sea level, water is visible above the ground.** `ground < seaLevel` implies a water pixel above the ground (waves may rise by the wave margin).
2. **Above it, there is none.** `ground > seaLevel + margin` implies no water pixel, and no water *tile* is drawn there (`UpdateDryTiles` agrees with the pixels).
3. **No ground never means deep ocean inside the world.** Unloaded tiles use the ground function (or a coarse resident copy), not "100 m deep".
4. **The depth the water uses equals the ground the player sees**, at any LOD, up to the margin. A coarse mesh never leaves the sea visible over land the heightfield says is dry.
5. **A re-bake never changes what a body means**: moving, turning or scaling a body does not move the shoreline in the world.
6. **Sculpting and river carving update the shoreline** when the stroke ends (during a stroke it may lag).
7. **A river is only water inside its corridor**, and its level never rises downstream unless it is a drop.

---

## 6. Verification

- Pure logic (no Direct3D) goes in `tests/`; every item above names the test that proves it.
- Rendering items are checked in the editor with the named debug view, and the result is written under the item (state, date, what was seen).
- Performance items name a timer, and the number is written under the item once measured. Until then it is "not measured".
- A `[~]` item becomes `[x]` only after someone has run it in the editor.

---

## 7. Appendix

### What the lessons already built
Lessons 24 to 51 in `docs/lessons/` cover Gerstner water and tiles (24), the heightfield (25), many water bodies (26), sculpting (27), the
shore depth map (28), baked terrain weights (29), painting layers (30), scatter culling and LOD (31), water cost and GPU timer (32), boats
and the hull mask (33), a better sea (34), shores, wind and water types (35), scatter variants (36), foam and flowing rivers (37), rivers
as curves (38), drawing a river (39), only-the-river-is-water (40), a river that falls (41), selecting water (42), the water inspector
(43), waves that scale (44), rivers that find their way (45), reversed-Z (46), the world as tiles (47), tiles on screen (48), streaming
(49), distant tiles (50) and rivers in the world (51).

### Old names to new IDs (for comments and commit messages written before this file)
| Old reference | Now |
|---|---|
| "phase 1" (sources, bounded worlds), "heightmap customisation" | section 1, `[~]` rows |
| "phase 3 and 4" (edits, migration) | section 1 `[~]`; scatter and removal of the old terrain are C2, C3 |
| "7b / 7c / 7d / 7e" (world grid, tiles, streaming, camera-relative) | section 1 `[x]`; 7e is C5 |
| water roadmap 15, 20a to 20c | `[x]` (section 1) |
| water roadmap 20d | W4 |
| water roadmap 16, 17, 18, 19; polish C, D, E | W5 (1 to 6) |
| scatter design phases 1 to 10 | S1 to S10, revised in M5 (S3 dropped, S8 split between S4 and S13) |
| "scatter over every tile" (C2) | S12 |
| "terrain generators: ridged noise, terracing, erosion" | G1 |
