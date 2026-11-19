// Tests for src/WorldShape.{h,cpp}: the controls that make a picture into good terrain (levels, curve, smoothing, bicubic, detail noise).
// No Direct3D, no framework.
#include "WorldShape.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance = 1e-5) { return std::fabs(a - b) <= tolerance; }

// A picture that only uses the grey band 0.35 .. 0.45: the typical "my terrain is flat" picture.
static std::shared_ptr<HeightImage> NarrowImage(int size)
{
    auto image = std::make_shared<HeightImage>();
    image->width = image->height = size;
    image->values.resize(static_cast<size_t>(size) * size);
    for (int z = 0; z < size; ++z)
        for (int x = 0; x < size; ++x) image->values[static_cast<size_t>(z) * size + x] = 0.35f + 0.10f * (0.5f + 0.5f * std::sin(x * 0.4f) * std::cos(z * 0.3f));
    return image;
}

static void TestIdentityAndLevels()
{
    HeightShapeSettings none;
    CHECK(none.IsIdentity());
    for (float h : { 0.0f, 0.2f, 0.5f, 0.77f, 1.0f }) CHECK(ShapeHeight01(h, none) == h);   // exactly, not nearly

    // The wrapper with no shaping returns the base's very bits.
    const auto picture = std::make_shared<const PictureHeightSource>(NarrowImage(32), 0.0, 0.0, 320.0, 320.0);
    const ShapedHeightSource same(picture, none, 100.0f);
    bool exact = true;
    for (double x = 0.0; x < 320.0; x += 7.3) for (double z = 0.0; z < 320.0; z += 11.1) if (same.Height01(x, z) != picture->Height01(x, z)) exact = false;
    CHECK(exact);

    HeightShapeSettings levels;
    levels.blackPoint = 0.4f; levels.whitePoint = 0.8f;
    CHECK(Near(ShapeHeight01(0.4f, levels), 0.0f) && Near(ShapeHeight01(0.6f, levels), 0.5f) && Near(ShapeHeight01(0.8f, levels), 1.0f));
    CHECK(Near(ShapeHeight01(0.1f, levels), 0.0f) && Near(ShapeHeight01(0.95f, levels), 1.0f));   // outside the band: clamped

    HeightShapeSettings curve;
    curve.curve = 2.0f;
    CHECK(Near(ShapeHeight01(0.5f, curve), 0.25f) && Near(ShapeHeight01(1.0f, curve), 1.0f) && Near(ShapeHeight01(0.0f, curve), 0.0f));
    curve.curve = 0.5f;
    CHECK(ShapeHeight01(0.25f, curve) > 0.25f);   // < 1: broader highlands

    HeightShapeSettings invert;
    invert.invert = true;
    CHECK(Near(ShapeHeight01(0.2f, invert), 0.8f));

    HeightShapeSettings bad;
    bad.blackPoint = 0.9f; bad.whitePoint = 0.1f; bad.curve = 100.0f; bad.detailStrength = -5.0f; bad.detailOctaves = 99;
    bad.Clamp();
    CHECK(bad.whitePoint >= bad.blackPoint + HeightShapeSettings::kMinLevelGap && bad.curve <= HeightShapeSettings::kCurveMax && bad.detailStrength == 0.0f && bad.detailOctaves == 8);
}

static void TestAutoLevelsFixFlatness()
{
    const auto image = NarrowImage(64);
    const HeightImageStats stats = ComputeHeightImageStats(*image);
    CHECK(stats.minimum >= 0.349f && stats.maximum <= 0.451f);
    CHECK(stats.UsedRange() < 0.15f);   // "the picture uses 10% of the grey": the explanation of a flat terrain
    CHECK(stats.meanGradient > 0.0f);

    float black = 0.0f, white = 1.0f;
    CHECK(ComputeAutoLevels(*image, 0.5f, 99.5f, black, white));
    CHECK(black > 0.34f && black < 0.40f && white > 0.42f && white < 0.46f);

    // With those levels the picture spans (nearly) the whole range.
    HeightShapeSettings levels;
    levels.blackPoint = black; levels.whitePoint = white;
    float lo = 1.0f, hi = 0.0f;
    for (const float v : image->values) { const float s = ShapeHeight01(v, levels); lo = (std::min)(lo, s); hi = (std::max)(hi, s); }
    CHECK(lo < 0.02f && hi > 0.98f);

    // A flat grey has nothing to stretch; an empty picture is refused.
    HeightImage flat;
    flat.width = flat.height = 8; flat.values.assign(64, 0.5f);
    float b2 = 0.25f, w2 = 0.75f;
    CHECK(!ComputeAutoLevels(flat, 0.5f, 99.5f, b2, w2) && b2 == 0.25f && w2 == 0.75f);
    HeightImage empty;
    CHECK(!ComputeAutoLevels(empty, 0.5f, 99.5f, b2, w2));

    CHECK(Near(SlopeDegrees(0.01f, 100.0f, 1.0f), 45.0, 1e-3));   // rises 1 m per 1 m
    CHECK(SlopeDegrees(0.01f, 100.0f, 0.0f) == 0.0f);
}

