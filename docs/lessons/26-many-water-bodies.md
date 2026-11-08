# Lesson 26 - Many water bodies: a budget for mirrors, and shapes that fit

## Understand

**A scene used to hold one sea. Now it can hold a lake, a river, and a pool on a hill.** Each is a `Water` object with its own settings, name, shape, size and transform. Add one from *Add > Water body*, duplicate it with Ctrl+D, delete it with Del. Everything that was a single flag ("does the water want a mirror?") had to become a decision per body.

## Visualize

**The planar mirror is the expensive part.** A mirror image of the scene is a whole extra scene draw, and it is only correct for ONE water level: the camera is reflected in the plane `y = level`. A pool at y = 30 cannot reuse the lake's image at y = 4.

So `Scene::PlanWater` makes a plan every frame, before any drawing:

1. Skip bodies that are off screen, and bodies the camera is under (they are culled from below).
2. Of the rest that want a planar mirror, sort by distance to the camera.
3. Walk that list. A body at the **same level** as an image already planned (within 5 cm) shares it. Otherwise it gets a new one, until `WaterFramePlan::kMaxReflections` (2) are used. Bodies over budget keep the cheap sky-cubemap reflection.

The renderer draws the planned images; each `Water::Render` asks the plan which image is its own and binds it to `t3` just before its draw. The shader learns from its own constant block whether a mirror is bound (`waterShape.x`), because two bodies drawn in the same frame can disagree.

## Why it is built this way

**Shapes.** A river is a long thin rectangle, and a round pond is the same square plane cut to its inscribed ellipse. The vertex shader passes the plane's own coordinates (-1..1); the pixel shader throws away what lies outside the unit circle and fades the last few centimetres. Tiles that lie wholly outside the circle are never drawn at all.

**The mesh follows the proportions.** Eight by eight tiles are wasteful on a thin strip, so `WaterMesh::ChooseTileGrid` picks 8x8, 12x6, 16x4, 16x2 or 16x1 tiles from the body's length-to-width ratio. Tiles stay square, so cells stay square. A change of grid rebuilds the mesh, so the choice has *hysteresis*: the proportions must move well past a boundary first, otherwise dragging a slider near it would rebuild every frame.

**Old scenes still load.** `sizeZ` did not exist, so a water entry without it is square: the length is set to the width when the scene is read. Names are saved too (*Lake*, *Pool*).

**Asking the water at a position.** With several bodies, "the water height" needs a position. `Scene::FindWaterAt(x, y, z)` finds the body whose outline covers `(x, z)` and whose surface is the highest one at or below `y`: an object floating in a pool above a lake belongs to the pool. If `y` is below every surface, it returns the nearest one above (the point is under water). Then `water->SampleHeight(x, z, scene.GetTime())` gives the exact wave height.

## Other ways, and the trade-offs

**One image per body, no sharing** is simpler but costs a full scene draw per pool. **Screen-space reflections** need no extra scene draw and handle any number of levels, but they cannot show what is off screen and break at the edges. **A reflection probe** (one cubemap baked at the water level) is cheap and rotation-proof but blurry and static. **Splines** make real rivers that bend; a rectangle only goes straight.

## Predict, then check

1. Add a second water body at the same level as the lake, and turn on *Planar* for both. How many mirror images are drawn?
2. Raise the new body 20 units. How many now?
3. Add a third at another level. What does it reflect, and where in the inspector can you see it?

**Answers.** (1) One: same level, shared. (2) Two: one per level. (3) Only the sky; the inspector's Performance section says "Mirror: sky only", because two images is the budget.
