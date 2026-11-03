# Lesson 10 - Planar reflection

## Understand

**The cubemap can only reflect the sky.** The water showed no terrain, no cube, nothing standing near it. A real mirror shows the world as seen from a *mirrored viewpoint*. So we draw the scene a second time from that viewpoint into a texture, and each water pixel looks up the matching spot in it.

## Visualize

**Picture your reflection twin.** You stand at height 20 above a lake at height 4. Your mirror twin hangs below the surface at `2 x 4 - 20 = -12`, looking up at the same point on the water that you look down at. Whatever the twin sees through that point is what the mirror shows you.

## Why it is built this way

**Mirror the camera, not the world.** The obvious trick (scale the world by -1 on Y) turns every triangle inside out, so culling breaks, and every material with its own cull state (the skybox culls front faces) breaks differently. Instead `Scene::RenderReflection` moves the camera to `2h - y`, flips its look direction and up vector, and builds an ordinary `LookAt`. The scene is untouched and winding stays correct.

**The price: the image comes out left-right flipped.** A reflection reverses handedness, and `LookAt` rebuilds its right vector from up and forward, so the result is a horizontal mirror of the true reflection. The water shader undoes it with `1 - screenUV.x`.

**A clip plane hides what is under the water.** The mirrored camera sits *below* the surface, so the lake bed between it and the surface would appear in the mirror. `vs.hlsl` now outputs `SV_ClipDistance0 = dot(worldPos, clipPlane)`; the rasterizer discards pixels where it is negative. The plane `(0, 1, 0, -h)` keeps only what is above water level `h`. The main pass sets `(0, 0, 0, 1)`, which is never negative, so nothing is clipped. The skybox has no clip output, so the sky is drawn whole.

**Looking it up.** A water pixel's screen position (`SV_POSITION.xy` times the `1/size` in `screenParams`) is the same position in the reflection image, because both use the same projection. We nudge it by the wave normal (`N.xz * distortion`) so waves wobble the mirror, then flip `u`. A clamp sampler (`s2`) stops lookups wrapping around the screen edge.

**Half resolution by default.** The reflection is seen through ripples anyway, so a quarter of the pixels looks nearly identical. It is stored in HDR format, so sun glints above 1.0 survive.

**Same ownership rule, again.** The reflection image is a *render target* in its pass and a *shader input* in the water pass. `RenderReflectionPass` unbinds `t3` before drawing into it, and the renderer unbinds it after the water.

## Cost and other ways

**One extra scene draw.** The terrain is now drawn three times per frame (sun, mirror, camera). Mitigations: a coarser mesh or fewer objects in the mirror, half resolution (done), updating every few frames, skipping it when no water is on screen.

**Screen-space reflections.** Ray-march the depth buffer: no extra scene draw, but it can only reflect what is already visible on screen (nothing behind you or off-screen), and breaks at the edges. **Cubemap probes** are cheap but static. **Ray tracing** is the exact answer, if the hardware allows.

**Limits of this version.** The mirror plane is the water's flat base level, so waves only distort the image rather than change it. Objects that cross the surface (the cube) are cut at the plane. And with the camera below the water the pass is skipped and the cubemap is used.

## Predict, then check

1. The camera is at y = 20 and the water at y = 4. Where is the mirrored camera?
2. Why is flipping the world's Y scale a poor way to make the mirror image?
3. What would you see without the clip plane?

**Answers.** (1) `y = 2 x 4 - 20 = -12`. (2) It reverses triangle winding, so culling hides the wrong faces; every material with its own cull state needs special handling. (3) Ground that lies under the water, between the mirrored camera and the surface, would show up in the reflection and block the view.
