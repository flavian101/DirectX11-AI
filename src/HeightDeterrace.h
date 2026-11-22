#pragma once
// Taking the STAIRS out of an 8-bit heightmap without inventing any ground (roadmap H4). Plain maths: no Direct3D.
// Tests: tests/height_deterrace_test.cpp.
//
// THE PROBLEM. An 8-bit file has only 256 heights. On a gentle hill that is a staircase: wide flat terraces with a step between them.
// Blending between the pixels (H3) cannot fix it, the terraces ARE the pixels.
//
// THE METHOD. The file says every pixel is its level plus or minus half a level (it was rounded). So:
//   repeat 16 times:  blur the heights a little, then push every pixel back into its own level +/- 0.5.
// The blur rounds the steps off; the push-back guarantees the result is still something the file could have meant. A pixel never
// leaves its quantisation bin, so two pixels the file ranks one above the other are never swapped.
//
// WHAT IT CAN AND CANNOT DO (measured). On smooth ground the error against the true surface drops about 3.5 to 5 times. On ground that
// is already rough from pixel to pixel it changes almost nothing (it neither helps nor hurts): the staircase is lost in the roughness.
// A 16-bit picture has no staircase and is returned as it is. Use 16-bit files when you can.
#include "HeightPicture.h"
#include <vector>

constexpr int kDeterraceIterations = 16;

// The picture's levels as floats, row by row. 16-bit pictures (and iterations <= 0) come back exactly as stored. For an 8-bit picture each
// value stays within half a level of its pixel's level, and within the picture's own lowest and highest level.
std::vector<float> DeterraceLevels(const HeightPicture& picture, int iterations = kDeterraceIterations);
