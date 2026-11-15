#include "RiverCarve.h"
#include <algorithm>
#include <cmath>
#include <limits>

float CarvedHeight(float d, float halfWidth, float ground, const RiverCarveSettings& settings)
{
    halfWidth = std::max(halfWidth, 1e-3f);
    float target;
    if (d <= halfWidth) {
        const float t = d / halfWidth;
        target = settings.level - settings.depth * (1.0f - t * t);
    } else {
        const float bank = std::max(settings.bankWidth, 1e-3f);
        const float u = std::clamp((d - halfWidth) / bank, 0.0f, 1.0f);
        const float ease = u * u * (3.0f - 2.0f * u);
        target = settings.level + settings.bankHeight * ease;
    }
    return std::min(ground, target);
}

GridRect CarveRiver(Heightfield& field, const std::vector<RiverPath::Sample>& path, const RiverCarveSettings& settings,
                    StrokeRecorder* recorder)
{
    GridRect changed;
    if (field.IsEmpty() || path.size() < 2) return changed;
    const HeightfieldLayout& layout = field.GetLayout();
    const float stepX = layout.sizeX / static_cast<float>(layout.cellsX);
    const float stepZ = layout.sizeZ / static_cast<float>(layout.cellsZ);
    const float originX = -layout.sizeX * 0.5f, originZ = -layout.sizeZ * 0.5f;
    const float bank = std::max(settings.bankWidth, 1e-3f);

    // The samples the river can touch: one rectangle around the whole path, then per piece of the path a smaller one.
    // (Asking every sample about every piece would be a few hundred million questions for a long river.)
    auto gridRange = [&](float minV, float maxV, float origin, float step, int cells, int& lo, int& hi) {
        lo = std::clamp(static_cast<int>(std::floor((minV - origin) / step)), 0, cells);
        hi = std::clamp(static_cast<int>(std::ceil((maxV - origin) / step)), 0, cells);
    };
    int rx0, rx1, rz0, rz1;
    {
        float minX = std::numeric_limits<float>::max(), maxX = -minX, minZ = minX, maxZ = -minX, reach = 0.0f;
        for (const RiverPath::Sample& s : path) {
            minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
            minZ = std::min(minZ, s.z); maxZ = std::max(maxZ, s.z);
            reach = std::max(reach, s.halfWidth + bank);
        }
        gridRange(minX - reach, maxX + reach, originX, stepX, layout.cellsX, rx0, rx1);
        gridRange(minZ - reach, maxZ + reach, originZ, stepZ, layout.cellsZ, rz0, rz1);
    }
    const int rw = rx1 - rx0 + 1, rh = rz1 - rz0 + 1;
    if (rw <= 0 || rh <= 0) return changed;
    struct Best { float d2 = std::numeric_limits<float>::max(); float halfWidth = 0.0f; };
    std::vector<Best> best(static_cast<size_t>(rw) * static_cast<size_t>(rh));

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const RiverPath::Sample& a = path[i];
        const RiverPath::Sample& b = path[i + 1];
        const float reach = std::max(a.halfWidth, b.halfWidth) + bank;
        int x0, x1, z0, z1;
        gridRange(std::min(a.x, b.x) - reach, std::max(a.x, b.x) + reach, originX, stepX, layout.cellsX, x0, x1);
        gridRange(std::min(a.z, b.z) - reach, std::max(a.z, b.z) + reach, originZ, stepZ, layout.cellsZ, z0, z1);
        x0 = std::max(x0, rx0); x1 = std::min(x1, rx1); z0 = std::max(z0, rz0); z1 = std::min(z1, rz1);
        const float ex = b.x - a.x, ez = b.z - a.z;
        const float length2 = ex * ex + ez * ez;
        for (int gz = z0; gz <= z1; ++gz) {
            for (int gx = x0; gx <= x1; ++gx) {
                const float px = originX + static_cast<float>(gx) * stepX, pz = originZ + static_cast<float>(gz) * stepZ;
                const float t = length2 > 1e-12f ? std::clamp(((px - a.x) * ex + (pz - a.z) * ez) / length2, 0.0f, 1.0f) : 0.0f;
                const float dx = px - (a.x + ex * t), dz = pz - (a.z + ez * t);
                const float d2 = dx * dx + dz * dz;
                Best& cell = best[static_cast<size_t>(gz - rz0) * static_cast<size_t>(rw) + static_cast<size_t>(gx - rx0)];
                if (d2 < cell.d2) { cell.d2 = d2; cell.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * t; }
            }
        }
    }

    for (int gz = rz0; gz <= rz1; ++gz) {
        for (int gx = rx0; gx <= rx1; ++gx) {
            const Best& cell = best[static_cast<size_t>(gz - rz0) * static_cast<size_t>(rw) + static_cast<size_t>(gx - rx0)];
            if (cell.d2 == std::numeric_limits<float>::max()) continue;
            const float d = std::sqrt(cell.d2);
            if (d > cell.halfWidth + bank) continue;
            const int index = field.IndexOf(gx, gz);
            const float before = field.Samples()[static_cast<size_t>(index)];
            const float after = CarvedHeight(d, cell.halfWidth, before, settings);
            if (after >= before - 1e-6f) continue;
            if (recorder) recorder->Remember(index, before);
            field.SetSample(index, after);
            changed.Include(gx, gz);
        }
    }
    return changed;
}
