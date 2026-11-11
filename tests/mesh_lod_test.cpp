// Tests for src/MeshLod.{h,cpp}: automatic levels of detail. No Direct3D, no framework.
//   1. A dense sphere gets all four levels, each clearly cheaper than the one before, all valid.
//   2. Every level only uses vertices that exist, in whole triangles, and keeps the shape (its bounds stay close).
//   3. Small meshes, empty input and levels = 1 are left alone.
//   4. Attributes are accepted (a mesh with a UV seam still simplifies), the result is deterministic, and vertex-cache
//      ordering does not change WHICH triangles exist.
#include "MeshLod.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

// The layout of the engine's Vertex (src/Vertex.h): position, normal, uv, tangent = 12 floats = 48 bytes.
struct V { float p[3]; float n[3]; float uv[2]; float t[4]; };
static_assert(sizeof(V) == 48, "same layout as Vertex");

static void MakeSphere(int slices, int stacks, float radius, std::vector<V>& vertices, std::vector<std::uint32_t>& indices)
{
    const float pi = 3.14159265f;
    for (int i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / stacks, phi = v * pi;
        for (int j = 0; j <= slices; ++j) {   // the seam column (j = slices) duplicates j = 0 with a different u: a real UV seam
            const float u = static_cast<float>(j) / slices, theta = u * 2.0f * pi;
            V vert{};
            vert.n[0] = std::sin(phi) * std::cos(theta); vert.n[1] = std::cos(phi); vert.n[2] = std::sin(phi) * std::sin(theta);
            for (int k = 0; k < 3; ++k) vert.p[k] = vert.n[k] * radius;
            vert.uv[0] = u; vert.uv[1] = v; vert.t[0] = 1.0f; vert.t[3] = 1.0f;
            vertices.push_back(vert);
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const std::uint32_t a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
}

static std::vector<std::vector<std::uint32_t>> Build(const std::vector<V>& vertices, const std::vector<std::uint32_t>& indices, const MeshLodSettings& s, bool attributes = true)
{
    return BuildMeshLods(&vertices[0].p[0], sizeof(V), attributes ? &vertices[0].n[0] : nullptr, sizeof(V), attributes ? 5 : 0, vertices.size(),
                         indices.data(), indices.size(), s);
}

static void TestSphere()
{
    std::vector<V> vertices; std::vector<std::uint32_t> indices;
    MakeSphere(64, 32, 2.0f, vertices, indices);   // 4096 triangles
    const size_t baseTriangles = indices.size() / 3;
    CHECK(baseTriangles == 4096);

    const auto levels = Build(vertices, indices, MeshLodSettings{});
    CHECK(levels.size() == 4);
    if (levels.size() != 4) return;
    CHECK(levels[0].size() == indices.size());
    for (size_t k = 1; k < levels.size(); ++k) CHECK(levels[k].size() * 10 < levels[k - 1].size() * 9);   // each level is at least 10% cheaper
    CHECK(levels[1].size() / 3 <= baseTriangles * 6 / 10);    // asked for 50%
    CHECK(levels[2].size() / 3 <= baseTriangles * 3 / 10);    // asked for 20%
    CHECK(levels[3].size() / 3 <= baseTriangles * 12 / 100);  // asked for 6%
    CHECK(levels[3].size() / 3 >= 8);

    for (const auto& level : levels) {
        CHECK(level.size() % 3 == 0);
        bool valid = true; float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        for (std::uint32_t index : level) {
            if (index >= vertices.size()) { valid = false; break; }
            for (int k = 0; k < 3; ++k) { lo[k] = (std::min)(lo[k], vertices[index].p[k]); hi[k] = (std::max)(hi[k], vertices[index].p[k]); }
        }
        CHECK(valid);
        if (!valid) continue;
        for (int k = 0; k < 3; ++k) CHECK(hi[k] - lo[k] > 3.0f);   // a sphere of radius 2 is 4 wide: even the coarsest keeps most of it
    }
}

static void TestLeftAlone()
{
    std::vector<V> vertices; std::vector<std::uint32_t> indices;
    MakeSphere(8, 6, 1.0f, vertices, indices);   // 96 triangles: under minTriangles
    CHECK(Build(vertices, indices, MeshLodSettings{}).size() == 1);

    MakeSphere(0, 0, 1.0f, vertices, indices);
    vertices.clear(); indices.clear();
    MakeSphere(64, 32, 1.0f, vertices, indices);
    MeshLodSettings one; one.levels = 1;
    const auto levels = Build(vertices, indices, one);
    CHECK(levels.size() == 1 && levels[0].size() == indices.size());

    CHECK(BuildMeshLods(nullptr, 0, nullptr, 0, 0, 0, nullptr, 0, MeshLodSettings{}).empty());
    CHECK(BuildMeshLods(&vertices[0].p[0], sizeof(V), nullptr, 0, 0, vertices.size(), indices.data(), 2, MeshLodSettings{}).empty());   // not even one triangle
}

static std::multiset<std::vector<std::uint32_t>> Triangles(const std::vector<std::uint32_t>& indices)
{
    std::multiset<std::vector<std::uint32_t>> set;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        std::vector<std::uint32_t> t = { indices[i], indices[i + 1], indices[i + 2] };
        std::rotate(t.begin(), std::min_element(t.begin(), t.end()), t.end());   // the same triangle however it starts
        set.insert(t);
    }
    return set;
}

static void TestAttributesDeterminismAndCacheOrder()
{
    std::vector<V> vertices; std::vector<std::uint32_t> indices;
    MakeSphere(64, 32, 2.0f, vertices, indices);

    const auto withAttributes = Build(vertices, indices, MeshLodSettings{}, true);
    const auto without = Build(vertices, indices, MeshLodSettings{}, false);
    CHECK(withAttributes.size() >= 3 && without.size() >= 3);

    const auto again = Build(vertices, indices, MeshLodSettings{}, true);
    CHECK(again == withAttributes);   // same input, same output

    // Vertex-cache ordering re-orders triangles but never changes which ones they are.
    MeshLodSettings plain; plain.optimizeVertexCache = false;
    const auto unordered = Build(vertices, indices, plain);
    CHECK(Triangles(unordered[0]) == Triangles(withAttributes[0]));
    CHECK(Triangles(unordered[0]) == Triangles(indices));

    // A tighter error limit can only keep MORE triangles in a level, and may give up on the coarsest levels altogether.
    MeshLodSettings strict; strict.maxError = 0.002f;
    const auto careful = Build(vertices, indices, strict);
    CHECK(careful.size() <= withAttributes.size());
    for (size_t k = 1; k < careful.size(); ++k) CHECK(careful[k].size() >= withAttributes[k].size());

    // Asking for fewer levels gives exactly the first ones.
    MeshLodSettings two; two.levels = 2;
    const auto short_ = Build(vertices, indices, two);
    CHECK(short_.size() == 2 && short_[1] == withAttributes[1]);
}

int main()
{
    TestSphere();
    TestLeftAlone();
    TestAttributesDeterminismAndCacheOrder();
    if (g_failures == 0) std::printf("MeshLodTests: all passed\n");
    else std::printf("MeshLodTests: %d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
