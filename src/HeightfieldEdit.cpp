#include "HeightfieldEdit.h"
#include <algorithm>
#include <cmath>

void GridRect::Include(int x, int z)
{
    if (IsEmpty()) { x0 = x1 = x; z0 = z1 = z; return; }
    x0 = (std::min)(x0, x); x1 = (std::max)(x1, x);
    z0 = (std::min)(z0, z); z1 = (std::max)(z1, z);
}

GridRect GridRect::Grown(int by, int maxX, int maxZ) const
{
    if (IsEmpty()) return *this;
    GridRect grown;
    grown.x0 = (std::max)(0, x0 - by);
    grown.z0 = (std::max)(0, z0 - by);
    grown.x1 = (std::min)(maxX, x1 + by);
    grown.z1 = (std::min)(maxZ, z1 + by);
    return grown;
}

// The inner part of the brush (1 - falloff of the radius) acts at full strength; from there to the edge the weight
// eases down along a smoothstep curve, which is flat at both ends so the brush has no visible rim.
float BrushWeight(float distance, float radius, float falloff)
{
    if (!(radius > 0.0f) || distance >= radius) return 0.0f;
    const float t = distance / radius;
    const float fullStrengthUntil = 1.0f - std::clamp(falloff, 0.0f, 1.0f);
    if (t <= fullStrengthUntil) return 1.0f;
    const float s = (t - fullStrengthUntil) / (1.0f - fullStrengthUntil);   // 0..1 across the soft band
    return 1.0f - s * s * (3.0f - 2.0f * s);
}

namespace {
constexpr float kSameHeight = 1e-6f;
}

void StrokeRecorder::Remember(int index, float valueBeforeTheStroke)
{
    before_.emplace(index, valueBeforeTheStroke);   // emplace keeps the FIRST value if the sample is already there
}

HeightfieldPatch StrokeRecorder::Finish(const Heightfield& field) const
{
    return Finish(field.Samples());
}

HeightfieldPatch StrokeRecorder::Finish(const std::vector<float>& samples) const
{
    std::vector<int> touched;
    touched.reserve(before_.size());
    for (const auto& entry : before_) touched.push_back(entry.first);
    std::sort(touched.begin(), touched.end());   // a fixed order, so the same stroke always gives the same patch

    HeightfieldPatch patch;
    for (const int index : touched) {
        if (index < 0 || static_cast<size_t>(index) >= samples.size()) continue;
        const float before = before_.at(index);
        const float after = samples[static_cast<size_t>(index)];
        // Went somewhere and came back: nothing to undo. "Came back" allows for float rounding (0.5 + x - x is not always
        // exactly 0.5); 1e-6 is far below the 1/65535 step a saved 16-bit heightmap can even store.
        if (std::fabs(after - before) <= kSameHeight) continue;
        patch.index.push_back(index);
        patch.before.push_back(before);
        patch.after.push_back(after);
    }
    return patch;
}

GridRect ApplyBrush(Heightfield& field, const BrushSettings& brush, float localX, float localZ, float seconds,
                    StrokeRecorder* recorder)
{
    GridRect changed;
    if (field.IsEmpty() || !(brush.radius > 0.0f) || !(seconds > 0.0f)) return changed;
    const HeightfieldLayout& layout = field.GetLayout();
    const float cellWidth = layout.sizeX / static_cast<float>(layout.cellsX);
    const float cellDepth = layout.sizeZ / static_cast<float>(layout.cellsZ);

    // Where the brush is, in grid units, and the block of samples its circle can reach.
    const float centerX = (localX + layout.sizeX * 0.5f) / cellWidth;
    const float centerZ = (localZ + layout.sizeZ * 0.5f) / cellDepth;
    const int x0 = (std::max)(0, static_cast<int>(std::floor(centerX - brush.radius / cellWidth)));
    const int x1 = (std::min)(layout.cellsX, static_cast<int>(std::ceil(centerX + brush.radius / cellWidth)));
    const int z0 = (std::max)(0, static_cast<int>(std::floor(centerZ - brush.radius / cellDepth)));
    const int z1 = (std::min)(layout.cellsZ, static_cast<int>(std::ceil(centerZ + brush.radius / cellDepth)));
    if (x1 < x0 || z1 < z0) return changed;

    const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
    const float lift = strength * kRaiseRatePerSecond * seconds;
    const float blend = strength * kBlendRatePerSecond * seconds;

    // TWO PASSES. First work out every new value from the OLD ones, then write them. Smoothing reads a sample's
    // neighbours; if it wrote as it went, the result would depend on which direction the loop runs.
    struct Pending { int index; float value; };
    std::vector<Pending> pending;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = (static_cast<float>(x) - centerX) * cellWidth;   // distance in LOCAL units, so the brush
            const float dz = (static_cast<float>(z) - centerZ) * cellDepth;   // is a circle even on a stretched grid
            const float weight = BrushWeight(std::sqrt(dx * dx + dz * dz), brush.radius, brush.falloff);
            if (weight <= 0.0f) continue;

            const float old = field.Sample(x, z);
            float value = old;
            switch (brush.mode) {
            case BrushMode::Raise: value = old + weight * lift; break;
            case BrushMode::Lower: value = old - weight * lift; break;
            case BrushMode::Flatten: value = old + (brush.targetHeight - old) * (std::min)(1.0f, weight * blend); break;
            case BrushMode::Smooth: {
                float sum = 0.0f;
                for (int nz = -1; nz <= 1; ++nz)
                    for (int nx = -1; nx <= 1; ++nx) sum += field.Sample(x + nx, z + nz);   // clamped at the edges
                value = old + (sum / 9.0f - old) * (std::min)(1.0f, weight * blend);
                break;
            }
            }
            value = std::clamp(value, 0.0f, 1.0f);
            if (value != old) pending.push_back({ field.IndexOf(x, z), value });
        }
    }

    const int stride = layout.cellsX + 1;
    for (const Pending& change : pending) {
        if (recorder) recorder->Remember(change.index, field.Samples()[static_cast<size_t>(change.index)]);
        field.SetSample(change.index, change.value);
        changed.Include(change.index % stride, change.index / stride);
    }
    return changed;
}

GridRect ApplyPatch(Heightfield& field, const HeightfieldPatch& patch, bool useAfter)
{
    GridRect changed;
    if (field.IsEmpty()) return changed;
    const int stride = field.GetLayout().cellsX + 1;
    const std::vector<float>& values = useAfter ? patch.after : patch.before;
    for (size_t i = 0; i < patch.index.size() && i < values.size(); ++i) {
        const int index = patch.index[i];
        if (index < 0 || static_cast<size_t>(index) >= field.Samples().size()) continue;
        field.SetSample(index, values[i]);
        changed.Include(index % stride, index / stride);
    }
    return changed;
}
