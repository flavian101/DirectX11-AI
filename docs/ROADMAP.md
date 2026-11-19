# Terrain, World and Water: roadmap and source of truth

This is the **only** planning document for the terrain, the world, the water and the scatter system. It replaces
`terrain-world-design.md`, `terrain-world-roadmap.md`, `water-roadmap.md` and `scatter-ecosystem-design.md` (still readable in git
history at commit `f9e0401`, e.g. `git show f9e0401:docs/water-roadmap.md`). `docs/lessons/` is history: it explains how
things were built and is never updated to match the plan.

**Rules for this file**
1. A change that finishes, starts or reorders an item edits this file in the same commit.
2. Every item has an ID (`T2`, `W1`...). Code comments and commit messages refer to IDs, never to phase numbers.
3. An item is done only when its **acceptance** line holds and its tests exist in `tests/`.
4. Status: `[x]` done, `[~]` written and unit-tested but not yet checked in the editor, `[ ]` open, `[!]` known bug.
5. Facts in section 1 were read from the code at commit `f9e0401`. Nothing here was measured on a running build unless it says so.

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
| **Biome** | A label on the ground (what is here). Data, not geometry. Recipes drive ground layers, scatter and mood. |

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
| Scatter over several tiles | `[ ]` (feeds on the first terrain) | `ScatterObject` |
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

---

## 2. Known problems (the work queue starts here)

| ID | Problem | Cause (read in the code) | Fix |
|---|---|---|---|
| T1 `[!]` | Cliffs look like stairs and vertical streaks; flat pads | A 2048 px picture over 1024 m is 0.5 m/px, sampled every 4 m: about 8 px per cell, **no prefilter**, so thin escarpments alias. `smoothing` defaults to 0. About 13% of the demo picture's pixels sit at the minimum value (flat floor). | T1 below |
| T2 `[!]` | Sculpting a world is slow | `SculptStep` marks tiles stale; `Stream()` builds a **new `Terrain` per touched tile per step** (heights for 257² + apron through the whole source chain, 64 chunks of vertices and index buffers, splat bake). The single-terrain path already patches chunks in place, but `Terrain::Sculpt` returns false for tiles. | T2 below |
| T3 `[ ]` | Every chunk builds its own identical index buffers (3 LODs plus skirts) | `BuildTerrainModel` | T3 below |
| W1 `[!]` | **Ocean is drawn under / through terrain that is below the shore level** | Not yet reproduced here (I could not run the engine). Ranked suspects in W1 below. | W1 below |
| W2 `[ ]` | The sea is one plane, at most 2000 m, with a coarse CPU depth map, and it has no idea about tiles that are not loaded | `Water`, `WaterShore::Bake` | W2 below |
| W3 `[ ]` | Rivers are one plane up to 2000 m (a found river is capped at 1500 m); no confluences or lake outlets | lesson 45 | W4 below |
| X1 `[ ]` | The bounded world is a floating slab: raw edges, dark void | no backdrop | X1 below |

---

## 3. Milestones

Order is by what you see and what unblocks the rest. Items inside a milestone can be done in any order unless a dependency is named.

### M1. Make what exists look and feel right

**T1. Prefilter the picture; show pixels per cell** `[ ]`
- Compute `pixelsPerCell = metresPerCell / PictureMetresPerPixel`. Show it in the world inspector; warn above 2.
- Build a box-filtered mip chain of the `HeightImage` once per picture (derived data, so the height function stays a pure function of the
  world position). `PictureHeightSource` picks the level from `pixelsPerCell` and blends two levels.
- Interim: the existing `smoothing` setting, about half of `pixelsPerCell`.
- Acceptance: a picture with a 1 px wide ridge, sampled at 8 px per cell, gives heights that change smoothly when the sample point moves by
  half a cell (test in `world_picture_test.cpp`); the demo picture shows no stair terraces on its escarpments.

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
| **Scatter over every tile** | deterministic per tile, seed from the tile id | `[ ]` **C2** |
| Save and load | settings in the scene, edits in `.worldedits` | `[~]` |

- **C1** `World::HeightAt` (ground function, plus an exact tile raycast for picking). Acceptance: matches `Terrain::GetHeightAt` on a one-tile world.
- **C2** Scatter reads the ground of every tile; placement stays a pure function of (rules, ground, square index).
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
- Rock and cliff meshes scattered on steep slopes (a slope rule on a species; `ScatterObject` already exists), blended into the terrain material.
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
- Swamps: a biome rule (low altitude within a small height of the water level, low slope, high flow accumulation) plus a **shallow-water**
  body type: water 0.1 to 0.5 m over the ground, calm waves, dark mud layer, reeds and dead trees from scatter, ground fog.
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

### M5. Scatter and ecosystems

Placement is a pure function of (rules, ground, square index): no state, so cells can be made alone, in any order, on any thread. Keep that.
The GPU instance stays 32 bytes. Settings are plain JSON, undone by snapshot.

| ID | Item | Risk | State |
|---|---|---|---|
| S1 | Variants per species (mesh, materials, weight, layer bias, slope band) | low | `[x]` lesson 36 |
| S2 | Built-in rocks and a starter scatter | very low | `[x]` |
| S3 | Groups with inherited defaults (authoring only; compiles away) | low | `[ ]` |
| S4 | Rule-based biomes: `ScatterSample.biome[]` from height, slope, layers, noise; `biomeDensity[]` per species | low-medium | `[ ]` |
| S5 | Density-mask brush (paint and erase per species), reusing the paint tool, undo and dirty-cell regeneration | medium | `[ ]` |
| S6 | Painted `BiomeMap` (a second texture beside the splat map, PNG next to it) | medium | `[ ]` |
| S7 | Per-variant radius; share identical textures between variants | low-medium | `[ ]` |
| S8 | Biome transitions and exclusion masks (roads, buildings) | medium | `[ ]` |
| S9 | Direct instance painting (hero objects) | high | `[ ]` |
| S10 | GPU-driven culling, texture-array variants | high | `[ ]` only if the profiler shows draw calls or CPU culling |

A biome is a recipe: ground layers, scatter rules and mood (fog, ambient, water level). A forest is mostly scatter rules; a town is
hand-placed objects on a flattened foundation and is a *place*, not a biome. Biome weights overlap, so blending between biomes is free
(`density *= sum_b(sample.biome[b] * rules.biomeDensity[b])`). A species in no biome keeps today's behaviour. Known limits: one
`minDistance` per species (S7); past `maxInstancesPerSpecies` the cut is by buffer order (minor); the instanced draw has no GPU test.

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
| scatter design phases 1 to 10 | S1 to S10 |
| "terrain generators: ridged noise, terracing, erosion" | G1 |
