#include "ProceduralModels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265358979f;

std::uint32_t Mix(std::uint32_t h)
{
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
float Hash01(int a, int b, int c)
{
    return static_cast<float>(Mix(static_cast<std::uint32_t>(a) * 73856093u ^ static_cast<std::uint32_t>(b) * 19349663u ^ static_cast<std::uint32_t>(c) * 83492791u) >> 8) / 16777216.0f;
}
// Smooth 3D value noise, 0..1.
float Noise3(float x, float y, float z)
{
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y)), zi = static_cast<int>(std::floor(z));
    const float fx = x - std::floor(x), fy = y - std::floor(y), fz = z - std::floor(z);
    auto fade = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    float c[2][2][2];
    for (int dx = 0; dx < 2; ++dx) for (int dy = 0; dy < 2; ++dy) for (int dz = 0; dz < 2; ++dz) c[dx][dy][dz] = Hash01(xi + dx, yi + dy, zi + dz);
    return lerp(lerp(lerp(c[0][0][0], c[1][0][0], u), lerp(c[0][1][0], c[1][1][0], u), v),
                lerp(lerp(c[0][0][1], c[1][0][1], u), lerp(c[0][1][1], c[1][1][1], u), v), w);
}

// Collects triangles and fixes their winding. The engine is left-handed with CLOCKWISE front faces, and for that
// convention the outward normal of a triangle (a, b, c) is cross(b - a, c - a). Every triangle is given an "outward hint"
// (a rough direction away from the solid) and flipped if it faces the other way, so the generators below never have to get
// the winding right by hand.
struct Builder {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    std::uint32_t Add(float x, float y, float z, float u, float v)
    {
        Vertex vertex;
        vertex.position = XMFLOAT3(x, y, z);
        vertex.texCoord = XMFLOAT2(u, v);
        vertices.push_back(vertex);
        return static_cast<std::uint32_t>(vertices.size() - 1);
    }
    void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, float hx, float hy, float hz)
    {
        const XMFLOAT3 &pa = vertices[a].position, &pb = vertices[b].position, &pc = vertices[c].position;
        const float e1x = pb.x - pa.x, e1y = pb.y - pa.y, e1z = pb.z - pa.z, e2x = pc.x - pa.x, e2y = pc.y - pa.y, e2z = pc.z - pa.z;
        const float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
        if (nx * nx + ny * ny + nz * nz < 1e-14f) return;   // a degenerate sliver: nothing to draw
        if (nx * hx + ny * hy + nz * hz < 0.0f) std::swap(b, c);
        indices.push_back(a); indices.push_back(b); indices.push_back(c);
    }
    // Smooth normals: every vertex gets the area-weighted average of the triangles that share it.
    void Finish()
    {
        std::vector<XMFLOAT3> sum(vertices.size(), XMFLOAT3(0, 0, 0));
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const XMFLOAT3 &pa = vertices[indices[i]].position, &pb = vertices[indices[i + 1]].position, &pc = vertices[indices[i + 2]].position;
            const float e1x = pb.x - pa.x, e1y = pb.y - pa.y, e1z = pb.z - pa.z, e2x = pc.x - pa.x, e2y = pc.y - pa.y, e2z = pc.z - pa.z;
            const XMFLOAT3 n(e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x);   // length = 2 * area
            for (int k = 0; k < 3; ++k) { XMFLOAT3& s = sum[indices[i + k]]; s.x += n.x; s.y += n.y; s.z += n.z; }
        }
        for (size_t i = 0; i < vertices.size(); ++i) {
            const float length = std::sqrt(sum[i].x * sum[i].x + sum[i].y * sum[i].y + sum[i].z * sum[i].z);
            vertices[i].normal = length > 1e-8f ? XMFLOAT3(sum[i].x / length, sum[i].y / length, sum[i].z / length) : XMFLOAT3(0, 1, 0);
        }
        ComputeTangents(vertices, indices);
    }
};

ImportedPart ToPart(const char* name, Builder& builder, int material)
{
    builder.Finish();
    ImportedPart part;
    part.name = name;
    part.vertices = std::move(builder.vertices);
    part.indices = std::move(builder.indices);
    part.material = material;
    return part;
}

ImportedMaterial MakeMaterial(const char* name, float r, float g, float b, float roughness)
{
    ImportedMaterial material;
    material.name = name;
    material.baseColor = XMFLOAT4(r, g, b, 1.0f);   // linear
    material.roughness = roughness;
    material.metallic = 0.0f;
    return material;
}

void Finalize(ImportedModel& model)
{
    bool first = true;
    model.triangleCount = 0;
    for (const ImportedPart& part : model.parts) {
        model.triangleCount += static_cast<std::uint32_t>(part.indices.size() / 3);
        for (const Vertex& v : part.vertices) {
            if (first) { model.boundsMin = model.boundsMax = v.position; first = false; continue; }
            model.boundsMin = XMFLOAT3((std::min)(model.boundsMin.x, v.position.x), (std::min)(model.boundsMin.y, v.position.y), (std::min)(model.boundsMin.z, v.position.z));
            model.boundsMax = XMFLOAT3((std::max)(model.boundsMax.x, v.position.x), (std::max)(model.boundsMax.y, v.position.y), (std::max)(model.boundsMax.z, v.position.z));
        }
    }
}

