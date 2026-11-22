// Tests for src/HeightDeterrace.{h,cpp} (roadmap H4): the stairs of an 8-bit heightmap come out without inventing ground.
// No Direct3D, no framework. Run from the repository root (the real-picture test reads assets/).
#include "HeightDeterrace.h"
#include "HeightPicture.h"
#include "HeightRaster.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

struct Quantised { HeightPicture picture; std::vector<double> truth; };

// A smooth surface (in levels), rounded to whole levels like an 8-bit file does. `truth` keeps the surface the file was trying to say.
static Quantised Smooth(int n, double slopeX, double slopeZ, double hillAmplitude, double hillPeriod)
{
    Quantised q;
    q.picture.width = q.picture.height = n; q.picture.bits = 8;
    q.truth.resize(static_cast<size_t>(n) * n);
    double lowest = 1e30;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const double v = slopeX * x + hillAmplitude * std::sin(z / hillPeriod) + slopeZ * z;
        q.truth[static_cast<size_t>(z) * n + x] = v; lowest = (std::min)(lowest, v);
    }
    for (double& v : q.truth) v -= lowest;
    for (double v : q.truth) q.picture.levels.push_back(static_cast<std::uint16_t>(std::floor(v + 0.5)));
    return q;
}

static double Rms(const std::vector<float>& values, const std::vector<double>& truth)
{
    double sum = 0.0;
    for (size_t i = 0; i < values.size(); ++i) { const double d = values[i] - truth[i]; sum += d * d; }
    return std::sqrt(sum / static_cast<double>(values.size()));
}

static void TestSmoothGround()
{
    // Gentle: 0.06 level per pixel, so every terrace is about 17 pixels wide. (Reference RMS 0.2888 -> 0.068 computed in Python.)
    const Quantised gentle = Smooth(256, 0.06, 0.02, 6.0, 40.0);
    std::vector<float> raw; for (std::uint16_t l : gentle.picture.levels) raw.push_back(l);
    const std::vector<float> fixed = DeterraceLevels(gentle.picture);
    const double rawRms = Rms(raw, gentle.truth), fixedRms = Rms(fixed, gentle.truth);
    CHECK(Near(rawRms, 0.2888, 2e-3) && Near(fixedRms, 0.068, 3e-3));
    CHECK(rawRms > 3.0 * fixedRms);

    // Steeper: 0.4 level per pixel, terraces about 2 pixels wide. (Reference 0.288 -> 0.052.)
    const Quantised steep = Smooth(256, 0.4, 0.0, 3.0, 20.0);
    std::vector<float> rawSteep; for (std::uint16_t l : steep.picture.levels) rawSteep.push_back(l);
    const std::vector<float> fixedSteep = DeterraceLevels(steep.picture);
    CHECK(Rms(rawSteep, steep.truth) > 3.0 * Rms(fixedSteep, steep.truth));
}

static void TestNeverLeavesItsBinOrSwapsPixels()
{
    const Quantised q = Smooth(128, 0.15, 0.05, 5.0, 17.0);
    const std::vector<float> v = DeterraceLevels(q.picture);
    bool inBin = true;
    const int lowestLevel = *std::min_element(q.picture.levels.begin(), q.picture.levels.end()), highestLevel = *std::max_element(q.picture.levels.begin(), q.picture.levels.end());
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::fabs(v[i] - q.picture.levels[i]) > 0.5f) inBin = false;
        if (v[i] < lowestLevel || v[i] > highestLevel) inBin = false;
    }
    CHECK(inBin);   // exact: the bounds are whole numbers +/- 0.5

    // So two pixels the file ranks one above the other are never swapped: the highest height of every level is at or below the lowest of the next.
    std::vector<float> lo(256, 1e9f), hi(256, -1e9f);
    for (size_t i = 0; i < v.size(); ++i) { const int l = q.picture.levels[i]; lo[l] = (std::min)(lo[l], v[i]); hi[l] = (std::max)(hi[l], v[i]); }
    bool ordered = true; float highestSoFar = -1e9f;
    for (int l = 0; l < 256; ++l) { if (hi[l] < lo[l]) continue; if (lo[l] < highestSoFar) ordered = false; highestSoFar = (std::max)(highestSoFar, hi[l]); }
    CHECK(ordered);
}

