// Tests for the baked tile file (roadmap C6): src/WorldTileFile.{h,cpp}. No Direct3D, no framework.
//   1. The checksum is the standard CRC-32, and heights quantise to 16 bits with a bounded error.
//   2. THE FORMAT IS FROZEN: a tile written today produces exactly these bytes (made with Python's zlib, not with this code),
//      and those bytes read back into the same tile. If this fails, the layout changed: raise the version instead.
//   3. A round trip keeps everything, with and without holes, on a tile big enough to matter.
//   4. A damaged file is refused: every single byte changed, every shorter length, an extra byte.
//   5. Versions: a newer file is "newer", not "damaged"; unknown flags, version 0, a wrong magic and a bad size are refused.
//   6. Files on disk: saved, loaded, replaced, and refused when missing or damaged.
#include "WorldTileFile.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static unsigned g_seed = 4242u;
static unsigned Next() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }

static bool Same(const WorldTileData& a, const WorldTileData& b)
{
    return a.cells == b.cells && a.tileX == b.tileX && a.tileZ == b.tileZ && a.heights == b.heights && a.splat == b.splat && a.holes == b.holes;
}

// A tile of random heights and splat texels that add up to 255, with or without holes.
static WorldTileData RandomTile(int cells, bool holes)
{
    WorldTileData t;
    t.cells = cells; t.tileX = -7; t.tileZ = 12;
    t.heights.resize(static_cast<size_t>(cells + 1) * (cells + 1));
    for (auto& h : t.heights) h = static_cast<std::uint16_t>(Next() & 0xFFFF);
    t.splat.resize(static_cast<size_t>(cells) * cells * 4);
    for (size_t i = 0; i < t.splat.size(); i += 4) {
        const unsigned a = Next() % 256, b = Next() % (256 - a), c = Next() % (256 - a - b);
        t.splat[i] = static_cast<std::uint8_t>(a); t.splat[i + 1] = static_cast<std::uint8_t>(b); t.splat[i + 2] = static_cast<std::uint8_t>(c);
        t.splat[i + 3] = static_cast<std::uint8_t>(255 - a - b - c);
    }
    if (holes) { t.holes.resize(static_cast<size_t>(cells) * cells); for (auto& h : t.holes) h = (Next() % 5 == 0) ? 1 : 0; }
    return t;
}

// After changing bytes, recompute the checksum, so a test about the header sees the header and not the checksum.
static void Reseal(std::vector<std::uint8_t>& bytes)
{
    const std::uint32_t crc = Crc32(bytes.data(), bytes.size() - 4);
    for (int i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFF);
}

static void TestChecksumAndQuantising()
{
    const char* check = "123456789";
    CHECK(Crc32(reinterpret_cast<const std::uint8_t*>(check), 9) == 0xCBF43926u);
    CHECK(Crc32(nullptr, 0) == 0u);

    CHECK(QuantiseTileHeight(0.0f) == 0 && QuantiseTileHeight(1.0f) == 65535 && QuantiseTileHeight(0.5f) == 32768);
    CHECK(QuantiseTileHeight(-3.0f) == 0 && QuantiseTileHeight(7.0f) == 65535);
    CHECK(QuantiseTileHeight(std::nanf("")) == 0);
    double worst = 0.0;
    for (int i = 0; i <= 10000; ++i) {
        const float h = static_cast<float>(i) / 10000.0f;
        const double error = std::fabs(static_cast<double>(DequantiseTileHeight(QuantiseTileHeight(h))) - static_cast<double>(h));
        if (error > worst) worst = error;
    }
    std::printf("height round trip: worst error %.2e of the range (%.4f m at 240 m)\n", worst, worst * 240.0);
    CHECK(worst <= 0.5 / 65535.0 + 1e-7);
    for (int v : { 0, 1, 255, 256, 4660, 32767, 65534, 65535 }) CHECK(QuantiseTileHeight(DequantiseTileHeight(static_cast<std::uint16_t>(v))) == v);   // a stored value is stable
}

static void TestFrozenFormat()
{
    WorldTileData t;
    t.cells = 1; t.tileX = -2; t.tileZ = 3;
    t.heights = { 0, 65535, 256, 0x1234 };
    t.splat = { 10, 20, 30, 195 };
    t.holes = { 1 };
    const std::vector<std::uint8_t> golden = {
        0x44, 0x58, 0x57, 0x54, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0x00, 0x01, 0x34, 0x12, 0x0A, 0x14, 0x1E, 0xC3, 0x01, 0x60, 0xC7, 0xD3, 0x40 };
    CHECK(WriteWorldTile(t) == golden);
    WorldTileData back;
    CHECK(ReadWorldTile(golden.data(), golden.size(), back) == WorldTileResult::Ok);
    CHECK(Same(back, t));
    // The same tile without holes: no flag, no hole byte, and a different checksum.
    t.holes.clear();
    const std::vector<std::uint8_t> bare = WriteWorldTile(t);
    CHECK(bare.size() == golden.size() - 1 && bare[6] == 0);
}

static void TestRoundTrip()
{
    for (bool holes : { false, true }) {
        for (int cells : { 1, 2, 7, 32, 64 }) {
            const WorldTileData tile = RandomTile(cells, holes);
            std::string why;
            CHECK(tile.IsValid(&why));
            const std::vector<std::uint8_t> bytes = WriteWorldTile(tile);
            WorldTileData back;
            CHECK(ReadWorldTile(bytes.data(), bytes.size(), back) == WorldTileResult::Ok);
            CHECK(Same(back, tile));
        }
    }
    // The size on disk is exactly what the layout says.
    const WorldTileData big = RandomTile(64, true);
    CHECK(WriteWorldTile(big).size() == 20 + 65 * 65 * 2 + 64 * 64 * 4 + (64 * 64 + 7) / 8 + 4);
}

