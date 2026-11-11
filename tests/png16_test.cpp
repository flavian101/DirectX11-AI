// Unit tests for src/Png16.{h,cpp}: write a 16-bit PNG, read it back with stb_image (the same reader the terrain uses).
#include "Png16.h"
#include "libs/stb_image.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static std::uint32_t g_rng = 12345;
static std::uint16_t RandomU16() { g_rng = g_rng * 1664525u + 1013904223u; return static_cast<std::uint16_t>(g_rng >> 16); }

// Writes, then reads back through stb_image, and demands every value is identical.
static bool RoundTrip(const std::string& path, int width, int height, const std::vector<std::uint16_t>& values)
{
    std::string error;
    if (!WriteGrayscale16Png(path, width, height, values, error)) { std::printf("  write failed: %s\n", error.c_str()); return false; }
    if (!stbi_is_16_bit(path.c_str())) { std::printf("  not recognised as 16-bit\n"); return false; }
    int w = 0, h = 0, channels = 0;
    stbi_us* image = stbi_load_16(path.c_str(), &w, &h, &channels, 1);   // 1 = ask for greyscale
    if (!image) { std::printf("  stb_image refused it: %s\n", stbi_failure_reason()); return false; }
    bool same = (w == width && h == height);
    for (size_t i = 0; same && i < values.size(); ++i) same = (image[i] == values[i]);
    stbi_image_free(image);
    return same;
}

int main()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "png16_test";
    std::filesystem::create_directories(dir);
    auto file = [&](const char* name) { return (dir / name).string(); };

    // tiny and awkward sizes
    CHECK(RoundTrip(file("1x1.png"), 1, 1, { 40000 }));
    CHECK(RoundTrip(file("3x2.png"), 3, 2, { 0, 1, 255, 256, 65534, 65535 }));
    CHECK(RoundTrip(file("1x5.png"), 1, 5, { 9, 8, 7, 65535, 0 }));

    // the stored-block edges: raw size = height * (1 + 2 * width) bytes, blocks hold at most 65535
    {
        std::vector<std::uint16_t> v(static_cast<size_t>(10922) * 3);   // 3 * (1 + 21844) = 65535 bytes: exactly ONE full block
        for (auto& x : v) x = RandomU16();
        CHECK(RoundTrip(file("one_full_block.png"), 10922, 3, v));
    }
    {
        std::vector<std::uint16_t> v(static_cast<size_t>(10923) * 3);   // 65541 bytes: a full block plus a 6-byte one
        for (auto& x : v) x = RandomU16();
        CHECK(RoundTrip(file("two_blocks.png"), 10923, 3, v));
    }
    {
        std::vector<std::uint16_t> v(static_cast<size_t>(1025) * 1025);  // a real terrain: 2 MB, ~33 blocks
        for (size_t i = 0; i < v.size(); ++i) v[i] = (i % 3 == 0) ? RandomU16() : static_cast<std::uint16_t>(i);
        CHECK(RoundTrip(file("terrain.png"), 1025, 1025, v));
        std::printf("  1025x1025 file size: %ju bytes\n", static_cast<std::uintmax_t>(std::filesystem::file_size(file("terrain.png"))));
    }

    // writing over an existing file works, and leaves no ".tmp" behind
    CHECK(RoundTrip(file("1x1.png"), 2, 1, { 1, 2 }));
    CHECK(!std::filesystem::exists(file("1x1.png") + ".tmp"));

    // bad input is refused, not written
    std::string error;
    CHECK(!WriteGrayscale16Png(file("bad.png"), 4, 4, std::vector<std::uint16_t>(15), error) && !error.empty());
    CHECK(!WriteGrayscale16Png(file("bad.png"), 0, 4, {}, error));
    CHECK(!std::filesystem::exists(file("bad.png")));

    // quantising: 0..1 floats to 0..65535 integers, and back within half a step
    const std::vector<float> samples = { 0.0f, 1.0f, 0.5f, -0.3f, 1.7f, 0.123456f };
    const std::vector<std::uint16_t> q = QuantizeHeights(samples);
    CHECK(q[0] == 0 && q[1] == 65535 && q[2] == 32768 && q[3] == 0 && q[4] == 65535);
    CHECK(std::fabs(q[5] / 65535.0f - 0.123456f) <= 0.5f / 65535.0f + 1e-7f);

    // 8-bit RGBA (the paint map): write, read back as 4 channels, demand every byte identical
    {
        auto rgbaRoundTrip = [&](const std::string& path, int w, int h, const std::vector<std::uint8_t>& bytes) {
            std::string err;
            if (!WriteRgba8Png(path, w, h, bytes, err)) { std::printf("  rgba write failed: %s\n", err.c_str()); return false; }
            if (stbi_is_16_bit(path.c_str())) return false;                      // must be 8-bit
            int rw = 0, rh = 0, channels = 0;
            stbi_uc* image = stbi_load(path.c_str(), &rw, &rh, &channels, 4);
            if (!image) { std::printf("  stb_image refused the RGBA file: %s\n", stbi_failure_reason()); return false; }
            bool same = rw == w && rh == h && channels == 4;
            for (size_t i = 0; same && i < bytes.size(); ++i) same = image[i] == bytes[i];
            stbi_image_free(image);
            return same;
        };
        CHECK(rgbaRoundTrip(file("rgba1.png"), 1, 1, { 10, 20, 30, 40 }));
        CHECK(rgbaRoundTrip(file("rgba2.png"), 3, 2, { 0,0,0,0, 255,255,255,255, 1,2,3,4, 250,0,5,0, 7,7,7,7, 0,255,0,255 }));
        std::vector<std::uint8_t> big(static_cast<size_t>(512) * 512 * 4);      // a real paint map: 1 MB, many stored blocks
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i % 5 == 0 ? RandomU16() : i);
        CHECK(rgbaRoundTrip(file("rgba512.png"), 512, 512, big));
        std::string err;
        CHECK(!WriteRgba8Png(file("badrgba.png"), 2, 2, std::vector<std::uint8_t>(15), err) && !err.empty());
        CHECK(!std::filesystem::exists(file("badrgba.png")));
    }

    std::filesystem::remove_all(dir);
    if (g_failures == 0) std::printf("Png16: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
