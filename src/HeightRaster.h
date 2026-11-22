#pragma once
// The BASE GROUND of a world: a grid of heights in METRES (world Y), made from a picture without clipping it (roadmap H1).
// Plain maths: no Direct3D. Tests: tests/height_raster_test.cpp.
//
// THE IMPORT RULE (roadmap decision 2). The picture's true minimum becomes `lowestY` and its true maximum becomes
// `lowestY + relief`, every other level in a straight line between them. Nothing is clipped, nothing merges (two different levels
// give two different heights), and the order of the heights is the order of the levels. The file is never modified: to change the
// relief or the lowest Y, import again from the file.
//
// There is no 0..1 height past this point. Everything that reads the ground gets metres.
#include "HeightPicture.h"
#include <string>
#include <vector>

struct HeightRaster {
    int width = 0;
    int height = 0;
    double originX = 0.0, originZ = 0.0;     // world position of sample (0, 0), metres
    double spacingX = 1.0, spacingZ = 1.0;   // metres between neighbouring samples
    std::vector<float> metres;               // world Y of each sample, row by row

    bool IsValid() const { return width >= 2 && height >= 2 && metres.size() == static_cast<size_t>(width) * static_cast<size_t>(height); }
    float At(int x, int z) const { return metres[static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
    double SizeX() const { return spacingX * static_cast<double>(width - 1); }
    double SizeZ() const { return spacingZ * static_cast<double>(height - 1); }
};

struct HeightImportSettings {
    double widthMetres = 1024.0;   // how wide the picture is in the world; the depth follows the picture's shape (square pixels)
    double originX = 0.0, originZ = 0.0;   // world position of the picture's top-left pixel
    float lowestY = 0.0f;          // world Y of the picture's lowest point
    float relief = 100.0f;         // metres from the lowest point to the highest

    static constexpr double kWidthMin = 1.0, kWidthMax = 100000.0;
    static constexpr float kYMin = -10000.0f, kYMax = 10000.0f;
    static constexpr float kReliefMin = 0.01f, kReliefMax = 20000.0f;
    void Clamp();
};

struct HeightImportInfo {
    int minLevel = 0, maxLevel = 0;   // the file's true range, in levels
    bool flat = false;                // every pixel has one level: the raster is flat at lowestY (the relief has nothing to stretch)
};

// Fills `raster` from `picture`. Returns false (and leaves `raster` alone) for a picture that is not valid.
bool ImportHeightRaster(const HeightPicture& picture, const HeightImportSettings& settings, HeightRaster& raster, HeightImportInfo* info = nullptr);
