#pragma once
// READING a height raster at any world position, smoothly, and shrinking one that is finer than the world's lattice (roadmap H3).
// Plain maths: no Direct3D. Tests: tests/height_sample_test.cpp.
//
// TWO PROBLEMS, TWO TOOLS
//  * Straight creases. Straight-line (bilinear) blending between pixels has a kink at every pixel border, so a stretched picture reads
//    as facets and ridges as straight lines. SampleHeightBicubic is smooth (continuous in value AND slope) through the pixels.
//  * Stairs and streaks. When one lattice cell covers several pixels, sampling the picture at the cell's corner hits or misses thin
//    features by chance. ResampleToSpacing averages the picture over the whole cell first, once, at import.
// Neither clamps: a height is metres, and ground that really is higher than its neighbours stays higher.
#include "HeightRaster.h"

// The ground height at a world position, in metres. Catmull-Rom through the sixteen nearest samples: it passes exactly through every
// sample, reproduces a straight slope exactly, and is continuous in value and slope. Like every cubic through points it may overshoot a
// little beside a very sharp step; that is not clamped. Outside the raster the edge sample repeats. 0 for an invalid raster.
double SampleHeightBicubic(const HeightRaster& raster, double worldX, double worldZ);

// A coarser copy whose samples are the AVERAGE of the raster over the area each one stands for (a box one new cell wide, partial pixels
// weighted by how much of them is covered). The corners stay on the corners, so the copy covers exactly the same area; the new spacing
// is that area divided by a whole number of cells, which is `targetSpacing` to within half a cell. An axis whose spacing is already
// `targetSpacing` or more is left alone (a raster is never made finer: that would invent detail). An invalid raster comes back unchanged.
HeightRaster ResampleToSpacing(const HeightRaster& raster, double targetSpacing);
