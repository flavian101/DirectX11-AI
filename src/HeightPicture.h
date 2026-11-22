#pragma once
// A heightmap PICTURE as integer levels, exactly as the file stores them (roadmap H1). Plain data plus one image decoder:
// no Direct3D. Tests: tests/height_raster_test.cpp.
//
// WHY INTEGERS. The importer needs to know how many different heights the file really holds, whether the file is 8-bit
// (stair-stepped, see H4) or 16-bit, and where its true minimum and maximum are. A float in 0..1 hides all three.
#include <cstdint>
#include <string>
#include <vector>

struct HeightPicture {
    int width = 0;
    int height = 0;
    int bits = 16;                       // 8 or 16: what the FILE stores per pixel
    std::vector<std::uint16_t> levels;   // row by row; 0..255 for an 8-bit file, 0..65535 for a 16-bit one

    int MaxLevel() const { return bits == 8 ? 255 : 65535; }
    bool IsValid() const { return width >= 2 && height >= 2 && levels.size() == static_cast<size_t>(width) * static_cast<size_t>(height); }
    std::uint16_t At(int x, int z) const { return levels[static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
};

// Decodes an 8-bit or 16-bit PNG (or any format stb_image reads). A colour picture contributes its RED channel, a grey one its grey.
// Returns false and says why in `error` when the file cannot be read or is smaller than 2 x 2 pixels.
bool LoadHeightPicture(const std::string& path, HeightPicture& picture, std::string& error);
