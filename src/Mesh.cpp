#include "Mesh.h"
#include "Logger.h"
#include <cstdint>

Mesh::Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
{
    if (!device || vertices.empty() || indices.empty()) {
        LOG_ERROR("Cannot create mesh from empty data or without a device");
        return;
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

    desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices.data();
    hr = device->CreateBuffer(&desc, &data, &indexBuffer_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Index buffer creation failed");
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
        return;
    }
    indexCount_ = static_cast<UINT>(indices.size());
}

Mesh::~Mesh()
{
    if (vertexBuffer_) vertexBuffer_->Release();
    if (indexBuffer_) indexBuffer_->Release();
}

void Mesh::Draw(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !vertexBuffer_ || !indexBuffer_ || indexCount_ == 0) return;
    ctx->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride_, &offset_);
    ctx->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->DrawIndexed(indexCount_, 0, 0);
}
