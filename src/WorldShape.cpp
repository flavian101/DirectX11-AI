#include "WorldShape.h"
#include <algorithm>
#include <cmath>
#include <vector>

void HeightShapeSettings::Clamp()
{
    blackPoint = std::clamp(blackPoint, 0.0f, 1.0f - kMinLevelGap);
    whitePoint = std::clamp(whitePoint, blackPoint + kMinLevelGap, 1.0f);
    curve = std::clamp(curve, kCurveMin, kCurveMax);
    smoothing = std::clamp(smoothing, 0.0f, kSmoothingMax);
    detailStrength = std::clamp(detailStrength, 0.0f, kDetailStrengthMax);
    detailWavelength = std::clamp(detailWavelength, kDetailWavelengthMin, kDetailWavelengthMax);
    detailOctaves = std::clamp(detailOctaves, 1, 8);
    detailValleyMask = std::clamp(detailValleyMask, 0.0f, 1.0f);
}

bool HeightShapeSettings::IsIdentity() const
{
    return blackPoint == 0.0f && whitePoint == 1.0f && curve == 1.0f && !invert && detailStrength <= 0.0f;
}

const char* HeightShapePresetName(int preset)
{
    switch (preset) {
    case 1: return "Rolling hills";
    case 2: return "Mountains";
    case 3: return "Plains with relief";
    default: return "Natural (the picture as it is)";
    }
}

HeightShapeSettings HeightShapePreset(int preset)
{
    HeightShapeSettings s;
    switch (preset) {
    case 1:   // soft, round, a little detail
        s.curve = 1.25f; s.smoothing = 1.0f; s.bicubic = true;
        s.detailStrength = 3.0f; s.detailWavelength = 90.0f; s.detailOctaves = 4; s.detailRidged = false; s.detailValleyMask = 0.6f;
        break;
    case 2:   // flat valleys, sharp ridged peaks
        s.curve = 1.8f; s.bicubic = true;
        s.detailStrength = 14.0f; s.detailWavelength = 120.0f; s.detailOctaves = 6; s.detailRidged = true; s.detailValleyMask = 0.7f;
        break;
    case 3:   // wide flat land with small bumps everywhere
        s.curve = 1.4f; s.smoothing = 2.0f; s.bicubic = true;
        s.detailStrength = 2.0f; s.detailWavelength = 40.0f; s.detailOctaves = 3; s.detailRidged = false; s.detailValleyMask = 0.2f;
        break;
    default: break;
    }
    s.Clamp();
    return s;
}

float ShapeHeight01(float height, const HeightShapeSettings& s)
{
    const float gap = (std::max)(s.whitePoint - s.blackPoint, HeightShapeSettings::kMinLevelGap);
    float t = std::clamp((height - s.blackPoint) / gap, 0.0f, 1.0f);
    if (s.invert) t = 1.0f - t;
    if (s.curve != 1.0f) t = std::pow(t, s.curve);
    return t;
}

ShapedHeightSource::ShapedHeightSource(std::shared_ptr<const WorldHeightSource> base, const HeightShapeSettings& settings, float heightScaleMetres)
    : base_(std::move(base)), settings_(settings),
      detailAmplitude_(settings.detailStrength / (std::max)(heightScaleMetres, 1.0f)),
      detail_(FractalWorldSettings{ settings.detailSeed, settings.detailOctaves, settings.detailWavelength })
{
    settings_.Clamp();
    detailAmplitude_ = settings_.detailStrength / (std::max)(heightScaleMetres, 1.0f);
}

float ShapedHeightSource::Height01(double worldX, double worldZ) const
{
    if (!base_) return 0.0f;
    const float raw = base_->Height01(worldX, worldZ);
    if (settings_.IsIdentity()) return raw;   // exactly the base: shaping that does nothing costs nothing and changes nothing
    const float shaped = ShapeHeight01(raw, settings_);
    if (detailAmplitude_ <= 0.0f) return shaped;
    // The detail is value noise in world space, -1..1. Ridged turns the round bumps into creases: 1 - |n| is a sharp ridge at n = 0.
    float n = std::clamp((detail_.Height01(worldX, worldZ) - 0.5f) * 2.0f, -1.0f, 1.0f);
    if (settings_.detailRidged) n = (1.0f - std::fabs(n)) * 2.0f - 1.0f;
    const float mask = (1.0f - settings_.detailValleyMask) + settings_.detailValleyMask * shaped;   // low ground gets less detail
    return std::clamp(shaped + n * detailAmplitude_ * mask, 0.0f, 1.0f);
}

