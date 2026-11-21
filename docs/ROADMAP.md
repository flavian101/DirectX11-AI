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
6. A problem found while working that is not part of a milestone goes to section 8 ("Later fixes") with an ID `F<n>`, and is fixed after the milestones. It is not forgotten, and it does not interrupt the milestone.

---

## 1. Where we are

### Units and words
One world unit is one metre. A height is stored 0..1 and multiplied by `heightScale` (metres), then lifted by `baseHeight` (metres): a ground
value g stands at world Y = `baseHeight` + g x `heightScale`. **Two different numbers, easy to mix up:** `heightScale` (the editor's **Hill height**) is
how *tall* the ground is, from its lowest possible point to its highest; `baseHeight` (the editor's **Ground level**) is how *high* it starts, the world Y of
that lowest point. The ground is a function of the world position: **source -> shape -> edits -> rivers**.

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
| Where the ground starts (`baseHeight`, "Ground level"); a migrated terrain keeps its Y | `[~]` **H1** | `TerrainWorldSettings::baseHeight`, `WorldGround.*` |
| `World::HeightAt(x, z)` for collision | `[ ]` | |
| Camera-relative rendering, cascaded shadows, tile building off the main thread | `[ ]` | |

Defaults worth knowing: world `heightScale` 240 m, `baseHeight` 0 m (allowed -1000 to 1000), `metresPerCell` 4 (allowed 0.25 to 32), `pictureWidth` 1024 m, `viewDistance`
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
| T1 `[x]` | Cliffs look like stairs and vertical streaks; flat pads | A 2048 px picture over 1024 m is 0.5 m/px, sampled every 4 m: about 8 px per cell, **no prefilter**, so thin escarpments alias. `smoothing` defaults to 0. About 13% of the demo picture's pixels sit at the minimum value (flat floor). | T1 below |
| T2 `[x]` | Sculpting a world is slow | `SculptStep` marks tiles stale; `Stream()` builds a **new `Terrain` per touched tile per step** (heights for 257² + apron through the whole source chain, 64 chunks of vertices and index buffers, splat bake). The single-terrain path already patches chunks in place, but `Terrain::Sculpt` returns false for tiles. | T2 below |
| T3 `[x]` | Every chunk builds its own identical index buffers (3 LODs plus skirts) | `BuildTerrainModel` | T3 below |
| W1 `[~]` | **Ocean is drawn under / through terrain that is below the shore level** | Not reproduced here (no editor). Suspects 2 and 3 (below) were real flaws and are fixed (lesson 57); suspect 1 (coarse mesh) is measured but open. | W1 below |
| W2 `[ ]` | The sea is one plane, at most 2000 m, with a coarse CPU depth map, and it has no idea about tiles that are not loaded | `Water`, `WaterShore::Bake` | W2 below |
| W3 `[ ]` | Rivers are one plane up to 2000 m (a found river is capped at 1500 m); no confluences or lake outlets | lesson 45 | W4 below |
| X1 `[x]` | The bounded world is a floating slab: raw edges, dark void | no backdrop | X1 below |
| H1 `[~]` | **"Height" of a world is two things and only one has a control.** The slider "Height (m)" is how *tall* the hills are (`heightScale`); how *high* the ground starts (its Y) had no control, it was always 0. And **migrating an old terrain dropped its Y** (the default terrain stands at -2 m, the demo's water at 4 m): the world came out higher than the terrain by that much, against the water, the objects on it and the shore/snow rules. Not reproduced in the editor; found by reading (commit `265b50d`) | The slider had no tooltip and a one-word label. A tile was always made at `position.y = 0`; `WorldSettingsFromTerrain` copied the old terrain's X and Z and not its Y. Also: `MakeWorldMaterial` put the snow line at 0.72 x height, but `TerrainSettings::Clamp` held it to 200 m, so above a hill height of 277 m the snow line stopped following the slider | H1 below |
| S12 `[!]` | Nothing grows on a world's tiles | Scatter has one grid over one `Terrain`'s extent, takes a `const Terrain*`, and tracks changes with that terrain's revision log | S12 below |
| S4 `[ ]` | No biomes: a texture layer is the only way to say "where" | `layerDensity[4]` and `layerBias[4]` are tied to the 4 splat channels; a forest and a swamp on the same grass cannot differ; more than 4 layers (L2) would break the rules | S4 below |
| S11 `[x]` | Dead and duplicate code in scatter | see S11 for the list, each item read in the code | S11 below |
| S13 `[ ]` | Scatter does not know about water | `ScatterGround` has no water input | S13 below |

---

## 3. Milestones

Order is by what you see and what unblocks the rest. Items inside a milestone can be done in any order unless a dependency is named.

### M1. Make what exists look and feel right

**T1. Prefilter the picture; show pixels per cell** `[x]` (lesson 54; the author checked it in the editor and it works)
- `[x]` `PicturePixelsPerCell(metresPerCell, pictureWidth, imageWidth)`. The world inspector shows it under the picture controls and warns in orange above 2
  when the prefilter is off. (A world's pixel spacing is `width / (pixels - 1)`: pixel 0 and the last pixel sit on the two edges.)
- `[x]` A mip pyramid of the `HeightImage`, built once per picture (`BuildHeightPyramid`; each level is a low-pass of the one before, about half as wide, same area).
  `PictureHeightSource` reads one fixed level, `log2(pixelsPerCell)`, blended between the two levels around it. The level is fixed per world, so the height stays
  a pure function of the world position and tiles at every level of detail still agree.
- `[x]` New setting `HeightShapeSettings::prefilter` (default **on**; checkbox "Prefilter the picture"). It does nothing when a cell covers one pixel or less.
  A world migrated from an old terrain gets it **off**, so it still matches the old terrain sample for sample.
- The older `smoothing` setting (a blur in pixels) is unchanged and still useful for the steps of an 8-bit picture; prefilter handles thin features.
- Acceptance: `world_picture_test.cpp` `TestPrefilter`: a 1 px ridge at 8 px per cell changes by 0.062 over half a cell instead of 1.000; a footprint of one pixel
  or less equals the unfiltered source exactly; a step edge stays in range and only rises. Checked in the editor: the stairs on the demo escarpments are gone. Opening an existing world, the shape changes a little because `prefilter` is on by default; saved edits are deltas and stay.

**T2. Sculpt a world without rebuilding tiles** `[x]` (lesson 55; the author applied it and reports that sculpting works well)
- `[x]` `Terrain::PatchTile(rect, groundIsFinal)`: rewrites only the samples of a loaded level-0 tile inside `rect` (tile-local, apron included) from the world's
  ground, then calls the existing `RefreshChunks` and `RefreshSplat` for that rectangle. The tile keeps its `Terrain` object, its index buffers and its textures.
- `[x]` The affected samples come from pure functions: `TileSamplesAffectedBy` (the changed lattice points grown by one sample, clipped to the apron) and
  `GenerateTileBlock` (just that block of the tile's heights, rivers dug in; each value is bit for bit the one `GenerateTileHeights` gives).
- `[x]` `TerrainWorld` keeps the union of the changed lattice points per loaded tile (`pendingEdits_`). `Stream` patches them once per frame, so a stroke of many steps
  costs one patch per tile per frame. A tile that is not at full detail (cheap, far away) and paint edits are rebuilt as before. An undo or redo patches with the
  ground marked final at once.
- `[x]` The ground revision (the water re-measures its shore by it) moves when the stroke ends, not on every step.
- `[x]` Timers in the world inspector: last tile build split into heights, mesh and splat, and the last patch (a steady clock; `std::chrono` is what
  `QueryPerformanceCounter` is on this compiler).
- Decision: no full rebuild at the end of a stroke. The first version of this item asked for one as a safety net; the test below shows a patched tile equals a
  rebuilt one after every stroke, so the rebuild would only cost time.
- Not done: paint strokes still rebuild the tile (same idea, `ReplacePaint` plus a splat refresh; later).
- Acceptance: `tile_patch_test.cpp`: after each of 200 random brush strokes (all four brushes, strokes that only touch the apron included, with and without a river
  crossing the tile) the patched tile equals a tile built from scratch, sample for sample, apron included. Checked in the editor by the author: sculpting a world works well.

**T3. Shared index buffers** `[x]` (lesson 56; the author applied it and reports that it works)
- `[x]` The index buffers of a chunk depend only on its size in cells. `BuildChunkIndexLevels(cellsInX, cellsInZ, lodCount)` (`TerrainChunkIndices.cpp`, pure, moved
  out of `BuildTerrainModel` without a change) makes the three levels of detail, each a surface and four skirts. `GetSharedChunkIndices` makes the GPU buffers
  once per size and level count and hands the same set to every chunk; each `Mesh` keeps its own reference (`AddRef`), so the buffers live until the last chunk is gone.
- `[x]` New `Mesh` constructor that takes a `SharedIndexLevels` and makes only the vertex buffer. The old constructors are unchanged.
- `[x]` `ReleaseSharedChunkIndices()` runs in `Direct3D11Renderer::Release` before the device is released (the cached buffers would hold it alive).
- Effect: a tile of 8 x 8 chunks used to make 64 x 3 index buffers and build their index data again each time; after the first tile it makes 64 vertex buffers.
- Acceptance: a tile creates vertex buffers only (by construction: the only `CreateBuffer` left in a chunk is its vertex buffer). `terrain_chunk_indices_test.cpp`:
  counts per level (32 x 32: 6144, 1536, 384 surface indices; skirts a sixth of that per edge), every index inside the vertex range for square, wide and
  edge-sized chunks, the last grid line kept at every level, surface and skirt winding (skirts face outward), and a golden hash of the old code's output.
  Checked in the editor by the author: it works.

**W1. The ocean must not show where it should not** `[~]` (lesson 57: step 1 written and unit-tested; **to check in the editor**, see the end of this item)
The rule (the **water-terrain contract**, section 5): where ground is below the sea level the surface is visible above the ground;
where ground is above sea level plus the wave margin there is no water pixel; where the world has no ground, no deep ocean appears over
land that exists in the world.
Suspects, most likely first. Each one has a check that tells it from the others:
1. **LOD mismatch.** `[ ]` open. The depth map is baked from true heights but the screen shows the *coarser LOD mesh*. A coarse tile (1/4, 1/16, 1/64 of the
   cells) cuts across peaks and ridges, so a ridge above the sea in the ground can dip below the sea plane in the mesh. **Measured** (`water_visibility_test.cpp`,
   gentle test hills, 4 m cells): a mesh at level of detail 1 stands up to 0.7 m off the true ground, level 2 up to 1.8 m, level 3 (32 m cells) up to 3.1 m,
   both above and below. Rugged ground and a high `heightScale` give more. Check: set `viewDistance` small, or force every tile to LOD 0. If the artefact vanishes, this is it.
2. **"No ground" means 100 m deep.** `[x]` fixed in step 1. `Bake` used to fill every texel without a loaded terrain with `kDeepWater`, so tiles not yet streamed in,
   tiles being replaced, and (as far as the mesh is concerned) the gaps all read as deep ocean. The ground now comes from the world's ground function, which has an
   answer for every point inside the world's edge; only beyond the edge of a **bounded** world is there still "no ground".
3. **Stale or coarse depth map.** `[x]` the stale half is fixed in step 1: the stamp that triggers a re-bake no longer moves when tiles load and unload; it follows the
   world's own ground revision (settings, rivers, finished strokes, undo, loaded edits), so there is no bake against half-loaded ground. The map is still about
   360 x 360 texels for the biggest bodies (W2 step 2 removes the limit).
4. **Skirts.** `[ ]` open. The skirt under a tile edge hangs `max(1.5, 0.06 x heightScale)` m (14.4 m at 240 m) and is in the depth buffer the shader
   measures `thickness` against. Check: wireframe, and the water's foam band along tile seams.
- Step 1 `[x]`: `WorldGround.{h,cpp}` (`BuildWorldGroundGrid`, pure) makes a grid of the ground's heights in metres over a body's footprint from `source -> shape ->
  edits -> rivers`, sitting on the world's own lattice, so at full detail each sample is exactly a tile vertex's height. `TerrainWorld::BuildGroundGrid` and
  `GroundRevision()` expose it; `Water::RefreshShoreDepth` takes the worlds and bakes from the grid; `Scene::Update` leaves a world's tiles out of the list and stamps
  by the world's revision. Single terrains still use their own heightfield.
- Step 2 `[ ]` (only if suspect 1 reproduces): keep tiles whose bounds cross a water level at LOD 0 or 1, or lower the coarse mesh's vertices at the shore
  (skirt-style, never above the true height).
- Acceptance: `water_visibility_test.cpp` (contract items 1 to 5): the depth map agrees with the ground function to 0.21 m, with water where the ground is below the
  level and none where it is above; inside a bounded world there is always ground and beyond its edge none; the ground grid equals the tile vertices
  (0.000000 m, with and without a river); two bodies, moved, turned and resized, agree on the shoreline to 0.05 m. Items 6 and 7 of the contract are not covered yet.
  Still open (editor): load a world with a sea body and fly around: the ocean should not show under land, also while tiles stream in. If it still does, tell me
  which of the four checks above makes it vanish. Seen by the author before this step (Nov 2026, screenshot of a new fractal bounded world with a body the size of the
  terrain): a dark band over low ground with no water on it, near a shore; to be looked at again after step 1.

**X1. A world edge that does not look like a slab** `[x]` (lesson 58; the author applied it, including the bottom, and reports that it works well)
- **Decision (written once, here):** the bounded world is closed with **walls**: the skirt on every side that is the world's edge goes all the way down to a floor below the
  lowest ground, so the world reads as a block (a diorama) and not a floating sheet. The floor is at `-max(10 m, heightScale / 10)` (-24 m at 240 m). Fading the edge into
  fog and a backdrop is not done: it needs the environment (fog, sky) and is left until a world needs it.
- `[x]` `OuterEdgesOfTile(bounds, tile)` (pure) says which sides of a tile are the edge of the world; `TerrainTile::outerEdges` carries it; `BuildChunkVertices` puts the skirt
  vertices of those sides on the floor and gives them an outward normal and tangent, so they are lit as a wall. Only the chunks touching that side of the tile get it
  (`ChunkWallEdges`); the seams between tiles keep their ordinary short skirts. The indices are the shared ones of T3 (only vertex positions change), so a wall costs nothing extra.
- Acceptance: `terrain_walls_test.cpp`: a side is a wall exactly when the tile across it is not part of the world (checked for every rectangle of up to 5 x 5 tiles, negative
  tile numbers included); an endless world has none; a one-tile world has all four; only the outline chunks of a tile carry a wall; the floor is always below the lowest ground.
  Checked in the editor by the author: it works well. The look of the wall and the bottom (stretched layers, ambient light only) is polish: F1.
- `[x]` The bottom (added after the author's screenshot from below showed the block open underneath): part 5 of every chunk mesh, two triangles facing down on four extra
  corner vertices at the floor, the same at every level of detail; every tile of a bounded world draws it (`TerrainTile::capBottom`, bit `kTerrainCapPartBit` of the part mask).
  The vertex normal points down, so from below it is lit as a floor. The first five parts are exactly what T3 froze (the golden hash covers them and is unchanged).
- Not done: a different material for the wall and the bottom (they reuse the edge's texture coordinates; the layers are stretched down the wall, which the author's screenshot shows).

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
| **Collision and height queries** | `World::HeightAt(x, z)` across tiles | `[~]` **C1** |
| **Water reads world ground** | via the ground function (W1) | `[~]` |
| **Scatter over every tile** | deterministic per tile, seed from the tile id | `[ ]` **C2 = S12** |
| Save and load | settings in the scene, edits in `.worldedits` | `[~]` |

- **C1** `[~]` The ground at one point (lesson 59: written and unit-tested; **to check in the editor**: snap an object to a world, aim a river at it, with the camera far from the tiles).
  - `[x]` `WorldGroundFunction` (`WorldGround.h`, pure): `HeightAt`, `NormalAt` and `Raycast` over the ground function. It reads the very triangles a full-detail tile is drawn
    from (the split of `Heightfield::Locate`), so it is the same whether a tile is loaded there or not, and at any level of detail the tiles are drawn.
  - `[x]` `TerrainWorld::HeightAt / NormalAt / RaycastGround`. `Scene::GetGroundHeight` now asks the worlds first and skips the tiles of a world that has ground; new
    `Scene::GetGroundNormal`, `Scene::RaycastGround` and `Scene::HasGround`. The editor's "Snap to terrain", the river overlay and the river "ground above the water"
    readout use them, so they work on a world. (Picking for sculpting still uses `TerrainWorld::Raycast`, the loaded tiles' meshes: it is fast.)
  - Acceptance: `world_height_test.cpp`: on a one-tile world the height is within 0.000015 m of `Heightfield::HeightAt` (0.000080 m with a river dug in) and the normal within
    0.000005 of `Heightfield::NormalAt`, at 4000 points including every edge (where the apron matters); across tile borders the ground is continuous with no tile loaded; a
    bounded world has ground inside its edge and none beyond; 300 rays from above land on the ground to 0.000023 m and agree with a 5 cm brute-force walk to 0.05 m,
    and miss when they should (straight up, starting underground, too short, flying away from a bounded world).
  - Still open (editor): "Snap to terrain" on a world with the camera far from the object; the river overlay follows the cursor over unloaded ground.
- **C2** Scatter over every tile. The plan lives in **S12** only; this line is a pointer.
- **H1** The two heights `[~]` (lesson 60: written and unit-tested; **to check in the editor**, see the end of this item). Found while starting C3 (the question "does Height mean the hills or the starting Y?").
  - **What the slider did, and does:** it was already correct for what it is, the *hill height*: it multiplies the 0..1 ground, so the lowest ground stays at Y = 0 and
    the highest rises to `heightScale`. Nothing was broken in the multiplication; what was missing was a second control and a clear name.
  - `[x]` `TerrainWorldSettings::baseHeight` (metres, default 0, saved with the scene: an old scene has none and loads at 0, so nothing moves). `TerrainTile::baseHeight`
    puts every tile at `position.y = baseHeight`; everything a tile already did with world Y (bounds, the shore/snow rules baked into the splat, picking, scatter's ground)
    reads `position_.y`, so it follows without a change.
  - `[x]` `WorldGroundFunction::baseHeight` and `BuildWorldGroundGrid(..., baseHeight)` (last parameter, default 0, so a caller that does not pass it gets the old answer).
    Everything that asks the ground in metres goes through these two: `Scene::GetGroundHeight / GetGroundNormal / RaycastGround`, the water's shore depth, "Snap to terrain", the river overlay.
  - `[x]` `MakeWorldMaterial` lifts the automatic shore and snow lines by the base, and `TerrainSettings::Clamp` allows -2000 to 2000 m for them (the single terrain's *sliders* keep
    -50 to 200: `kBandMin/kBandMax`). A world now keeps its sand and snow at the same fraction of the ground whatever the hill height (up to 500 m) and the ground level.
  - `[x]` Editor: the slider is **Hill height (m)** with a tooltip, **Ground level (m)** is new, and a line says "The ground spans A to B m (world Y)". Carving a river into a world
    measures the water's Y from the ground level (`river.carve.level = (water Y - baseHeight) / heightScale`); before, with a ground level other than 0 the channel would have been dug at the wrong height.
  - What it does **not** do (on purpose): water bodies, the cube and every object stay at their own Y, so a new ground level moves shorelines (the tooltip says so). The ground is not
    a movable object (no gizmo): the base height is a setting of the world.
  - For **S12 / C2**: scatter must read ground heights from `TerrainWorld::HeightAt` or `GroundFunction()` (both add the base), never by multiplying a 0..1 height by `heightScale` itself. A second copy of that formula would ignore the ground level.
  - Acceptance: `world_base_height_test.cpp`: every height is exactly `baseHeight` + the old height (at bases of -250, -2, 0, 37.5 and 800 m, with and without a river; worst difference 0.00013 m);
    a hill height of 100 -> 250 m at a base of 55 m makes every height *above the base* 2.5 times higher and leaves the base alone; normals do not change and a ray lifted by the base
    hits at the same distance; the ground grid the water bakes from equals the function's heights plus the base and a base of 0 gives the old grid bit for bit.
  - Still open (editor): drag **Ground level** on a world with the sea at y = 4: shorelines move, the sand and snow bands follow the ground, the cube snaps to the new ground ("Snap to terrain"). Raise **Hill height** to 500: the snow line stays at about 72% of the height.
- **C3** Migration, then the deletions `[~]` **step 1 only** (the rest waits).
  - `[x]` Step 1, run the demo scene through migration, by reading it (there is nothing to run here): `BuildDemoScene` adds **one** `Terrain` at its default position `(0, -2, 0)`, a `Water` at y = 4 and a `Cube` at y = 4, and the repo holds no scene files (`assets/` is textures and skyboxes). Migrating that terrain
    used to give a world that rose 2 m (the Y was dropped: H1). `WorldSettingsFromTerrain` now carries it through `HeightFrameOfOldTerrain(position.y, heightScale, scale.y)`, so the migrated ground stands where the terrain stood.
    Acceptance: `world_base_height_test.cpp` `TestMigrationKeepsTheOldHeight`: at (y -2, scale 1), (0, 1), (35, 2) and (-80, 0.5) the world's ground equals the old terrain's `position.y + Heightfield::HeightAt x scale.y` to 0.00003 m anywhere inside, and the test shows that
    without the base the world would be off by exactly `|position.y|` (2 m for the default terrain).
  - `[ ]` Step 1b, **scenes you saved yourself are not covered**: for each one, *Add > Terrain world from this terrain* and check that the world's **Ground level** equals the terrain's Y and the shoreline did not move. If one does not, the cause is somewhere else and goes in section 8.
  - `[ ]` Step 2, delete the `Scene::GetTerrain()` callers, `Terrain::CarveRiver` and the heightmap-only settings. **Blocked**: the old terrain is deleted only when every row of the table above is `[x]` (most are `[~]` until checked in the editor), and scatter still calls `GetTerrain()` until C2 (S12) is in.
    F10 is the list of callers to audit.
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
- Seen by the author (Nov 2026): a body sized by hand to a bounded world only half covers it, and because most of a new fractal world lies below the body's level the
  terrain reads as one flat teal slab. The sea of a world should cover the world's bounds by itself and sit at a level chosen with the ground in mind (a default near the
  median ground height).
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
13. **A world has two heights: its extent and its placement.** `heightScale` (Hill height) is how tall the ground is, `baseHeight` (Ground level) is the world Y of its lowest point; world Y = `baseHeight` + g x `heightScale`.
    A tile stands at `position.y = baseHeight`, and anything that needs the ground in metres asks the ground function, which adds it. No other code adds or subtracts the base (H1).

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

---

## 8. Later fixes (found while working, not part of a milestone)

Fixed after the milestones are done (rule 6). Each item says where it was found and what would fix it. Order is not priority.

| ID | Found in | Problem | Fix |
|---|---|---|---|
| F1 | X1 | The wall and the bottom of a bounded world reuse the edge vertex's texture coordinates, so the layers are stretched down the wall (green streaks, seen in a screenshot) and the bottom is lit by ambient light only. | A rock-like wall material and a plain floor colour, chosen in the shader by a vertex flag or a per-part material. |
| F2 | T2 | Paint strokes on a world still rebuild the tile (they mark it stale); only height edits are patched in place. | `Terrain::ReplacePaint` plus a splat refresh for the dirty rectangle, then the same pending-edit path as heights. |
| F3 | the MSVC test build | A missing `#include <array>` in `WorldEdits.h` built with one compiler and not with MSVC. A scan found a dozen smaller gaps. | A CMake target that compiles each header on its own (a header must include what it uses), and a build with MSVC and with GCC or Clang in CI. |
| F4 | W1 | The water now bakes its depth from a ground grid: up to about 250,000 height evaluations for a 2000 m body at 4 m cells. Not measured. | Time `BuildGroundGrid` with a timer like T2's; cache the grid per world revision and body footprint; use a coarser step far from the camera. |
| F5 | C1 | Picking for sculpting (`TerrainWorld::Raycast`) tests the loaded tiles' meshes at their levels of detail; far away it differs slightly from the ground function. | Use `RaycastGround` for far hits, with a faster early-out than half-cell steps (a coarse-to-fine march). |
| F6 | S11 | `editor/EditorLayer.cpp` is still about 4,450 lines. | Move the water, terrain and world inspectors out the way the scatter inspector was (lesson 53). |
| F7 | T2 | `Terrain::BuildTimes` is one shared global, written by every tile build. It is not thread-safe, which matters once tiles are built off the main thread (C4). | Make each tile build return its own times, and let the world add them up. |
| F8 | T1 | `prefilter` defaults to on, so opening an existing world changes its shape slightly (its edits are deltas and stay). | Store a version in the world settings and default old files to the old behaviour, with a button to convert. |
| F9 | W1 | Items 6 and 7 of the water-terrain contract (sculpting and rivers update the shoreline; a river is only water inside its corridor) have no test. | Add them to `water_visibility_test.cpp`; item 6 needs the ground revision (lesson 57) to be observable from a test. |
| F11 | H1 | A river dug into a world stores its level as a fraction of the ground (`carve.level`, `depth`, `bankHeight` are metres / `heightScale`), but its water body stays at its world Y. Changing **Hill height** (and now **Ground level**) *after* carving moves the channel against the water. Found by reading, not seen. The old slider did this too. | Store the river's carve in metres (world Y) and convert it when the tile is built, or re-derive it from the water body when the settings change. Until then: set the height first, then draw rivers. |
| F12 | H1 | The ground level is a setting of the world, not a movable object: no gizmo, and objects standing on the ground do not follow when it changes. | An optional "keep objects on the ground" step when the level changes, or a gizmo handle on the world; decide when a real scene asks for it. |
| F10 | C1 | Many editor tools still start from `scene_->GetTerrain()`, which is the first tile of a world and not the world (the river carve, the sculpt and paint paths that use a single terrain, the scatter menu). | Audit them at C3: each either asks the scene's ground (as C1 did) or the `TerrainWorld`. |
