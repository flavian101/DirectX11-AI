#include "TerrainSplat.h"
#include <algorithm>
#include <cmath>
#include <vector>

// std::thread is missing from MinGW builds that use the "win32" thread model (libstdc++ leaves _GLIBCXX_HAS_GTHREADS
// undefined there). Without it a full bake simply runs on one thread: same result, a little slower.
#if defined(_MSC_VER) || defined(_LIBCPP_VERSION) || defined(_GLIBCXX_HAS_GTHREADS)
#include <thread>
#define TERRAIN_SPLAT_THREADS 1
#endif

namespace {
float Saturate(float v) { return (std::min)((std::max)(v, 0.0f), 1.0f); }

// HLSL's smoothstep, so the baked rules match the old shader's exactly.
float SmoothStep(float edge0, float edge1, float x)
{
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float HashToUnit(int x, int z, int seed)
{
    std::uint32_t v = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(z) * 668265263u +
                      static_cast<std::uint32_t>(seed) * 1442695041u;
    v = (v ^ (v >> 13u)) * 1274126177u;
    v ^= v >> 16u;
    return static_cast<float>(v & 0x00ffffffu) / 16777215.0f;   // 0..1
}

// Smooth value noise in 0..1 (hashed grid, S-curve between the corners).
float ValueNoise(float x, float z, int seed)
{
    const float fx = std::floor(x), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = HashToUnit(ix, iz, seed), b = HashToUnit(ix + 1, iz, seed);
    const float c = HashToUnit(ix, iz + 1, seed), d = HashToUnit(ix + 1, iz + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

// Two octaves, centred on 0 (-0.5 .. 0.5). The old shader afforded one octave per pixel; baked, a second one is free
// and makes the borders between layers wander like real ones instead of like one smooth wobble.
float Breakup(float x, float z, float scale, int seed)
{
    const float coarse = ValueNoise(x * scale, z * scale, seed);
    const float fine = ValueNoise(x * scale * 2.3f + 17.0f, z * scale * 2.3f - 31.0f, seed + 1);
    return (coarse + 0.5f * fine) / 1.5f - 0.5f;
}

// Four weights that add up to 1 -> four bytes that add up to EXACTLY 255 (the largest channel absorbs the rounding).
void Quantise(const float w[kSplatChannels], std::uint8_t* out)
{
    int q[kSplatChannels];
    int sum = 0, best = 0;
    for (int i = 0; i < kSplatChannels; ++i) {
        q[i] = static_cast<int>(Saturate(w[i]) * 255.0f + 0.5f);
        sum += q[i];
        if (w[i] > w[best]) best = i;
    }
    q[best] = (std::min)((std::max)(q[best] + 255 - sum, 0), 255);
    for (int i = 0; i < kSplatChannels; ++i) out[i] = static_cast<std::uint8_t>(q[i]);
}

// The weights of lesson 12: the old shader stacked lerps (ground -> sand -> rock -> snow); multiplied out, that is one
// weight per layer, and the four always add up to 1.
void Weights(float height, float slope, const SplatRules& r, float out[kSplatChannels])
{
    const float rock = SmoothStep(r.slopeStart, r.slopeStart + r.slopeFade, slope);
    const float snow = SmoothStep(r.snowHeight, r.snowHeight + r.snowFade, height) * (1.0f - rock);
    const float shore = (1.0f - SmoothStep(r.shoreHeight, r.shoreHeight + r.shoreWidth, height)) * (1.0f - rock * 0.7f);
    out[0] = (1.0f - shore) * (1.0f - rock) * (1.0f - snow);   // grass
    out[1] = rock * (1.0f - snow);                              // rock
    out[2] = shore * (1.0f - rock) * (1.0f - snow);             // sand
    out[3] = snow;                                              // snow
}
}

void QuantiseSplat(const float weights[kSplatChannels], std::uint8_t* out)
{
    Quantise(weights, out);
}

GridRect SplatCellsForSamples(const GridRect& editedSamples, int cellsX, int cellsZ)
{
    GridRect cells;
    if (editedSamples.IsEmpty() || cellsX < 1 || cellsZ < 1) return cells;
    cells.x0 = (std::max)(0, editedSamples.x0 - 2);
    cells.z0 = (std::max)(0, editedSamples.z0 - 2);
    cells.x1 = (std::min)(cellsX - 1, editedSamples.x1 + 1);
    cells.z1 = (std::min)(cellsZ - 1, editedSamples.z1 + 1);
    return cells;
}

void BakeSplat(const Heightfield& field, const SplatRules& rules, const SplatPlacement& placement, const GridRect& cells,
               SplatMap& map)
{
    if (field.IsEmpty()) return;
    const HeightfieldLayout& layout = field.GetLayout();
    if (map.width != layout.cellsX || map.height != layout.cellsZ || map.rgba.size() != static_cast<size_t>(map.width) * map.height * kSplatChannels) return;

    const int x0 = (std::max)(cells.x0, 0), z0 = (std::max)(cells.z0, 0);
    const int x1 = (std::min)(cells.x1, layout.cellsX - 1), z1 = (std::min)(cells.z1, layout.cellsZ - 1);
    if (x1 < x0 || z1 < z0) return;

    const float cellWidth = layout.sizeX / static_cast<float>(layout.cellsX);
    const float cellDepth = layout.sizeZ / static_cast<float>(layout.cellsZ);
    const float invScaleX = 1.0f / placement.scaleX, invScaleY = 1.0f / placement.scaleY, invScaleZ = 1.0f / placement.scaleZ;
    const int rowCount = x1 - x0 + 2;   // the corner vertices of the block: one more than the cells

    // Bakes the rows zFirst..zLast. Rows are independent (each reads the field and writes its own texels), so a big block
    // can be cut into strips and baked on several threads.
    auto bakeRows = [&](int zFirst, int zLast) {
        // The normals of two rows of vertices at a time (the top and bottom edge of the cells being baked): each vertex
        // normal is worked out once per row instead of four times.
        std::vector<HfVec3> top(rowCount), bottom(rowCount);
        auto fillRow = [&](int z, std::vector<HfVec3>& row) {
            for (int i = 0; i < rowCount; ++i) row[i] = field.VertexNormal(x0 + i, z);
        };
        fillRow(zFirst, top);

        for (int z = zFirst; z <= zLast; ++z) {
            fillRow(z + 1, bottom);
            for (int x = x0; x <= x1; ++x) {
                const HfVec3& a = top[x - x0];
                const HfVec3& b = top[x - x0 + 1];
                const HfVec3& c = bottom[x - x0];
                const HfVec3& d = bottom[x - x0 + 1];

                // The mesh draws a surface between four vertex normals; the middle of the cell sees their average. Then
                // the terrain's transform: a normal divides by the scale (it is not a direction along the surface).
                const float nx = (a.x + b.x + c.x + d.x) * 0.25f * invScaleX;
                const float ny = (a.y + b.y + c.y + d.y) * 0.25f * invScaleY;
                const float nz = (a.z + b.z + c.z + d.z) * 0.25f * invScaleZ;
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                const float slope = 1.0f - (length > 1e-8f ? ny / length : 1.0f);

                const float localHeight = (field.Sample(x, z) + field.Sample(x + 1, z) + field.Sample(x, z + 1) + field.Sample(x + 1, z + 1)) *
                                          0.25f * layout.heightScale;
                const float localX = (static_cast<float>(x) + 0.5f) * cellWidth - layout.sizeX * 0.5f;
                const float localZ = (static_cast<float>(z) + 0.5f) * cellDepth - layout.sizeZ * 0.5f;
                // The noise is read in the terrain's LOCAL space, so moving the terrain sideways no longer drags the
                // layer borders across it (the old shader read world X/Z).
                const float noise = Breakup(localX, localZ, rules.breakupScale, rules.seed);
                const float height = placement.originY + localHeight * placement.scaleY + noise * rules.breakupAmount * 8.0f;
                const float noisySlope = slope + noise * rules.breakupAmount * 0.4f;

                float w[kSplatChannels];
                Weights(height, noisySlope, rules, w);
                Quantise(w, &map.rgba[(static_cast<size_t>(z) * map.width + x) * kSplatChannels]);
            }
            top.swap(bottom);
        }
    };

    // A brush stroke touches a few thousand texels: not worth a thread. A full bake (a rule slider, a new terrain) touches
    // up to a million: cut it into strips of rows, one per core.
    const int rows = z1 - z0 + 1;
    const long long texels = static_cast<long long>(rows) * (x1 - x0 + 1);
    int workers = 1;
#ifdef TERRAIN_SPLAT_THREADS
    if (texels >= 65536) workers = (std::min)(static_cast<int>(std::thread::hardware_concurrency()), (std::max)(rows / 16, 1));
    if (workers > 1) {
        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(workers) - 1);
        for (int w = 1; w < workers; ++w) {
            threads.emplace_back(bakeRows, z0 + rows * w / workers, z0 + rows * (w + 1) / workers - 1);
        }
        bakeRows(z0, z0 + rows / workers - 1);   // this thread takes the first strip
        for (std::thread& t : threads) t.join();
        return;
    }
#else
    (void)texels;
    (void)workers;
#endif
    bakeRows(z0, z1);
}

void BakeSplatAll(const Heightfield& field, const SplatRules& rules, const SplatPlacement& placement, SplatMap& map)
{
    if (field.IsEmpty()) return;
    const HeightfieldLayout& layout = field.GetLayout();
    if (map.width != layout.cellsX || map.height != layout.cellsZ) map.Resize(layout.cellsX, layout.cellsZ);
    GridRect everything;
    everything.Include(0, 0);
    everything.Include(layout.cellsX - 1, layout.cellsZ - 1);
    BakeSplat(field, rules, placement, everything, map);
}
