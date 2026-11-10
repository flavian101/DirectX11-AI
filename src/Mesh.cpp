#include "Mesh.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace DirectX;

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : Mesh(device, vertices, std::vector<std::vector<uint32_t>>{ indices })
{
}

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices,
           const std::vector<std::vector<uint32_t>>& lodIndices)
    : Mesh(device, vertices, [&lodIndices] {
          std::vector<MeshLevel> levels;
          for (const auto& indices : lodIndices) levels.push_back({ indices, {} });
          return levels;
      }())
{
}

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices, const std::vector<MeshLevel>& levels,
           bool updatableVertices)
{
    if (!device || vertices.empty() || levels.empty()) {
        LOG_ERROR("Cannot create mesh from empty data or without a device");
        return;
    }
    for (const MeshLevel& level : levels) {
        if (level.indices.empty()) {
            LOG_ERROR("Cannot create mesh: a level of detail has no indices");
            return;
        }
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    // IMMUTABLE = fastest, but never writable. DEFAULT = still lives on the GPU, and UpdateSubresource can overwrite it.
    desc.Usage = updatableVertices ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    HRESULT hr = device->CreateBuffer(&desc, &data, &vertexBuffer_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Vertex buffer creation failed");
        return;
    }
    updatable_ = updatableVertices;
    vertexCount_ = static_cast<UINT>(vertices.size());

    for (const MeshLevel& level : levels) {
        const std::vector<uint32_t>& indices = level.indices;
        D3D11_BUFFER_DESC indexDesc{};
        indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
        indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexData{};
        indexData.pSysMem = indices.data();
        ID3D11Buffer* buffer = nullptr;
        hr = device->CreateBuffer(&indexDesc, &indexData, &buffer);
        if (FAILED(hr)) {
            LOG_HR(hr, "Index buffer creation failed");
            for (ID3D11Buffer* created : indexBuffers_) created->Release();
            indexBuffers_.clear();
            indexCounts_.clear();
            partCounts_.clear();
            vertexBuffer_->Release();
            vertexBuffer_ = nullptr;
            return;
        }
        indexBuffers_.push_back(buffer);
        indexCounts_.push_back(static_cast<UINT>(indices.size()));
        // A level without explicit parts is one part holding everything.
        partCounts_.emplace_back(level.partIndexCounts.begin(), level.partIndexCounts.end());
        if (partCounts_.back().empty()) partCounts_.back().push_back(static_cast<UINT>(indices.size()));
    }
}

bool Mesh::UpdateVertices(ID3D11DeviceContext* ctx, const std::vector<Vertex>& vertices)
{
    if (!ctx || !vertexBuffer_ || !updatable_ || vertices.size() != vertexCount_) return false;
    ctx->UpdateSubresource(vertexBuffer_, 0, nullptr, vertices.data(), 0, 0);   // the whole buffer, from the CPU copy
    return true;
}

Mesh::~Mesh()
{
    if (vertexBuffer_) vertexBuffer_->Release();
    for (ID3D11Buffer* buffer : indexBuffers_) if (buffer) buffer->Release();
}

void Mesh::Draw(ID3D11DeviceContext* ctx, int lod, unsigned int partMask) const
{
    if (!ctx || !vertexBuffer_ || indexBuffers_.empty()) return;
    const size_t level = static_cast<size_t>(std::clamp(lod, 0, GetLodCount() - 1));
    ctx->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride_, &offset_);
    ctx->IASetIndexBuffer(indexBuffers_[level], DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Walk the parts in order. Parts that are wanted AND touch each other in the index buffer are drawn with ONE
    // call (a draw call has a cost; ranges that are side by side cost nothing extra to merge).
    const std::vector<UINT>& parts = partCounts_[level];
    UINT runStart = 0, runCount = 0, position = 0;
    for (size_t part = 0; part < parts.size(); ++part) {
        const bool wanted = (part == 0) || ((partMask >> (part - 1)) & 1u);
        if (wanted) {
            if (runCount == 0) runStart = position;
            runCount += parts[part];
        } else if (runCount > 0) {
            ctx->DrawIndexed(runCount, runStart, 0);
            runCount = 0;
        }
        position += parts[part];
    }
    if (runCount > 0) ctx->DrawIndexed(runCount, runStart, 0);
}
