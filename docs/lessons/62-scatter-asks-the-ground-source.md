# Lesson 62 - Scatter asks a ground source, not a terrain (S12, stage 1)

## Understand

**Scatter was built for one terrain.** Its placement takes a `ScatterGround`: one heightfield, one splat map, a position and a scale. A world has no single heightfield, only tiles that come and go. To grow a forest on a world, the placement has to ask something more general than "this terrain".

**What does placement actually ask?** Three things: where there can be ground, whether there is any at this point, and what it is like there (height, slope, layer weights). Nothing else. So those three questions become an interface, `ScatterGroundSource`, and the placement only knows the interface.

**A fourth question appears for worlds: is it ready?** On a world, some ground is not there yet because its tile is still loading. Placing trees next to a missing tile gives a wrong answer that changes when the tile arrives. `IsReady(area)` lets a cell wait.

## How it works

**One terrain keeps working as it did.** `ScatterGround` now implements the interface by calling the functions it already had. Nothing in the maths moved, and the golden hash from lesson 52 and every placement test pass untouched. That is the proof that the refactor changed no tree.

**A world is a set of tiles.** `ScatterTileGround` holds one `ScatterGround` per loaded tile, borrowed from the tile. A point is answered by the tile that contains it, with a shared edge belonging to the next tile. Because the answer depends only on the position, it does not depend on the order the tiles arrived in.

**Only full-detail tiles belong.** A tile drawn at a coarser level has a coarser heightfield and coarser layer weights. Trees placed on it would stand somewhere else once it sharpened, so the scene leaves such tiles out and `IsReady` says "not yet".

**Ready means every touching tile is loaded.** The area is half-open, so a point exactly on its far edge is not in it. Tiles beyond the edge of a bounded world are not waited for, because they will never come.

## What the tests show

**No seam across tile borders.** Nine tiles of 256 m give 6,840 trees. Placing the whole 768 m square at once gives exactly the same trees as placing it in cells of 32 m, and in cells of 100 m that cross the tile borders: none twice, none missing, none moved.

**Order does not matter, and a tile can leave and return.** Forward, backward and shuffled arrival give the same trees. Removing a tile and adding it back gives them again.

**Why a cell must wait.** A cell on a tile border has 7 trees before its neighbour tile arrives and 14 after. Those extra trees would pop into view, so the scatter will not place that cell until `IsReady` says the ground around it is complete.

**Not done yet.** The scatter object still draws from one terrain. Stage 2 feeds it the loaded tiles of a world, with a grid that follows the camera.

## Check yourself

1. Why does a shared tile edge belong to the next tile in `Sample`, and what would go wrong if both tiles answered for it?
2. A cell sits 40 m inside a tile, and the species spacing is 8 m. Does it need its neighbour tile to be loaded before it is placed? What if the spacing is 50 m?
3. Why are coarser tiles left out of the set instead of being used with their coarser data?
