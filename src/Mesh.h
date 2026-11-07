#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

#include "Vertex.h"

// One level of detail, split into consecutive PARTS. Part 0 is always drawn; every further part only when the caller
// asks for it. The terrain uses this for its skirts: part 0 is the surface, parts 1..4 are the four edge skirts.
struct MeshLevel {
    std::vector<uint32_t> indices;
    std::vector<uint32_t> partIndexCounts;   // how many indices each part has, in order; empty = one part holding everything
};

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
    // Levels of detail whose indices are split into parts (see MeshLevel).
    Mesh(ID3D11Device* device,
         const std::vector<Vertex>& vertices,
         const std::vector<MeshLevel>& levels);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Draws part 0, plus every further part whose bit is set in `partMask` (bit 0 = part 1, bit 1 = part 2, ...).
    void Draw(ID3D11DeviceContext* ctx, int lod = 0, unsigned int partMask = ~0u) const;
    int GetLodCount() const { return static_cast<int>(indexBuffers_.size()); }

private:
    ID3D11Buffer* vertexBuffer_ = nullptr;
    std::vector<ID3D11Buffer*> indexBuffers_;   // one per level of detail
    std::vector<UINT> indexCounts_;
    std::vector<std::vector<UINT>> partCounts_;   // per level: the index count of each part
    UINT stride_ = sizeof(Vertex);
    UINT offset_ = 0;
};
