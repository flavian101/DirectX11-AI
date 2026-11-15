# Lesson 41 - A river that falls

## Understand

**The problem.** A water body has ONE level: the plane sits at `settings.height`, the depth map measures `level - ground`, the carve digs to one height, `SampleHeight` returns one height. A river that runs down a hillside cannot be that. Either the water floats above the lower ground or the upper ground sticks out of it.

**The idea.** Give the river's points a height of their own, and make every part of the system that used "the level" ask "the level HERE" instead. `RiverPoint::level` is measured relative to the body's own level (world units, negative = lower). Relative, because 0 everywhere is then exactly the river of before: old scenes load unchanged (the JSON macro fills a missing field with the default), and moving the body moves the whole river with its fall.

## Visualize

**Along the curve.** `Build` gives each sample a level, straight between the two points it lies between (not a spline: a spline can overshoot and run uphill between two points that do not). `Closest` returns the level at the nearest spot, so anything that asks "where is this spot on the river" also gets "how high is the water there".

**Who reads it.**
- **The carve** digs each part of the channel to ITS level: `CarvedHeight` takes the level of the nearest sample, so the bed, the water's edge and the bank all fall together. The terrain converts the fall from world units to sample values like it converts everything else.
- **The depth map** is `level + offset - ground` with the offset read per texel. The offset also goes into the map's fourth float, which was unused, so no new texture or slot was needed.
- **The vertex shader** lifts each vertex by that fourth float (`WaterLevelAt`), so the surface follows the curve down. `DepthField::SampleLevel` is the CPU twin, and a test checks it against the shader's own bilinear blend over the packed texels.
- **`SampleHeight`** adds `LevelAtWorld`, so a boat, a wave query and `SampleNormal` (a finite difference of heights) see the slope.

**The level has to reach beyond the river.** The vertex shader runs at the corners of every triangle of a drawn tile, and a far-LOD triangle can reach well past the corridor. A zero out there would pull the edge of a falling river up toward the body's level. So the river's bake spreads each texel's level outward from the river to fill the whole map: two sweeps (forward, then backward), each texel taking the nearest source its visited neighbours know. Not an exact distance, only a smooth one, which is all that is needed.

**Follow terrain.** `FollowGround` sets each point's level to the ground under it, but never higher than the point before: a river runs downhill. Where the ground rises, the water stays level and the carve digs through.

**Editing.** Each point has a level field. The curve, its handles and the problem rings are drawn at the water's height along the river, and a point is dragged on ITS plane: a point on a lower stretch lands under the cursor, not beside it. A new point starts at the level its neighbour (or the curve, if it is added on the water) has.

## What changed

- `RiverPath.h`: `level` on `RiverPoint`, `Sample`, `Hit`; `Build`, `Closest`, `Place`; `FollowGround`.
- `RiverCarve`, `Terrain::CarveRiver`: the carve follows the level.
- `WaterShore.h`: `Bake` takes per-texel offsets; `DepthField` keeps them (`SampleLevel`, the fourth packed float).
- `WaterRiver.h`: `Bake` can output the levels; `SpreadToNearest`.
- `Water`: bakes the levels with the flow map, measures the depth map again when they change (the revision), `LevelAtWorld`, `SampleHeight`.
- Shaders: `WaterLevelAt`, and the vertex shader's lift.
- Editor: the Fall section (Follow terrain, Make level, an "uphill" warning), per-point level, level-aware handles.
- `SceneJson.h`: `level` is saved.

## What this does NOT do

**A depth map is needed.** The levels travel in the depth map, which exists only for a body over terrain. Without terrain the river stays level.

**The reflection is one plane.** Planar reflections mirror about the body's own height. A gentle fall looks fine; a steep one reflects the wrong place.

**The shading normal ignores the slope.** The normal comes from the waves alone, so a steep stretch is lit as if flat. A waterfall will need its own.

**The current is not faster on a steep stretch.** Flow speed does not depend on the fall yet.

**A cliff is a stretched slope.** Two points with a big fall over a short run make a steep ramp of large triangles, not a step. Roadmap 20d.

## Questions before you read the code

1. Why is `level` relative to the body's level and not an absolute height? What would break in old scenes, and when the body is moved, if it were absolute?
2. `Build` interpolates levels straight between points while positions use a Catmull-Rom spline. What goes wrong if the levels used the spline too?
3. The depth map is measured again when `riverLevelRevision_` moves, and the revision moves only when the levels really change. Why compare the vectors instead of bumping it every time the river is baked?
4. Why does `SpreadToNearest` run forward then backward, and what would one sweep leave unfilled?