static void TestSmoothingAndBicubic()
{
    // An 8-bit staircase: a smooth ramp quantised to 16 steps.
    HeightImage steps;
    steps.width = 64; steps.height = 8;
    steps.values.resize(64 * 8);
    for (int z = 0; z < 8; ++z) for (int x = 0; x < 64; ++x) steps.values[static_cast<size_t>(z) * 64 + x] = std::floor(x / 64.0f * 16.0f) / 16.0f;
    auto roughness = [](const HeightImage& im) {   // how far each pixel is from the average of its two neighbours: a staircase is rough
        double sum = 0.0;
        for (int x = 1; x + 1 < im.width; ++x) { const float a = im.values[x - 1], b = im.values[x], c = im.values[x + 1]; sum += std::fabs(b - 0.5f * (a + c)); }
        return sum;
    };
    const HeightImage smooth = SmoothHeightImage(steps, 3);
    CHECK(smooth.width == steps.width && smooth.values.size() == steps.values.size());
    CHECK(roughness(smooth) < roughness(steps) * 0.7);
    // The average height is kept (a blur redistributes, it does not raise or lower), apart from the edge pixels being repeated.
    double sumA = 0.0, sumB = 0.0;
    for (size_t i = 0; i < steps.values.size(); ++i) { sumA += steps.values[i]; sumB += smooth.values[i]; }
    CHECK(Near(sumA / steps.values.size(), sumB / smooth.values.size(), 0.02));
    CHECK(SmoothHeightImage(steps, 0).values == steps.values);

    // Bicubic passes exactly through the pixels, and clamps to 0..1.
    const auto image = NarrowImage(16);
    bool through = true;
    for (int z = 0; z < 16; z += 3) for (int x = 0; x < 16; x += 3) {
        const float u = static_cast<float>(x) / 15.0f, v = static_cast<float>(z) / 15.0f;
        if (!Near(SampleHeightImageBicubic(*image, u, v), image->values[static_cast<size_t>(z) * 16 + x], 1e-4)) through = false;
    }
    CHECK(through);
    HeightImage spike;
    spike.width = spike.height = 4; spike.values.assign(16, 0.0f); spike.values[5] = 1.0f;
    float lowest = 1.0f, highest = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.01f) for (float v = 0.0f; v <= 1.0f; v += 0.01f) { const float s = SampleHeightImageBicubic(spike, u, v); lowest = (std::min)(lowest, s); highest = (std::max)(highest, s); }
    CHECK(lowest >= 0.0f && highest <= 1.0f);
}

