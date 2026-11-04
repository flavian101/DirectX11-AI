# Lesson 14 - Level of detail for the terrain

## Understand

**A far tile does not need 2,048 triangles.** At 200 units away a whole tile covers a handful of pixels, so its triangles are smaller than a pixel: the GPU still pays to process them, and you cannot see the difference. Level of detail (LOD) draws near tiles finely and distant tiles coarsely.

## Visualize

**Tick "Wireframe" in the terrain panel and fly around.** Near you the mesh is dense; farther out it steps down in bands to a sparser net. Each tile picks one of three levels. The "LOD a/b/c" line shows how many drawn chunks use each.

## Why it is built this way

**Same vertices, different connections.** A 32 x 32 tile has 33 x 33 vertices. Level 0 uses all of them (2,048 triangles). Level 1 connects only every second grid line (512 triangles); level 2 every fourth (128 triangles). The vertex buffer is shared and only the index buffers differ, so `Mesh` gained a list of index buffers and `Draw(ctx, lod)`. The last grid line is always kept, so a tile's outline never changes size.

**Choosing a level.** For each visible tile we measure the distance from the camera to the *nearest point of its box* (`DistanceToBox`) and compare it with two thresholds. The nearest point matters: a big tile's centre can be far away while its near edge is right in front of you.

**The real camera decides, for every view.** The mirror pass and the sun pass measure LOD distance from the *real* camera too (`RenderView::lodOrigin`). If each view picked its own level, the terrain's shape in the shadow map or the reflection would disagree with the shape you see, and you would get shadows and reflections that do not line up.

**Hysteresis.** A tile sitting exactly at a threshold would flip levels every frame as the camera wobbles. So a tile only switches level once the distance has passed the threshold by 10%. Cheap, and it removes flicker.

**The crack problem.** Two neighbours at different levels do not share edge vertices. The coarse tile has a straight line between every second vertex; the fine tile's middle vertex sits off that line. In between there is a thin gap where you can see through the terrain. **Skirts** fix it: below every tile edge hangs a vertical strip of triangles, so anything behind a crack is more terrain, not sky. They are copies of the edge vertices moved straight down by `skirtDepth`.

**Winding differs per edge.** Triangles are front-facing when they run clockwise *as seen from outside*, and "outside" is a different direction on each of the four sides, so each edge needs its own vertex order. Get one wrong and that skirt disappears from view. It is worth checking such things with a ten-line script that computes the normals.

**Lighting does not change.** All levels share the vertices' normals, computed from the full-resolution height grid. Only silhouettes and fine bumps simplify, so the transitions between levels are hard to notice.

## Other ways, and the trade-offs

**Stitching.** Rebuild the edge triangles of the finer tile to match its coarser neighbour exactly. No skirts and no extra triangles, but you need to know the neighbours' levels and have index buffers for the combinations. **Geomorphing / CDLOD.** Slide vertices smoothly toward their coarser positions as the tile nears its switch distance, so there is no visible pop at all. **Clipmaps.** Nested rings of fixed-size grids centred on the camera: GPU-friendly and ideal for enormous terrain. **Tessellation.** Let the GPU subdivide patches by distance in the hull and domain shaders.

**Skirt trade-offs.** Simple and robust, but a steep skirt can stretch the texture, and the depth has to be big enough to cover the height gap between levels.

## Predict, then check

1. How many surface triangles does a 32 x 32 tile have at level 0 and at level 2?
2. Why measure distance to the tile's box instead of its centre?
3. Why does the sun's view use the camera position for LOD?

**Answers.** (1) 2,048 and 128, which is 16 times fewer. (2) The centre can be far while the nearest edge is close, so a large tile would be drawn too coarsely right in front of you. (3) All views must draw the same shape, otherwise shadows and reflections would not match what you see.