// ---- A pine: a slightly crooked trunk and four tiers of needles, each a bumpy cone with a lid underneath ------------------
ImportedModel MakePine()
{
    ImportedModel model;
    model.name = "Pine (built-in)";
    model.sourcePath = kBuiltinPine;
    model.materials.push_back(MakeMaterial("Bark", 0.10f, 0.055f, 0.03f, 0.95f));
    model.materials.push_back(MakeMaterial("Needles", 0.012f, 0.06f, 0.02f, 0.85f));

    Builder trunk;
    constexpr int kTrunkSides = 14, kTrunkRings = 8;
    constexpr float kTrunkHeight = 4.2f;
    std::vector<std::uint32_t> ring[kTrunkRings + 1];
    for (int r = 0; r <= kTrunkRings; ++r) {
        const float t = static_cast<float>(r) / kTrunkRings, y = t * kTrunkHeight;
        const float radius = 0.38f * (1.0f - 0.55f * t);
        const float bendX = 0.10f * std::sin(t * 3.0f), bendZ = 0.08f * std::sin(t * 2.3f + 1.0f);   // a little crooked
        for (int s = 0; s < kTrunkSides; ++s) {
            const float a = 2.0f * kPi * s / kTrunkSides;
            const float bump = 1.0f + 0.12f * (Noise3(std::cos(a) * 1.7f, y * 1.3f, std::sin(a) * 1.7f) - 0.5f) * 2.0f;
            ring[r].push_back(trunk.Add(bendX + std::cos(a) * radius * bump, y, bendZ + std::sin(a) * radius * bump, static_cast<float>(s) / kTrunkSides, t * 3.0f));
        }
    }
    for (int r = 0; r < kTrunkRings; ++r)
        for (int s = 0; s < kTrunkSides; ++s) {
            const int n = (s + 1) % kTrunkSides;
            const XMFLOAT3 c = trunk.vertices[ring[r][s]].position;
            trunk.Triangle(ring[r][s], ring[r + 1][s], ring[r + 1][n], c.x, 0.0f, c.z);
            trunk.Triangle(ring[r][s], ring[r + 1][n], ring[r][n], c.x, 0.0f, c.z);
        }

    Builder needles;
    constexpr int kTiers = 4, kSides = 30, kRings = 7;
    for (int tier = 0; tier < kTiers; ++tier) {
        const float baseY = 2.2f + tier * 1.9f, height = 3.4f - tier * 0.2f, radius = 2.7f - tier * 0.55f;
        const int salt = tier * 31;
        std::vector<std::uint32_t> rings[kRings + 1];
        for (int r = 0; r <= kRings; ++r) {
            const float t = static_cast<float>(r) / kRings;   // 0 at the rim, 1 at the tip
            const float y = baseY + height * t;
            for (int s = 0; s < kSides; ++s) {
                const float a = 2.0f * kPi * s / kSides;
                // Branches: the rim is ragged, and the droop between two bumps hangs lower.
                const float ragged = 1.0f + 0.22f * (Noise3(std::cos(a) * 2.5f + salt, t * 2.0f, std::sin(a) * 2.5f) - 0.5f) * 2.0f;
                const float rim = radius * (1.0f - t) * ragged;
                const float droop = -0.35f * (1.0f - t) * (0.5f + 0.5f * std::sin(a * 7.0f + salt));
                if (r == kRings) rings[r].push_back(needles.Add(0.0f, y + 0.25f, 0.0f, 0.5f, 1.0f));   // all the tip vertices are one point
                else rings[r].push_back(needles.Add(std::cos(a) * rim, y + droop, std::sin(a) * rim, static_cast<float>(s) / kSides, t));
            }
        }
        for (int r = 0; r < kRings; ++r)
            for (int s = 0; s < kSides; ++s) {
                const int n = (s + 1) % kSides;
                const XMFLOAT3 c = needles.vertices[rings[r][s]].position;
                if (r + 1 < kRings) {   // a quad between two rings
                    needles.Triangle(rings[r][s], rings[r + 1][s], rings[r + 1][n], c.x, 0.6f, c.z);
                    needles.Triangle(rings[r][s], rings[r + 1][n], rings[r][n], c.x, 0.6f, c.z);
                } else {                // the last row ends in the tip: one triangle
                    needles.Triangle(rings[r][s], rings[r + 1][s], rings[r][n], c.x, 0.6f, c.z);
                }
            }
        // The lid underneath, so the tree is solid when you look up into it.
        const std::uint32_t centre = needles.Add(0.0f, baseY - 0.05f, 0.0f, 0.5f, 0.5f);
        for (int s = 0; s < kSides; ++s) needles.Triangle(centre, rings[0][s], rings[0][(s + 1) % kSides], 0.0f, -1.0f, 0.0f);
    }
    model.parts.push_back(ToPart("Trunk", trunk, 0));
    model.parts.push_back(ToPart("Needles", needles, 1));
    Finalize(model);
    return model;
}

