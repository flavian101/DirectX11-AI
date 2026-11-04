#include "Mesh.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace DirectX;

void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    // Per triangle: the edges are  e1 = T*du1 + B*dv1  and  e2 = T*du2 + B*dv2  (T, B = how
    // position changes per unit of U and V). Two equations, two unknowns: solve for T and B.
    std::vector<XMFLOAT3> tangentSum(vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    std::vector<XMFLOAT3> bitangentSum(vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const XMVECTOR p0 = XMLoadFloat3(&vertices[ia].position);
        const XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&vertices[ib].position), p0);
        const XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&vertices[ic].position), p0);
        const float du1 = vertices[ib].texCoord.x - vertices[ia].texCoord.x;
        const float dv1 = vertices[ib].texCoord.y - vertices[ia].texCoord.y;
        const float du2 = vertices[ic].texCoord.x - vertices[ia].texCoord.x;
        const float dv2 = vertices[ic].texCoord.y - vertices[ia].texCoord.y;
        const float determinant = du1 * dv2 - du2 * dv1;
        if (std::fabs(determinant) < 1e-12f) continue;   // the triangle has no UV area: it cannot define a direction
        const float inverse = 1.0f / determinant;
        const XMVECTOR t = XMVectorScale(XMVectorSubtract(XMVectorScale(e1, dv2), XMVectorScale(e2, dv1)), inverse);
        const XMVECTOR b = XMVectorScale(XMVectorSubtract(XMVectorScale(e2, du1), XMVectorScale(e1, du2)), inverse);
        for (const uint32_t index : { ia, ib, ic }) {   // shared vertices average their triangles
            XMStoreFloat3(&tangentSum[index], XMVectorAdd(XMLoadFloat3(&tangentSum[index]), t));
            XMStoreFloat3(&bitangentSum[index], XMVectorAdd(XMLoadFloat3(&bitangentSum[index]), b));
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&vertices[i].normal));
        XMVECTOR t = XMLoadFloat3(&tangentSum[i]);
        // Gram-Schmidt: remove the part of T that points along the normal, so T is exactly perpendicular.
        t = XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t))));
        if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-12f) {
            // No usable UV direction (a point, a skybox corner, a degenerate quad): any perpendicular will do.
            const XMVECTOR axis = (std::fabs(XMVectorGetX(n)) < 0.9f) ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0);
            t = XMVectorSubtract(axis, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, axis))));
        }
        t = XMVector3Normalize(t);
        // Handedness: does cross(N, T) point the same way as the real bitangent? If a mesh's UVs are
        // mirrored it will not, and w = -1 flips the rebuilt bitangent to match.
        const float handedness = XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), XMLoadFloat3(&bitangentSum[i]))) < 0.0f ? -1.0f : 1.0f;
        XMFLOAT3 tangent;
        XMStoreFloat3(&tangent, t);
        vertices[i].tangent = XMFLOAT4(tangent.x, tangent.y, tangent.z, handedness);
    }
}

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
