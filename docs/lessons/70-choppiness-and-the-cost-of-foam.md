# Lesson 70 - Choppiness, and the cost of foam (O2, found in the editor)

## Understand

**What choppiness is.** An FFT sea moves each point only up and down: the crests are rounded hills. Choppiness also pushes every point sideways, toward the crest of the wave it sits on, by `choppiness x displacement`. Points bunch up at the crests and spread out in the troughs, so crests get sharp and troughs wide. At 0 the waves are rounded; at 1 the push is as big as the wave's height.

**Why white appears.** If the push is large enough, points near a crest are pushed past each other and the surface folds over itself. The Jacobian measures this: 1 on a flat sea, smaller where points bunch, below 0 where the surface has folded. A wave breaks where it folds, so the shader paints foam where the Jacobian is low. White patches popping out at high choppiness are the foam doing its job.

**Why 3.34x height made it extreme.** The sideways push has the same size as the height it comes from, so a sea made 3.34x taller is also pushed 3.34x harder. With choppiness 1 it folded nearly everywhere. The height scale was meant to exaggerate height only (wavelengths, and now the sharpness of the crests, stay as the wind makes them), so the water divides the choppiness by the scale before the maps are made. Below 1 the push shrinks with the height, which keeps the shape.

## The cost

**Why 8 FPS.** Foam is not free. For every foam pixel the shader computes a pattern: five noise lookups, a warp and a nine-cell Voronoi, which is hundreds of arithmetic operations. When foam covers most of the water, that runs on most of the screen, on top of refraction and reflection. Surfaces that fold also draw triangles on top of each other, and every extra layer runs the whole pixel shader again.

**Not computing what is not used.** Far away, bubbles are smaller than two pixels and cannot be drawn without shimmer, so the shader already fell back to an even cover there (`detail = 0`). But it computed the pattern first and then threw it away. Now the pattern is computed only inside `if (detail > 0.001)`. The picture is the same; the far foam pixels skip the expensive part. Rule of thumb: find the cheapest test that says "this result will not be used" and branch on it before the work, not after.

## What to measure

The Performance panel shows the GPU time of the Water pass. Compare it at choppiness 0.55 and 1, and with foam intensity 0. If the second drops a lot, foam is the cost; if not, the cost is elsewhere (overdraw, the compute passes, the maps) and the numbers tell us where to look next.

## Check yourself

1. At choppiness 0 the Jacobian is exactly 1 everywhere. Which part of the foam code does that switch off, and what remains?
2. The branch `if (detail > 0.001)` changes the result of a pixel with `detail = 0.0005` by at most how much, and why is that acceptable?
3. Dividing the choppiness by the height scale keeps the push at the size of the unexaggerated sea. If you wanted the slider "Height" to scale the sideways push as well, what would you see at 3.34x?
