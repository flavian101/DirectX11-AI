# Water roadmap (after lesson 37)

Ordered by how much each step changes what you see. Each is a patch with a lesson, like the ones before.

## 15. A river that is a curve (DONE: lesson 38)
- River curve through control points, drawn in the viewport; the terrain carved along it as one undo step; a flow map so ripples, foam and `SampleFlow` follow the bends.
- Left for later, from lesson 38: a ribbon mesh (needed only if the plane's wasted area ever shows in profiling), waves that follow the curve, round river ends, confluences and a lake outlet.

## 16. Foam with memory
- A foam texture around the camera, ping-pong: each frame it decays and gets new foam written (crests, shores, rocks), advected by the flow map.
- Replaces the analytic trail (`WaveFold` x3) with one texture read, and makes wakes possible.

## 17. Wakes and objects
- Boats stamp foam and a V-shaped ripple into the foam texture; boats drift with `SampleFlow`.
- Rocks in a river: standing waves upstream, foam downstream, flow bending around them (obstacle distance in the flow map).

## 18. Water under the second light
- The water shader ignores the moon (`dirDirection2`/`dirColor2`): no moon glint, foam unlit at night. Add it, with the fog and underwater look already in.

## 19. The shore on land
- Wet sand band that follows the swash on the terrain, foam residue that fades, darker wet rocks.

## 20. Sloped rivers, waterfalls and drops
A river that falls cannot have one level. Four steps, each usable on its own:
- **20a. Drawing a river (DONE: lesson 39).** Add > River goes straight to the curve; a click extends the nearer end or bends the curve on the water; the plane follows the river; overlaps and tight bends are marked while drawing. Water outside the river's corridor is clipped.
- **20b. Only the river is water (DONE: lesson 40).** Tiles no part of the corridor reaches are not drawn; `IsOverWater` (so `SampleHeight`, `SampleFlow`, the camera's "under water") says no outside the corridor. NOT a ribbon mesh: the depth map, flow map, vertex shader, LOD tiles and mirror passes all live in the plane's own space, and a ribbon would fork every one of them for no visible gain. Revisit only if profiling shows the vertices of the tiles the corridor touches are the cost.
- **20c. A water LEVEL along the curve.** `RiverPoint` gets a level, and four things stop assuming one level: (1) the depth map (`depth = level - ground` becomes `levelAt(s) - ground`; the flow map and the depth map share one texel grid, so `s` is already at hand when the depth is measured), (2) the vertex shader (Y offset = `levelAt(s) - baseLevel`, `s` read from the flow map: it must be bound to the vertex stage too), (3) `CarveRiver` and the editor's pick plane (a level function instead of one height), (4) `SampleHeight`/`SampleFlow` on the CPU. A "Follow terrain" button sets each point to the ground under it minus the bed depth, never rising downstream, so a river drawn on a slope runs downhill by itself.
- **20d. Steps in the level.** A point marked as a drop gives a height jump over a short run: a falling sheet (a steep strip of water, fed by the same level function), plunge-pool foam, mist (needs the particle system).

## Later
- FFT ocean for very large open water; caustics on the bed; ice and frozen rivers.
