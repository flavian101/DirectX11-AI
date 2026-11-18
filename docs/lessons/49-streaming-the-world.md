# Lesson 49 - Streaming: a world that follows the camera

## Understand

**The problem.** Lesson 48 spawned nine tiles at once. A world you can walk across has hundreds, far too many to hold in memory. The fix is the oldest trick in open-world games: keep alive only the tiles near the camera, add the ones you are walking towards and drop the ones you left behind.

**Deciding what to load is a pure function.** `PlanTileStreaming(grid, camera, viewDistance, maxTiles, loaded)` returns two lists: tiles to load (nearest first) and tiles to unload. No GPU, so `tests/world_grid_test.cpp` checks it:

- A tile is *wanted* when it comes within `viewDistance` of the camera.
- A loaded tile is only *unloaded* when it is farther than `viewDistance + half a tile`. That gap is the **hysteresis**. Without it, a camera standing on the edge of the loaded area (or wobbling across it) would load and drop the same tile every frame. The test wobbles the camera across a tile border and checks that, once settled, nothing loads or unloads.
- At most `maxTiles` (64) stay alive, and when the cap bites it is the far tiles that go.

**The world is one object.** `TerrainWorld` is a `SceneObject` with no geometry that holds the settings (seed, hill size, height, view distance, metres per cell). Because it is an ordinary object, saving, loading, deleting and undo all work without special cases. Its tiles are ordinary `Terrain` objects that it adds to the scene and removes again: **derived state**. Delete the world and `Scene::Update` removes the leftover tiles; undo the delete and the world grows them back. The scene file stores only the world's settings, so a saved scene stays tiny and the world comes back identical (the height function is deterministic).

**One build per frame.** A tile costs a few milliseconds (about 5 ms of heights and 4 ms of layer weights on the CPU, plus the mesh), so `Stream` builds only the nearest missing tile each frame. Walking at speed, tiles appear one after another instead of in one big hitch. (Building off the main thread is a later step.)

**Layer textures survive the first tile.** Lesson 48 let tiles borrow the first tile's texture arrays. With streaming the first tile gets unloaded, so the world keeps a `TerrainLayerSet` (the shared arrays as a value) and lends it to every new tile.

## Two things a big world broke

**The water only saw the first tile.** The shore effect (foam, depth tint) measures the ground under a water body. It took ONE terrain. It now takes every terrain and uses the highest ground that covers a point, and it measures again whenever the set of terrains changes (a `groundStamp` made from each terrain's identity and revision, so with a single terrain it behaves as before). Scatter and rivers still use the first terrain.

**Shadows.** The sun's shadow box was sized to wrap every shadow caster, so a 5 km world would stretch one shadow map over 5 km: a blur. In a world the box now follows the camera and covers 400 m around it. A moving box makes shadow edges shimmer, because its texel grid slides over the ground; so `ShadowMap::Begin` now snaps the box to whole texels in the sun's own space. In an ordinary scene the box does not move and the snap shifts it by less than one texel. (Cascades are the proper fix.)

## What changed

- `WorldGrid`: `PlanTileStreaming`.
- `TerrainWorld` (the object), `TerrainLayerSet`, `Terrain(..., const TerrainLayerSet*)`.
- `Scene::Update` streams the world; `Scene` computes the ground for the water; the sun's box follows the camera in a world.
- `Water::RefreshShoreDepth(terrains, stamp)`.
- `ShadowMap::Begin` snaps to texels.
- Scene file: `TerrainWorld` with its settings; tiles are not written.
- Editor: Add > Terrain world; one entry in the Hierarchy (the tiles are hidden); an inspector with seed, detail, hill size, height, view distance and resolution (applied when you release a control).

## Known limits

- The world ends abruptly at the view distance: distant levels of detail and fog are next.
- Scatter, rivers and sculpting still use the first terrain, and tiles cannot be sculpted.
- Painting a tile is lost when it unloads.
- The shadow snap and the camera-following box are written but not covered by a test (they use the GPU maths library); look at them by eye.

## Check it

1. Add > Terrain world. Tiles pop in around you; select the world in the Hierarchy and watch "tiles loaded".
2. Fly in one direction: ahead tiles appear, behind tiles disappear, and nothing flickers back and forth at the boundary.
3. Move the camera slowly across the ground: shadow edges do not shimmer.
4. Change the seed or the height and release the control: the whole world rebuilds.
5. Add a water body in the world: the shore effects follow the terrain under it, including across tile borders.
6. Save and reload the scene: the world returns, with the same hills.
7. Delete the world: the tiles vanish. Undo: they come back.
