# Lesson 51 - Rivers that are part of the world

## Understand

**The problem.** In a world, tiles are made from a height function and thrown away when they stream out. The editor's *Carve channel* digs a river into the heights of ONE terrain: in a tile that edit would be lost the moment the tile unloaded, and a river that crosses a tile border would be cut in two. Waterfalls (the next big goal) sit on rivers, so this has to be solved first.

**The idea: a river is part of the FUNCTION, not an edit of the heights.** The ground at a point is the world's base height, dug by every river near that point. Any tile that needs that point computes the same answer, so the river is identical whichever tile (at whichever level of detail) draws it, comes back whenever a tile streams in again, and is saved as a few numbers instead of a changed heightmap.

**Why this was easy.** The carve in `RiverCarve.h` was already written as a pure function: the new height depends only on the distance to the river's middle line and on the old ground ("never higher than the old ground, and the limit depends only on the distance, never on the ground"). `CarveRiversIntoBlock` is the same algorithm as `CarveRiver`, working on a block of samples at exact WORLD positions (an exact integer index times the cell size, in double) instead of on a heightfield in local space.

**Rivers are carved together with the apron.** A tile's heights and its apron (lesson 47) are made as one block that is one sample bigger on every side, carved once, then split. So the lighting along a river is seamless across tile borders too.

**What the tests prove** (`tests/world_rivers_test.cpp`):

- it agrees with the carve a single terrain uses: worst difference **6e-8**, 4,480 samples changed in the first tile;
- neighbouring tiles hold the same samples along a border the river crosses, **bit for bit**, and so do a tile and its coarser levels of detail;
- the normals of two tiles agree exactly along the seam;
- samples farther than the river's reach are untouched, and a tile with no river nearby is exactly the plain tile.

**Only what changed is rebuilt.** `TerrainWorld::AddRiver` stores the river in the world's settings, makes a new snapshot of all the rivers (what new tiles are built with), and marks the loaded tiles the river touches as stale. `Stream` rebuilds the stale tiles first, one budget at a time, and the new tile replaces the old in the same call.

**Undo.** *Carve channel* in a world adds the river to the world and records an undo step that removes it (and puts it back with the same id on redo). Saving stores the rivers with the world: `[x, z, halfWidth, level]` per path sample and four numbers for the channel.

## What changed

- `WorldRivers.h/.cpp`: `WorldRiver`, `CarveRiversIntoBlock`, `RiverTouchesTile`, `GenerateTileHeights`.
- `TerrainTile::rivers`; `Terrain` builds a tile's samples and apron with `GenerateTileHeights`.
- `TerrainWorld`: `AddRiver`, `RemoveRiver`, the snapshot, stale tiles; rivers in `TerrainWorldSettings` (saved and loaded).
- Editor: *Carve channel* works in a world; *Follow terrain*, *Pick a spring* and *Continue downhill* read the ground of every tile (`Scene::GetGroundHeight`).

## Known limits

- A world holds at most 32 rivers of at most 4096 samples each.
- A river can only be drawn where tiles are loaded (the ground has to exist to follow it).
- The inspector's "ground above the water" readout still looks at the first terrain only.
- Scatter and sculpting still see only the first terrain.

## Check it

1. In a terrain world, add a river (spring, *Let the river find its way*) and press *Finish river*. The tiles it crosses rebuild, nearest first, and the channel appears with no seam at the borders.
2. Fly away until those tiles unload, then come back: the river is still there.
3. Ctrl+Z removes the channel; Ctrl+Y puts it back.
4. Save and reload: the river is in the world.
5. Look along a border between a fine and a coarse tile where the river crosses: no gap.
