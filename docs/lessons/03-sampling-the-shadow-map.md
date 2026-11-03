# Lesson 3 - Sampling the shadow map

## Understand

**Every pixel asks the sun's map a question.** "If I stand where the sun can see me, is anything nearer to the sun along my ray?" If something is, the pixel is in shadow. All the work is in turning a world position into the right texel and the right depth to compare against.

## Visualize

**A pixel's trip, in five hops.** World position, then `lightViewProj` (the sun's camera), then normalized device coordinates, then a texture coordinate `uv`, and finally a lookup. Two details trip people up. The sun is orthographic, so `w` is 1 and no perspective divide is really needed (we divide anyway, it costs nothing). And NDC has *y up* while textures have *v down*, hence `0.5 - ndc.y * 0.5`.

## Why it is built this way

**A comparison sampler.** `SampleCmpLevelZero` does not return the stored depth; it compares it with the depth you pass and returns 0 or 1. With a linear filter the GPU compares the four nearest texels and blends the *results*, which gives soft edges for free. You cannot get this by sampling depth yourself and averaging first: the average of two depths is a depth that exists nowhere in the scene.

**Border address mode, white border.** Outside the map `uv` leaves 0..1. With a border colour of 1.0 and `LESS_EQUAL`, those lookups answer "lit". Without it, everything beyond the map's edge would go dark.

**PCF 3x3.** Nine comparison taps, each already a 2x2 blend, so edges soften over about four texels. It costs nine samples per pixel, only on the sun term.

**The matrix rides the per-frame buffer.** `PerFrame` grew by `lightViewProj` and `shadowParams`: 240 bytes, still a multiple of 16. The C++ struct and the HLSL `cbuffer` must list fields in the same order, because HLSL packs on 16-byte boundaries. A mismatch does not crash; shadows are just quietly wrong.

## Shadow acne, and the answer to the question from before

**Why a lit surface shadows itself.** The map keeps *one* depth per texel, measured at that texel. A surface tilted away from the sun has a depth that *changes across* the texel's footprint. Some of the pixels inside the footprint are farther than the stored value, so they decide "something is in front of me", and that something is the surface itself. The result is stripes that follow the texel grid.

**Three tools, used in small doses.** (1) Constant and slope-scaled bias, applied while drawing the map (lesson 2). (2) Normal offset in the shader: move the lookup point off the surface along its normal, scaled by texel size and by how grazing the angle is. (3) More resolution, which only shrinks the stripes. Too much of (1) or (2) causes *peter-panning*: shadows detach from the base of objects and light leaks under them.

## Other ways, and the trade-offs

**Poisson-disc or rotated taps.** Smoother than a square kernel for the same tap count, but needs per-pixel noise. **VSM/ESM.** Store moments or exponentials so the map can be blurred; cheap soft shadows but light bleeding. **PCSS.** Shadows sharpen near the contact point and blur far from it, as in real life; expensive, since it searches for blockers first.

**Only the sun is shadowed.** `ps.hlsl` multiplies just the directional term by `ShadowFactor`; ambient and point lights are unshadowed, so shadowed areas keep ambient light. The water does not receive shadows yet; it could call the same function.

## Predict, then check

1. Why average comparison *results* rather than depths?
2. You see shadows floating away from the cube's base. Which setting is too high?
3. What does a pixel outside the map's coverage get, and why?

**Answers.** (1) Averaged depths describe surfaces that do not exist. (2) Depth bias, slope bias or normal offset. (3) Lit, because the border colour is 1.0 and the comparison is `LESS_EQUAL`.
