# Lesson 31 - Scatter, culling and levels of detail

## Understand

**A forest is not a thousand models.** One pine is 1900 triangles. A thousand pines is 1.9 million triangles and a thousand draw calls, and the CPU spends more time *telling* the GPU what to draw than the GPU spends drawing it. Three ideas fix that, and they stack:

1. **Instancing.** Send the mesh once and a list of places. One draw call, `DrawIndexedInstanced`, draws every copy. Each place is 32 bytes: a position, a size and a rotation.
2. **Culling.** Do not draw what the camera cannot see. A test costs a few multiplications, a drawn tree costs thousands of triangles.
3. **Level of detail (LOD).** A tree 300 metres away covers a handful of pixels. Draw it with 100 triangles instead of 1900 and nobody can tell.

**Placing by rules.** Nobody places a hundred thousand trees by hand. Instead the artist says what a species wants from the ground, and the computer decides each position (`src/ScatterPlacement.h`):

1. **Where could something stand?** A lattice of squares, `spacing` metres wide, each holding one *candidate* at a random spot. The random numbers are a *hash of the square's index*, not a running generator, so the same square always gives the same candidate.
2. **Does the ground want it?** The candidate looks at the terrain under it: the layer weights (lessons 29 and 30, so painted grass grows trees and painted rock does not), the slope, the height, and a dice roll against the density.
3. **Is there room?** Two survivors closer than `minDistance` cannot both stay; the one with the lower random priority loses.

**Why hashing matters.** Because a candidate depends only on its own square and its neighbours, a cell can be generated alone and gives exactly what a whole-terrain run would. A sculpt stroke regenerates only the cells under the brush. `tests/scatter_placement_test.cpp` checks this: the whole terrain and the same terrain cut into odd-sized cells produce byte-identical instances.

**Size on screen, not distance, picks the level.** The terrain picks its level from a distance in metres (lesson 28), fine for one big thing. A 30 m pine and a 0.5 m rock should not change level at the same distance. So we measure what the eye sees: the fraction of the screen's height that the object's bounding sphere covers.

```
screen = radius * cot(fov / 2) / distance
```

`screen * screenHeightInPixels` is the sphere's diameter in pixels. Level 1 starts below 0.20, level 2 below 0.08, level 3 below 0.03, and below 0.004 (about 4 pixels) the object is not drawn.

## Visualize

**Try it.** Open a scene with a terrain, choose *Add > Scatter on terrain*. Pines grow on the grass, boulders on the rock. The Inspector shows *Statistics*.

- Fly low through the forest, then rise: the *draw calls* and *triangles* counters fall as cells leave the view or switch level.
- Turn **Frustum culling** off and turn around: the instance counter stays high even though you look at nothing.
- Turn **Level of detail** off and watch the triangle count jump.
- Paint sand on the grass with the paint brush (key `4`): the trees under the brush disappear, only those cells are regenerated, and the *Placing: n cells left* line counts down.
- Move the *Max slope* slider of the pines and watch them retreat from the hillsides.
- Set *Clumping* to 1 for groves and clearings.
- Select a model you imported with *Model from file* and read *Culling and detail*: parts outside the view are skipped, and parts that look small use a simpler mesh.

**Your own trees.** *Add species... > From a model file*. If the file has meshes named `Pine_LOD0`, `Pine_LOD1`, `Pine_LOD2` they are used as the levels. Otherwise levels are generated.

## Why it is built this way

**Cells.** The terrain is cut into square cells (32 m by default). A cell is the unit of three things: *regeneration* (only dirty cells are placed again), *culling* (one box test per cell, not per tree) and *LOD* (one level per cell). The cost is that a whole cell changes level at once. Hysteresis (a 10% margin before changing level, `SelectLod`) stops a cell on a boundary from flickering, and the cell size slider trades precision for draw calls.

**One buffer per species, cells as ranges.** All instances of a species live in one vertex buffer sorted by cell, so a cell is a range `[start, start+count)`. Neighbouring visible cells that chose the same level touch in the buffer and are merged into one range (`AppendRun`): a stretch of forest is one draw call. The range goes to the GPU as `StartInstanceLocation`.

**The instance stream.** `shaders/scatter_vs.hlsl` reads a second vertex stream, slot 1, that advances per *instance*. `Shader.cpp` adds those two input elements only to shaders that ask for `INSTPOS`. The output matches `vs.hlsl`, so the ordinary `ps.hlsl` lights a scattered tree exactly like a hand-placed one.

