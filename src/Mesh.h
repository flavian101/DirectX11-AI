#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

#include "Vertex.h"

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
