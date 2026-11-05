#pragma once
// The CPU-side vertex and its helpers. Deliberately free of Direct3D, so model importing (and tests)
// can use it without a graphics device.
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
    // xyz = the direction the texture's U axis runs across the surface (the "tangent").
    // w   = handedness, +1 or -1: the bitangent is rebuilt in the shader as cross(normal, tangent) * w,
    //       so we store 4 floats instead of 6. Used by normal maps. Layout offset: 32 (see Shader.cpp).
    DirectX::XMFLOAT4 tangent;

    Vertex()
        : position(0.0f, 0.0f, 0.0f),
        normal(0.0f, 0.0f, 0.0f),
        texCoord(0.0f, 0.0f),
        tangent(1.0f, 0.0f, 0.0f, 1.0f) {}

    Vertex(DirectX::XMFLOAT3 pos,
        DirectX::XMFLOAT3 norm,
        DirectX::XMFLOAT2 uv)
        : position(pos), normal(norm), texCoord(uv), tangent(1.0f, 0.0f, 0.0f, 1.0f) {}
};
static_assert(sizeof(Vertex) == 48, "Vertex layout must match the input layout in Shader.cpp");

// Fills in every vertex's `tangent` from the triangles' positions and UVs. Call it once after you
// build positions, normals and UVs, before creating the Mesh. Meshes whose UVs are degenerate keep
// a safe fallback tangent instead of NaNs.
void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
