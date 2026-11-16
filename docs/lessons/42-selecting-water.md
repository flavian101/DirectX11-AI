# Lesson 42 - Selecting water

## Understand

**The problem.** Every other object in the scene can be clicked in the 3D view, outlined when it is selected, and moved, turned and scaled with the gizmo. Water could only be selected from the Hierarchy. The cause was one missing override: the editor picks and outlines an object through `GetOrientedBounds`, and `Water` does not provide one (the base class says "cannot be picked by clicking"). The Scale tool was also a trap: a water body has a Width and a Length AND a transform scale that multiplies them, so there were two ways to resize it and one of them (the Y scale) did nothing.

**Why not just give it a box?** A box is the wrong shape. A river is a ribbon that follows a curve across a rectangle many times bigger than the water; a box would make the empty part of that rectangle clickable, and it would steal clicks from the ground and the objects beside the river. A river that falls has no single height to put a box at. A pond is round.

## Visualize

**Picked by its surface.** A click is a ray. `WaterSelect::RayMeetsSurface` finds where it meets the water's surface: first on the level plane, then again at the height the surface has where that landed (a river that falls is lower downstream), until the guess settles. The spot it settles on must then be water: `Water::RayHit` asks `IsOverWater` (inside a river's corridor, lesson 40) and the depth map (over wet ground, not the dry beach). The editor takes the nearest of the objects' boxes, the water bodies and the ground. The terrain is not clickable itself but it can stand in front: a hill between you and a lake hides the lake.

**Click-through.** A water body that is already selected lets the click through to what lies behind it, so a rock under the surface can still be picked. Without that, the water would win every click over it.

**Outlined by its shape.** `DrawWaterSelection` draws the edge of the plane (a rectangle, or an ellipse for a round body) at the water's level, with a light tint inside it. For a river that is a curve the plane's edge is faint and the two banks of the WATER are bright, at the water's own height along the river. These come from `PlaneOutline` and `RiverBanks`, which are plain maths and are tested.

**One way to size it.** The gizmo's Scale tool edits Width and Length: it is composed at the plane's real size in the world, and what it is dragged to becomes the new size (divided by the body's own scale, so a scene saved with a scale of 2 still comes out right). The transform's scale stays as it was. The Transform panel shows the size as text and no scale fields.

**Level, always.** Water can be turned about the vertical and never tilted: the river's frame, the depth map and `IsOverWater` all assume a level body. `SetEditorTransform` keeps only the Y rotation, the gizmo offers only the Y ring, and the panel shows "Rotation Y".

**The Hierarchy.** Double-click any row to frame it (what F does). Water rows say which kind of water they are (ocean, lake, river, pond, custom).

## What changed

- `WaterSelect.h` (new): `RayMeetsHeight`, `RayMeetsSurface`, `PlaneOutline`, `RiverBanks`. Tested in `tests/river_test.cpp`; I broke each on purpose and the tests failed.
- `Water`: `RayHit`; `SupportsScale()` is false; `SetEditorTransform` keeps only the yaw.
- Editor: `PickObject`, `DrawWaterSelection`, the gizmo's water handling, the Transform panel, the Hierarchy rows.

## What this does NOT do

**No hover highlight.** A hover outline needs the pick on every frame, and the terrain's ray cast is not free. It is on the roadmap (E).

**The waves are ignored when picking.** The ray meets the water at rest. A tall wave can put the visible surface a little above or below it, so a click right at the edge of a shore can be a hair off.

**A steep fall at a shallow angle.** `RayMeetsSurface` settles quickly for a gentle fall; on a steep one seen at a grazing angle it may not settle, and uses its last guess.

## Questions before you read the code

1. Why does `PickObject` compare the water's distance with the terrain's AND with the objects', and not just take the nearest object like before?
2. The gizmo is composed with the plane's real size as its scale. Why does `SetEditorTransform` then get `bodyScale` and not the scale the gizmo gave back?
3. What would a click on a river's empty rectangle do if `RayHit` only intersected the level plane?
