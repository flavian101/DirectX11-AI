#pragma once
// The terrain's height data as plain numbers, and the questions you can ask of it. Pure logic: no Direct3D, no
// DirectXMath, so it can be tested on its own (see tests/heightfield_test.cpp) - the same way as TerrainSkirts.h.
//
// WHY THIS EXISTS (lesson 25). The terrain mesh is built from a grid of heights and then the grid was thrown away: the
// GPU holds the triangles, and nothing on the CPU could answer "how high is the ground at x, z?". Item placement,
// clicking on the ground and sculpting brushes all need that answer, so the grid is now kept here.
//
// LOCAL SPACE. The field is described in the terrain's own space, before position and scale are applied:
//     x runs from -sizeX/2 to +sizeX/2,   z from -sizeZ/2 to +sizeZ/2,   y from 0 to heightScale.
// That is exactly where Terrain.cpp puts its mesh vertices, so the two always agree.
#include <vector>

struct HfVec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct HeightfieldLayout {
    int cellsX = 0;             // quads along X. The grid has (cellsX + 1) x (cellsZ + 1) samples.
    int cellsZ = 0;
    float sizeX = 0.0f;         // local-space size of the whole field
    float sizeZ = 0.0f;
    float heightScale = 1.0f;   // local-space height of a sample whose value is 1.0
};

class Heightfield {
public:
    // Takes the samples (row by row, z-major: index = z * (cellsX + 1) + x), each 0..1. Returns false and changes
    // nothing if the layout is unusable or the sample count does not match it.
    bool Reset(const HeightfieldLayout& layout, std::vector<float> samples);

    bool IsEmpty() const { return samples_.empty(); }
    const HeightfieldLayout& GetLayout() const { return layout_; }

    // One stored sample (0..1). Grid indices outside the grid are clamped to the nearest edge sample.
    float Sample(int gridX, int gridZ) const;

    // Height (local-space Y) of the SURFACE at (x, z), exactly as the mesh draws it at full detail: the surface is
    // made of triangles, so this interpolates inside the triangle that holds the point, not across the whole cell.
    // False if (x, z) is outside the field.
    bool HeightAt(float x, float z, float& y) const;

    // Unit surface normal at (x, z): the same smoothed vertex normals the mesh uses, interpolated the same way.
    bool NormalAt(float x, float z, HfVec3& normal) const;

    // First place the ray meets the surface when it comes FROM ABOVE. `direction` need not be unit length: `t` is
    // measured in the same units as `direction` (hit point = origin + direction * t), which is what lets the terrain
    // pass in a world ray that has been moved into local space. False if the ray misses, or starts under the surface.
    bool Raycast(const HfVec3& origin, const HfVec3& direction, float& t) const;

private:
    // Which triangle holds a point, as its three corner samples and the weight of each (the weights add up to 1).
    struct Location {
        int gridX[3];
        int gridZ[3];
        float weight[3];
    };
    bool Locate(float x, float z, Location& out) const;
    HfVec3 VertexNormal(int gridX, int gridZ) const;                          // the normal the mesh stores at a vertex
    float HeightClamped(float x, float z) const;                              // like HeightAt, but never fails

    HeightfieldLayout layout_;
    std::vector<float> samples_;
};
