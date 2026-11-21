#include "WorldTileFile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
constexpr size_t kHeaderBytes = 20;
constexpr size_t kChecksumBytes = 4;
constexpr std::uintmax_t kLargestFile = 256ull * 1024 * 1024;   // a 4096-cell tile is about 100 MB; anything bigger is not a tile

void Put16(std::vector<std::uint8_t>& out, std::uint16_t v) { out.push_back(static_cast<std::uint8_t>(v & 0xFF)); out.push_back(static_cast<std::uint8_t>(v >> 8)); }
void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
std::uint16_t Get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t Get32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }

size_t SampleCount(int cells) { return static_cast<size_t>(cells + 1) * static_cast<size_t>(cells + 1); }
size_t TexelCount(int cells) { return static_cast<size_t>(cells) * static_cast<size_t>(cells); }
size_t HoleBytes(int cells) { return (TexelCount(cells) + 7) / 8; }
}

const char* WorldTileResultText(WorldTileResult result)
{
    switch (result) {
        case WorldTileResult::Ok: return "ok";
        case WorldTileResult::TooShort: return "the file is too short to be a tile";
        case WorldTileResult::BadMagic: return "this is not a tile file";
        case WorldTileResult::BadHeader: return "the tile header is not valid";
        case WorldTileResult::NewerVersion: return "this tile was saved by a newer version of the program";
        case WorldTileResult::BadSize: return "the file's length does not match its header";
        case WorldTileResult::BadChecksum: return "the tile is damaged (its checksum does not match)";
        case WorldTileResult::BadContent: return "the tile holds numbers that cannot be right";
    }
    return "unknown";
}

bool WorldTileData::IsValid(std::string* why) const
{
    auto fail = [&](const char* text) { if (why) *why = text; return false; };
    if (cells < 1 || cells > kWorldTileMaxCells) return fail("cells must be 1 to 4096");
    if (heights.size() != SampleCount(cells)) return fail("heights must hold (cells + 1)^2 samples");
    if (splat.size() != TexelCount(cells) * 4) return fail("splat must hold cells^2 texels of 4 bytes");
    if (!holes.empty() && holes.size() != TexelCount(cells)) return fail("holes must be empty or hold cells^2 bytes");
    for (std::uint8_t h : holes) if (h > 1) return fail("a hole is 0 or 1");
    for (size_t t = 0; t < TexelCount(cells); ++t) {
        const unsigned sum = splat[t * 4] + splat[t * 4 + 1] + splat[t * 4 + 2] + splat[t * 4 + 3];
        if (sum != 255) return fail("every splat texel must add up to 255");
    }
    return true;
}

std::uint16_t QuantiseTileHeight(float height01)
{
    if (!(height01 > 0.0f)) return 0;   // also catches NaN
    if (height01 >= 1.0f) return 65535;
    return static_cast<std::uint16_t>(std::lround(static_cast<double>(height01) * 65535.0));
}

float DequantiseTileHeight(std::uint16_t value) { return static_cast<float>(static_cast<double>(value) / 65535.0); }

