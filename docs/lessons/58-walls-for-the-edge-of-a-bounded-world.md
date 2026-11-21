# Lesson 58 - Walls for the edge of a bounded world (X1)

## Understand

**A bounded world ended in a thin skirt.** Every tile hangs a short skirt under its edges, a few metres deep, to hide cracks between tiles at different levels of detail. On the outer edge of a bounded world that skirt is all there is, so from the side the world looked like a sheet floating in the void.

**A wall is the same skirt taken to a floor.** The skirt is made of copies of the edge vertices moved straight down. For the world's outer edge the copies go to a fixed floor below the lowest ground instead of a few metres below the edge, so the world reads as a block.

**Only the outline gets one.** The seams between tiles, and the sides that face another tile, keep their short skirts. A wall in the middle of the world would be a visible curtain.

## How it works

**Which sides are the edge.** `OuterEdgesOfTile` compares a tile with the world's bounds: a side is the edge when the tile across it is not part of the world. An endless world has no edge, and a one-tile world has all four sides.

**Which chunks carry it.** A tile is 8 x 8 chunks. `ChunkWallEdges` gives a wall to a chunk only on the tile side it touches, so the outline of the tile carries it and the inside does not.

**The vertices decide, not the indices.** The index buffers are the shared ones from lesson 56. Only the vertex positions of the skirt change, so a wall costs no extra triangles and no extra buffers.

**It is lit as a wall.** The copied vertex would still say "ground, facing up". For a wall the normal points outward and the tangent runs along it, so the light falls on it the way it falls on a cliff.

**The floor is a tenth of the height.** It sits at the larger of 10 m and a tenth of the height scale below zero, always under the lowest possible ground.

## What the test shows

**The outline is closed.** For every rectangle of up to 5 x 5 tiles, anywhere including negative tile numbers, a side is a wall exactly when the tile across it is not in the world. That means no gap in the outline and no wall inside the world. A 5 x 4 world has 14 tiles on the outline and 6 inside. An 8 x 8 tile has 28 outline chunks.

**Not checked: the editor.** Orbit a bounded world and look at its sides and from below. The wall reuses the edge's texture coordinates, so the layers stretch down it, and there is no cap under the floor.

## Check yourself

1. Why does the wall change only the vertex positions, and why does that matter for lesson 56?
2. A one-tile world is bounded. Which chunks of that tile carry walls, and how many walls does its corner chunk have?
3. Why is the floor tied to the height scale and not a fixed number of metres?
