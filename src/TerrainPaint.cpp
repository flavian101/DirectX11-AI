#include "TerrainPaint.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTiny = 1e-4f;   // below this a painted weight is treated as zero (an erased texel must really become unpainted)
}

GridRect ApplyPaintBrush(PaintMap& map, const HeightfieldLayout& layout, const PaintBrush& brush, float localX, float localZ,
                         float seconds, StrokeRecorder* recorder)
{
    GridRect changed;
    if (map.IsEmpty() || !(brush.radius > 0.0f) || !(seconds > 0.0f) || !(layout.sizeX > 0.0f) || !(layout.sizeZ > 0.0f)) return changed;
    if (!brush.erase && (brush.layer < 0 || brush.layer >= kSplatChannels)) return changed;

    const float cellWidth = layout.sizeX / static_cast<float>(map.width);
    const float cellDepth = layout.sizeZ / static_cast<float>(map.height);
    // The brush centre in cell units. Texel i covers cells i .. i+1, so its centre is at i + 0.5.
    const float centerX = (localX + layout.sizeX * 0.5f) / cellWidth;
    const float centerZ = (localZ + layout.sizeZ * 0.5f) / cellDepth;
    const int x0 = (std::max)(0, static_cast<int>(std::floor(centerX - brush.radius / cellWidth - 0.5f)));
    const int x1 = (std::min)(map.width - 1, static_cast<int>(std::ceil(centerX + brush.radius / cellWidth - 0.5f)));
    const int z0 = (std::max)(0, static_cast<int>(std::floor(centerZ - brush.radius / cellDepth - 0.5f)));
    const int z1 = (std::min)(map.height - 1, static_cast<int>(std::ceil(centerZ + brush.radius / cellDepth - 0.5f)));
    if (x1 < x0 || z1 < z0) return changed;

    const float amount = std::clamp(brush.strength, 0.0f, 1.0f) * kPaintRatePerSecond * seconds;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - centerX) * cellWidth;   // LOCAL units: round even on a stretched grid
            const float dz = (static_cast<float>(z) + 0.5f - centerZ) * cellDepth;
            const float weight = BrushWeight(std::sqrt(dx * dx + dz * dz), brush.radius, brush.falloff);
            if (weight <= 0.0f) continue;

            // Painting: move every channel towards (1 for the brush's layer, 0 for the others) by t. Erasing: move all of
            // them towards 0. Either way the four stay >= 0 and add up to at most 1.
            const float t = (std::min)(1.0f, weight * amount);
            const size_t base = map.TexelIndex(x, z) * kSplatChannels;
            bool texelChanged = false;
            for (int channel = 0; channel < kSplatChannels; ++channel) {
                const float old = map.weights[base + channel];
                const float target = (!brush.erase && channel == brush.layer) ? 1.0f : 0.0f;
                float value = old + (target - old) * t;
                if (value < kTiny) value = 0.0f;
                if (value == old) continue;
                if (recorder) recorder->Remember(static_cast<int>(base) + channel, old);
                map.weights[base + channel] = value;
                texelChanged = true;
            }
            if (texelChanged) changed.Include(x, z);
        }
    }
    return changed;
}

GridRect ApplyPaintPatch(PaintMap& map, const HeightfieldPatch& patch, bool useAfter)
{
    GridRect changed;
    if (map.IsEmpty()) return changed;
    const std::vector<float>& values = useAfter ? patch.after : patch.before;
    for (size_t i = 0; i < patch.index.size() && i < values.size(); ++i) {
        const int index = patch.index[i];
        if (index < 0 || static_cast<size_t>(index) >= map.weights.size()) continue;
        map.weights[static_cast<size_t>(index)] = values[i];
        const int texel = index / kSplatChannels;
        changed.Include(texel % map.width, texel / map.width);
    }
    return changed;
}

void ApplyPaint(const PaintMap& paint, const GridRect& texels, SplatMap& splat)
{
    if (paint.IsEmpty() || splat.IsEmpty() || paint.width != splat.width || paint.height != splat.height) return;
    const int x0 = (std::max)(texels.x0, 0), z0 = (std::max)(texels.z0, 0);
    const int x1 = (std::min)(texels.x1, splat.width - 1), z1 = (std::min)(texels.z1, splat.height - 1);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const size_t base = paint.TexelIndex(x, z) * kSplatChannels;
            float coverage = 0.0f;
            for (int channel = 0; channel < kSplatChannels; ++channel) coverage += paint.weights[base + channel];
            if (coverage <= 0.0f) continue;                       // nothing painted here: the baked weights stand
            const float scale = coverage > 1.0f ? 1.0f / coverage : 1.0f;   // belt and braces: coverage never above 1
            coverage = (std::min)(coverage, 1.0f);

            std::uint8_t* texel = &splat.rgba[base];               // the splat has the same texel order as the paint
            float result[kSplatChannels];
            for (int channel = 0; channel < kSplatChannels; ++channel) {
                result[channel] = (texel[channel] / 255.0f) * (1.0f - coverage) + paint.weights[base + channel] * scale;
            }
            QuantiseSplat(result, texel);
        }
    }
}

void ApplyPaintAll(const PaintMap& paint, SplatMap& splat)
{
    if (paint.IsEmpty()) return;
    GridRect everything;
    everything.Include(0, 0);
    everything.Include(paint.width - 1, paint.height - 1);
    ApplyPaint(paint, everything, splat);
}

std::vector<std::uint8_t> QuantisePaint(const PaintMap& paint)
{
    std::vector<std::uint8_t> bytes(paint.weights.size());
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(std::lround(std::clamp(paint.weights[i], 0.0f, 1.0f) * 255.0f));
    }
    return bytes;
}

void PaintFromBytes(const std::uint8_t* rgba, int width, int height, PaintMap& paint)
{
    paint.Resize(width, height);
    for (size_t texel = 0; texel < static_cast<size_t>(width) * height; ++texel) {
        float sum = 0.0f;
        for (int channel = 0; channel < kSplatChannels; ++channel) sum += rgba[texel * kSplatChannels + channel] / 255.0f;
        const float scale = sum > 1.0f ? 1.0f / sum : 1.0f;
        for (int channel = 0; channel < kSplatChannels; ++channel) {
            paint.weights[texel * kSplatChannels + channel] = rgba[texel * kSplatChannels + channel] / 255.0f * scale;
        }
    }
}
