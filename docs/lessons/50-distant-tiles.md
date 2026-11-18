# Lesson 50 - Distant tiles: a long view that stays cheap

## Understand

**The problem.** After lesson 49 the world ends in a hard edge at the view distance, and pushing it out costs a full tile (66,049 vertices, a 256 x 256 layer map, a mesh with four levels of detail per chunk) for every kilometre of land. A 8 km view would need over 200 tiles at full detail. Most of them would be a few pixels wide.

**Level of detail for a whole tile.** The same square of ground can be sampled coarsely. Level L has `2^L` times fewer cells along each side, each `2^L` times wider: 8 chunks of 4 m, then 4 of 8 m, 2 of 16 m, 1 of 32 m. A level-3 tile has 1/64 of the cells but covers the same 1024 m.

**Why it fits together.** Everything is a power of two, so the square is exactly the same size at every level, and the sample positions of a coarse tile are a **subset** of the fine tile's: sample `(i, j)` of level L is the position of sample `(i * 2^L, j * 2^L)` of level 0. Both read the same world height function, so along a border between a fine tile and a coarse one the two meet at the same points, bit for bit (`tests/world_grid_test.cpp` checks it for every level). Between those points the fine tile has a little more detail, which opens a thin crack, and the **skirts** the terrain already has hang a strip below every tile's outer edge, so nothing shows through.

**Which level.** `WantedTileLod`: level 1 from 1.5 tiles out, level 2 from 3, level 3 from 6. Changing level means building the tile again, so a tile that is already loaded keeps its level until the camera is a quarter tile past the boundary (the same hysteresis idea as lesson 49). With an 8 km view the test loads 225 tiles: 9 full, 36, 92 and 88 at the coarser levels. Walking 4 km changes the level of 147 tiles, and no tile changes more than twice.

**Building them.** A coarse tile is cheap, so `Stream` has a budget in FULL tiles: a level-L tile costs `4^-L`. Several far tiles are built in one frame; one near tile takes the whole budget. At least one request is always served. When a tile changes level the new tile is built and the old one removed in the same call, so no frame shows a hole or two tiles on top of each other.

**The edge.** Beyond the view distance there is no ground at all. Fog is what turns that into distance. The scene already has fog, so the world inspector gets a button, *Fog: hide the edge*, that turns it on with a visibility of 80% of the view distance. The world never touches the fog by itself: you may have tuned yours.

## What changed

- `WorldGridSettings::MaxTileLod`, `ForLod`; `WantedTileLod`; `PlanTileStreaming` returns requests with a level and understands rebuilds.
- `TerrainTile::lod`; `TerrainWorld::Stream` builds by cost; the view distance can be 8 km and 256 tiles can be alive (the default view is 5 km).
- Inspector: tiles loaded per level, and the fog button.

## Known limits

- A tile changing level pops (the new mesh has a little more or less detail). The fog hides most of it, because changes happen far away.
- Coarse tiles light their borders from their own neighbours' samples, so the lighting can differ slightly across a level border.
- Tiles are still built on the main thread.

## Check it

1. Add the terrain world, then press *Fog: hide the edge*. Fly up and look at the horizon: far land fades into the fog; no hard edge.
2. Select the world: the counts show many coarse tiles and few full ones.
3. Fly forward: tiles refine as you approach; the world inspector counts change; frame rate stays steady.
4. Look along a border between a fine and a coarse tile: no gaps showing sky.
5. Raise the view distance to 8000 and release the slider.
