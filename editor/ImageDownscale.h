#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

// Shrinks an RGBA8 image so its longer side is at most `maxSide`, by averaging the block of source pixels that falls on
// each output pixel (a box filter: no shimmer, unlike picking every Nth pixel). Never enlarges. No GPU, no ImGui: it is
// the pure part of the Assets panel's thumbnails, and has a unit test.
inline bool BoxDownscale(const unsigned char* rgba, int width, int height, int maxSide,
                         std::vector<unsigned char>& out, int& outWidth, int& outHeight)
{
    if (!rgba || width <= 0 || height <= 0 || maxSide <= 0) return false;
    const int longSide = (std::max)(width, height);
    if (longSide <= maxSide) {
        outWidth = width;
        outHeight = height;
    } else {
        const double scale = static_cast<double>(maxSide) / static_cast<double>(longSide);
        outWidth = (std::max)(1, static_cast<int>(width * scale + 0.5));
        outHeight = (std::max)(1, static_cast<int>(height * scale + 0.5));
    }
    out.assign(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u, 0);
    for (int oy = 0; oy < outHeight; ++oy) {
        const int y0 = static_cast<int>(static_cast<long long>(oy) * height / outHeight);
        const int y1 = (std::max)(y0 + 1, static_cast<int>(static_cast<long long>(oy + 1) * height / outHeight));
        for (int ox = 0; ox < outWidth; ++ox) {
            const int x0 = static_cast<int>(static_cast<long long>(ox) * width / outWidth);
            const int x1 = (std::max)(x0 + 1, static_cast<int>(static_cast<long long>(ox + 1) * width / outWidth));
            unsigned long long sum[4] = { 0, 0, 0, 0 };
            for (int y = y0; y < y1; ++y) {
                const unsigned char* row = rgba + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x0)) * 4u;
                for (int x = x0; x < x1; ++x, row += 4) {
                    sum[0] += row[0]; sum[1] += row[1]; sum[2] += row[2]; sum[3] += row[3];
                }
            }
            const unsigned long long count = static_cast<unsigned long long>(y1 - y0) * static_cast<unsigned long long>(x1 - x0);
            unsigned char* pixel = out.data() + (static_cast<size_t>(oy) * static_cast<size_t>(outWidth) + static_cast<size_t>(ox)) * 4u;
            for (int channel = 0; channel < 4; ++channel) pixel[channel] = static_cast<unsigned char>((sum[channel] + count / 2) / count);
        }
    }
    return true;
}
