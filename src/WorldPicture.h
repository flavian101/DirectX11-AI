#pragma once
// A PICTURE as the ground of a world (phase 1 of docs/terrain-world-roadmap.md). Plain maths plus one image decoder: no Direct3D.
// Tests: tests/world_picture_test.cpp.
//
// WHY. The old single terrain stretches a heightmap over `width` metres. A world cannot do that per tile (two tiles would each
// stretch their own copy), so the picture is placed in the WORLD once, and every tile asks the same question as for the
// fractal hills: "how high is the ground at this world position?" (WorldHeightSource). Tiles that share an edge still agree, at
// any level of detail, and a small terrain is a small bounded world.
//
// THE PLACEMENT. The picture's top-left corner is at world (offsetX, offsetZ). It is `sizeX` metres wide, and `sizeZ` metres
// deep (the old terrain's rule: the depth follows the picture's shape). Pixel (0, 0) is exactly at the corner and pixel
// (width - 1, height - 1) exactly at the opposite one, the same convention as the old terrain's SampleHeightmap, so a picture
// gives the same heights on the old terrain and on a world with the same cell size.
//
// OUTSIDE THE PICTURE the edge pixel is repeated (clamp). A bounded world that covers the picture only shows that where the
// picture's size is not a whole number of tiles.
#include "WorldGrid.h"
#include <memory>
#include <string>
#include <vector>

struct HeightImage {
    int width = 0;
    int height = 0;
    std::vector<float> values;   // 0..1, row by row (the red channel; 16-bit images keep their 65,536 steps)
};

// Decodes an 8-bit or 16-bit greyscale (or colour: the red channel is used) image. On failure returns false and says why in `error`.
bool LoadHeightImage(const std::string& path, HeightImage& image, std::string& error);

// Bilinear lookup at (u, v) in 0..1 across the picture (clamped). Identical to the old terrain's SampleHeightmap.
float SampleHeightImage(const HeightImage& image, float u, float v);

class PictureHeightSource final : public WorldHeightSource {
public:
    // `image` is shared, never copied: every tile of the world reads the same one. Sizes are metres and must be > 0.
    PictureHeightSource(std::shared_ptr<const HeightImage> image, double offsetX, double offsetZ, double sizeX, double sizeZ);
    float Height01(double worldX, double worldZ) const override;

    double SizeX() const { return sizeX_; }
    double SizeZ() const { return sizeZ_; }
    double OffsetX() const { return offsetX_; }
    double OffsetZ() const { return offsetZ_; }

private:
    std::shared_ptr<const HeightImage> image_;
    double offsetX_, offsetZ_, sizeX_, sizeZ_;
};

// How deep a picture of this shape is when it is `width` metres wide.
double PictureDepth(double width, int imageWidth, int imageHeight);

// --- rebuilding the old single terrain as a world -----------------------------------------------------------------------
// The grid a world needs so that ONE tile is exactly as wide as an old terrain of `width` metres and `maxCells` cells along X,
// with the same cell size: the heights of that terrain and of the world's tile then agree sample for sample. A terrain wider
// than the finest cell the world allows (WorldGridSettings::kMetresPerCellMin) simply becomes several tiles.
WorldGridSettings WorldGridForTerrain(float width, int maxCells);
