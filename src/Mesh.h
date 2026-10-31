#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
};

class Mesh {
public:
    Mesh(ID3D11Device* device,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();

    void Draw(ID3D11DeviceContext* ctx) const;

private:
    ID3D11Buffer* vertexBuffer_ = nullptr;
    ID3D11Buffer* indexBuffer_ = nullptr;
    UINT stride_ = sizeof(Vertex);
    UINT offset_ = 0;
    UINT indexCount_ = 0;
};
