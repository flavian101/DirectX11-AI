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

double PictureMetresPerPixel(double widthMetres, int imageWidth)
{
    return imageWidth > 1 ? widthMetres / static_cast<double>(imageWidth - 1) : widthMetres;
}

double PicturePixelsPerCell(double metresPerCell, double widthMetres, int imageWidth)
{
    const double perPixel = PictureMetresPerPixel(widthMetres, imageWidth);
    return perPixel > 0.0 ? metresPerCell / perPixel : 1.0;
}

const HeightImage& HeightPyramid::Level(int level) const
{
    if (level <= 0 || coarser.empty()) return *base;
    return coarser[static_cast<size_t>((std::min)(level, static_cast<int>(coarser.size())) - 1)];
}

HeightPyramid BuildHeightPyramid(std::shared_ptr<const HeightImage> base)
{
    HeightPyramid pyramid;
    pyramid.base = std::move(base);
    if (!pyramid.base || pyramid.base->width < 2 || pyramid.base->height < 2) return pyramid;
    // Each level is a low-pass of the one before. The corners stay on the corners (pixel 0 and the last pixel sit on the picture's two
    // edges, like the picture itself), so a level has about half the pixels and covers exactly the same area. A pixel of the next level
    // averages four bilinear reads of this level, half a pixel to each side: a window two pixels wide.
    constexpr size_t kMaxLevels = 14;
    while (pyramid.coarser.size() < kMaxLevels) {
        const HeightImage& previous = pyramid.Level(static_cast<int>(pyramid.coarser.size()));
        if (previous.width <= 4 || previous.height <= 4) break;
        HeightImage next;
        next.width = (std::max)(2, (previous.width + 1) / 2);
        next.height = (std::max)(2, (previous.height + 1) / 2);
        next.values.resize(static_cast<size_t>(next.width) * static_cast<size_t>(next.height));
        const double stepX = static_cast<double>(previous.width - 1) / static_cast<double>(next.width - 1);
        const double stepZ = static_cast<double>(previous.height - 1) / static_cast<double>(next.height - 1);
        for (int z = 0; z < next.height; ++z) {
            for (int x = 0; x < next.width; ++x) {
                const double px = x * stepX, pz = z * stepZ;   // this pixel's place in the previous level, in its pixels
                float sum = 0.0f;
                for (int dz = -1; dz <= 1; dz += 2) {
                    for (int dx = -1; dx <= 1; dx += 2) {
                        const float u = static_cast<float>((px + 0.5 * dx) / static_cast<double>(previous.width - 1));
                        const float v = static_cast<float>((pz + 0.5 * dz) / static_cast<double>(previous.height - 1));
                        sum += SampleHeightImage(previous, u, v);   // clamps at the edges
                    }
                }
                next.values[static_cast<size_t>(z) * next.width + x] = sum * 0.25f;
            }
        }
        pyramid.coarser.push_back(std::move(next));
    }
    return pyramid;
}

float PyramidLevelForFootprint(double pixels)
{
    return pixels > 1.0 ? static_cast<float>(std::log2(pixels)) : 0.0f;
}

float SampleHeightPyramid(const HeightPyramid& pyramid, float u, float v, float level, bool bicubic)
{
    if (!pyramid.base) return 0.0f;
    const float last = static_cast<float>(pyramid.LevelCount() - 1);
    level = std::clamp(level, 0.0f, last);
    const int lower = static_cast<int>(level);
    const int upper = (std::min)(lower + 1, pyramid.LevelCount() - 1);
    auto read = [&](int index) {
        const HeightImage& image = pyramid.Level(index);
        return bicubic ? SampleHeightImageBicubic(image, u, v) : SampleHeightImage(image, u, v);
    };
    const float a = read(lower);
    const float blend = level - static_cast<float>(lower);
    if (blend <= 0.0f || upper == lower) return a;
    return a + (read(upper) - a) * blend;
}

PictureHeightSource::PictureHeightSource(std::shared_ptr<const HeightPyramid> pyramid, double filterMetres, double offsetX, double offsetZ, double sizeX, double sizeZ, bool bicubic)
    : image_(pyramid ? pyramid->base : nullptr), pyramid_(std::move(pyramid)), offsetX_(offsetX), offsetZ_(offsetZ), sizeX_((std::max)(sizeX, 1e-6)), sizeZ_((std::max)(sizeZ, 1e-6)), bicubic_(bicubic)
{
    if (image_ && pyramid_) {
        const double pixels = filterMetres / PictureMetresPerPixel(sizeX_, image_->width);
        level_ = (std::min)(PyramidLevelForFootprint(pixels), static_cast<float>(pyramid_->LevelCount() - 1));
    }
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
    if (pyramid_ && level_ > 0.0f) return std::clamp(SampleHeightPyramid(*pyramid_, u, v, level_, bicubic_), 0.0f, 1.0f);
    return std::clamp(bicubic_ ? SampleHeightImageBicubic(*image_, u, v) : SampleHeightImage(*image_, u, v), 0.0f, 1.0f);
}

double PictureDepth(double width, int imageWidth, int imageHeight)
{
    if (imageWidth < 1) return width;
    return width * static_cast<double>(imageHeight) / static_cast<double>(imageWidth);
}

