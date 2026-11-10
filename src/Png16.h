#pragma once
// Writing a 16-bit greyscale PNG, which is how a sculpted terrain is saved (lesson 27). Pure logic: no Direct3D.
// Reading is already done by the terrain's heightmap loader (stb_image understands 16-bit PNGs).
#include <cstdint>
#include <string>
#include <vector>

// Height samples (0..1) as 16-bit integers: round(value * 65535). Out-of-range values are clamped.
std::vector<std::uint16_t> QuantizeHeights(const std::vector<float>& samples);

// Writes `values` (row by row, width * height of them) as a 16-bit greyscale PNG. The file is written to "<path>.tmp"
// first and then moved into place, so a crash half-way never leaves a broken image where a good one was.
// Returns false and fills `error` on failure. The image is stored uncompressed: simple, always valid, about 2 bytes per
// sample (a 1025 x 1025 terrain is ~2 MB).
bool WriteGrayscale16Png(const std::string& path, int width, int height, const std::vector<std::uint16_t>& values,
                         std::string& error);
