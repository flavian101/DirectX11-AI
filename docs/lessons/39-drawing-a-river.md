# Lesson 39 - Drawing a river

## Understand

**The problem.** Lesson 38 made a river a curve, but drawing one was awkward. Every click joined the downstream end, so clicking out along the river in whatever order the mouse went (1, 2, 3, 4 to the left, then 5 back on the right) made the curve double back along itself: two outlines on top of each other. A new River also started as a 14 x 120 plane with a page of settings, and the curve tool was one of them.

**The second problem.** The pixel shader only clips for ellipse-shaped bodies. A river with a curve still drew water over EVERY spot of the plane's rectangle where the ground was below the level, not just in the river.

## Visualize

**A click does the obvious thing, and shows it first.**
- On the water of the river: a new bend point goes ONTO the middle line, between the two control points it lies between. The curve does not change; the point is a handle.
- Anywhere else: the nearer END of the river is extended, upstream (prepend) or downstream (append).
- A click past an end always extends that end, even right beside it.

The viewport draws what the next click would do (a ring on the curve, or a line from the end that would grow) before you click. Click order no longer matters; the numbers (and the flow direction) follow the geometry. `Reverse` flips it.

**Problems are marked where they are.** `RiverPath::Analyse` finds a bend tighter than the river is wide (lesson 38's warning, now with its place) and two stretches whose water lies on top of each other (far apart ALONG the river, closer than their half widths ACROSS). They show as red rings while you draw.

**The plane follows the river.** After a point is added, deleted or a drag ends, the body is moved to the middle of the points' box and every point is shifted the other way: the river stays where it was in the world, and the plane (sized to the box plus the banks) covers it without being twice as big as it needs to.

**The river is only where its curve is.** `RiverFrame` reports when a pixel is outside the river's corridor (the flow map is empty there) and the shader clips it.

## What changed

- `RiverPath.h`: `Sample::span`, `Hit::qx/qz`, `TightestBend` (with the place), `Place`, `Analyse`. Tested in `tests/river_test.cpp`, including the click sequence above.
- Editor: Add > River enters draw mode at once; click = `Place`; the next-click preview; red rings; Enter or Esc = done; a point moves only after the mouse really drags (so a click never nudges one); `FitRiverPlane` and the "Fit the plane as I draw" switch.
- `water_ps.hlsl`: the water is clipped outside the river's corridor.

## What this patch does not do

**The river is still level and still a plane.** Water outside the corridor is hidden, but the mesh is still a rectangle, the ends are still round, and the level is still one number. Roadmap item 20 (b to d) fixes that.

**Boats and buoyancy.** `Water::IsOverWater` still answers for the whole rectangle, so something floating outside the corridor floats on water that is no longer drawn. The ribbon mesh makes the two agree.

## Questions before you read the code

1. Why does `Place` insert at the point projected onto the middle line (`Hit::qx/qz`) and not at the click?
2. Why is "far apart along the river" part of the overlap test? What would a plain "two samples closer than their half widths" report on every bend?
3. `FitRiverPlane` asks `BodyToWorld` for the centre BEFORE it changes anything. What goes wrong if it asks after `SetSettings`?
