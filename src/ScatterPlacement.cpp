#include "ScatterPlacement.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;

float Saturate(float v) { return (std::min)((std::max)(v, 0.0f), 1.0f); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float SmoothStep(float e0, float e1, float x)
{
    const float t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// ---- Random numbers from indices, not from a running generator --------------------------------------------------------
// "Hash(i, j, seed, salt)" is a fixed, well-mixed number for that exact input. Nothing is remembered between calls, so any
// candidate can be evaluated at any time, in any order, on any thread, and always gives the same answer.
std::uint32_t Mix(std::uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
std::uint32_t Hash(int i, int j, std::uint32_t seed, std::uint32_t salt)
{
    return Mix(seed * 0x9e3779b1u ^ Mix(static_cast<std::uint32_t>(i) * 0x85ebca6bu ^ Mix(static_cast<std::uint32_t>(j) * 0xc2b2ae35u ^ salt)));
}
float Unit(std::uint32_t h) { return static_cast<float>(h >> 8) / 16777216.0f; }   // 0 <= v < 1

// Smooth value noise, 0..1: random values on a lattice, blended with a smooth curve.
float ValueNoise(float x, float z, std::uint32_t seed)
{
    const float fx = std::floor(x), fz = std::floor(z);
    const int xi = static_cast<int>(fx), zi = static_cast<int>(fz);
    const float tx = x - fx, tz = z - fz;
    const float sx = tx * tx * (3.0f - 2.0f * tx), sz = tz * tz * (3.0f - 2.0f * tz);
    const float a = Unit(Hash(xi, zi, seed, 101u)), b = Unit(Hash(xi + 1, zi, seed, 101u));
    const float c = Unit(Hash(xi, zi + 1, seed, 101u)), d = Unit(Hash(xi + 1, zi + 1, seed, 101u));
    return Lerp(Lerp(a, b, sx), Lerp(c, d, sx), sz);
}

// ---- A tiny quaternion, just enough to turn "yaw, then lean towards the ground normal" into four numbers ----
struct Quat { float x, y, z, w; };
Quat Multiply(const Quat& a, const Quat& b)   // the rotation "b first, then a"
{
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
Quat Normalized(const Quat& q)
{
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length < 1e-8f) return { 0.0f, 0.0f, 0.0f, 1.0f };
    const float inverse = 1.0f / length;
    return { q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse };
}
// The shortest rotation that turns "up" (0,1,0) into the unit vector t. (Only valid for t pointing somewhat upwards,
// which a ground normal always does here.)
Quat TurnUpInto(float tx, float ty, float tz)
{
    // axis = cross(up, t) = (tz, 0, -tx); the half-angle trick: w = 1 + dot(up, t), then normalise.
    return Normalized({ tz, 0.0f, -tx, 1.0f + ty });
}

// ---- Reading the ground -----------------------------------------------------------------------------------------------
void SampleWeights(const SplatMap* splat, const HeightfieldLayout& layout, float localX, float localZ, float weights[kSplatChannels])
{
    if (!splat || splat->IsEmpty() || layout.sizeX <= 0.0f || layout.sizeZ <= 0.0f) {
        weights[0] = 1.0f; weights[1] = weights[2] = weights[3] = 0.0f;
        return;
    }
    // Texel i covers cell i, so its CENTRE is at (i + 0.5) cells: shift by half a texel before taking the bilinear fraction.
    const float u = (localX / layout.sizeX + 0.5f) * static_cast<float>(splat->width) - 0.5f;
    const float v = (localZ / layout.sizeZ + 0.5f) * static_cast<float>(splat->height) - 0.5f;
    const float fu = std::floor(u), fv = std::floor(v);
    const float tx = u - fu, tz = v - fv;
    auto clampX = [&](int x) { return (std::min)((std::max)(x, 0), splat->width - 1); };
    auto clampZ = [&](int z) { return (std::min)((std::max)(z, 0), splat->height - 1); };
    const int x0 = clampX(static_cast<int>(fu)), x1 = clampX(static_cast<int>(fu) + 1);
    const int z0 = clampZ(static_cast<int>(fv)), z1 = clampZ(static_cast<int>(fv) + 1);
    const std::uint8_t* a = &splat->rgba[(static_cast<size_t>(z0) * splat->width + x0) * kSplatChannels];
    const std::uint8_t* b = &splat->rgba[(static_cast<size_t>(z0) * splat->width + x1) * kSplatChannels];
    const std::uint8_t* c = &splat->rgba[(static_cast<size_t>(z1) * splat->width + x0) * kSplatChannels];
    const std::uint8_t* d = &splat->rgba[(static_cast<size_t>(z1) * splat->width + x1) * kSplatChannels];
    for (int k = 0; k < kSplatChannels; ++k) {
        weights[k] = Lerp(Lerp(a[k], b[k], tx), Lerp(c[k], d[k], tx), tz) / 255.0f;
    }
}

// One lattice square's candidate: where it is, how it ranks, and whether the ground wants it.
struct Candidate {
    float x = 0.0f, z = 0.0f;
    std::uint32_t priority = 0;
    bool accepted = false;
    int variant = 0;
    ScatterSample ground;
};

void Evaluate(const ScatterGround& ground, const ScatterRules& rules, const ScatterRect& extent, int i, int j, Candidate& c)
{
    c.x = (static_cast<float>(i) + Unit(Hash(i, j, rules.seed, 1u))) * rules.spacing;
    c.z = (static_cast<float>(j) + Unit(Hash(i, j, rules.seed, 2u))) * rules.spacing;
    c.priority = Hash(i, j, rules.seed, 3u);
    c.accepted = false;
    if (c.x < extent.minX || c.x >= extent.maxX || c.z < extent.minZ || c.z >= extent.maxZ) return;
    if (!SampleScatterGround(ground, c.x, c.z, c.ground)) return;

    if (c.ground.y < rules.minHeight || c.ground.y > rules.maxHeight) return;
    const float slope = std::acos(Saturate(c.ground.normal[1])) * (180.0f / kPi);
    if (slope < rules.minSlope || slope > rules.maxSlope) return;

    // The layers' say: a weighted vote of the four terrain layers under this point.
    float chance = 0.0f;
    for (int k = 0; k < kSplatChannels; ++k) chance += c.ground.weights[k] * rules.layerDensity[k];
    chance *= rules.density;
    if (rules.clusterStrength > 0.0f) {
        const float inv = 1.0f / rules.clusterScale;
        const float noise = 0.65f * ValueNoise(c.x * inv, c.z * inv, rules.seed) +
                            0.35f * ValueNoise(c.x * inv * 2.3f + 17.0f, c.z * inv * 2.3f - 9.0f, rules.seed);
        chance *= Lerp(1.0f, SmoothStep(0.30f, 0.70f, noise), rules.clusterStrength);
    }
    if (Unit(Hash(i, j, rules.seed, 4u)) >= chance) return;
    // Which look? (salt 9: a new stream, so adding variants never changes where anything stands or how it is rotated.)
    c.variant = ChooseScatterVariant(rules, c.ground, slope, Unit(Hash(i, j, rules.seed, 9u)));
    if (c.variant < 0) return;
    c.accepted = true;
}
}  // namespace

int ChooseScatterVariant(const ScatterRules& rules, const ScatterSample& ground, float slopeDegrees, float roll)
{
    const int count = (std::min)((std::max)(rules.variantCount, 1), kMaxScatterVariants);
    float weight[kMaxScatterVariants];
    float total = 0.0f;
    for (int v = 0; v < count; ++v) {
        const ScatterVariantRules& variant = rules.variants[v];
        float w = 0.0f;
        if (slopeDegrees >= variant.minSlope && slopeDegrees <= variant.maxSlope) {
            float layers = 0.0f;   // the same weighted vote of the terrain layers that the species' density uses
            for (int k = 0; k < kSplatChannels; ++k) layers += ground.weights[k] * variant.layerBias[k];
            w = (std::max)(variant.weight, 0.0f) * layers;
        }
        weight[v] = w;
        total += w;
    }
    if (!(total > 1e-6f)) return -1;
    // Walk the cumulative weights. `last` remembers the final variant that can be chosen, so rounding at the top end of the
    // roll can never land on a variant whose weight is zero.
    float target = Saturate(roll) * total, run = 0.0f;
    int last = -1;
    for (int v = 0; v < count; ++v) {
        if (weight[v] <= 0.0f) continue;
        last = v;
        run += weight[v];
        if (target < run) return v;
    }
    return last;
}

void ClampScatterRules(ScatterRules& r)
{
    r.spacing = (std::min)((std::max)(r.spacing, 0.25f), 500.0f);
    r.minDistance = (std::min)((std::max)(r.minDistance, 0.0f), r.spacing);   // beyond the spacing it could not be enforced (see GenerateScatter)
    r.density = Saturate(r.density);
    for (float& d : r.layerDensity) d = Saturate(d);
    r.minSlope = (std::min)((std::max)(r.minSlope, 0.0f), 90.0f);
    r.maxSlope = (std::min)((std::max)(r.maxSlope, r.minSlope), 90.0f);
    if (r.maxHeight < r.minHeight) std::swap(r.minHeight, r.maxHeight);
    r.clusterStrength = Saturate(r.clusterStrength);
    r.clusterScale = (std::max)(r.clusterScale, 1.0f);
    r.scaleMin = (std::max)(r.scaleMin, 0.01f);
    r.scaleMax = (std::max)(r.scaleMax, r.scaleMin);
    r.alignToNormal = Saturate(r.alignToNormal);
    r.tilt = (std::min)((std::max)(r.tilt, 0.0f), 45.0f);
    r.sink = Saturate(r.sink);
    r.modelHeight = (std::max)(r.modelHeight, 0.0f);
    r.variantCount = (std::min)((std::max)(r.variantCount, 1), kMaxScatterVariants);
    for (int v = 0; v < kMaxScatterVariants; ++v) {
        ScatterVariantRules& variant = r.variants[v];
        variant.weight = (std::min)((std::max)(variant.weight, 0.0f), 1000.0f);
        for (float& bias : variant.layerBias) bias = (std::min)((std::max)(bias, 0.0f), 4.0f);
        variant.minSlope = (std::min)((std::max)(variant.minSlope, 0.0f), 90.0f);
        variant.maxSlope = (std::min)((std::max)(variant.maxSlope, variant.minSlope), 90.0f);
        variant.scaleMul = (std::min)((std::max)(variant.scaleMul, 0.05f), 20.0f);
        if (variant.modelHeight >= 0.0f) variant.modelHeight = (std::min)(variant.modelHeight, 10000.0f);
        else variant.modelHeight = -1.0f;
    }
}

bool ScatterGroundIsValid(const ScatterGround& g)
{
    return g.field && !g.field->IsEmpty() && std::fabs(g.scaleX) > 1e-6f && std::fabs(g.scaleY) > 1e-6f && std::fabs(g.scaleZ) > 1e-6f;
}

ScatterRect ScatterGroundExtent(const ScatterGround& g)
{
    if (!ScatterGroundIsValid(g)) return {};
    const HeightfieldLayout& layout = g.field->GetLayout();
    const float halfX = 0.5f * layout.sizeX * std::fabs(g.scaleX), halfZ = 0.5f * layout.sizeZ * std::fabs(g.scaleZ);
    return { g.posX - halfX, g.posZ - halfZ, g.posX + halfX, g.posZ + halfZ };
}

bool SampleScatterGround(const ScatterGround& g, float x, float z, ScatterSample& out)
{
    if (!ScatterGroundIsValid(g)) return false;
    const float localX = (x - g.posX) / g.scaleX, localZ = (z - g.posZ) / g.scaleZ;
    float localY = 0.0f;
    if (!g.field->HeightAt(localX, localZ, localY)) return false;
    HfVec3 n;
    if (!g.field->NormalAt(localX, localZ, n)) return false;
    out.y = g.posY + localY * g.scaleY;
    // A normal is not a direction along the surface: a stretched terrain divides it by the scale (see Terrain::GetNormalAt).
    float nx = n.x / g.scaleX, ny = n.y / g.scaleY, nz = n.z / g.scaleZ;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length < 1e-8f) { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
    else { nx /= length; ny /= length; nz /= length; }
    if (ny < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }   // a mirrored (negative) scale must not flip the ground upside down
    out.normal[0] = nx; out.normal[1] = ny; out.normal[2] = nz;
    SampleWeights(g.splat, g.field->GetLayout(), localX, localZ, out.weights);
    return true;
}

int GenerateScatter(const ScatterGround& ground, const ScatterRules& requested, const ScatterRect& wanted, std::vector<ScatterInstance>& out,
                    std::vector<std::uint8_t>* variantOut)
{
    if (!ScatterGroundIsValid(ground)) return 0;
    ScatterRules rules = requested;
    ClampScatterRules(rules);

    // Only the part of the area that is on the terrain can hold anything.
    const ScatterRect extent = ScatterGroundExtent(ground);
    const ScatterRect area{ (std::max)(wanted.minX, extent.minX), (std::max)(wanted.minZ, extent.minZ),
                            (std::min)(wanted.maxX, extent.maxX), (std::min)(wanted.maxZ, extent.maxZ) };
    if (area.IsEmpty()) return 0;

    // The lattice squares that overlap the area. A candidate stays inside its own square, so a square outside this block
    // cannot contribute a point to the area.
    const float pitch = rules.spacing;
    const int i0 = static_cast<int>(std::floor(area.minX / pitch)), i1 = static_cast<int>(std::ceil(area.maxX / pitch)) - 1;
    const int j0 = static_cast<int>(std::floor(area.minZ / pitch)), j1 = static_cast<int>(std::ceil(area.maxZ / pitch)) - 1;
    if (i1 < i0 || j1 < j0) return 0;

    // Evaluate those squares AND a ring around them once: the thinning below must look at the neighbours of the squares
    // on the border, and the neighbours belong to the next cell, which would give the same answer for them.
    const int width = i1 - i0 + 3, depth = j1 - j0 + 3;
    std::vector<Candidate> table(static_cast<size_t>(width) * depth);
    for (int tj = 0; tj < depth; ++tj)
        for (int ti = 0; ti < width; ++ti) Evaluate(ground, rules, extent, i0 - 1 + ti, j0 - 1 + tj, table[static_cast<size_t>(tj) * width + ti]);

    // Is another accepted candidate closer than minDistance with a higher rank? Only the 8 neighbouring squares can be:
    // two squares apart, candidates are at least one pitch away, and minDistance never exceeds the pitch.
    const float minDistanceSquared = rules.minDistance * rules.minDistance;
    const int before = static_cast<int>(out.size());
    for (int tj = 1; tj < depth - 1; ++tj) {
        for (int ti = 1; ti < width - 1; ++ti) {
            const Candidate& c = table[static_cast<size_t>(tj) * width + ti];
            if (!c.accepted) continue;
            if (c.x < area.minX || c.x >= area.maxX || c.z < area.minZ || c.z >= area.maxZ) continue;   // belongs to a neighbouring cell

            bool beaten = false;
            if (minDistanceSquared > 0.0f) {
                for (int dj = -1; dj <= 1 && !beaten; ++dj) {
                    for (int di = -1; di <= 1 && !beaten; ++di) {
                        if (di == 0 && dj == 0) continue;
                        const Candidate& other = table[static_cast<size_t>(tj + dj) * width + (ti + di)];
                        if (!other.accepted) continue;
                        const float dx = other.x - c.x, dz = other.z - c.z;
                        if (dx * dx + dz * dz >= minDistanceSquared) continue;
                        // The higher random priority wins. Equal priorities (one in four billion) fall back to the
                        // lattice index, so there is always exactly one winner.
                        const bool otherWins = other.priority > c.priority ||
                            (other.priority == c.priority && (dj > 0 || (dj == 0 && di > 0)));
                        if (otherWins) beaten = true;
                    }
                }
            }
            if (beaten) continue;

            // Survivor: give it a size and a rotation (more hashes of the same square, so also repeatable).
            const int i = i0 - 1 + ti, j = j0 - 1 + tj;
            const ScatterVariantRules& variant = rules.variants[c.variant];
            const float height = variant.modelHeight >= 0.0f ? variant.modelHeight : rules.modelHeight;
            const float scale = Lerp(rules.scaleMin, rules.scaleMax, Unit(Hash(i, j, rules.seed, 5u))) * variant.scaleMul;
            const float yaw = Unit(Hash(i, j, rules.seed, 6u)) * 2.0f * kPi;

            // The direction the plant's "up" points: between straight up and the ground normal, plus a random lean.
            float ux = c.ground.normal[0] * rules.alignToNormal;
            float uy = Lerp(1.0f, c.ground.normal[1], rules.alignToNormal);
            float uz = c.ground.normal[2] * rules.alignToNormal;
            if (rules.tilt > 0.0f) {
                const float lean = std::tan(rules.tilt * (kPi / 180.0f));
                ux += (Unit(Hash(i, j, rules.seed, 7u)) * 2.0f - 1.0f) * lean;
                uz += (Unit(Hash(i, j, rules.seed, 8u)) * 2.0f - 1.0f) * lean;
            }
            const float upLength = std::sqrt(ux * ux + uy * uy + uz * uz);
            ux /= upLength; uy /= upLength; uz /= upLength;

            const Quat yawQ{ 0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
            const Quat q = Multiply(TurnUpInto(ux, uy, uz), yawQ);   // spin around the plant's own axis FIRST, then lean it

            ScatterInstance instance;
            instance.x = c.x;
            instance.z = c.z;
            instance.y = c.ground.y - rules.sink * height * scale;
            instance.scale = scale;
            instance.qx = q.x; instance.qy = q.y; instance.qz = q.z; instance.qw = q.w;
            out.push_back(instance);
            if (variantOut) variantOut->push_back(static_cast<std::uint8_t>(c.variant));
        }
    }
    return static_cast<int>(out.size()) - before;
}

ScatterGrid MakeScatterGrid(const ScatterRect& extent, float cellSize)
{
    ScatterGrid grid;
    grid.cellSize = (std::max)(cellSize, 1.0f);
    grid.originX = extent.minX;
    grid.originZ = extent.minZ;
    if (extent.IsEmpty()) return grid;
    grid.cellsX = (std::max)(1, static_cast<int>(std::ceil((extent.maxX - extent.minX) / grid.cellSize)));
    grid.cellsZ = (std::max)(1, static_cast<int>(std::ceil((extent.maxZ - extent.minZ) / grid.cellSize)));
    return grid;
}

ScatterRect ScatterCellRect(const ScatterGrid& grid, int cx, int cz)
{
    return { grid.originX + cx * grid.cellSize, grid.originZ + cz * grid.cellSize,
             grid.originX + (cx + 1) * grid.cellSize, grid.originZ + (cz + 1) * grid.cellSize };
}

bool ScatterCellsTouching(const ScatterGrid& grid, const ScatterRect& rect, int& cx0, int& cz0, int& cx1, int& cz1)
{
    if (grid.cellsX <= 0 || grid.cellsZ <= 0 || rect.IsEmpty()) return false;
    cx0 = (std::max)(0, static_cast<int>(std::floor((rect.minX - grid.originX) / grid.cellSize)));
    cz0 = (std::max)(0, static_cast<int>(std::floor((rect.minZ - grid.originZ) / grid.cellSize)));
    cx1 = (std::min)(grid.cellsX - 1, static_cast<int>(std::floor((rect.maxX - grid.originX) / grid.cellSize)));
    cz1 = (std::min)(grid.cellsZ - 1, static_cast<int>(std::floor((rect.maxZ - grid.originZ) / grid.cellSize)));
    return cx1 >= cx0 && cz1 >= cz0;
}

void GroupScatterByVariant(std::vector<ScatterInstance>& instances, const std::vector<std::uint8_t>& variants, int variantCount,
                           std::uint32_t offsets[kMaxScatterVariants + 1])
{
    const int count = (std::min)((std::max)(variantCount, 1), kMaxScatterVariants);
    const size_t n = instances.size();
    for (int v = 0; v <= kMaxScatterVariants; ++v) offsets[v] = 0;
    if (variants.size() != n) {   // a caller bug: keep the order, call it all variant 0
        offsets[0] = 0;
        for (int v = 1; v <= count; ++v) offsets[v] = static_cast<std::uint32_t>(n);
        return;
    }
    auto variantOf = [&](size_t i) { return static_cast<int>(variants[i]) < count ? static_cast<int>(variants[i]) : 0; };
    std::uint32_t tally[kMaxScatterVariants] = {};
    for (size_t i = 0; i < n; ++i) ++tally[variantOf(i)];
    std::uint32_t run = 0;
    for (int v = 0; v < count; ++v) { offsets[v] = run; run += tally[v]; }
    offsets[count] = run;
    if (count == 1) return;   // already in order
    std::vector<ScatterInstance> sorted(n);
    std::uint32_t next[kMaxScatterVariants];
    for (int v = 0; v < count; ++v) next[v] = offsets[v];
    for (size_t i = 0; i < n; ++i) sorted[next[variantOf(i)]++] = instances[i];
    instances.swap(sorted);
}

std::uint32_t PlanScatterLayout(const std::vector<ScatterCellCounts>& cells, int variantCount, std::uint32_t cap,
                                std::vector<ScatterCellLayout>& layout)
{
    const int count = (std::min)((std::max)(variantCount, 1), kMaxScatterVariants);
    layout.assign(cells.size(), ScatterCellLayout{});
    std::uint64_t used = 0;
    for (int v = 0; v < count; ++v) {
        for (size_t c = 0; c < cells.size(); ++c) {
            const std::uint64_t room = cap > used ? cap - used : 0;
            const std::uint32_t n = static_cast<std::uint32_t>((std::min<std::uint64_t>)(cells[c].count[v], room));
            layout[c].start[v] = static_cast<std::uint32_t>(used);
            layout[c].count[v] = n;
            used += n;
        }
    }
    return static_cast<std::uint32_t>(used);
}
