#pragma once
// A PICTURE as the ground of a world (phase 1 of docs/ROADMAP.md). Plain maths plus one image decoder: no Direct3D.
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

// The same lookup with a smooth (Catmull-Rom) curve through the pixels instead of straight lines between them: less faceted when a
// small picture is stretched over a large world. It can overshoot a little at sharp edges, so the result is clamped to 0..1.
float SampleHeightImageBicubic(const HeightImage& image, float u, float v);

// --- prefiltering (roadmap T1) -------------------------------------------------------------------------------------------
// THE PROBLEM. A world samples the picture once per cell. When a cell covers several pixels (a 2048 px picture over 1024 m is
// 0.5 m per pixel; at 4 m per cell that is 8 pixels per cell) a thin feature, such as a one-pixel escarpment, is either hit or
// missed by chance: the result is stairs and streaks (aliasing). The cure is the one textures use: average the picture over the
// area one sample stands for BEFORE sampling it. That is a mip pyramid; the level is chosen from pixels per cell.

// Metres between two neighbouring pixels of a picture `widthMetres` wide (pixel 0 and pixel width-1 sit on the two edges).
double PictureMetresPerPixel(double widthMetres, int imageWidth);

// How many pixels one sample of the ground covers: the spacing of the samples (a cell) over the spacing of the pixels.
// 1 or less: the picture is no finer than the sampling and needs no filter. 8: eight pixels are squeezed into one sample.
double PicturePixelsPerCell(double metresPerCell, double widthMetres, int imageWidth);

// The picture and its coarser copies: level 1 is about half as wide and a low-pass of level 0, level 2 of level 1, and so on.
// Built once per picture and shared by every tile, so the height stays a pure function of the world position.
struct HeightPyramid {
    std::shared_ptr<const HeightImage> base;   // level 0: the picture itself (not copied)
    std::vector<HeightImage> coarser;          // levels 1, 2, ...
    int LevelCount() const { return 1 + static_cast<int>(coarser.size()); }
    const HeightImage& Level(int level) const;   // clamped to the levels that exist; needs a base
};
HeightPyramid BuildHeightPyramid(std::shared_ptr<const HeightImage> base);

// The (fractional) level that matches a footprint of `pixels` source pixels: log2, and 0 for a footprint of one pixel or less.
float PyramidLevelForFootprint(double pixels);

// Reads the pyramid at (u, v) in 0..1. A fractional level blends the two levels around it (trilinear), so there is no step where
// the level changes. `bicubic` is the same choice as for SampleHeightImage / SampleHeightImageBicubic.
float SampleHeightPyramid(const HeightPyramid& pyramid, float u, float v, float level, bool bicubic);

class PictureHeightSource final : public WorldHeightSource {
public:
    // `image` is shared, never copied: every tile of the world reads the same one. Sizes are metres and must be > 0.
    // `bicubic` false is the old terrain's straight-line blend (what a migrated terrain needs to match sample for sample).
    PictureHeightSource(std::shared_ptr<const HeightImage> image, double offsetX, double offsetZ, double sizeX, double sizeZ, bool bicubic = false);
    // The same picture, prefiltered: every sample averages the picture over `filterMetres` (the cell size of the world) first.
    // With a footprint of one pixel or less this is exactly the source above.
    PictureHeightSource(std::shared_ptr<const HeightPyramid> pyramid, double filterMetres, double offsetX, double offsetZ, double sizeX, double sizeZ, bool bicubic = false);
    float Height01(double worldX, double worldZ) const override;

    double SizeX() const { return sizeX_; }
    double SizeZ() const { return sizeZ_; }
    double OffsetX() const { return offsetX_; }
    double OffsetZ() const { return offsetZ_; }

private:
    std::shared_ptr<const HeightImage> image_;
    std::shared_ptr<const HeightPyramid> pyramid_;   // null = no prefilter
    float level_ = 0.0f;                             // the pyramid level every sample reads (fixed per world: the height stays a pure function of position)
    double offsetX_, offsetZ_, sizeX_, sizeZ_;
    bool bicubic_ = false;
};

// How deep a picture of this shape is when it is `width` metres wide.
double PictureDepth(double width, int imageWidth, int imageHeight);

// --- rebuilding the old single terrain as a world -----------------------------------------------------------------------
// The grid a world needs so that ONE tile is exactly as wide as an old terrain of `width` metres and `maxCells` cells along X,
// with the same cell size: the heights of that terrain and of the world's tile then agree sample for sample. A terrain wider
// than the finest cell the world allows (WorldGridSettings::kMetresPerCellMin) simply becomes several tiles.
WorldGridSettings WorldGridForTerrain(float width, int maxCells);
