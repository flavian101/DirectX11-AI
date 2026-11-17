# Lesson 46 - Reversed-Z: seeing 10 km without the ground flickering

## Understand

**Why now.** The terrain's width stops at 1 km because the camera's far plane was 1000 m, and the next step toward big worlds (tiles, see `docs/terrain-world-design.md`) needs the camera to see much farther. Pushing the far plane out is one number, but it makes the depth buffer lose its ability to tell two far surfaces apart.

**The depth buffer, in one paragraph.** For every pixel the GPU stores one number, the *depth*, and a new surface is drawn only if its depth says it is in front of what is stored. A perspective projection does not store the distance itself: it stores something shaped like 1/distance, scaled so the near plane is 0 and the far plane is 1. That is a good idea (nearby things need fine detail) but two things go wrong far away:

1. the 1/distance curve spends nearly all of the 0..1 range in the first few metres, so the far land is squeezed into the last sliver below 1;
2. a 32-bit float has the *finest* steps near 0 and the *coarsest* near 1, so the squeezed sliver is stored with the coarsest steps.

**The numbers** (`tests/depth_math_test.cpp`; near plane 0.1 m, far plane 10 km, 4 km away): with the normal mapping, two surfaces must be about **9.5 m** apart in depth before the buffer can tell them apart; the terrain, the water and a boat on it fight each other. With the reversed mapping it is about **0.15 mm**.

**The fix is to flip the mapping.** Near plane = 1, far plane = 0. The curve is the same, but now the far land is squeezed into the sliver just above 0, which is exactly where a float has its finest steps. The squeeze and the fine steps cancel instead of adding up. The picture does not change; only the numbers in the buffer do.

## What changed

- `Camera::Update` passes the near and far planes to the projection **swapped**. That alone reverses the depth.
- Everything that clears or tests depth follows the new rule, *clear to 0, and in front means GREATER*: the camera's and the mirror's depth buffers (`Direct3D11Renderer`), the default state objects use (`Scene::cameraDepthState_`), the water, the sky and the boat-hull mask.
- The sky pins itself to the far plane, so it is now `z = 0` in `skybox_vs.hlsl` (it was `z = w`).
- The water shader turns a stored depth back into a distance, `LinearizeDepth`; it has the inverse formula for the new mapping (`DepthMath.h` has the same one, tested).
- `MakePickRay` un-projects the near point at depth 1 and the far point at depth 0, or every click would aim backwards.
- The sun's shadow map is a different, orthographic buffer. It keeps clear 1 and LESS.
- `Camera::kFarPlane` is 10000.

## Check it (by eye)

1. Everything looks the same as before: terrain, sky, water, models, shadows.
2. Fly up high and look at a far corner of a 1 km terrain: it is not clipped.
3. Click to select objects, sculpt and place things: clicks still land under the cursor.
4. Water: shorelines and foam where it meets the ground look as before; sit a boat in the water, and the hull still keeps the water out.
5. Gizmos still follow the selection (ImGuizmo detects a reversed projection, as far as I know; if the handles misbehave, this is where to look).
