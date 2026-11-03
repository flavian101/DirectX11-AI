# Lesson 2 - The shadow pass

## Understand

**A shadow is a question: can the sun see this point?** We answer it in two steps. First, draw the scene from the sun's position and keep only depth: each texel remembers the *nearest* surface along its ray. Later (lesson 3), any pixel can ask "is something nearer to the sun than me?".

**Today you can see the map.** Open the Lights panel and tick "Show sun depth map". Darker red means closer to the sun.

## Visualize

**Stand where the sun is, look along its rays.** Every ray hits something first. That first hit is all the map stores. A hill, the cube, the ground behind them: only the front-most survives per texel.

## Why it is built this way

**Orthographic, because the sun's rays are parallel.** A perspective projection would make rays fan out like a torch. `XMMatrixOrthographicLH` gives a box; everything inside it is seen along parallel lines.

**The box wraps a bounding sphere of every caster.** `Scene::ComputeShadowBounds` merges each caster's sphere, and `ShadowMap::Begin` sizes the box to `2 x radius`. The terrain is about 240 x 240 x 45 units, so radius is roughly 171 and each of the 2048 texels covers about 0.17 units. The cube is 2 units wide, so it gets only about 12 texels. That is why edges look soft at low resolution.

**Bias is applied while drawing the map.** `DepthBias` and `SlopeScaledDepthBias` in the rasterizer state push the stored depths slightly away from the sun. Lesson 3 explains why this is needed; for now notice the state is built in `CreateRasterizerState`, and that a rasterizer state is immutable, so changing the bias means building a new one.

**Cull back faces, not front faces.** Drawing only back faces into a shadow map is a classic trick: the stored depth sits on the far side of a closed object, far from the lit surface, so self-shadowing nearly disappears. It cannot work here: the terrain is a one-sided sheet, and with front faces culled it would vanish from the map. So we cull back and use bias.

**`DepthClipEnable = FALSE` ("pancaking").** A caster between the sun and our box would be clipped by the near plane and cast no shadow. With clipping off its depth clamps to 0 instead, so it still blocks light.

**State hygiene.** `Begin` first unbinds the shadow map from shader slot t2, because it is about to be bound as a depth target (lesson 1). It also resets the depth-stencil state: the last thing drawn last frame was the water, which switches depth *writes* off. Forgetting that gives you an empty map and no error. `End` unbinds the depth target so lesson 3 can read it.

## Other ways, and the trade-offs

**Tight box in light space.** Better texel density, but its size changes whenever the sun moves, so shadow edges crawl. The sphere is rotation-proof.

**Cascaded shadow maps.** Several maps, each covering a slice of the camera's view: sharp near you, coarse far away. This is what large games do. It is the upgrade path when the world grows.

**A render graph.** `Renderer::Render` now lists two passes by hand. With four or five (shadows, scene, post-processing, reflections) a small pass list becomes cleaner. Not yet.

## Predict, then check

1. Why does `Begin` reset the depth-stencil state?
2. What would the shadow map look like if the terrain were drawn with `D3D11_CULL_FRONT`?

**Answers.** (1) Water leaves depth writes disabled; without the reset the map stays empty. (2) The terrain's front faces point at the sun, so they would all be culled and the terrain would be missing from the map; only the cube's far side would remain.
