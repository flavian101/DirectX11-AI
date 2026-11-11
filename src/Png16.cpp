#include "Png16.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

// ---- the two checksums PNG needs -----------------------------------------------------------------------------------
// CRC-32 guards every PNG chunk. It is polynomial division over bits: the table holds the answer for each possible byte.
std::uint32_t Crc32(const std::uint8_t* data, size_t size, std::uint32_t crc = 0)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

// Adler-32 guards the zlib stream inside the IDAT chunk: two running sums, kept below 65521.
std::uint32_t Adler32(const std::vector<std::uint8_t>& data)
{
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v)   // PNG numbers are big-endian
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

// A chunk is: length of the data, 4 letters naming the kind, the data, then a CRC over the name AND the data.
void PutChunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data)
{
    PutU32(out, static_cast<std::uint32_t>(data.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutU32(out, Crc32(out.data() + crcStart, out.size() - crcStart));
}

}  // namespace

std::vector<std::uint16_t> QuantizeHeights(const std::vector<float>& samples)
{
    std::vector<std::uint16_t> out(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        out[i] = static_cast<std::uint16_t>(std::lround(std::clamp(samples[i], 0.0f, 1.0f) * 65535.0f));
    }
    return out;
}

namespace {

// Steps 2-4: everything that does not depend on the pixel format. `raw` is the picture ALREADY in PNG's row format
// (every row: a filter byte, then the pixels); `bitDepth` and `colourType` go into the header (16 / 0 = 16-bit grey,
// 8 / 6 = 8-bit RGBA).
bool WritePng(const std::string& path, int width, int height, int bitDepth, int colourType,
              const std::vector<std::uint8_t>& raw, std::string& error)
{
    // 2. Wrap it as a zlib stream made of "stored" (uncompressed) deflate blocks of up to 65535 bytes each:
    //    2 header bytes, then per block [1 byte: 1 if last | LEN (2 bytes, little-endian) | NLEN = ~LEN | the bytes],
    //    then the Adler-32 of the raw data, big-endian.
    std::vector<std::uint8_t> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    zlib.push_back(0x78);   // deflate, 32 KB window
    zlib.push_back(0x01);   // no preset dictionary, fastest "level"; chosen so the 16-bit header value is a multiple of 31
    for (size_t offset = 0; offset < raw.size();) {
        const size_t length = (std::min)(static_cast<size_t>(65535), raw.size() - offset);
        const bool last = offset + length == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~length & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }
    PutU32(zlib, Adler32(raw));

    // 3. The file: 8-byte signature, IHDR (size and format), IDAT (the zlib stream), IEND.
    std::vector<std::uint8_t> file = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<std::uint8_t> header;
    PutU32(header, static_cast<std::uint32_t>(width));
    PutU32(header, static_cast<std::uint32_t>(height));
    header.push_back(static_cast<std::uint8_t>(bitDepth));
    header.push_back(static_cast<std::uint8_t>(colourType));   // 0 = greyscale, 6 = RGBA
    header.push_back(0);    // compression method (always 0)
    header.push_back(0);    // filter method (always 0)
    header.push_back(0);    // no interlacing
    PutChunk(file, "IHDR", header);
    PutChunk(file, "IDAT", zlib);
    PutChunk(file, "IEND", {});

    // 4. Write beside the target, then move into place.
    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path temporary = target.string() + ".tmp";
    std::error_code fsError;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), fsError);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write to " + temporary.string(); return false; }
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!out) { error = "writing failed (disk full?): " + temporary.string(); return false; }
    }
    fs::rename(temporary, target, fsError);
    if (fsError) {   // some platforms refuse to rename over an existing file
        fs::remove(target, fsError);
        fs::rename(temporary, target, fsError);
        if (fsError) { error = "could not replace " + target.string() + ": " + fsError.message(); return false; }
    }
    return true;
}

}  // namespace

bool WriteGrayscale16Png(const std::string& path, int width, int height, const std::vector<std::uint16_t>& values,
                         std::string& error)
{
    if (width < 1 || height < 1 || values.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        error = "the image size does not match the number of values";
        return false;
    }

    // 1. The raw picture: every row starts with a "filter type" byte (0 = none), then the pixels, 2 bytes each, big-endian.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 2));
    for (int row = 0; row < height; ++row) {
        raw.push_back(0);
        for (int column = 0; column < width; ++column) {
            const std::uint16_t v = values[static_cast<size_t>(row) * width + column];
            raw.push_back(static_cast<std::uint8_t>(v >> 8));
            raw.push_back(static_cast<std::uint8_t>(v & 0xFF));
        }
    }
    return WritePng(path, width, height, 16, 0, raw, error);
}

bool WriteRgba8Png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba, std::string& error)
{
    if (width < 1 || height < 1 || rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        error = "the image size does not match the number of bytes";
        return false;
    }
    // The same raw layout with 4 one-byte channels per pixel (red, green, blue, alpha).
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 4));
    for (int row = 0; row < height; ++row) {
        raw.push_back(0);
        const std::uint8_t* first = rgba.data() + static_cast<size_t>(row) * width * 4;
        raw.insert(raw.end(), first, first + static_cast<size_t>(width) * 4);
    }
    return WritePng(path, width, height, 8, 6, raw, error);
}
