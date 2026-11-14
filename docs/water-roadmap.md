# Water roadmap (after lesson 37)

Ordered by how much each step changes what you see. Each is a patch with a lesson, like the ones before.

## 15. A river that is a curve (fixes "just a plane")
- **River spline** object: control points you drag in the viewport, width and depth per point.
- **Carve** the terrain along it with a bank profile (steep outside of bends, gentle inside), as ONE undoable stroke. The carve is pure logic in `HeightfieldEdit` (tested), like the brushes.
- **Ribbon mesh** that follows the curve, with a coordinate ALONG the river, so ripples and foam follow bends instead of running straight.
- **Flow map**: flow direction and speed baked per texel from the spline (RG16F), replacing the plane-wide direction. `SampleFlow` then reads it too.
- Confluences and a lake outlet that the spline can end in.

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

## 20. Waterfalls and drops
- Where the river spline steps down: a falling sheet, plunge-pool foam, mist (needs the particle system).

## Later
- FFT ocean for very large open water; caustics on the bed; ice and frozen rivers.
