#include "WorldPicture.h"
#include "libs/stb_image.h"
#include <algorithm>
#include <cmath>

bool LoadHeightImage(const std::string& path, HeightImage& image, std::string& error)
{
    int width = 0, height = 0, channels = 0;
    std::vector<float> values;
    if (stbi_is_16_bit(path.c_str())) {
        stbi_us* pixels = stbi_load_16(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) { error = "could not read the image: " + path; return false; }
        if (width < 2 || height < 2) { stbi_image_free(pixels); error = "the image is smaller than 2 x 2 pixels: " + path; return false; }
        values.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < values.size(); ++i) values[i] = pixels[i * 4] / 65535.0f;
        stbi_image_free(pixels);
    } else {
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) { error = "could not read the image: " + path; return false; }
        if (width < 2 || height < 2) { stbi_image_free(pixels); error = "the image is smaller than 2 x 2 pixels: " + path; return false; }
        values.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < values.size(); ++i) values[i] = pixels[i * 4] / 255.0f;
        stbi_image_free(pixels);
    }
    image.width = width;
    image.height = height;
    image.values = std::move(values);
    return true;
}

float SampleHeightImage(const HeightImage& image, float u, float v)
{
    if (image.width < 2 || image.height < 2 || image.values.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height)) return 0.0f;
    const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
    const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
    const int x0 = std::min(static_cast<int>(fx), image.width - 1);
    const int z0 = std::min(static_cast<int>(fz), image.height - 1);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int z1 = std::min(z0 + 1, image.height - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    auto at = [&](int x, int z) { return image.values[static_cast<size_t>(z) * image.width + x]; };
    const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
    const float bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
    return top + (bottom - top) * tz;
}

float SampleHeightImageBicubic(const HeightImage& image, float u, float v)
{
    if (image.width < 2 || image.height < 2 || image.values.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height)) return 0.0f;
    const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
    const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
    const int x1 = std::min(static_cast<int>(fx), image.width - 1);
    const int z1 = std::min(static_cast<int>(fz), image.height - 1);
    const float tx = fx - static_cast<float>(x1);
    const float tz = fz - static_cast<float>(z1);
    auto at = [&](int x, int z) { return image.values[static_cast<size_t>(std::clamp(z, 0, image.height - 1)) * image.width + std::clamp(x, 0, image.width - 1)]; };
    // Catmull-Rom through four neighbours: passes exactly through the middle two, so a pixel centre gives the pixel's own value.
    auto curve = [](float a, float b, float c, float d, float t) {
        return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    };
    float rows[4];
    for (int j = -1; j <= 2; ++j) rows[j + 1] = curve(at(x1 - 1, z1 + j), at(x1, z1 + j), at(x1 + 1, z1 + j), at(x1 + 2, z1 + j), tx);
    return std::clamp(curve(rows[0], rows[1], rows[2], rows[3], tz), 0.0f, 1.0f);
}

PictureHeightSource::PictureHeightSource(std::shared_ptr<const HeightImage> image, double offsetX, double offsetZ, double sizeX, double sizeZ, bool bicubic)
    : image_(std::move(image)), offsetX_(offsetX), offsetZ_(offsetZ), sizeX_((std::max)(sizeX, 1e-6)), sizeZ_((std::max)(sizeZ, 1e-6)), bicubic_(bicubic)
{
}

float PictureHeightSource::Height01(double worldX, double worldZ) const
{
    if (!image_) return 0.0f;
    // The subtraction and the division are done in double (a tile far from the origin still gets an exact position); only the
    // fraction across the picture, a number between 0 and 1, becomes a float.
    const float u = static_cast<float>(std::clamp((worldX - offsetX_) / sizeX_, 0.0, 1.0));
    const float v = static_cast<float>(std::clamp((worldZ - offsetZ_) / sizeZ_, 0.0, 1.0));
    return std::clamp(bicubic_ ? SampleHeightImageBicubic(*image_, u, v) : SampleHeightImage(*image_, u, v), 0.0f, 1.0f);
}

double PictureDepth(double width, int imageWidth, int imageHeight)
{
    if (imageWidth < 1) return width;
    return width * static_cast<double>(imageHeight) / static_cast<double>(imageWidth);
}

WorldGridSettings WorldGridForTerrain(float width, int maxCells)
{
    WorldGridSettings grid;
    grid.chunksPerTile = std::clamp(static_cast<int>(std::lround(static_cast<double>(maxCells) / kWorldChunkCells)), WorldGridSettings::kChunksMin, WorldGridSettings::kChunksMax);
    grid.metresPerCell = width / static_cast<float>(grid.TileCells());
    grid.Clamp();
    return grid;
}
