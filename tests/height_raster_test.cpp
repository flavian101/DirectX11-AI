// Tests for src/HeightPicture.{h,cpp} and src/HeightRaster.{h,cpp} (roadmap H1): a picture becomes a grid of metres without clipping.
// No Direct3D, no framework. Run from the repository root (the real-picture test reads assets/).
#include "HeightPicture.h"
#include "HeightRaster.h"
#include "Png16.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static HeightPicture MakePicture(int width, int height, int bits, const std::vector<std::uint16_t>& levels)
{
    HeightPicture p;
    p.width = width; p.height = height; p.bits = bits; p.levels = levels;
    return p;
}

static size_t CountDistinct(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    return static_cast<size_t>(std::unique(values.begin(), values.end()) - values.begin());
}

static void TestMapping()
{
    // Levels 1000 .. 5000 of a 16-bit file: a narrow band, the typical "flat looking" picture.
    const HeightPicture picture = MakePicture(4, 3, 16, { 1000, 2000, 3000, 5000, 1000, 1500, 2500, 4000, 5000, 4999, 1001, 3333 });
    HeightImportSettings settings;
    settings.lowestY = -10.0f; settings.relief = 50.0f; settings.widthMetres = 300.0;
    HeightRaster raster; HeightImportInfo info;
    CHECK(ImportHeightRaster(picture, settings, raster, &info));
    CHECK(raster.IsValid() && raster.width == 4 && raster.height == 3);
    CHECK(info.minLevel == 1000 && info.maxLevel == 5000 && !info.flat);

    // The lowest level is exactly lowestY and the highest exactly lowestY + relief: the whole relief is used, no dead range.
    CHECK(raster.At(0, 0) == -10.0f);
    CHECK(raster.At(3, 0) == 40.0f);
    CHECK(raster.At(0, 2) == 40.0f && picture.At(0, 2) == 5000);   // the other pixel at the highest level

    // Nothing is clipped, nothing merges, and the order of the levels is the order of the heights.
    bool inRange = true, monotonic = true;
    for (size_t i = 0; i < raster.metres.size(); ++i) {
        if (raster.metres[i] < -10.0f || raster.metres[i] > 40.0f) inRange = false;
        for (size_t j = 0; j < raster.metres.size(); ++j) {
            const bool levelLess = picture.levels[i] < picture.levels[j];
            const bool heightLess = raster.metres[i] < raster.metres[j];
            const bool levelEqual = picture.levels[i] == picture.levels[j];
            const bool heightEqual = raster.metres[i] == raster.metres[j];
            if (levelLess != heightLess || levelEqual != heightEqual) monotonic = false;
        }
    }
    CHECK(inRange && monotonic);
    CHECK(std::fabs(raster.At(1, 0) - (-10.0f + 50.0f * (1000.0f / 4000.0f))) < 1e-5f);   // 2000 is a quarter of the way up

    // Placement: 4 pixels across 300 m is 100 m between samples; the depth follows the shape.
    CHECK(raster.spacingX == 100.0 && raster.spacingZ == 100.0 && raster.SizeX() == 300.0 && raster.SizeZ() == 200.0);
}

static void TestFlatAndBadInput()
{
    const HeightPicture flat = MakePicture(3, 3, 8, std::vector<std::uint16_t>(9, 77));
    HeightImportSettings settings; settings.lowestY = 12.5f; settings.relief = 80.0f;
    HeightRaster raster; HeightImportInfo info;
    CHECK(ImportHeightRaster(flat, settings, raster, &info));
    CHECK(info.flat);
    bool allLowest = true;
    for (float v : raster.metres) if (v != 12.5f) allLowest = false;
    CHECK(allLowest);   // one level, nothing to stretch: it sits at lowestY, never at NaN

    HeightRaster untouched; untouched.width = 99;
    CHECK(!ImportHeightRaster(HeightPicture{}, settings, untouched));
    CHECK(untouched.width == 99);   // a failed import leaves the raster alone

    HeightImportSettings wild;
    wild.widthMetres = -5.0; wild.lowestY = 1e9f; wild.relief = -3.0f;
    wild.Clamp();
    CHECK(wild.widthMetres == HeightImportSettings::kWidthMin && wild.lowestY == HeightImportSettings::kYMax && wild.relief == HeightImportSettings::kReliefMin);
    HeightImportSettings nan; nan.relief = std::nanf(""); nan.widthMetres = std::nan("");
    nan.Clamp();
    CHECK(nan.relief == HeightImportSettings::kReliefMin && nan.widthMetres == HeightImportSettings::kWidthMin);
}

