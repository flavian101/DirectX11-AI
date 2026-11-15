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
- A water LEVEL along the curve (the river falls toward its mouth), then steps in it: a falling sheet: a falling sheet, plunge-pool foam, mist (needs the particle system).

## Later
- FFT ocean for very large open water; caustics on the bed; ice and frozen rivers.
