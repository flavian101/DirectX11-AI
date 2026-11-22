#pragma once
// LOOKING at a height raster, so the user can FIT a picture to the world instead of clipping it (roadmap H2). Plain maths: no Direct3D.
// Tests: tests/height_stats_test.cpp.
//
// These replace the old percentile auto-levels. Auto-levels answered "which part of the picture shall I throw away?"; the questions
// worth asking are the ones below: what is the true range, how much of the ground lies below the sea, and how steep is it.
#include "HeightRaster.h"

struct HeightStats {
    float lowest = 0.0f, highest = 0.0f;   // the TRUE range of the raster, metres (world Y)
    double meanGradient = 0.0;             // average steepness between neighbouring samples, rise over run (1.0 = 45 degrees)
    double MeanSlopeDegrees() const;       // the same as an angle
};

// Needs a valid raster (returns zeros for an invalid one). The gradient is read from forward differences along X and along Z in each
// grid cell, divided by the sample spacing, combined as sqrt(gx^2 + gz^2), and averaged over all (width-1) x (height-1) cells.
HeightStats ComputeHeightStats(const HeightRaster& raster);

// The share of samples strictly below `y`, 0..1: "how much of this ground would be under a sea at y". 0 for an invalid raster.
double FractionBelow(const HeightRaster& raster, float y);

// The height that `fraction` (0..1) of the samples lie below: the inverse of FractionBelow. A default sea level can be chosen with this
// ("cover the lowest 30% of the ground"). Returns 0 for an invalid raster.
float HeightAtFraction(const HeightRaster& raster, double fraction);