static void TestLoading()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    std::string error;

    // A 16-bit PNG comes back with its 16 bits.
    const std::string path16 = (directory / "height_raster_test_16.png").string();
    const std::vector<std::uint16_t> levels16 = { 0, 1, 65535, 300, 301, 40000 };
    CHECK(WriteGrayscale16Png(path16, 3, 2, levels16, error));
    HeightPicture picture;
    CHECK(LoadHeightPicture(path16, picture, error));
    CHECK(picture.bits == 16 && picture.width == 3 && picture.height == 2 && picture.levels == levels16);

    // An 8-bit PNG comes back with its 8 bits (the red channel of an RGBA file).
    const std::string path8 = (directory / "height_raster_test_8.png").string();
    const std::vector<std::uint8_t> rgba = { 0, 9, 9, 255,  10, 9, 9, 255,  255, 9, 9, 255,  128, 9, 9, 255 };
    CHECK(WriteRgba8Png(path8, 2, 2, rgba, error));
    CHECK(LoadHeightPicture(path8, picture, error));
    CHECK(picture.bits == 8 && picture.levels == std::vector<std::uint16_t>({ 0, 10, 255, 128 }));

    CHECK(!LoadHeightPicture((directory / "height_raster_test_missing.png").string(), picture, error) && !error.empty());
    std::error_code ignored;
    std::filesystem::remove(path16, ignored);
    std::filesystem::remove(path8, ignored);
}

// The repository's own picture, measured independently with Python (PIL + numpy) before this test was written.
static void TestTheRealPicture()
{
    const std::string path = "assets/dark alien/Height Map.png";
    HeightPicture picture; std::string error;
    if (!LoadHeightPicture(path, picture, error)) { std::printf("FAIL: %s (run this test from the repository root)\n", error.c_str()); ++g_failures; return; }
    CHECK(picture.bits == 16 && picture.width == 2048 && picture.height == 2048);

    HeightImportSettings settings;
    settings.lowestY = -30.0f; settings.relief = 240.0f; settings.widthMetres = 1024.0;
    HeightRaster raster; HeightImportInfo info;
    CHECK(ImportHeightRaster(picture, settings, raster, &info));
    CHECK(info.minLevel == 0 && info.maxLevel == 6475);   // the picture uses 0.0988 of its 16 bits

    // The whole relief is used: the old pipeline would have given 0.0988 x 240 = 23.7 m unless levels were set by hand.
    const auto range = std::minmax_element(raster.metres.begin(), raster.metres.end());
    CHECK(*range.first == -30.0f && *range.second == 210.0f);

    // Nothing merges: as many different heights as the file has different levels.
    std::vector<bool> used(65536, false);
    size_t distinctLevels = 0;
    for (std::uint16_t level : picture.levels) if (!used[level]) { used[level] = true; ++distinctLevels; }
    CHECK(distinctLevels == 6425);
    CHECK(CountDistinct(raster.metres) == distinctLevels);

    // The old auto-levels (0.5% / 99.5%) put white at level 5178 and clamped everything above it flat.
    // Here those pixels keep their own heights: 20,954 pixels, 1,246 different heights, the top 20% of the range.
    std::vector<float> aboveOldWhite;
    for (size_t i = 0; i < picture.levels.size(); ++i) if (picture.levels[i] > 5178) aboveOldWhite.push_back(raster.metres[i]);
    CHECK(aboveOldWhite.size() == 20954);
    CHECK(CountDistinct(aboveOldWhite) == 1246);
    const float oldWhiteHeight = -30.0f + 240.0f * (5178.0f / 6475.0f);
    CHECK(*std::max_element(aboveOldWhite.begin(), aboveOldWhite.end()) - oldWhiteHeight > 0.19f * 240.0f);

    // The flat floor of the file is the file's, 12.436% of it, and nothing else is flat because of the import.
    size_t atFloor = 0;
    for (float v : raster.metres) if (v == -30.0f) ++atFloor;
    CHECK(atFloor == 521605);
}

int main()
{
    TestMapping();
    TestFlatAndBadInput();
    TestLoading();
    TestTheRealPicture();
    if (g_failures == 0) std::printf("All height raster tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
