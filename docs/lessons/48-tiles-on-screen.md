# Lesson 48 - Tiles on screen

## Understand

**What we have.** Lesson 47 made the numbers for tiles: a height source that depends on the world position, tile samples with shared edges, and an apron for matching normals. This lesson uses them. A `Terrain` can now be **one tile of a world**, and *Add > Terrain world (3 x 3 tiles)* makes nine of them side by side: a 3 km world of 1 km tiles, each 256 cells of 4 m.

**A tile is a Terrain with a different source of heights.** Everything a terrain does (chunks, levels of detail, skirts, culling, shadows, the splat map, painting, height queries) already worked on one heightfield. So the tile is not new machinery: `Terrain` gets a `TerrainTile` (the grid settings, the tile's coordinate, and the shared `WorldHeightSource`), and in three places asks the world instead of the heightmap picture: the shape of the grid, the samples (`GenerateTileSamples`), and the apron (`GenerateTileApron`, set after every `Reset`, because `Reset` forgets it).

**Three things had to be shared or made world-wide, or the tiles would show:**

1. **The layer textures.** Each terrain reads and packs twelve images into two texture arrays. Nine tiles would do that nine times and hold nine copies on the GPU. The arrays are now `shared_ptr`s and tiles after the first borrow the first tile's.
2. **The layer-edge noise.** The splat bake roughens the borders between layers with noise read in the terrain's LOCAL space (so that moving a terrain does not drag its borders around). In a world that means every tile repeats the same noise, and layer borders jump at the seams. `SplatPlacement::noiseOffset` adds the tile centre's world position, so the noise is one continuous field. `tests/world_grid_test.cpp` proves it: baking the layers tile by tile gives **exactly** what one big bake gives (0 of 1,048,576 weights differ), and with each tile's own local noise **52,128** differ.
3. **What a tile may not do.** Its heights belong to the world, so a sculpt or a river carve would open a seam with its neighbours. For now a tile cannot be sculpted, carved, moved, or given another size, resolution or noise (the editor greys those controls out and says why). Painting works.

## What changed

- `TerrainTile`, `Terrain(device, tile, material, shareLayersFrom)`, `IsTile()`; layer arrays are `shared_ptr`.
- `TerrainSplat`: `noiseOffsetX/Z`.
- `TerrainWorld.h/.cpp`: `TerrainWorldSettings`, `MakeWorldMaterial` (the shore and snow heights scaled to the world's height, longer detail distances for 1 km tiles), `SpawnTerrainWorld`.
- `Scene::GetGroundHeight`: the ground under a point from whichever terrain covers it. (Used where the editor puts a new water body; the rest of the editor still asks `GetTerrain()`, the FIRST terrain.)
- Editor: the menu item, the inspector note, one undo step per tile.
- The scene file: tiles are left out, with a warning (a world is not saved yet).

## Known limits (each is its own later step)

- Water, scatter, rivers and sculpting use the first terrain only.
- The sun's shadow map has one fixed size, so shadows over 3 km are soft (cascades come with camera-relative rendering).
- A world is not saved.
- All nine tiles are built at once; streaming and distant detail are the next lesson.

## Check it

1. Add > Terrain world: nine tiles appear, the camera frames the middle one. Fly out and look at the seams: no cracks, no lighting line, no jump in the grass / rock / snow borders.
2. Press F with different tiles selected: the camera frames each.
3. Select a tile: the inspector explains why Shape and Noise are greyed out. Try the sculpt brush: nothing happens.
4. Edit > Undo removes the last tile added.
5. Save the scene: the warning appears.
