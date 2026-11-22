#include "HeightSample.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Catmull-Rom through four values: passes exactly through b (t = 0) and c (t = 1).
double CatmullRom(double a, double b, double c, double d, double t)
{
    return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}

// For each of `outCount` output samples along one axis: which source pixels it averages and with what weights.
struct AxisWeights {
    int first = 0;
    std::vector<double> weight;   // weight[k] belongs to source pixel first + k; they add up to 1
};

std::vector<AxisWeights> BuildAxisWeights(int sourceCount, int outCount)
{
    // Pixel k stands for [k - 0.5, k + 0.5]. Output sample i sits at source position i * ratio and averages the stretch of one ratio
    // around it (clipped at the picture's edge). A pixel counts by how much of it that stretch covers.
    const double ratio = static_cast<double>(sourceCount - 1) / static_cast<double>(outCount - 1);
    std::vector<AxisWeights> axis(static_cast<size_t>(outCount));
    for (int i = 0; i < outCount; ++i) {
        const double centre = static_cast<double>(i) * ratio;
        const double lo = (std::max)(centre - 0.5 * ratio, -0.5);
        const double hi = (std::min)(centre + 0.5 * ratio, static_cast<double>(sourceCount) - 0.5);
        const int first = (std::max)(0, static_cast<int>(std::floor(lo + 0.5)));
        const int last = (std::min)(sourceCount - 1, static_cast<int>(std::floor(hi + 0.5)));
        AxisWeights& w = axis[static_cast<size_t>(i)];
        w.first = first;
        double total = 0.0;
        for (int k = first; k <= last; ++k) {
            const double covered = (std::min)(hi, k + 0.5) - (std::max)(lo, k - 0.5);
            w.weight.push_back((std::max)(covered, 0.0));
            total += w.weight.back();
        }
        if (total <= 0.0) { w.weight.assign(1, 1.0); w.first = (std::min)(first, sourceCount - 1); total = 1.0; }
        for (double& v : w.weight) v /= total;
    }
    return axis;
}

int OutputCount(int sourceCount, double size, double spacing, double target)
{
    if (!(target > spacing * (1.0 + 1e-9))) return sourceCount;   // already as coarse as asked (or coarser)
    const int wanted = static_cast<int>(std::llround(size / target)) + 1;
    return std::clamp(wanted, 2, sourceCount);
}
}   // namespace

double SampleHeightBicubic(const HeightRaster& raster, double worldX, double worldZ)
{
    if (!raster.IsValid()) return 0.0;
    const double u = std::clamp((worldX - raster.originX) / raster.spacingX, 0.0, static_cast<double>(raster.width - 1));
    const double v = std::clamp((worldZ - raster.originZ) / raster.spacingZ, 0.0, static_cast<double>(raster.height - 1));
    const int x1 = (std::min)(static_cast<int>(u), raster.width - 1);
    const int z1 = (std::min)(static_cast<int>(v), raster.height - 1);
    const double tx = u - x1, tz = v - z1;
    auto at = [&](int x, int z) { return static_cast<double>(raster.At(std::clamp(x, 0, raster.width - 1), std::clamp(z, 0, raster.height - 1))); };
    double rows[4];
    for (int j = -1; j <= 2; ++j) rows[j + 1] = CatmullRom(at(x1 - 1, z1 + j), at(x1, z1 + j), at(x1 + 1, z1 + j), at(x1 + 2, z1 + j), tx);
    return CatmullRom(rows[0], rows[1], rows[2], rows[3], tz);
}

HeightRaster ResampleToSpacing(const HeightRaster& raster, double targetSpacing)
{
    if (!raster.IsValid() || !(targetSpacing > 0.0)) return raster;
    const int outW = OutputCount(raster.width, raster.SizeX(), raster.spacingX, targetSpacing);
    const int outH = OutputCount(raster.height, raster.SizeZ(), raster.spacingZ, targetSpacing);
    if (outW == raster.width && outH == raster.height) return raster;

    // Two passes, along X and then along Z: a box is separable. Doubles inside, one rounding to float at the end.
    std::vector<AxisWeights> weightsX, weightsZ;
    if (outW != raster.width) weightsX = BuildAxisWeights(raster.width, outW);
    if (outH != raster.height) weightsZ = BuildAxisWeights(raster.height, outH);

    std::vector<double> alongX(static_cast<size_t>(outW) * static_cast<size_t>(raster.height));
    for (int z = 0; z < raster.height; ++z) {
        for (int x = 0; x < outW; ++x) {
            double sum = 0.0;
            if (weightsX.empty()) sum = raster.At(x, z);
            else {
                const AxisWeights& w = weightsX[static_cast<size_t>(x)];
                for (size_t k = 0; k < w.weight.size(); ++k) sum += w.weight[k] * static_cast<double>(raster.At(w.first + static_cast<int>(k), z));
            }
            alongX[static_cast<size_t>(z) * outW + x] = sum;
        }
    }

    HeightRaster result;
    result.width = outW;
    result.height = outH;
    result.originX = raster.originX;
    result.originZ = raster.originZ;
    result.spacingX = outW == raster.width ? raster.spacingX : raster.SizeX() / static_cast<double>(outW - 1);
    result.spacingZ = outH == raster.height ? raster.spacingZ : raster.SizeZ() / static_cast<double>(outH - 1);
    result.metres.resize(static_cast<size_t>(outW) * static_cast<size_t>(outH));
    for (int z = 0; z < outH; ++z) {
        for (int x = 0; x < outW; ++x) {
            double sum = 0.0;
            if (weightsZ.empty()) sum = alongX[static_cast<size_t>(z) * outW + x];
            else {
                const AxisWeights& w = weightsZ[static_cast<size_t>(z)];
                for (size_t k = 0; k < w.weight.size(); ++k) sum += w.weight[k] * alongX[static_cast<size_t>(w.first + static_cast<int>(k)) * outW + x];
            }
            result.metres[static_cast<size_t>(z) * outW + x] = static_cast<float>(sum);
        }
    }
    return result;
}
