#pragma once
// The ground of a world in METRES (roadmap H6, H7). Plain maths: no Direct3D. Tests: tests/world_ground_source_test.cpp.
//
// The world's ground is  raster (metres)  ->  detail (metres)  ->  edits (metres).  Each layer is an addition in metres, so
// nothing is stretched, clipped or reshaped by a curve: what the picture says is what the ground is, and a bump is as many metres tall
// whatever the relief is. The world's tiles still store a ground VALUE between 0 and 1 (`Height01`): the value of a height in metres is
//     (metres - lowestY) / relief,        world Y = lowestY + value * relief,
// so `lowestY` and `relief` are not stretch controls any more: they are the numbers the picture was IMPORTED with (HeightImportSettings)
// and the range the tiles can hold.
#include "HeightDetail.h"
#include "HeightPicture.h"
#include "HeightRaster.h"
#include "HeightStats.h"
#include "WorldGrid.h"
#include <memory>
#include <string>

// A raster as the ground. Reads are smooth (bicubic, H3): continuous in value and slope, no clamp, and the edge sample repeats outside
// the raster. The ground value is clamped to the range the tiles can hold, 0 to 1.
class RasterHeightSource final : public WorldHeightSource {
public:
    RasterHeightSource(std::shared_ptr<const HeightRaster> raster, float lowestY, float relief);
    float Height01(double worldX, double worldZ) const override;
private:
    std::shared_ptr<const HeightRaster> raster_;
    float lowestY_;
    float relief_;
};

// Any ground with the detail layer (H5) added in metres. With no detail the base answers as it is.
class DetailedHeightSource final : public WorldHeightSource {
public:
    DetailedHeightSource(std::shared_ptr<const WorldHeightSource> base, const HeightDetailSettings& detail, float relief);
    float Height01(double worldX, double worldZ) const override;
private:
    std::shared_ptr<const WorldHeightSource> base_;
    HeightDetailSettings detail_;
    float relief_;
};

// What the inspector shows about the picture that became the ground (H2): what the file holds, and the TRUE range and steepness of the ground.
struct WorldPictureInfo {
    bool valid = false;
    int widthPixels = 0, heightPixels = 0, bits = 16;
    double metresPerPixel = 0.0;
    HeightStats stats;            // the imported raster as the file holds it (not the averaged one the world reads)
    double shareBelowZero = 0.0;  // the share of the ground below world Y = 0 (the default sea level), 0..1
    bool flat = false;            // every pixel has one level
    bool deterraced = false;      // the stairs of an 8-bit file were taken out first (H4)
    double cellPixels = 0.0;      // how many pixels one cell of the world covers (more than 1: the raster is averaged down to the cell)
};

// Reads a picture and imports it (H1, H4) and shrinks it to the world's lattice (H3): when the picture is finer than one cell the raster is
// averaged down to the cell size, once, so a thin ridge neither vanishes nor aliases into stairs. False, with the reason in `error`, if the
// picture cannot be read or is not usable.
bool ImportWorldRaster(const std::string& path, const HeightImportSettings& settings, double cellMetres,
                       std::shared_ptr<const HeightRaster>& raster, WorldPictureInfo& info, std::string& error);
