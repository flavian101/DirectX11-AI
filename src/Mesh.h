#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

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

class Mesh {
public:
    Mesh(ID3D11Device* device,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    // Several levels of detail sharing ONE vertex buffer: lodIndices[0] is the finest, later
    // entries use fewer triangles (each is just a different way of connecting the same vertices).
    Mesh(ID3D11Device* device,
         const std::vector<Vertex>& vertices,
         const std::vector<std::vector<uint32_t>>& lodIndices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Draw(ID3D11DeviceContext* ctx, int lod = 0) const;
    int GetLodCount() const { return static_cast<int>(indexBuffers_.size()); }

private:
    ID3D11Buffer* vertexBuffer_ = nullptr;
    std::vector<ID3D11Buffer*> indexBuffers_;   // one per level of detail
    std::vector<UINT> indexCounts_;
    UINT stride_ = sizeof(Vertex);
    UINT offset_ = 0;
};
