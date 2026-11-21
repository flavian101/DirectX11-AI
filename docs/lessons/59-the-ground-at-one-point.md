# Lesson 59 - The ground at one point (C1)

## Understand

**Many things need one height.** A character's feet, an object dropped on the ground, a river laid over the terrain, a line of sight: each asks "how high is the ground here, and which way does it slope?" The engine answered by asking the loaded tile meshes, so the answer depended on which tiles happened to be loaded.

**The world already knows.** Decision 3 says the ground is a function of the world position. Lesson 57 used it for the water's depth map. This lesson uses the same function for single questions.

**It has to agree with what you see.** A height that differs from the mesh by a metre puts a character's feet in the air or in the rock. So the query reads the same triangles a tile is drawn from, not a smoother curve through the same heights.

## How it works

**The same triangles.** Each cell of the grid is split into two triangles along one diagonal, and `Heightfield::Locate` decides which triangle a point is in and how much each corner counts. `WorldGroundFunction::HeightAt` does exactly that over lattice points it computes on demand: four ground evaluations, rivers included. Nothing has to be loaded.

**Normals, the same way.** `NormalAt` blends the normals of the triangle's three corners, each worked out from its neighbours' heights, as `Terrain::GetNormalAt` does. It takes sixteen ground evaluations for one answer, which is fine for one character and too much for thousands of queries a frame.

**A ray finds the surface by walking and then narrowing.** `Raycast` steps along the ray in half-cell steps and watches the ray's height over the ground. When it goes from above to below, bisection narrows the crossing to a fraction of a millimetre. It misses a ridge thinner than half a cell that the ray clips between two steps, but a mesh has no such ridge, because its features are a cell wide.

**Edges and misses are defined.** A bounded world has ground inside its edge and none beyond it, so `HeightAt` says no. A ray that starts below the ground, enters the world below the surface, flies away from the world, or is too short, does not hit.

**The scene asks the world first.** `Scene::GetGroundHeight` now asks every world, then every single terrain, and skips the tiles of a world that has ground. New `GetGroundNormal`, `RaycastGround` and `HasGround` use the same rule. The editor's "Snap to terrain", the river overlay and the river readout call them, so they work on a world too.

## What the test shows

**It matches the tile's own query.** Against `Heightfield::HeightAt` over a tile's samples and apron: within 0.000015 m, 0.000080 m with a river dug in, and normals within 0.000005, at 4000 points, a quarter of them on a tile edge where the apron matters. Across tile borders the ground is continuous with no tile loaded at all.

**Rays land on the ground.** 300 rays from above hit within 0.000023 m of the surface and agree with a slow 5 cm walk to 0.05 m, which is the walk's own step. They miss when they should.

**Not checked: the editor.** Snap an object to a world with the camera far away, and aim a river over ground whose tiles are not loaded.

## Check yourself

1. Why does `HeightAt` use the lattice points around the query and not just ask the source for the height at the exact point?
2. A ray starts above the world, flies over its edge and comes down outside it. Which of the three outcomes (hit, miss, hit on the edge) does `Raycast` give, and why?
3. Sculpting still picks with `TerrainWorld::Raycast`, the tile meshes. Why not use the new ray for that too?
