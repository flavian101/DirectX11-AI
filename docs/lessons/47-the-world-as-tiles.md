# Lesson 47 - The world as a grid of tiles (the maths first)

## Understand

**Where we are.** Lesson 46 let the camera see 10 km. The terrain is still ONE picture stretched over one square of at most 1024 cells, so a wide terrain gets coarse (4 km = 4 m per cell). A big world cannot be one grid. It has to be many small tiles at one fixed resolution, with only the nearby ones in memory. This lesson does not draw a tile yet: it builds and tests the numbers that make tiles fit together, so that when they are drawn (next lesson) nothing has to be undone.

**Idea 1: the world has coordinates.** A tile of 256 cells of 4 m is 1024 m wide. Tile `(x, z)` covers the world square from `x * 1024` to `(x + 1) * 1024`, and the same in Z. The numbers are integers and can be negative, so the grid has no edge. `TileContaining` finds the tile that holds a point (it uses `floor`, because -0.1 m is in tile -1, not tile 0), and `TilesWithin` lists the tiles near a point, nearest first. Streaming will use that list.

**Idea 2: shared edges come for free, if heights depend only on the world position.** A tile of 256 cells has 257 samples per side. Its last sample is the same point of the world as the first sample of the next tile. If both tiles ask the same question, "how high is the ground at this world position?", they get the same answer, and the seam is closed without stitching anything. That is why the old way (a heightmap picture stretched over each terrain, noise in picture coordinates) cannot be tiled, and why `WorldHeightSource` is a function of world X and Z. The test compares the edge samples of neighbours **bit for bit**, including at tile (1000, -1000), a million metres from the origin.

**Idea 3: do the world maths in double.** Sample `i` of tile `t` sits at `(t * cells + i) * metresPerCell`. The integer is exact (64-bit) and the one multiplication is a double. A float would be 8 cm off at 100 km. Only a tile's own small local numbers ever go to the GPU, which is what the camera-relative rendering of a later lesson relies on.

**Idea 4: lighting needs the neighbour's samples.** A vertex's normal is the slope between the samples on each side of it. On a tile's edge one of them lives in the next tile, and the old code clamped to the edge sample, so the slope came out too flat and two tiles lit the seam differently. The test measures it: the normals of two tiles differ by up to **0.09** along the seam without a fix, and by **exactly 0** with it. The fix is the **apron**: a ring of one sample outside the tile (`HeightfieldApron`), copied from the neighbours by `GenerateTileApron`, which `Heightfield::VertexNormal` reads when it is set. A tile without an apron behaves exactly as before, so nothing existing changes.

## What changed

- `src/WorldGrid.h/.cpp`: `WorldGridSettings` (tile size in chunks, metres per cell), tile maths, `WorldHeightSource` and `FractalHeightSource`, `GenerateTileSamples`, `GenerateTileApron`, `TileLayout`.
- `Heightfield`: `HeightfieldApron`, `SetApron`, `SampleBeyond`; `VertexNormal` uses it. `Reset` drops the apron.
- `tests/world_grid_test.cpp` (and every existing heightfield test still passes).

## What is NOT done

Nothing on screen changes in this lesson. The next one makes a `Terrain` that is built from a tile (not from a picture) and puts several side by side.
