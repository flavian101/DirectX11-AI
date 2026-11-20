# Lesson 55 - Patch a tile in place instead of building it again (T2)

## Understand

**A sculpt step used to rebuild whole tiles.** Each step marked the tiles it touched as stale, and the next frame built a new `Terrain` for each: heights for 257 x 257 samples plus the apron through the whole source chain, 64 chunks of vertex and index buffers, and a splat bake. The brush changed a few hundred samples. The tile paid for 66,000.

**The single terrain already knew the trick.** Its `Sculpt` writes the changed samples and refreshes only the chunks around them. A tile of a world refused, because its heights come from the world and a local edit could open a seam. The seam worry goes away when the tile reads the same ground function everyone else reads.

**One fact makes it safe.** A sample's height depends only on its world position and the rivers, never on the block around it. So regenerating a small rectangle gives exactly the numbers a full rebuild would put there.

## How it works

**Three small pure functions do the thinking.** `TileSamplesAffectedBy` turns the changed lattice points into a block of tile samples: grown by one sample, clipped to `-1..cells + 1` so a change just outside the tile still reaches its apron. `GenerateTileBlock` makes the heights of just that block. Neither needs Direct3D, so both are tested.

**`Terrain::PatchTile` writes and refreshes.** It stores the new values with `SetSample`, or into the apron strips for samples outside the tile, then calls `RefreshChunks` and `RefreshSplat` for the rectangle. An edit next door only moves an edge vertex's normal, so only that border row is refreshed.

**The world batches the work.** `MarkEditedTiles` no longer marks a tile stale for a height edit. It adds the changed points to that tile's pending rectangle. `Stream` patches each pending tile once per frame, however many steps came in.

**Some tiles still rebuild.** A tile at a coarser level (far away, 4 to the minus level of the cells) is rebuilt as before. So is a tile touched by painting, and any tile that is stale for another reason.

**The shore waits for the stroke to end.** The water measures its shore by the ground revision, which is expensive, so the revision moves when the stroke ends. An undo or redo is final at once.

## What the test shows

**Patched equals rebuilt, every time.** `tile_patch_test.cpp` applies 200 random brush strokes, all four brush types, at positions that reach past the tile on every side. After each stroke it patches a running copy and compares it with a tile built from scratch: samples and apron, identical, with and without a river crossing the tile. About 140 of the 200 strokes reached the tile, so the test is not empty, and the ground really changed.

**Timers show the difference.** The world inspector now shows the last tile build split into heights, mesh and splat, and the last patch. While you sculpt, the "built" counter should stay still.

**Not checked: the editor.** Sculpt on a world, watch the timers, and look for any seam along a tile border.

## Check yourself

1. A stroke touches only lattice points outside a tile, two cells past its east edge. Does that tile get patched, and which of its samples change?
2. Why is the final rebuild at the end of a stroke, which the first plan asked for, not needed here?
3. Why does `Stream` patch once per frame instead of once per `SculptStep`?