static void TestLeavesTheRestAlone()
{
    // 16-bit: exactly as stored. Iterations 0: exactly as stored. A flat picture stays flat.
    HeightPicture p16; p16.width = 4; p16.height = 3; p16.bits = 16; p16.levels = { 0, 100, 65535, 7, 8, 9, 40000, 40001, 40002, 5, 6, 7 };
    const std::vector<float> same = DeterraceLevels(p16);
    bool exact = same.size() == p16.levels.size();
    for (size_t i = 0; i < same.size() && exact; ++i) if (same[i] != static_cast<float>(p16.levels[i])) exact = false;
    CHECK(exact);
    HeightPicture p8 = p16; p8.bits = 8; for (auto& l : p8.levels) l = static_cast<std::uint16_t>(l & 0xFF);
    CHECK(DeterraceLevels(p8, 0)[1] == 100.0f && DeterraceLevels(p8)[1] != 100.0f);   // 8-bit with work to do does change; with 0 iterations it does not
    HeightPicture flat; flat.width = flat.height = 5; flat.bits = 8; flat.levels.assign(25, 77);
    bool stillFlat = true; for (float v : DeterraceLevels(flat)) if (v != 77.0f) stillFlat = false;
    CHECK(stillFlat);
    CHECK(DeterraceLevels(HeightPicture{}).empty());
}

static void TestImport()
{
    // A gentle ramp, 64 x 16 pixels, one level every 10 pixels: seven wide terraces.
    HeightPicture ramp; ramp.width = 64; ramp.height = 16; ramp.bits = 8;
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 64; ++x) ramp.levels.push_back(static_cast<std::uint16_t>(x / 10));
    HeightImportSettings settings; settings.lowestY = -5.0f; settings.relief = 70.0f;

    settings.deterrace = false;
    HeightRaster stairs; CHECK(ImportHeightRaster(ramp, settings, stairs));
    settings.deterrace = true;
    HeightRaster smooth; HeightImportInfo info; CHECK(ImportHeightRaster(ramp, settings, smooth, &info));

    auto distinct = [](std::vector<float> v) { std::sort(v.begin(), v.end()); return static_cast<size_t>(std::unique(v.begin(), v.end()) - v.begin()); };
    CHECK(distinct(stairs.metres) == 7);              // as many heights as levels: stairs
    CHECK(distinct(smooth.metres) > 7 * 8);           // the terraces are rounded off
    // The relief is still the relief: lowest point exactly lowestY, highest exactly lowestY + relief (the fit uses what the blur left).
    const auto range = std::minmax_element(smooth.metres.begin(), smooth.metres.end());
    CHECK(*range.first == -5.0f && *range.second == 65.0f);
    CHECK(info.minLevel >= 0.0f && info.maxLevel <= 6.0f && info.maxLevel > 5.4f);
    // And it is still a ramp: never goes back down along X.
    bool rising = true;
    for (int z = 0; z < 16; ++z) for (int x = 1; x < 64; ++x) if (smooth.At(x, z) < smooth.At(x - 1, z) - 1e-4f) rising = false;
    CHECK(rising);
}

// The repository's picture reduced to 8 bits (round(level / 6475 * 255)) to play the part of an 8-bit file. Reference numbers from Python.
static void TestTheRealPictureAsEightBit()
{
    HeightPicture source; std::string error;
    if (!LoadHeightPicture("assets/dark alien/Height Map.png", source, error)) { std::printf("FAIL: %s (run this test from the repository root)\n", error.c_str()); ++g_failures; return; }
    HeightPicture eight = source; eight.bits = 8;
    std::vector<double> truth(source.levels.size());
    for (size_t i = 0; i < truth.size(); ++i) { truth[i] = static_cast<double>(source.levels[i]) / 6475.0 * 255.0; eight.levels[i] = static_cast<std::uint16_t>(std::floor(truth[i] + 0.5)); }
    std::vector<float> raw; for (std::uint16_t l : eight.levels) raw.push_back(l);
    const std::vector<float> fixed = DeterraceLevels(eight);
    const double rawRms = Rms(raw, truth), fixedRms = Rms(fixed, truth);
    CHECK(Near(rawRms, 0.2702, 2e-3) && Near(fixedRms, 0.2711, 2e-3));   // numpy: 0.2702 and 0.2711
    // This ground is rough from pixel to pixel, so the stairs are lost in it: de-terracing neither helps nor hurts (within 2%), and never leaves a bin.
    CHECK(fixedRms < rawRms * 1.02);
    bool inBin = true; for (size_t i = 0; i < fixed.size(); ++i) if (std::fabs(fixed[i] - eight.levels[i]) > 0.5f) inBin = false;
    CHECK(inBin);
}

int main()
{
    TestSmoothGround();
    TestNeverLeavesItsBinOrSwapsPixels();
    TestLeavesTheRestAlone();
    TestImport();
    TestTheRealPictureAsEightBit();
    if (g_failures == 0) std::printf("All height deterrace tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
