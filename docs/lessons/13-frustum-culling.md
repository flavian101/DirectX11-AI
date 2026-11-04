# Lesson 13 - Frustum culling

## Understand

**Do not draw what you cannot see.** The terrain was one mesh of about half a million triangles, drawn in full three times a frame (sun, mirror, camera), even when you faced away from it. Now it is cut into 32 x 32-quad tiles ("chunks"), each with a bounding box. For each view we first ask "can the camera possibly see this box?" and skip the tile if not. The picture is identical; the work is not.

## Visualize

**The view volume is a pyramid with its tip cut off** (a *frustum*). A tile is in or out depending on which side of six flat planes it sits. Turn on "Frustum culling" in the terrain panel, look at a corner of the map, and watch the "chunks drawn" count fall.

## Why it is built this way

**Six planes from one matrix.** A point is visible when its clip-space coordinates satisfy `-w <= x <= w`, `-w <= y <= w` and `0 <= z <= w`. Each inequality is a weighted sum of the point's coordinates, which is exactly a plane. With row vectors the weights are the matrix *columns*, so every plane is a sum or difference of two columns of `view * projection`: `column3 + column0` is the left plane, `column3 - column0` the right, and so on. This is the Gribb-Hartmann method; see `Frustum::FromViewProj`. We normalize each plane so that `dot(n, p) + d` is a true distance.

**Testing a box with the "positive vertex".** For each plane, pick the box corner that sticks out furthest in the plane's direction. If even that corner is behind the plane, the entire box is outside. One corner per plane, six planes, no loops over triangles.

**Conservative by design.** A box near a frustum corner can pass all six tests and still be invisible. That is fine: a wrong "maybe visible" costs a few extra triangles; a wrong "not visible" makes tiles pop out of existence. When in doubt, draw.

**One frustum per view.** `RenderView` carries the frustum of whatever is being drawn: the camera, the mirrored camera (reflection pass), or the sun's box. That is why `Render` now takes a `RenderView` instead of a `Camera`. It also carries `lodOrigin`, the real camera position, which patch 4 uses.

**World-space boxes.** Each chunk's box is stored in the terrain's local space. The terrain can be moved, scaled and rotated in the editor, so each frame the box is transformed: the centre goes through the matrix, and the half-size is spread through the matrix's absolute values (`TransformBounds`). The result is an axis-aligned box that safely contains the rotated one.

**Normals still use the whole grid.** Each tile's vertices are computed from the full height grid, so lighting has no seams along tile borders.

## Other ways, and the trade-offs

**Bounding spheres** are cheaper to test but fit a flat terrain tile badly. **A quadtree** tests big groups first and skips whole branches; worth it beyond a few thousand objects, overkill for 256 tiles. **GPU culling** (compute shaders plus indirect draws) removes the CPU cost entirely. **Occlusion culling** also skips things hidden behind other things (a mountain hiding a valley), which frustum culling cannot do.

**The sun view rarely culls anything.** Its box is fitted around the whole scene (lesson 2), so every tile is inside it. Culling there only pays off once shadows are fitted to what the camera sees (cascades).

## Predict, then check

1. Why is it safe to treat "maybe visible" as visible, but not the reverse?
2. Which two columns of the matrix give the right plane?
3. You rotate the terrain in the editor. Why does the chunk's world box get bigger than its local box?

**Answers.** (1) An extra draw is invisible; a missing draw is a visible hole. (2) Column 3 minus column 0 (`w - x >= 0`). (3) The smallest axis-aligned box around a rotated box is larger than the box itself.
