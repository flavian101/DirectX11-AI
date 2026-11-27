# Lesson 69 - The water draws the FFT sea (O2, G6b)

## Understand

**What changed.** Until now the water was a sum of 16 Gerstner waves the shaders evaluated by formula. Now the sea comes from three FFT cascades made by compute shaders (lessons 64 to 67) and the water just reads them. The vertex shader reads displacement maps, the pixel shader reads slope maps. The old table is no longer used.

**Rest position.** The water is a flat mesh. A vertex at rest position p is drawn at `p + displacement(p)`. The maps are indexed by p, not by where the point ended up, so the normal and the folding at p stay exact however far the point was pushed sideways. That is why the pixel shader reads the slope maps at `input.rest`, not at the world position.

**Three patches that do not line up.** Each cascade repeats every patch size: 2000 m (swell), 250 m (wind sea), 30 m (ripples). The sum repeats only every common multiple of the three, which is far beyond what you can see. Position / patch size, with a WRAP sampler, is the whole addressing scheme.

## How it works

**The vertex shader picks a mip.** A coarse far-away grid cannot hold a ripple smaller than its cells; sampling mip 0 would alias into crawling. The mesh already knows its grid spacing at each distance, so the vertex shader asks for the mip whose texels are about that big: `lod = log2(spacing / texelSize)`. A vertex shader has no neighbours to guess the mip from, so it must name one with `SampleLevel`.

**The pixel shader lets the hardware pick.** `Sample` uses how fast the position changes per pixel, so waves smaller than a pixel average out on their own and the horizon stays calm.

**Normals from slopes.** `N = normalize(-slopeX, 1, -slopeZ)`, summed over the three cascades. The three Jacobians add as `1 + sum(J - 1)`: small folds of one cascade add to those of another.

**The shore, for now.** The old shoaling was written per wave, so it cannot survive. In its place the surface is limited to 0.4 of the depth with a soft clamp `h / (1 + (h/L)^4)^(1/4)`, L = 0.4 depth. Deep water is untouched (within 0.01% while h < L/8), the waterline is flat, and the clamp is monotonic. The CPU copy (`OceanSea::ShoreFactor`) and the shader copy (`ShoreLimit`) are the same formula, so boats and the drawn surface agree. Real shore waves, run-up and backwash come in O6 and O7.

**One sea, two copies, rebuilt rarely.** `Water` builds the CPU sea and the GPU sea from the same numbers: the wind, its direction, the seed and the height scale. It rebuilds only when one of them changes, because building a spectrum on every colour-slider tick would stall the editor. `OceanGpu::Create` keeps its compiled shaders and constant buffers for the same reason.

**Once per moment.** The scene pushes its clock to the water (`SetSceneTime`), exactly as it pushes the wind. `Render` runs the compute passes the first time it is called for a clock value and skips them when the mirror view draws the same moment. The CPU copy updates in `SampleHeight` for the same clock, so a boat and the screen see the same wave.

**Binding discipline.** The maps are bound for the water draw and unbound right after. The next frame's compute passes write them, and a texture still bound for reading would be forced off by Direct3D with a warning.

## What is not there

**The foam trail** needed the sea at earlier times, which a texture made for this frame cannot give: it is off until O8. **The old sliders** (wave count, swell, groups, shelter, spread, swash, foam trail) do nothing now; height scale, seed and choppiness work. They are removed in the clean-up that closes O2.

## Check yourself

1. The vertex shader reads mip `log2(spacing / texel)`. For the 30 m ripple cascade (128 texels) and a grid spacing of 4 m, which mip is that, and what does the vertex see of the ripples?
2. A boat sits at a spot where the sea is pushed sideways by 0.6 m along the wave. What is the boat's height error, roughly, if the surface there has a slope of 0.3?
3. The clamp is applied to the displacement vector, not just its height. What would the crests look like near the shore if only the height were clamped?
