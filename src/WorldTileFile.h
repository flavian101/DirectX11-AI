#pragma once
// The file format of ONE baked tile (roadmap C6): its heights, its layer weights and, optionally, its holes. Pure C++, no Direct3D.
//
// WHAT IT IS FOR. A tile is normally made from the ground function (source -> shape -> edits -> rivers), and the edits live in the
// `.worldedits` file. A BAKED tile is the finished result stored as it is: an imported world, a tile cache, or a hole mask (caves and
// tunnel mouths, roadmap L2) that no function can make. Nothing in the engine reads this file yet; the format comes first so that
// whatever does is written against one fixed layout and a version number.
//
// THE LAYOUT (every number little-endian, whatever machine writes it):
//    0   4  magic "DXWT"
//    4   2  version (1)
//    6   2  flags: bit 0 = the file holds a hole mask; every other bit must be 0 in version 1
//    8   4  cells: cells per side of the tile (1 to 4096)
//   12   4  tile x (signed)           which tile of the world this is, so a loader can refuse a file made for another place
//   16   4  tile z (signed)
//   20      heights: (cells + 1)^2 samples, row by row, 16 bits each: 0..65535 stands for 0..1 of the ground's height range (R16)
//           splat:   cells^2 texels, row by row, 4 bytes each: grass, rock, sand, snow, adding up to 255 (RGBA8; one texel per cell)
//           holes:   only if flag bit 0 is set: cells^2 bits, row by row, least significant bit first; 1 = a hole (no ground)
//  end  4  CRC-32 (IEEE 802.3, the one of zip and PNG) of every byte before it
//
// VERSIONS. A reader accepts the versions it knows and refuses a newer one with a clear message (a file saved by a newer program is
// not "corrupt"). To change the layout: raise kWorldTileFormatVersion, keep the old reader branch in ReadWorldTile, and add a golden
// test for the old bytes. Never reuse a version number for a different layout.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint16_t kWorldTileFormatVersion = 1;
constexpr int kWorldTileMaxCells = 4096;
constexpr std::uint16_t kWorldTileFlagHoles = 1;

struct WorldTileData {
    std::int32_t tileX = 0, tileZ = 0;
    int cells = 0;                            // cells per side; a tile has cells + 1 samples per side
    std::vector<std::uint16_t> heights;       // (cells + 1)^2 samples
    std::vector<std::uint8_t> splat;          // cells^2 texels x 4 bytes
    std::vector<std::uint8_t> holes;          // empty = no holes; otherwise cells^2 bytes in memory, 0 or 1 (the file packs them into bits)

    // Do the sizes agree with `cells`, and does every splat texel add up to 255? `why` says what is wrong.
    bool IsValid(std::string* why = nullptr) const;
};

enum class WorldTileResult {
    Ok,
    TooShort,        // not even a header and a checksum
    BadMagic,        // not a tile file
    BadHeader,       // version 0, unknown flags, or a size no tile has
    NewerVersion,    // made by a newer program
    BadSize,         // the length does not match what the header promises
    BadChecksum,     // the bytes changed after they were written
    BadContent,      // the numbers are out of range (a splat texel that does not add up to 255)
};
const char* WorldTileResultText(WorldTileResult result);

// 0..1 <-> 0..65535. Values outside 0..1 (and NaN) are clamped; a round trip is off by at most half a step, 7.6e-6 of the height range.
std::uint16_t QuantiseTileHeight(float height01);
float DequantiseTileHeight(std::uint16_t value);

// The standard CRC-32 (check value: "123456789" gives 0xCBF43926).
std::uint32_t Crc32(const std::uint8_t* bytes, size_t size);

// The bytes of the file; empty if the data is not valid.
std::vector<std::uint8_t> WriteWorldTile(const WorldTileData& tile);
// Reads and checks a whole file. On anything but Ok, `out` is left empty.
WorldTileResult ReadWorldTile(const std::uint8_t* bytes, size_t size, WorldTileData& out);

// Files. Saving writes a temporary file beside the target and renames it, so a crash never leaves half a tile under the real name.
bool SaveWorldTileFile(const std::string& path, const WorldTileData& tile, std::string& error);
bool LoadWorldTileFile(const std::string& path, WorldTileData& out, std::string& error);
