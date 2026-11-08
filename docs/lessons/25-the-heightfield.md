# Lesson 25 - The heightfield: asking the terrain about itself

## Understand

**The problem.** The terrain knew how to draw itself but not how to answer questions. `BuildTerrainModel` read the heightmap, added noise, put the result in a vector called `heights`, built triangles from it and returned. The vector died at the closing brace. Only the triangles survived, and they live on the GPU.

**Why that blocks what you want.** Placing a tree needs "how high is the ground at x, z?". Clicking the ground needs "where does this ray meet it?". A sculpt brush needs both. The CPU cannot read vertex buffers back (and should not: it is slow), so none of these could be answered.

**Your first question, answered.** After `BuildTerrainModel` returns, `heights` is gone. `heightmap_.values` survives, but it is the *source picture* at the picture's resolution, before noise and before resampling to the mesh. The vertex buffers survive, but on the GPU. So the honest answer to "how do I find the height under the mouse?" was: you could not, not exactly. Re-reading the PNG and re-running the noise would duplicate the logic and drift out of sync with the mesh. The fix is to keep the final grid.

## Visualize

**Hover the terrain.** In the editor, move the mouse over the terrain: `Ground: x, y, z` appears at the bottom left of the viewport. Over a hill y rises, over the sea floor it falls, over the sky nothing shows.

**Snap to terrain.** Add a cube, drag it into the air, select it and press *Snap to terrain* in the Inspector. It drops until the bottom of its box touches the ground. On a slope it rests on the highest ground under it, so it never sinks in.

**No rotation for the terrain.** Select the terrain: its Rotation row is gone and the Rotate gizmo does not appear. Part of this lesson is why.

## Why it is built this way

**A pure class.** `Heightfield` (`src/Heightfield.h/.cpp`) has no Direct3D in it, like `TerrainSkirts.h`. That is why it has a test you can run without a window: build the `HeightfieldTests` target and run it.

**Local space.** The field lives in the terrain's own space, before position and scale: x from -sizeX/2 to +sizeX/2, same for z, y from 0 to `heightScale`. That is exactly where `BuildTerrainModel` puts its vertices, so the mesh and the field cannot disagree.

**A height is not "bilinear".** A cell is cut into two triangles, and the surface inside each is flat. To get the height the mesh really draws, find which triangle holds the point, then interpolate between *its* three corners.

```
 TL ------- TR      z grows downward, like v in the texture
  |       /  |      fx + fz <= 1  ->  triangle (TL, BL, TR)
  |     /    |      fx + fz  > 1  ->  triangle (TR, BL, BR)
  |   /      |      the diagonal is where fx + fz == 1
 BL ------- BR
```

**The weights are barycentric.** Inside a triangle, a point is a blend of its three corners, and the blend weights add up to 1. The first triangle uses weights `1 - fx - fz`, `fz`, `fx` for TL, BL, TR. The GPU interpolates colours and normals across the same triangle with the same weights, so using them here makes the CPU and the GPU agree.

**The test proves it.** `TestMatchesMesh` builds the mesh's triangles with the same index pattern as `BuildTerrainModel`, fires vertical rays at them by brute force, and compares. The worst difference is about 0.000006. To check the test can fail, flip the diagonal in `Locate`: the error jumps to 0.32 and the test goes red. A test that cannot fail proves nothing.

**Normals use the mesh's formula.** `VertexNormal` is `makeVertex`'s central difference (the height of the next sample minus the previous one, over twice the cell size). `NormalAt` blends the three corner normals with the triangle weights and renormalises, because blending shortens a vector.

**Ray against the field: clip, march, bisect.** First clip the ray to the box that contains the field; a miss there is a miss. Then walk along the ray in half-cell steps and watch the *gap*, the ray's height minus the ground's height. While the ray is above ground the gap is positive; the first step where it is zero or negative has gone through the surface. Finally halve that bracket 24 times, keeping the half where the gap changes sign.

