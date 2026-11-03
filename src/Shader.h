#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <memory>
#include <string>

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // psPath may be nullptr: a vertex-shader-only program for depth-only passes (shadow maps).
    // With no pixel shader bound the GPU skips all colour work and just writes depth.
    bool Load(const wchar_t* vsPath, const wchar_t* psPath, ID3D11Device* device);
    void Apply(ID3D11DeviceContext* ctx) const;
    void SetTexture(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* texture)
    {
        if (ctx && texture) {
            ctx->PSSetShaderResources(0, 1, &texture);
        }
    }

private:
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3DBlob* vsBlob_ = nullptr;
    ID3DBlob* psBlob_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    void Release();
};

// Hands out ONE compiled Shader per vertex/pixel file pair. Before this, every Renderable
// read and compiled its own copy from disk, so N objects meant N compiles (and each press
// of "Generate terrain" recompiled the terrain shaders). The cache only holds weak
// references, so a shader is freed when the last Renderable using it goes away and nothing
// outlives the D3D device. Single-threaded, like the rest of the renderer.
class ShaderCache {
public:
    static std::shared_ptr<Shader> Get(ID3D11Device* device, const wchar_t* vsPath, const wchar_t* psPath);
};

