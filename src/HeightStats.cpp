#include "HeightStats.h"
#include <algorithm>
#include <cmath>
#include <vector>

double HeightStats::MeanSlopeDegrees() const
{
    return std::atan(meanGradient) * (180.0 / 3.14159265358979323846);
}

HeightStats ComputeHeightStats(const HeightRaster& raster)
{
    HeightStats stats;
    if (!raster.IsValid()) return stats;
    const auto range = std::minmax_element(raster.metres.begin(), raster.metres.end());
    stats.lowest = *range.first;
    stats.highest = *range.second;
    double sum = 0.0;
    for (int z = 0; z + 1 < raster.height; ++z) {
        for (int x = 0; x + 1 < raster.width; ++x) {
            const double here = raster.At(x, z);
            const double gx = (static_cast<double>(raster.At(x + 1, z)) - here) / raster.spacingX;
            const double gz = (static_cast<double>(raster.At(x, z + 1)) - here) / raster.spacingZ;
            sum += std::sqrt(gx * gx + gz * gz);
        }
    }
    stats.meanGradient = sum / (static_cast<double>(raster.width - 1) * static_cast<double>(raster.height - 1));
    return stats;
}

double FractionBelow(const HeightRaster& raster, float y)
{
    if (!raster.IsValid()) return 0.0;
    size_t below = 0;
    for (float v : raster.metres) if (v < y) ++below;
    return static_cast<double>(below) / static_cast<double>(raster.metres.size());
}

float HeightAtFraction(const HeightRaster& raster, double fraction)
{
    if (!raster.IsValid()) return 0.0f;
    std::vector<float> sorted = raster.metres;
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const size_t index = static_cast<size_t>(clamped * static_cast<double>(sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
    return sorted[index];
}
