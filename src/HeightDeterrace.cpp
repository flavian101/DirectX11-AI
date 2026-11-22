#include "HeightDeterrace.h"
#include <algorithm>
#include <utility>

std::vector<float> DeterraceLevels(const HeightPicture& picture, int iterations)
{
    const size_t count = picture.levels.size();
    std::vector<float> original(count);
    for (size_t i = 0; i < count; ++i) original[i] = static_cast<float>(picture.levels[i]);
    if (picture.bits != 8 || iterations <= 0 || !picture.IsValid()) return original;

    const int w = picture.width, h = picture.height;
    std::vector<float> current = original, along(count), next(count);
    for (int step = 0; step < iterations; ++step) {
        // A 3 x 3 binomial blur (1 2 1 / 2 4 2 / 1 2 1 over 16), as two passes; the edge repeats.
        for (int z = 0; z < h; ++z) {
            const size_t row = static_cast<size_t>(z) * w;
            for (int x = 0; x < w; ++x) {
                const float left = current[row + static_cast<size_t>((std::max)(x - 1, 0))], right = current[row + static_cast<size_t>((std::min)(x + 1, w - 1))];
                along[row + x] = 0.25f * left + 0.5f * current[row + x] + 0.25f * right;
            }
        }
        for (int z = 0; z < h; ++z) {
            const size_t up = static_cast<size_t>((std::max)(z - 1, 0)) * w, here = static_cast<size_t>(z) * w, down = static_cast<size_t>((std::min)(z + 1, h - 1)) * w;
            for (int x = 0; x < w; ++x) {
                const float blurred = 0.25f * along[up + x] + 0.5f * along[here + x] + 0.25f * along[down + x];
                // Back into the pixel's own bin. The bounds are whole numbers +/- 0.5, exact in float.
                next[here + x] = (std::clamp)(blurred, original[here + x] - 0.5f, original[here + x] + 0.5f);
            }
        }
        current.swap(next);
    }
    return current;
}