**Levels of detail are index buffers.** `src/MeshLod.cpp` (meshoptimizer) collapses the cheapest edges until a triangle budget is met (50%, 20%, 6%). The levels *share one vertex buffer*; only the index buffer differs, which is what `Mesh` already supported. Every level is also re-ordered for the GPU's vertex cache.

**Placement is incremental.** Cells are generated nearest-the-camera first, within a time budget per frame (6 ms by default), so even a million trees never freeze the editor. The terrain keeps a short log of which patches of ground changed (`Terrain::GetSurfaceChangesSince`); the scatter marks the cells under those patches dirty.

**Shadows are limited on purpose.** The sun's shadow map covers the whole terrain. A tree beyond ~120 m would be a few blurry texels, so only cells near the camera are drawn into it, one level coarser. The real fix is cascaded shadow maps, which is a later lesson. Leaves with an alpha cutoff use a second shadow shader that cuts them out of the shadow.

**Reflections draw less.** The water's mirror draws the scene a second time. Scatter sees half the distance there.

## Other optimizations in this lesson

- **Frustum culling of ordinary models** (`ModelObject`): one box test for the whole model, then one per part, in the camera pass and the sun pass. Before this a model was drawn whether or not you could see it.
- **LOD for ordinary models**: each part gets generated levels (or the model's authored `_LOD0..n` meshes are used), chosen by the same screen-size rule, with hysteresis.
- **No redundant constant-buffer uploads** (`Renderable`): a draw whose world matrix, colour and parameters did not change since last frame skips the `Map` / `Unmap`. A scene of static objects stops paying that cost.
- **Fewer rasterizer state changes**: a model's parts switch the rasterizer only when the kind of part changes.
- **Vertex-cache ordering** for every mesh, even level 0.
- **Compact instances**: 32 bytes each, so a million trees is 32 MB.

## Where it lives

| File | What |
| --- | --- |
| `src/ScatterPlacement.*` | placement rules and the grid (pure logic, tested) |
| `src/LodSelect.h` | screen size, `SelectLod`, `AppendRun`, `ParseLodIndex` (pure logic, tested) |
| `src/MeshLod.*` | automatic simplification with meshoptimizer (tested) |
| `src/ProceduralModels.*` | the built-in pine and rock |
| `src/ScatterObject.*` | the scene object: generation, buffers, culling, drawing |
| `shaders/scatter_*.hlsl` | instanced vertex shader, sun-pass shaders |
| `src/ModelObject.*` | culling and LOD for loaded models |
| `src/Terrain.*` | `GetSplatMap`, the surface change log |

## Not done yet (and why)

- **GPU-driven culling.** A compute shader could cull and pick levels per *instance* and draw with `DrawIndexedInstancedIndirect`, taking the CPU out of it entirely. Cell culling is enough up to a few hundred thousand visible instances; per-instance GPU culling is the next step beyond that.
- **Billboard impostors** for the farthest trees: a camera-facing picture instead of 100 triangles. The last generated level is a very coarse mesh instead.
- **Cascaded shadow maps**, **occlusion culling** (hills hiding forests) and **streaming** per terrain tile (roadmap item 7).
- **Cross-fading between levels.** A cell changes level in one frame (with hysteresis); dithered fading would hide it.

## The tests

All pure logic, no window and no GPU (`ctest`, or run the executables):

- `ScatterPlacementTests`: determinism, **no seams** (whole terrain equals the union of any tiling), minimum distance, density and clumping, layers drive the species (painted weights included), slope and height limits, rotation (a leaning instance's "up" equals the ground normal), sinking, moved and stretched terrain, grid helpers, and a timing for a 1 km terrain (about 15 ms for 46,000 trees).
- `LodSelectTests`: screen size shrinks with distance, `SelectLod` thresholds, hysteresis (a camera jittering on a boundary never flickers), `ParseLodIndex` for names like `Pine_LOD2`, and range merging.
- `MeshLodTests`: a dense sphere gets four strictly cheaper levels, all indices valid, the shape is kept, results are deterministic, tiny meshes are left alone.
- `ProceduralModelsTests`: the built-in pine and rock are consistently wound with outward-facing normals.

Not covered by a test (it needs a GPU): the instanced draw itself. Check it by eye with the *Statistics* panel as described above.
