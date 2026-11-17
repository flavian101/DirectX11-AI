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
- **20c. A water LEVEL along the curve (DONE: lesson 41).** `RiverPoint::level` is the water's height at that point relative to the body's own level (0 everywhere = the level river of before; old scenes load that way). It is carried by the curve, read by the carve, baked into the depth map (`depth = level + offset - ground`, the offset also in the map's spare fourth channel), lifted by the vertex shader, and added by `SampleHeight`. "Follow terrain" sets the levels from the ground, never rising downstream. Still open: the reflection mirrors about one height, the shading normal ignores the slope, and the current does not speed up on a steeper stretch.
- **20d. Steps in the level.** A point marked as a drop gives a height jump over a short run: a falling sheet (a steep strip of water, fed by the same level function; it needs its own shading, because the normal there is not the waves' alone), plunge-pool foam, mist (needs the particle system). The slope-dependent current (faster where the fall is steeper) belongs here too.

## Water polish (the review)
A full review of the water's look and its editor workflow (rendering, then workflow) split the work into phases; each stands alone:
- **A. Selecting water (DONE: lesson 42).** Click water where it is water; an outline that follows its shape; the Scale tool edits Width and Length; water stays level; the Hierarchy frames on double-click and tags the kind.
- **B. The inspector (DONE: lesson 43).** Sections Presets / Shape / Current / Appearance / Waves / Reflection / Refraction / Lighting / Foam / Shoreline / Underwater / Performance / Debug, each a group of `WaterSettings` (one list, `VisitGroup`): right-click a header to reset the section, right-click a control to reset it, a dot and a * mark what is not the default. Looks, copy/paste and saved presets (`assets/presets/water`). A master foam intensity, Reflection split into Mode and Quality, the confusingly named controls renamed. Still open: a depth-to-colour gradient and a foam colour (both need the shading rewrite, C).
- **C. Shading v2 (a Classic / Physical switch).** The body colour as light scattered in a volume, not Lambert on the wave normal; per-channel absorption of what is seen through the water, with the real length of the line of sight; roughness from the waves and ripples the distance fades out, driving a GGX sun highlight and the reflection's blur; Fresnel that `reflectivity` no longer switches off; refraction in view space; the sun's shadow on the water; caustics.
- **D. Reflection.** A mip chain on the mirror image, sampled by roughness; faded at the screen edges and with distance; on a river that falls, the mirror plane at the mean level, faded by each pixel's distance from it.
- **E. Later.** Wet sand on the terrain at the waterline, bloom on the sun's highlight, a hover outline before the click.

## Later
- FFT ocean for very large open water; caustics on the bed; ice and frozen rivers.
