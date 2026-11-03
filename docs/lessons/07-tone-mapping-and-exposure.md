# Lesson 7 - Tone mapping and exposure

## Understand

**The HDR buffer can hold any brightness; the screen shows 0 to 1.** Something has to decide how the first becomes the second. If nothing does, values above 1.0 simply clip: a bright patch becomes flat white and loses its detail. Worse, each colour channel clips on its own, so the *hue* shifts too. Orange `(3.0, 1.5, 0.3)` clips to `(1, 1, 0.3)`, which is pale yellow.

**A tone mapper is a curve.** It takes unlimited input brightness and returns something in 0..1, bending the top end gently instead of chopping it.

## Visualize

**Plot input on the x axis, output on y.** *Clamp* is a straight diagonal that hits 1.0 and goes flat. *Reinhard* is a smooth bend that never quite reaches 1. *ACES* and *Filmic* are S-curves: a toe that deepens the darks, a middle that adds contrast, and a shoulder that rolls highlights off instead of cutting them.

## Why it is built this way

**Exposure, in stops.** `exposureEV` is a log scale: +1 means double the light, -1 half. The shader multiplies by `exp2(EV)`. Logarithmic because eyes and cameras respond to ratios, so equal slider steps feel like equal brightness changes. Lowering exposure slides the whole image left on the curve, which is how you recover detail in blown-out highlights.

**Order: exposure, then tone map, then encode.** Tone mapping curves are designed for *linear* light. If you applied the sRGB curve first, you would be bending already-bent values: the image looks washed out and double-brightened. This is why lesson 6's encode is the last line of the shader.

**Three operators to compare.** `TonemapReinhard` is one line and safe, but flattens contrast. `TonemapAces` (Narkowicz's fit) gives punchy contrast and soft highlights; it is the common default. `TonemapFilmic` is Hable's Uncharted 2 curve: strong toe and shoulder, with a white point so that a chosen brightness maps to exactly 1.0.

**HDR needs HDR content.** I raised the sun from 0.8 to 2.5 in `DemoScene`. With a sun at 0.8 almost nothing exceeds 1.0 and the tone mapper has nothing to do. Switch the operator to "None" and the sunlit terrain blows out; switch back and the detail returns. Revert the number if you prefer the old look.

## Other ways, and the trade-offs

**Per-channel curves (what we do) versus luminance-based.** Per-channel is simple but shifts hue in very bright colours. Newer operators (AgX, Khronos PBR Neutral, Tony McMapface) try to keep hues stable or keep base colours faithful.

**Auto-exposure.** Real cameras and eyes adapt. A game measures the scene's average luminance (a histogram or a chain of downsampled textures) and moves exposure toward it over time. Natural next step now that the HDR buffer exists.

**Bloom.** Also lives in HDR: bright pixels bleed light into their surroundings. It is much more convincing when "bright" can mean 10.0, not 1.0.

## Gotchas

**The output is still 8-bit.** Smooth gradients such as the sky can show banding after the curve. The usual fix is a tiny bit of noise (dithering) added right before the encode.

**A tone mapper cannot fix bad lighting.** If the scene is simply too dark, exposure compensates; if the maths is wrong, the curve only hides it.

## Predict, then check

1. With tone mapping set to None, what does `(3.0, 1.5, 0.3)` look like, and why is it wrong?
2. Why `exp2(EV)` rather than a plain multiplier slider?
3. What would you see if the sRGB encode ran *before* the tone map?

**Answers.** (1) `(1, 1, 0.3)`: pale yellow; each channel clips independently, so orange loses its hue. (2) A log scale matches perception and photography: each step is a doubling. (3) The curve would act on already-curved values: the image looks washed out and too bright.