static void TestDamagedFilesAreRefused()
{
    const WorldTileData tile = RandomTile(3, true);
    const std::vector<std::uint8_t> good = WriteWorldTile(tile);
    WorldTileData out;
    int refused = 0;
    for (size_t i = 0; i < good.size(); ++i) {
        for (std::uint8_t change : { std::uint8_t(0x01), std::uint8_t(0x5A), std::uint8_t(0xFF) }) {
            std::vector<std::uint8_t> bad = good;
            bad[i] ^= change;
            if (ReadWorldTile(bad.data(), bad.size(), out) != WorldTileResult::Ok) ++refused;
            CHECK(out.heights.empty());   // nothing half-read is left behind
        }
    }
    CHECK(refused == static_cast<int>(good.size()) * 3);   // every single change of a single byte is found

    int shorter = 0;
    for (size_t length = 0; length < good.size(); ++length) if (ReadWorldTile(good.data(), length, out) != WorldTileResult::Ok) ++shorter;
    CHECK(shorter == static_cast<int>(good.size()));       // every shorter length, none crashes
    std::vector<std::uint8_t> longer = good;
    longer.push_back(0);
    CHECK(ReadWorldTile(longer.data(), longer.size(), out) == WorldTileResult::BadSize);
    CHECK(ReadWorldTile(nullptr, 100, out) == WorldTileResult::TooShort);
}

static void TestVersionsAndHeaders()
{
    const std::vector<std::uint8_t> good = WriteWorldTile(RandomTile(2, false));
    WorldTileData out;
    auto with = [&](auto change) { std::vector<std::uint8_t> b = good; change(b); Reseal(b); return ReadWorldTile(b.data(), b.size(), out); };
    CHECK(with([](std::vector<std::uint8_t>&) {}) == WorldTileResult::Ok);                                   // resealing alone changes nothing
    CHECK(with([](std::vector<std::uint8_t>& b) { b[4] = 2; }) == WorldTileResult::NewerVersion);            // not "damaged": newer
    CHECK(with([](std::vector<std::uint8_t>& b) { b[5] = 1; }) == WorldTileResult::NewerVersion);            // 257
    CHECK(with([](std::vector<std::uint8_t>& b) { b[4] = 0; }) == WorldTileResult::BadHeader);               // version 0 never existed
    CHECK(with([](std::vector<std::uint8_t>& b) { b[6] = 2; }) == WorldTileResult::BadHeader);               // an unknown flag
    CHECK(with([](std::vector<std::uint8_t>& b) { b[0] = 'X'; }) == WorldTileResult::BadMagic);
    CHECK(with([](std::vector<std::uint8_t>& b) { b[8] = 0; }) == WorldTileResult::BadHeader);               // 0 cells
    CHECK(with([](std::vector<std::uint8_t>& b) { b[8] = 3; }) == WorldTileResult::BadSize);                 // says 3 cells, holds 2
    CHECK(with([](std::vector<std::uint8_t>& b) { b[9] = 0x20; }) == WorldTileResult::BadHeader);            // 8194 cells: more than any tile
    CHECK(with([](std::vector<std::uint8_t>& b) { b[6] = 1; }) == WorldTileResult::BadSize);                 // claims holes it does not have
    // A texel that does not add up to 255 is content, not a checksum matter.
    CHECK(with([](std::vector<std::uint8_t>& b) { b[20 + 9 * 2] = static_cast<std::uint8_t>(b[20 + 9 * 2] + 1); }) == WorldTileResult::BadContent);

    // What may not be written.
    WorldTileData broken = RandomTile(2, false);
    broken.heights.pop_back();
    CHECK(WriteWorldTile(broken).empty());
    broken = RandomTile(2, false);
    broken.splat[3] = static_cast<std::uint8_t>(broken.splat[3] + 1);
    CHECK(WriteWorldTile(broken).empty());
    broken = RandomTile(2, true);
    broken.holes[0] = 2;
    CHECK(WriteWorldTile(broken).empty());
    CHECK(WriteWorldTile(WorldTileData{}).empty());   // no cells
}

static void TestFiles()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "dxwt_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / "tile.dxwt").string();
    std::string error;

    const WorldTileData first = RandomTile(16, true), second = RandomTile(8, false);
    CHECK(SaveWorldTileFile(path, first, error));
    WorldTileData back;
    CHECK(LoadWorldTileFile(path, back, error) && Same(back, first));
    CHECK(SaveWorldTileFile(path, second, error));                                  // replaces the file that is there
    CHECK(LoadWorldTileFile(path, back, error) && Same(back, second));
    CHECK(!fs::exists(path + ".tmp"));                                              // no temporary file is left behind

    CHECK(!LoadWorldTileFile((dir / "missing.dxwt").string(), back, error) && !error.empty());
    CHECK(!SaveWorldTileFile((dir / "no" / "such" / "folder" / "t.dxwt").string(), first, error) && !error.empty());
    WorldTileData invalid = first;
    invalid.heights.clear();
    CHECK(!SaveWorldTileFile(path, invalid, error));
    CHECK(LoadWorldTileFile(path, back, error) && Same(back, second));              // a refused save left the old file alone

    {   // a damaged file on disk
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bytes[bytes.size() / 2] ^= 0x10;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(!LoadWorldTileFile(path, back, error) && error.find("damaged") != std::string::npos && back.heights.empty());
    fs::remove_all(dir, ec);
}

int main()
{
    TestChecksumAndQuantising();
    TestFrozenFormat();
    TestRoundTrip();
    TestDamagedFilesAreRefused();
    TestVersionsAndHeaders();
    TestFiles();
    if (g_failures == 0) std::printf("All world tile file tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
