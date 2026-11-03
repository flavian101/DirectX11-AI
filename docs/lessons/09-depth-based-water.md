# Lesson 9 - Water that knows how deep it is

## Understand

**One number drives everything: thickness.** For each water pixel, thickness is the distance from the water surface to the ground behind it along the view ray. It is 0 where the water meets the shore and grows as the ground drops away. Colour, transparency and foam are all just different functions of that one number.

## Visualize

**Look at the shore from the side.** The water is a flat sheet; the ground slopes up through it. Draw a vertical line from the camera through a water pixel: it enters the water at the surface and stops at the ground. The length of that stretch is the thickness.

## Why it is built this way

**`LinearizeDepth`.** The depth buffer does not store distance. It stores a value between 0 and 1 that bunches most of its precision near the camera. To compare two distances we must undo that: `z = near * far / (far - d * (far - near))`. Check it with the endpoints: `d = 0` gives `near`, `d = 1` gives `far`. We feed it the water's own depth (`SV_POSITION.z`) and the ground's depth (`t4`), then subtract.

**Colour: Beer-Lambert absorption.** Light passing through water fades exponentially with distance, so `1 - exp(-thickness * absorption)` goes from 0 at the shore towards 1 in deep water. We use it to blend the shallow colour into the deep colour: clear turquoise at the edge, dark blue far out.

**Transparency: a soft shoreline.** Before, the water plane cut through the terrain and left a hard line where it met the ground. Now `edgeFade` brings alpha to zero as thickness approaches 0, and `alphaBody` makes shallow water more see-through than deep water.

**Foam: a band plus noise.** `foamBand = 1 - saturate(thickness / foamDistance)` is 1 at the shore and fades to 0 over `foamDistance`. A clean band looks like a painted line, so we multiply it by moving noise. `ValueNoise` hashes grid corners into random numbers and blends them with the smooth curve `f*f*(3-2f)`. Two layers at different scales, thresholded with `smoothstep`, give ragged blobs. The foam uses `max(alpha, foam)` so it stays solid even where the water itself is nearly invisible.

**Why more parameters in the constant buffers.** The shader now needs a second colour and four more numbers, so `PerObject` gained `materialColor2` and `materialExtra2`, and `PerFrame` gained `screenParams` (near, far, one over the viewport size). It works, but `PerObject` is getting wide for objects that use none of it. A dedicated per-material buffer would be the cleaner next refactor.

## Other ways, and the trade-offs

**Per-channel absorption.** Real water absorbs red much faster than blue. Using `exp(-thickness * float3(0.45, 0.12, 0.05))` gives a more realistic shift to blue and removes the need for two colours. Less art control though.

**Noise textures and flow maps.** A tiled noise texture is cheaper and prettier than hashed noise, and a flow map lets foam move along the shore. **Crest foam** from wave steepness adds foam in open water. **Particle foam** is the high-end option.

**Known approximations.** Thickness is measured along view-space depth, not along the true ray, so at grazing angles the water looks thinner than it really is (divide by the view direction's depth component to fix). An underwater camera is not handled at all. And precision comes from lesson 8: the near plane matters here.

## Predict, then check

1. What is the thickness exactly at the shoreline, and what do the water alpha and the foam do there?
2. What goes wrong if you subtract raw depth values and skip `LinearizeDepth`?
3. Why combine foam with `max(alpha, foam)` instead of blending it into the alpha?

**Answers.** (1) Zero: `edgeFade` makes the water itself invisible while `foamBand` is at its maximum, so you see only foam. (2) Raw depth differences shrink with distance squared, so the foam band would be wide near the camera, thin far away, and in meaningless units. (3) At the shore the water alpha is about 0, so blending would hide the foam too; `max` keeps it visible.
