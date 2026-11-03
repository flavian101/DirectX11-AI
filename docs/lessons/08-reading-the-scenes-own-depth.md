# Lesson 8 - Reading the scene's own depth

## Understand

**To know how deep the water is, you must know where the ground is.** For every water pixel we want the distance from the water surface to whatever solid thing lies behind it along the view ray. That information already exists: it is the depth buffer the opaque objects just filled in. This patch makes it *readable* and splits the frame so the water draws after it is complete. Nothing looks different yet.

## Visualize

**The depth buffer is a height map seen from the camera.** Each pixel stores how far away the nearest solid surface is. The water pass can look up the same pixel and compare: ground at 12 m, water surface at 10 m, so there are 2 m of water here.

## Why it is built this way

**A depth buffer you can read must be created differently.** The old one was `D24_UNORM_S8_UINT`: usable as a depth target, but no shader view could be made for it. The scene depth is now a `DepthTarget` (lesson 1): a typeless 32-bit float texture with a depth view and a shader view. Stencil was unused, so nothing is lost.

**The read-only depth view.** Lesson 1 said: never bind a texture as depth target and shader input together. There is one legal exception, a *read-only* depth view (`D3D11_DSV_READ_ONLY_DEPTH`). The pass can still depth-*test* against the buffer, but cannot write to it, so reading it as a texture at the same time cannot conflict. The water never writes depth anyway, so this costs it nothing.

**Two phases instead of one.** `Scene::RenderPhase` draws either the *opaque* things (sky, terrain, cube: they write depth) or the *transparent* things (the water). Between the two, the renderer swaps to the read-only view and binds the depth texture at `t4`. Transparent objects must come after: they blend over what is already there, and they need the complete opaque depth to measure against.

**Un-bind it again.** After the water pass, `t4` is cleared, and `BindSceneTarget(false)` also clears it before binding the writable view next frame. If the depth texture were still bound as an input when the writable view is set, D3D silently drops one of them.

**The near plane moved from 0.01 to 0.1.** Perspective depth is not evenly spread: most of the precision sits close to the camera. Near the far end, the smallest depth step is roughly `z squared / near` times the float epsilon (about 6e-8). At 100 units: with `near = 0.01` that is about 6 cm; with `near = 0.1`, about 6 mm. Shoreline foam is measured in centimetres, so this matters. Nothing in your scene is closer than 0.1.

## Other ways, and the trade-offs

**Copy the depth.** `CopyResource` into a second texture and sample that. Works without read-only views, and is needed when a pass must *write* depth and read it, but costs memory and bandwidth.

**Reversed-Z.** Swap near and far and use a float depth buffer: float precision is densest near 0, which now holds the *far* distances. Greatly improves distant precision. A bigger change (projection, depth test, clear value), so a good later lesson.

**A depth pre-pass or G-buffer.** Techniques that make depth available earlier; worthwhile when many effects need it.

## Predict, then check

1. What happens if the writable depth view is bound while `t4` still holds the depth texture?
2. Why does the water not need to write depth anyway?
3. Why did moving the near plane improve the foam?

**Answers.** (1) D3D nulls one of the two bindings; the water would read a black depth texture (or the opaque objects would stop depth-testing correctly). (2) Water blends over the scene and is see-through: writing its depth would hide things behind it from later draws. (3) Foam depends on tiny depth differences at a distance, and a larger near plane gives those differences more precision.
