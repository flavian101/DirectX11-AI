# Lesson 6 - Linear light and gamma

## Understand

**Two facts that collide.** (1) Light adds up linearly: two identical lamps give twice the energy. (2) Screens and image files do *not* store brightness linearly; they use the sRGB curve, which gives dark tones more of the available numbers because our eyes notice dark differences more. So the value 0.5 in a PNG is not "half the light". It is about 0.21 of it.

**Lighting maths is only correct on linear numbers.** Multiplying a texture by a light, adding two lights, averaging four texels for a mipmap: all of those assume the numbers mean *amount of light*. Before this patch the engine did them on curved numbers: midtones came out too dark or too contrasty, mipmaps darkened distant detail, and added lights blew out too fast.

## Visualize

**A grey ramp.** Draw a bar from black to white in sRGB: the left half of the bar holds only about 21% of the actual light. The curve is roughly `value ^ 2.2`. The whole job is to be on the linear side while doing maths and on the curved side when talking to files or the monitor.

## The pipeline now

**Files (sRGB) -> sampler (linear) -> lighting (linear) -> HDR buffer (linear) -> resolve (encode to sRGB) -> monitor.** The monitor applies its own opposite curve, which cancels ours.

## Why it is built this way

**sRGB texture formats.** `Texture` creates colour images as `R8G8B8A8_UNORM_SRGB`. The GPU then converts each texel to linear *before* filtering, so bilinear blending and mip generation average real light, not curved numbers. This is the cheapest correct fix: no shader changes at all.

**Colours you pick are sRGB.** The ImGui picker shows display colours. We keep storing them that way, and convert at the boundary: `LightSystem::Apply`, the water material and the background clear colour call `SrgbToLinear` on the way to the GPU. Rule: *store what the user sees, upload what the maths needs.*

**Data is not colour.** `ColorSpace::Linear` exists for normal maps, roughness and masks, where the numbers mean what they say. Marking them sRGB would bend the data (flat-looking normal maps are the classic symptom). The terrain heightmap is read raw by `stb`, so it was never affected.

**Encoding in the resolve shader.** `LinearToSrgb` uses the exact two-part curve: a short straight segment near black and a 1/2.4 power above it.

## Other ways, and the trade-offs

**An sRGB render target view.** Create the back buffer view as `R8G8B8A8_UNORM_SRGB` and the hardware encodes on write. Less code, but it depends on swap-chain model and format rules, and tone mapping (lesson 7) has to happen *before* the encode anyway. A shader we control is explicit and works everywhere.

**`pow(c, 1/2.2)`.** Close to sRGB, not identical, and noticeably off in the darks. We use the exact curve.

**Skipping all this.** Possible, and many old engines did. The price is a permanent tuning fight: you keep adjusting light intensities to hide wrong maths, and every new effect (HDR, bloom, physically based shading) inherits the error.

## What you should see

**Soft, natural falloff.** Lit-to-shadow transitions on the terrain are smoother, and distant texture detail no longer darkens. Tick off "sRGB encode" in the Camera panel to see raw linear light: dark and harsh. That shows how much the curve was doing for you.

## Predict, then check

1. With sRGB encode switched *off*, is the scene darker or brighter than with it on? Why?
2. You add a normal map. Which `ColorSpace` do you load it with?
3. Why must the texture be marked sRGB for mipmap generation to be correct?

**Answers.** (1) Darker: linear values are small numbers, and the encode curve lifts them. (2) `ColorSpace::Linear`. (3) With an sRGB format the GPU decodes texels to linear, averages them, and re-encodes; without it, it would average the curved numbers and darken every mip level.