// ---- A boulder: an icosphere (20 triangles, split four times over) pushed around by noise and squashed -----------------
// What makes one built-in boulder different from another. The default is the original "builtin:rock", unchanged.
struct RockStyle {
    const char* id = kBuiltinRock;
    const char* label = "Rock (built-in)";
    float lumpShift = 0.0f, chipShift = 0.0f;                  // moves the noise so the lumps fall elsewhere
    float sx = 1.05f, sy = 0.65f, sz = 0.85f;                  // the squash of the unit sphere
    float chipAmount = 0.16f;                                  // more chips = more angular
    float r = 0.16f, g = 0.15f, b = 0.14f, roughness = 0.9f;   // linear stone colour
};

ImportedModel MakeRock(const RockStyle& style = RockStyle{})
{
    ImportedModel model;
    model.name = style.label;
    model.sourcePath = style.id;
    model.materials.push_back(MakeMaterial("Stone", style.r, style.g, style.b, style.roughness));

    // The 12 corners and 20 faces of an icosahedron.
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<XMFLOAT3> points = { {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1} };
    std::vector<std::array<int, 3>> faces = { {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                              {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1} };
    auto normalize = [](XMFLOAT3 p) { const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z); return XMFLOAT3(p.x / l, p.y / l, p.z / l); };
    for (XMFLOAT3& p : points) p = normalize(p);
    for (int level = 0; level < 3; ++level) {   // 20 -> 80 -> 320 -> 1280 triangles
        std::map<std::pair<int, int>, int> midpoints;
        auto midpoint = [&](int a, int b) {
            const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            const auto found = midpoints.find(key);
            if (found != midpoints.end()) return found->second;
            points.push_back(normalize(XMFLOAT3((points[a].x + points[b].x) * 0.5f, (points[a].y + points[b].y) * 0.5f, (points[a].z + points[b].z) * 0.5f)));
            return midpoints[key] = static_cast<int>(points.size()) - 1;
        };
        std::vector<std::array<int, 3>> next;
        for (const auto& f : faces) {
            const int ab = midpoint(f[0], f[1]), bc = midpoint(f[1], f[2]), ca = midpoint(f[2], f[0]);
            next.push_back({ f[0], ab, ca }); next.push_back({ f[1], bc, ab }); next.push_back({ f[2], ca, bc }); next.push_back({ ab, bc, ca });
        }
        faces.swap(next);
    }

    Builder rock;
    for (const XMFLOAT3& p : points) {
        // Two octaves of noise move each point along its own direction: lumps and smaller chips.
        const float lump = Noise3(p.x * 1.6f + 3.0f + style.lumpShift, p.y * 1.6f, p.z * 1.6f) - 0.5f;
        const float chip = Noise3(p.x * 4.5f, p.y * 4.5f + 7.0f + style.chipShift, p.z * 4.5f) - 0.5f;
        const float radius = 1.0f + 0.55f * lump + style.chipAmount * chip;
        rock.Add(p.x * radius * style.sx, p.y * radius * style.sy, p.z * radius * style.sz, 0.5f + 0.5f * p.x, 0.5f + 0.5f * p.z);
    }
    for (const auto& f : faces) {
        const XMFLOAT3 &a = rock.vertices[f[0]].position, &b = rock.vertices[f[1]].position, &c = rock.vertices[f[2]].position;
        rock.Triangle(f[0], f[1], f[2], a.x + b.x + c.x, a.y + b.y + c.y, a.z + b.z + c.z);   // outward = away from the centre
    }
    model.parts.push_back(ToPart("Boulder", rock, 0));
    Finalize(model);
    return model;
}
}  // namespace

bool MakeBuiltinModel(const std::string& id, ImportedModel& out)
{
    if (id == kBuiltinPine) { out = MakePine(); return true; }
    if (id == kBuiltinRock) { out = MakeRock(); return true; }
    if (id == kBuiltinRockFlat) {
        RockStyle style; style.id = kBuiltinRockFlat; style.label = "Flat rock (built-in)"; style.lumpShift = 11.0f; style.chipShift = 5.0f;
        style.sx = 1.35f; style.sy = 0.38f; style.sz = 1.05f; style.r = 0.19f; style.g = 0.18f; style.b = 0.165f;
        out = MakeRock(style); return true;
    }
    if (id == kBuiltinRockTall) {
        RockStyle style; style.id = kBuiltinRockTall; style.label = "Tall rock (built-in)"; style.lumpShift = 23.0f; style.chipShift = 13.0f;
        style.sx = 0.7f; style.sy = 1.25f; style.sz = 0.62f; style.chipAmount = 0.26f; style.r = 0.12f; style.g = 0.115f; style.b = 0.115f; style.roughness = 0.85f;
        out = MakeRock(style); return true;
    }
    if (id == kBuiltinRockMossy) {
        RockStyle style; style.id = kBuiltinRockMossy; style.label = "Mossy rock (built-in)"; style.lumpShift = 37.0f; style.chipShift = 29.0f;
        style.r = 0.07f; style.g = 0.13f; style.b = 0.05f; style.roughness = 0.95f;
        out = MakeRock(style); return true;
    }
    return false;
}