static void TestDetailNoise()
{
    const auto flat = std::make_shared<HeightImage>();
    flat->width = flat->height = 8; flat->values.assign(64, 0.5f);
    const auto picture = std::make_shared<const PictureHeightSource>(flat, 0.0, 0.0, 1000.0, 1000.0);

    HeightShapeSettings detail;
    detail.detailStrength = 5.0f; detail.detailWavelength = 50.0f; detail.detailValleyMask = 0.0f;
    const ShapedHeightSource shaped(picture, detail, 100.0f);   // 5 m of detail on a 100 m height scale: +-0.05
    float lo = 1.0f, hi = 0.0f;
    for (double x = 0.0; x < 1000.0; x += 3.7) for (double z = 0.0; z < 1000.0; z += 5.3) { const float h = shaped.Height01(x, z); lo = (std::min)(lo, h); hi = (std::max)(hi, h); }
    CHECK(lo >= 0.5f - 0.0501f && hi <= 0.5f + 0.0501f);   // never more than the strength
    CHECK(hi - lo > 0.02f);                               // and it does something
    // Deterministic: the same position gives the same height, from two sources.
    const ShapedHeightSource again(picture, detail, 100.0f);
    CHECK(shaped.Height01(123.4, 567.8) == again.Height01(123.4, 567.8));
    // The strength is in metres: doubling the height scale halves the detail in 0..1 units.
    const ShapedHeightSource tall(picture, detail, 200.0f);
    float hiTall = 0.0f;
    for (double x = 0.0; x < 1000.0; x += 3.7) for (double z = 0.0; z < 1000.0; z += 5.3) hiTall = (std::max)(hiTall, tall.Height01(x, z));
    CHECK(hiTall - 0.5f < 0.0251f);

    // The valley mask: no detail on the lowest ground, full detail on the highest.
    auto lowGround = std::make_shared<HeightImage>(*flat); lowGround->values.assign(64, 0.0f);
    auto highGround = std::make_shared<HeightImage>(*flat); highGround->values.assign(64, 1.0f);
    HeightShapeSettings masked = detail; masked.detailValleyMask = 1.0f;
    const ShapedHeightSource lowS(std::make_shared<const PictureHeightSource>(lowGround, 0.0, 0.0, 1000.0, 1000.0), masked, 100.0f);
    const ShapedHeightSource highS(std::make_shared<const PictureHeightSource>(highGround, 0.0, 0.0, 1000.0, 1000.0), masked, 100.0f);
    float lowSpread = 0.0f, highSpread = 0.0f;
    for (double x = 0.0; x < 1000.0; x += 9.1) for (double z = 0.0; z < 1000.0; z += 7.7) { lowSpread = (std::max)(lowSpread, std::fabs(lowS.Height01(x, z))); highSpread = (std::max)(highSpread, 1.0f - highS.Height01(x, z)); }
    CHECK(lowSpread == 0.0f);
    CHECK(highSpread > 0.01f);
}

static void TestTilesStillMeet()
{
    // Shaping is a function of the world position, so tiles that share an edge still agree: the world's core promise.
    auto image = NarrowImage(48);
    HeightShapeSettings shape = HeightShapePreset(2);   // mountains: levels off, curve, ridged detail
    shape.blackPoint = 0.35f; shape.whitePoint = 0.45f;
    const auto picture = std::make_shared<const PictureHeightSource>(image, 0.0, 0.0, 512.0, 512.0, /*bicubic=*/true);
    const ShapedHeightSource shaped(picture, shape, 120.0f);
    WorldGridSettings grid;
    grid.chunksPerTile = 2; grid.metresPerCell = 4.0f;   // 64 cells, 256 m tiles
    const int cells = grid.TileCells();
    const std::vector<float> a = GenerateTileSamples(shaped, grid, { 0, 0 });
    const std::vector<float> b = GenerateTileSamples(shaped, grid, { 1, 0 });
    bool equal = true;
    for (int i = 0; i <= cells; ++i) if (a[static_cast<size_t>(i) * (cells + 1) + cells] != b[static_cast<size_t>(i) * (cells + 1)]) equal = false;
    CHECK(equal);
    // The mountain preset really is more dramatic than the picture alone: a wider spread of heights.
    float loPlain = 1.0f, hiPlain = 0.0f, loShaped = 1.0f, hiShaped = 0.0f;
    for (double x = 0.0; x < 512.0; x += 5.0) for (double z = 0.0; z < 512.0; z += 5.0) {
        const float p = picture->Height01(x, z), s = shaped.Height01(x, z);
        loPlain = (std::min)(loPlain, p); hiPlain = (std::max)(hiPlain, p); loShaped = (std::min)(loShaped, s); hiShaped = (std::max)(hiShaped, s);
    }
    CHECK(hiShaped - loShaped > 3.0f * (hiPlain - loPlain));

    for (int preset = 0; preset < kHeightShapePresetCount; ++preset) {
        HeightShapeSettings p = HeightShapePreset(preset);
        HeightShapeSettings q = p; q.Clamp();
        CHECK(p.curve == q.curve && p.detailStrength == q.detailStrength);   // presets are already inside their limits
        CHECK(HeightShapePresetName(preset) != nullptr);
    }
    CHECK(HeightShapePreset(0).IsIdentity());
}

int main()
{
    TestIdentityAndLevels();
    TestAutoLevelsFixFlatness();
    TestSmoothingAndBicubic();
    TestDetailNoise();
    TestTilesStillMeet();
    if (g_failures == 0) std::printf("All world shape tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
