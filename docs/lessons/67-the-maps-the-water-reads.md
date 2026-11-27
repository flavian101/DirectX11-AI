# Lesson 67 - The maps the water reads (O2, GPU half: step G5)

## Understand

**The FFT's output is not what the water wants.** After the FFT, textures A and B hold eight numbers per texel packed two to a complex number: height, displacement, slopes and the three derivatives. The water's vertex shader needs "where does this vertex move"; its pixel shader needs "which way does the surface face, and is it folding". The assemble pass regroups the numbers by who reads them.

**Choppiness.** Left alone, every point of the water only moves up and down, and crests are rounded hills. Pushing each point sideways toward the crest (displacement x choppiness) makes the crests sharper and the troughs wider. The CPU's `Evolve` returns the displacement unscaled, so the slider is applied here, in the last pass, and changing it needs no new FFT.

**The Jacobian.** The pushing is a map from a flat grid to a moved one. Its Jacobian J says how much the area of a little patch changes: 1 on a flat sea, smaller where the crests pinch together, below 0 where the surface folds over itself. That fold is where waves break, so J is what the foam is made from later.

## How it works

**Two maps, by reader.** Displacement map: `(c * dx, height, c * dz, 0)`, which is already the offset a vertex needs, y up. Slope map: `(slope x, slope z, J, 0)`.

**Why RGBA16F, not 32-bit.** The maps get a mip chain, and a mip is an average of four texels. Half-float textures can be filtered and have mips made for them on every Direct3D 11 card; filtering 32-bit float textures is optional there. Half keeps about 1 part in 2000, 4 mm on an 8 m wave: fine for drawing. Boats read the exact CPU mirror, never these textures.

**Mip chains and why distant water needs them.** A pixel far away covers many texels; reading one of them is a sample from a random place and the water sparkles and crawls. A mip is the average of the area, so the far water is read from a small, averaged level and stays calm. `MipLevels = 0` asks for the full chain, `GENERATE_MIPS` plus a render-target bind flag lets `GenerateMips` fill it in, and the compute pass writes only mip 0.

**Why `UnbindCompute` before `GenerateMips`.** `GenerateMips` renders into the lower mips. A texture that is still bound as a UAV cannot be used that way: unbind first.

**Why the vertex shader will sample with `SampleLevel`.** A vertex shader has no neighbours to guess the mip from, so it must say which mip to read. The pixel shader can use normal `Sample`.

## What the tests show

**The maps are right.** The chain pack, evolve, FFT, assemble gives the displacement, height, slopes and Jacobian of `OceanFFT::Evolve` to 1e-4 of the wave size, in all three bands. With no choppiness nothing moves sideways and J is exactly 1. The half-float reader is checked on the special values and on every positive half (values only ever grow with the bit pattern; the sign bit just negates). Dropping the choppiness, swapping the slopes, adding where the Jacobian subtracts, and a wrong exponent in the half reader each make a test fail.

**On the GPU.** `SelfCheck` ends with three more log lines, "Ocean GPU map check", one per band. They compare the half-float maps with the reference (allowing a bit more than for 32-bit) and print the number of mip levels: 128 gives 8, 256 gives 9.

## Check yourself

1. The Jacobian of a flat sea is exactly 1. What does that tell you about each of the three terms in the formula, and about where in the maps J first drops below 1 as the wind rises?
2. The displacement map stores the offset already multiplied by the choppiness. Name one thing that is easier with that, and one that is harder, than storing it unscaled and multiplying in the shader.
3. A far-away vertex reads mip 0 of the displacement map, a nearby one too. Why can that make the far ocean's silhouette flicker, and what would you change in the shader?