HeightImageStats ComputeHeightImageStats(const HeightImage& image)
{
    HeightImageStats stats;
    if (image.width < 2 || image.height < 2 || image.values.size() < static_cast<size_t>(image.width) * image.height) return stats;
    constexpr int kBins = 4096;
    std::vector<std::uint32_t> histogram(kBins, 0);
    float lo = 1.0f, hi = 0.0f;
    for (const float v : image.values) {
        const float c = std::clamp(v, 0.0f, 1.0f);
        lo = (std::min)(lo, c);
        hi = (std::max)(hi, c);
        ++histogram[static_cast<size_t>((std::min)(static_cast<int>(c * (kBins - 1) + 0.5f), kBins - 1))];
    }
    stats.minimum = lo;
    stats.maximum = hi;
    const double total = static_cast<double>(image.values.size());
    double running = 0.0;
    bool lowFound = false;
    stats.low = lo;
    stats.high = hi;
    for (int bin = 0; bin < kBins; ++bin) {
        running += histogram[static_cast<size_t>(bin)];
        if (!lowFound && running >= 0.005 * total) { stats.low = static_cast<float>(bin) / (kBins - 1); lowFound = true; }
        if (running >= 0.995 * total) { stats.high = static_cast<float>(bin) / (kBins - 1); break; }
    }
    double gradient = 0.0;
    size_t count = 0;
    for (int z = 0; z < image.height; ++z) {
        for (int x = 0; x < image.width; ++x) {
            const float v = image.values[static_cast<size_t>(z) * image.width + x];
            if (x + 1 < image.width) { gradient += std::fabs(image.values[static_cast<size_t>(z) * image.width + x + 1] - v); ++count; }
            if (z + 1 < image.height) { gradient += std::fabs(image.values[static_cast<size_t>(z + 1) * image.width + x] - v); ++count; }
        }
    }
    stats.meanGradient = count ? static_cast<float>(gradient / static_cast<double>(count)) : 0.0f;
    return stats;
}

bool ComputeAutoLevels(const HeightImage& image, float lowPercent, float highPercent, float& blackPoint, float& whitePoint)
{
    if (image.width < 2 || image.height < 2 || image.values.size() < static_cast<size_t>(image.width) * image.height) return false;
    constexpr int kBins = 4096;
    lowPercent = std::clamp(lowPercent, 0.0f, 49.0f);
    highPercent = std::clamp(highPercent, 51.0f, 100.0f);
    std::vector<std::uint32_t> histogram(kBins, 0);
    for (const float v : image.values) ++histogram[static_cast<size_t>((std::min)(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (kBins - 1) + 0.5f), kBins - 1))];
    const double total = static_cast<double>(image.values.size());
    double running = 0.0;
    float black = 0.0f, white = 1.0f;
    bool blackFound = false;
    for (int bin = 0; bin < kBins; ++bin) {
        running += histogram[static_cast<size_t>(bin)];
        if (!blackFound && running >= total * lowPercent / 100.0) { black = static_cast<float>(bin) / (kBins - 1); blackFound = true; }
        if (running >= total * highPercent / 100.0) { white = static_cast<float>(bin) / (kBins - 1); break; }
    }
    if (white - black < 1e-3f) return false;   // a single grey: nothing to stretch
    blackPoint = black;
    whitePoint = white;
    return true;
}

namespace {
// One blur pass along a row or a column (box of 2r+1, edge pixels repeat), by a running sum: the cost does not grow with the radius.
void BoxBlurLine(const float* source, float* target, int count, int stride, int radius)
{
    const int window = 2 * radius + 1;
    double sum = 0.0;
    auto at = [&](int i) { return static_cast<double>(source[static_cast<size_t>(std::clamp(i, 0, count - 1)) * stride]); };
    for (int i = -radius; i <= radius; ++i) sum += at(i);
    for (int i = 0; i < count; ++i) {
        target[static_cast<size_t>(i) * stride] = static_cast<float>(sum / window);
        sum += at(i + radius + 1) - at(i - radius);
    }
}
}   // namespace

HeightImage SmoothHeightImage(const HeightImage& image, int radiusPixels)
{
    HeightImage result = image;
    if (radiusPixels <= 0 || image.width < 2 || image.height < 2) return result;
    const int radius = (std::min)(radiusPixels, 64);
    std::vector<float> scratch(result.values.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int z = 0; z < result.height; ++z) BoxBlurLine(result.values.data() + static_cast<size_t>(z) * result.width, scratch.data() + static_cast<size_t>(z) * result.width, result.width, 1, radius);
        for (int x = 0; x < result.width; ++x) BoxBlurLine(scratch.data() + x, result.values.data() + x, result.height, result.width, radius);
    }
    return result;
}

float SlopeDegrees(float gradient, float heightScaleMetres, float metresPerPixel)
{
    if (!(metresPerPixel > 0.0f)) return 0.0f;
    const float rise = gradient * heightScaleMetres;
    return std::atan(rise / metresPerPixel) * (180.0f / 3.14159265f);
}
