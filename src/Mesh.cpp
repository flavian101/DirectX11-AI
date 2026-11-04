#include "Mesh.h"
#include "Logger.h"
#include <algorithm>
#include <cstdint>

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : Mesh(device, vertices, std::vector<std::vector<uint32_t>>{ indices })
{
}

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices,
           const std::vector<std::vector<uint32_t>>& lodIndices)
{
    if (!device || vertices.empty() || lodIndices.empty()) {
        LOG_ERROR("Cannot create mesh from empty data or without a device");
        return;
    }
    for (const auto& indices : lodIndices) {
        if (indices.empty()) {
            LOG_ERROR("Cannot create mesh: a level of detail has no indices");
            return;
        }
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    HRESULT hr = device->CreateBuffer(&desc, &data, &vertexBuffer_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Vertex buffer creation failed");
        return;
    }

    for (const auto& indices : lodIndices) {
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
            vertexBuffer_->Release();
            vertexBuffer_ = nullptr;
            return;
        }
        indexBuffers_.push_back(buffer);
        indexCounts_.push_back(static_cast<UINT>(indices.size()));
    }
}

Mesh::~Mesh()
{
    if (vertexBuffer_) vertexBuffer_->Release();
    for (ID3D11Buffer* buffer : indexBuffers_) if (buffer) buffer->Release();
}

void Mesh::Draw(ID3D11DeviceContext* ctx, int lod) const
{
    if (!ctx || !vertexBuffer_ || indexBuffers_.empty()) return;
    const size_t level = static_cast<size_t>(std::clamp(lod, 0, GetLodCount() - 1));
    ctx->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride_, &offset_);
    ctx->IASetIndexBuffer(indexBuffers_[level], DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->DrawIndexed(indexCounts_[level], 0, 0);
}