std::uint32_t Crc32(const std::uint8_t* bytes, size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::vector<std::uint8_t> WriteWorldTile(const WorldTileData& tile)
{
    std::vector<std::uint8_t> out;
    if (!tile.IsValid()) return out;
    const bool hasHoles = !tile.holes.empty();
    out.reserve(kHeaderBytes + tile.heights.size() * 2 + tile.splat.size() + HoleBytes(tile.cells) + kChecksumBytes);
    out.insert(out.end(), { 'D', 'X', 'W', 'T' });
    Put16(out, kWorldTileFormatVersion);
    Put16(out, hasHoles ? kWorldTileFlagHoles : 0);
    Put32(out, static_cast<std::uint32_t>(tile.cells));
    Put32(out, static_cast<std::uint32_t>(tile.tileX));
    Put32(out, static_cast<std::uint32_t>(tile.tileZ));
    for (std::uint16_t h : tile.heights) Put16(out, h);
    out.insert(out.end(), tile.splat.begin(), tile.splat.end());
    if (hasHoles) {
        std::vector<std::uint8_t> bits(HoleBytes(tile.cells), 0);
        for (size_t i = 0; i < tile.holes.size(); ++i) if (tile.holes[i]) bits[i / 8] = static_cast<std::uint8_t>(bits[i / 8] | (1u << (i % 8)));
        out.insert(out.end(), bits.begin(), bits.end());
    }
    Put32(out, Crc32(out.data(), out.size()));
    return out;
}

WorldTileResult ReadWorldTile(const std::uint8_t* bytes, size_t size, WorldTileData& out)
{
    out = WorldTileData{};
    if (!bytes || size < kHeaderBytes + kChecksumBytes) return WorldTileResult::TooShort;
    if (bytes[0] != 'D' || bytes[1] != 'X' || bytes[2] != 'W' || bytes[3] != 'T') return WorldTileResult::BadMagic;
    const std::uint16_t version = Get16(bytes + 4), flags = Get16(bytes + 6);
    const std::uint32_t cells = Get32(bytes + 8);
    if (version == 0) return WorldTileResult::BadHeader;
    if (version > kWorldTileFormatVersion) return WorldTileResult::NewerVersion;   // before anything else about the layout is trusted
    // --- version 1 ---
    if ((flags & ~kWorldTileFlagHoles) != 0) return WorldTileResult::BadHeader;
    if (cells < 1 || cells > static_cast<std::uint32_t>(kWorldTileMaxCells)) return WorldTileResult::BadHeader;
    const int n = static_cast<int>(cells);
    const bool hasHoles = (flags & kWorldTileFlagHoles) != 0;
    const size_t expected = kHeaderBytes + SampleCount(n) * 2 + TexelCount(n) * 4 + (hasHoles ? HoleBytes(n) : 0) + kChecksumBytes;
    if (size != expected) return WorldTileResult::BadSize;
    if (Crc32(bytes, size - kChecksumBytes) != Get32(bytes + size - kChecksumBytes)) return WorldTileResult::BadChecksum;

    WorldTileData tile;
    tile.cells = n;
    tile.tileX = static_cast<std::int32_t>(Get32(bytes + 12));
    tile.tileZ = static_cast<std::int32_t>(Get32(bytes + 16));
    const std::uint8_t* p = bytes + kHeaderBytes;
    tile.heights.resize(SampleCount(n));
    for (std::uint16_t& h : tile.heights) { h = Get16(p); p += 2; }
    tile.splat.assign(p, p + TexelCount(n) * 4);
    p += TexelCount(n) * 4;
    if (hasHoles) {
        tile.holes.resize(TexelCount(n));
        for (size_t i = 0; i < tile.holes.size(); ++i) tile.holes[i] = static_cast<std::uint8_t>((p[i / 8] >> (i % 8)) & 1u);
    }
    if (!tile.IsValid()) return WorldTileResult::BadContent;
    out = std::move(tile);
    return WorldTileResult::Ok;
}

bool SaveWorldTileFile(const std::string& path, const WorldTileData& tile, std::string& error)
{
    std::string why;
    if (!tile.IsValid(&why)) { error = "the tile is not valid: " + why; return false; }
    const std::vector<std::uint8_t> bytes = WriteWorldTile(tile);
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) { error = "cannot create " + temporary; return false; }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) { error = "cannot write " + temporary; std::error_code ignore; std::filesystem::remove(temporary, ignore); return false; }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {   // some systems will not rename over an existing file
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temporary, path, ec);
        if (ec) { error = "cannot replace " + path + ": " + ec.message(); std::error_code ignore; std::filesystem::remove(temporary, ignore); return false; }
    }
    return true;
}

bool LoadWorldTileFile(const std::string& path, WorldTileData& out, std::string& error)
{
    out = WorldTileData{};
    std::error_code ec;
    const std::uintmax_t length = std::filesystem::file_size(path, ec);
    if (ec) { error = "cannot open " + path; return false; }
    if (length > kLargestFile) { error = path + " is too big to be a tile"; return false; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "cannot open " + path; return false; }
    std::vector<std::uint8_t> bytes(static_cast<size_t>(length));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file && !bytes.empty()) { error = "cannot read " + path; return false; }
    const WorldTileResult result = ReadWorldTile(bytes.data(), bytes.size(), out);
    if (result != WorldTileResult::Ok) { error = path + ": " + WorldTileResultText(result); return false; }
    return true;
}
