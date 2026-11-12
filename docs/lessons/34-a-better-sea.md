# Lesson 34 - A better sea, and why a big plane lost its waves

## Understand

**Why the waves died when you scaled the plane up.** The water is a fixed 8 x 8 grid of tiles, and every tile has the same number of cells. Scale the plane and the cells grow with it. The vertex shader fades out every wave shorter than three cells (`maxK` in `water_vs.hlsl`), because a grid that coarse can only draw noise instead of it. On a 1000-unit plane one cell was about 4 units wide, so even your main wave was fading and only the long ones survived.

**The fix is to grow the grid with the plane.** `ChooseTileGrid` now adds tiles as the body gets bigger, so the finest cell stays near a target size (derived from the wave frequency). The tile count moves in steps (8, 12, 16, 20, 24) so dragging a size slider rebuilds the mesh a few times, not every frame, and a vertex budget caps the total.

**The sea itself was a fixed list.** Eight hard-coded waves with a fixed angle, wavelength and phase each. It cannot make a swell, it repeats the same pattern everywhere, and every wave had the same steepness. The new Spectrum model builds the table from a handful of settings instead.

## Visualize

**Wind sea.** Picture the biggest wave pointing straight along the wind. Each next wave is about 1.4 times finer, lower, and aimed a little further off the wind, so the small waves scatter wider than the big one. That is how a real sea is built.

**Swell.** One to three long, low waves from their own direction (the swell angle). They are slow enough to see a boat heave on them.

**Wave groups.** Two very slow crossing waves, much longer than the sea's own, multiply all the waves by a number between 1 - strength and 1. Where both are at a crest the sea is rough; where they cancel it is calmer. The patches drift.

## Why it is built this way

**Why change only the table?** The shader and the CPU both read the same table, so building it differently on the CPU changes the picture AND what floating boats read, with no risk of the two disagreeing. Only the wave groups needed a few lines on each side, and the formula is the same in `WaterWaves::GroupFactor` and `WaveGroupFactor`.

**Why can groups only turn waves down?** The height bound (used for culling) and the no-fold-over limit (the sum of Q * k * a stays under 1) both assume no wave is taller than its table amplitude. A factor between 1 - g and 1 keeps both true. To keep the average height what you asked for, the table amplitudes are scaled up by 1 / (1 - g / 2), so Amplitude means "typical" and the roughest patches are a little taller.

**Why a seeded random generator?** Slightly irregular wavelength ratios and random phases stop the sea from looking tiled. A seed makes it repeatable: the same seed gives the same sea on every machine, and Re-roll gives another one with the same character.

**Why per-wave steepness?** Long waves are rounder, short ones sharper. Each wave gets Q = weight * choppiness * m, where weight is 1 for wind waves and 0.5 for swell, and m = min(1, 1 / sum of weight * k * a). That makes the sum of Q * k * a at most `choppiness`, so no crest can fold over, which the tests check for 300 random seas.

**Why 6 inversion steps now?** The CPU height finds where a surface point started by repeating a step that shrinks the error by about the choppiness. At the sharpest setting 4 steps left up to 2 cm of error; 6 leave under 5 mm. Boats read this height, so it is worth the extra few multiplications.

## Other ways, and the trade-offs

**FFT ocean.** Hundreds of waves from a real spectrum, computed on the GPU into a displacement texture. Much richer and it tiles, but it needs compute shaders, texture cascades and a very different CPU height query for boats. Worth it for a hero ocean, a big step from here.

**Camera-following grid (clipmap).** A fixed set of nested rings of tiles that move with the camera, dense near it and coarse far away. It is the real answer for very large oceans: the vertex budget no longer depends on the plane's size. This patch only raises the ceiling (a 1000-unit plane now has about 1-unit cells); beyond about 1000 units the cells grow again.

**Wave groups from a noise texture.** More natural patches, but the CPU would need the same texture. Two crossing sines are cheaper and identical on both sides.

## Predict, then check

1. A 600-unit plane with 32 cells per tile and a target cell of 1.0. How many tiles per side does it get, and what is the finest cell width?
2. Wave groups = 0.4. What are the lowest and highest the group factor can reach, and by what factor are the table amplitudes scaled up?
3. On a grid that is too coarse for the whole sea, which waves does the vertex shader fade out first: the swell or the finest wind wave? Why?

**Answers.** (1) 600 / 32 = 18.75, so 19 tiles needed, which the steps round up to 20; 600 / (20 * 32) = 0.94 units per cell. (2) Between 0.6 and 1; amplitudes are scaled by 1 / (1 - 0.2) = 1.25. (3) The finest wind wave. The fade starts where a wave is shorter than three cells (k above maxK), so the highest-k waves go first and the swell, the longest, is the last to survive.

## Rebuild cold

Without looking: write the group factor for one position and time, then write the rule that makes the sum of Q * k * a stay under 1.