**Why half a cell.** A step longer than a cell can jump over a hill. Half a cell is a safety margin, not a proof: a spike narrower than that can still slip between two steps. The exact method walks cell by cell (a *DDA*) with a true ray-triangle test in each cell. It is the upgrade if a missed spike ever becomes a real problem.

**"From above" only.** If the ray starts under the ground, `Raycast` returns false. Clicking from below the terrain has no useful meaning for placement.

**Position and scale are allowed, rotation is not.** With only a move and a scale, world to local is "subtract the position, divide by the scale". With a rotation, a straight-down query becomes a slanted line through the field, and every query needs an inverse matrix. Normal engines avoid it: terrain is an axis-aligned grid (see `docs/terrain-world-design.md`). So `Terrain::SetEditorTransform` now ignores rotation.

**Far chunks and level of detail.** The queries answer for the full-detail mesh. A far chunk drawn at a coarse level skips vertices, so its visible surface differs slightly. Game logic must not depend on where the camera is, so objects are placed against the full-detail ground. At that distance nobody sees the small mismatch.

## Your second question: what changes on the GPU when a brush touches 3 chunks?

**A mesh holds two kinds of data.** The *vertex buffer* is data: position, normal, texture coordinate, tangent (48 bytes each). The *index buffers* are topology: which vertices form each triangle, once per level of detail, plus the skirt parts. Raising a hill moves vertices. It never changes which vertex connects to which.

**What a stroke must change.** The height of the touched vertices. Their normals, and the normals of their four neighbours, because a normal reads the samples on either side of it. The chunk's bounding box, or culling would use stale bounds. The skirt vertices, which are copies of the edge vertices moved down. And the border vertices, which every chunk keeps its own copy of: a stroke on a seam updates both chunks.

**What stays.** All three index buffers of every chunk, and every chunk the stroke did not touch.

**Why it cannot be done today.** `Mesh` creates both buffers with `D3D11_USAGE_IMMUTABLE`: the GPU may read them but nobody may write them, ever. So today a one-vertex change means rebuilding every chunk: about 60 MB of vertices at 1024 cells. In the next step (sculpt brushes) the terrain's vertex buffers become `D3D11_USAGE_DEFAULT`, and `UpdateSubresource` with a box rewrites just the touched chunks. One chunk is 1,221 vertices (33 x 33 grid plus 132 skirt) times 48 bytes, about 57 KB. A stroke over four chunks uploads roughly 230 KB instead of 60 MB.

## Other ways, and the trade-offs

**Keep heights on the GPU only.** Store the heights as a texture and let the vertex shader read it. Edits become GPU draws, memory is small and chunks can share one grid mesh. But the CPU still needs its own copy for picking, placement and physics, so you keep this class anyway, and the rewrite is large.

**Store 16-bit integers instead of floats.** Half the memory, and the exact format of a 16-bit heightmap PNG. The sculpting step will probably switch, since saving edited heights needs that format.

**A min/max quadtree for rays.** Skip whole regions that the ray flies over. Worth it for very long rays on huge terrains; not for a click.

## Predict, then check

1. The terrain sits at `position.y = 5` with scale 1, and the field says the local height at a point is 12. What does `GetHeightAt` return?
2. A ray starts at height 3 where the ground is at 8. What does `Raycast` return?
3. A brush edits one sample at the corner shared by four chunks. How many chunks' vertex data must change?
4. Why does the heightfield test flip the diagonal on purpose?

**Answers.** (1) 17: `position.y + localHeight * scale.y`. (2) False: the ray starts under the ground, and this is a from-above query. (3) Four: each chunk keeps its own copy of the border vertices. (4) To prove the test can fail; a test that always passes tells you nothing.

## Rebuild cold

**Do this before reading `Heightfield.cpp` again.** Delete the bodies of `Locate` and `HeightAt`, and rewrite them from the diagram above. Build `HeightfieldTests`: if `TestPlane` and `TestMatchesMesh` pass, you have it. If they fail, the printed error tells you whether the diagonal, the weights or the edge handling is wrong.
