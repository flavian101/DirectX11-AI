# Lesson 54 - Prefilter the picture (T1)

## Understand

**The stairs come from sampling, not from the picture.** A 2048 px picture over 1024 m is 0.5 m per pixel. The world samples it once per 4 m cell, so one cell stands for about 8 pixels and reads only one of them. A cliff one pixel wide is either hit or missed, and the result is terraces and streaks.

**This is aliasing, and textures solved it long ago.** Before reading a texture that is shown smaller than it is, the GPU averages it down: a mip chain. The same idea fixes the heights: average the picture over the area one sample stands for, then sample.

**Pixels per cell is the number that matters.** It is the distance between samples divided by the distance between pixels. At 1 or less the picture is no finer than the sampling and nothing needs to change. At 8, eight pixels are squeezed into one sample. The inspector now shows it and turns orange above 2 when the prefilter is off.

## How it works

**A pyramid is built once.** `BuildHeightPyramid` makes level 1 from level 0, level 2 from level 1, and so on. Each level has about half the pixels and covers the same area, because the first and last pixel stay on the picture's two edges. A pixel of the next level averages four bilinear reads of the one before, half a pixel to each side.

**One level is chosen for the whole world.** The level is `log2(pixelsPerCell)`. At 8 pixels per cell that is level 3, where one pixel is about 8 source pixels wide. A fractional level blends the two levels around it, so nothing steps when you change the cell size.

**The height stays a pure function of position.** This is why the level is fixed per world and not chosen per tile. Tiles at every level of detail sample the same function, so they still agree on their shared edges. The pyramid is shared by all tiles, like the picture.

**A migrated terrain is not filtered.** `WorldSettingsFromTerrain` sets `prefilter` to false, so a migrated terrain still gives the old terrain's heights sample for sample. New and existing worlds default to on, which changes their shape a little; saved edits are deltas and stay as they are.

## What the test shows

**The ridge test measures the artefact.** A one-pixel ridge is sampled at 8 pixels per cell and the sample point moves half a cell. Unfiltered, the height changes by 1.000. Prefiltered, it changes by 0.062, and the ridge is still there as a low bump of 0.125. A footprint of one pixel or less gives exactly the unfiltered numbers, and a step edge stays between 0 and 1 and only rises.

**Not checked: the editor.** The roadmap keeps T1 at `[~]` until you open the demo picture and look at its escarpments. Compare with the checkbox off.

## Check yourself

1. Why is the filter level fixed for the whole world instead of chosen per tile from that tile's level of detail?
2. At what pixels per cell does the prefilter do nothing, and what does the code return in that case?
3. Prefilter averages; `smoothing` blurs. Which one would you reach for to remove the steps of an 8-bit picture, and which for a thin cliff?
