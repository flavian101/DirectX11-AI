#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool Load(const wchar_t* vsPath, const wchar_t* psPath, ID3D11Device* device);
    void Apply(ID3D11DeviceContext* ctx) const;

private:
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3DBlob* vsBlob_ = nullptr;
    ID3DBlob* psBlob_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    void Release();
};

