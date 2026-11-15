# Lesson 40 - Only the river is water

## Understand

**The problem.** After lesson 39 the shader clipped the water outside a river's corridor, but nothing else knew. The plane's tiles were still drawn (and reflected), and `IsOverWater` still answered "yes" for the whole rectangle. A boat beside the river floated on water that was not drawn; the camera beside it could count as "under water" over a pit.

**The idea.** One rule, in one place: a spot is in the corridor when its distance from the middle line is under 1.5 half widths. That number is not a free choice. The flow map's strength is `(floor + (1 - floor) * profile) * mask`; at the bank the profile is 0, and `Decode` says "there is a river" while the strength is above 0.05, which stops where the mask falls to a half, in the middle of the smoothstep between 1.2 and 1.8. `WaterRiver::InCorridor` is the CPU twin of the shader's clip, and the test checks it against `Decode` on thousands of random spots.

## Visualize

**Tiles.** A body is cut into square tiles. Each one now gets a third flag next to "dry" and "outside the ellipse": **off the river**. A tile is off the river when no spot of it can be in the corridor. `WaterRiver::CorridorReaches(centre, radius)` answers for a round patch around the tile's box: for each piece of the curve, is the patch within `1.5 * (widest half width of that piece) + radius`? It says "no" only when that is false for every piece, so a skipped tile can never hide water. The test also checks it is not a function that always says yes.

**Decided once.** The flags are worked out when the curve, the body's place or size, or the mesh changes (inside `RefreshRiverFlow`, which already knows when any of those happened), not every frame. Until then the tiles all count (the same rule `tileDry_` uses when the ground is unknown).

**One definition of "water".** `IsOverWater` asks the corridor for a curved river. `SampleHeight`, `SampleFlow` and `Scene::FindWaterAt` all go through it, so what is drawn, what floats and what is "under water" agree.

## Why not a ribbon mesh?

It was the plan (roadmap 20b). Reading the code changed it. Everything that shapes the water (the depth map, the flow map, the shore, the vertex shader's local coordinates, the tile LOD, the mirror passes, the stats) lives in the plane's own -1..1 space and works per tile. A ribbon would need its own vertex layout, LOD, culling and shader inputs, and every system above would need a second path. Skipping tiles gets the visible result (no rectangle, round ends, no cost for empty tiles) in a few lines.

## What changed

- `WaterRiver.h`: `kClipAcross`, `InCorridor`, `CorridorReaches`. Tested in `tests/river_test.cpp` (agreement with `Decode`, conservativeness, and that it skips things).
- `Water`: `tileOffRiver_`, `UpdateRiverTiles`, used by `ChooseTiles` and `IsWetVisible`; `IsOverWater` is corridor-aware; the flow-map key includes the mesh grid, so a rebuilt mesh is measured again.
- Editor: the water stats show "tiles skipped: away from the river".

## What this patch does not do

**The level is still one number.** A river cannot fall yet (roadmap 20c). **The end is round**, because the corridor around the last point is a circle. **A tile that the corridor only touches at its edge is still drawn whole;** the shader clips the rest of its pixels.

## Questions before you read the code

1. `CorridorReaches` uses the widest half width of each piece, not the width at the nearest point. What would break if it used the narrower end?
2. Why is the tile's reach grown by the wave margin (`heightBound + 0.25`)?
3. Why is `UpdateRiverTiles` called from `RefreshRiverFlow` and not from `RebuildMesh`? What does `RebuildMesh` do instead, and why is that safe for one frame?
