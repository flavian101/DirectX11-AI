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
A river that falls cannot be a flat plane: the plane has ONE level, and it draws water over every low spot of its rectangle. So this item is four steps, each usable on its own:
- **20a. Drawing a river (DONE: lesson 39).** Add > River goes straight to the curve; a click extends the nearer end or bends the curve on the water; the plane follows the river; overlaps and tight bends are marked while drawing. Water outside the river's corridor is clipped.
- **20b. A ribbon mesh.** Build the water surface from the `RiverPath` samples (a strip, `d` across and `s` along as its own coordinates) instead of a plane. No wasted rectangle, no square ends, the flow map's (d, s) comes from the mesh. Still level.
- **20c. A water LEVEL along the curve.** `RiverPoint` gets a level; the ribbon, `CarveRiver` (a level function instead of one height) and `SampleFlow` use it. A "Follow terrain" button sets each point to the ground under it minus the bed depth, never rising downstream, so a river drawn on a slope runs downhill by itself.
- **20d. Steps in the level.** A point marked as a drop gives a height jump over a short run: a falling sheet (a vertical strip), plunge-pool foam, mist (needs the particle system).

## Later
- FFT ocean for very large open water; caustics on the bed; ice and frozen rivers.
