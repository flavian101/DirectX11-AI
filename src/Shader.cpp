#include "Shader.h"
#include "Logger.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

Shader::~Shader() { Release(); }

bool Shader::Load(const wchar_t* vsPath, const wchar_t* psPath, ID3D11Device* device)
{
    Release();
    if (!device || !vsPath) {
        LOG_ERROR("Shader::Load received a null device or vertex shader path");
        return false;
    }

    auto readFile = [](const wchar_t* path, std::vector<char>& source) {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) {
            Logger::Instance().LogErrorWithContext("Could not open shader file", "Shader.cpp", 0,
                                                   "Shader::Load", std::filesystem::path(path).string());
            return false;
        }
        source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !source.empty();
    };

    std::vector<char> vsSource, psSource;
    if (!readFile(vsPath, vsSource) || (psPath && !readFile(psPath, psSource))) return false;

    // pSourceName + D3D_COMPILE_STANDARD_FILE_INCLUDE lets `#include "common.hlsli"`
    // resolve relative to the shader's own folder (and puts the file name in error messages).
    auto compile = [](const std::vector<char>& source, const std::string& sourceName,
                      const char* profile, ID3DBlob** blob, const char* label) {
        ID3DBlob* errors = nullptr;
        const HRESULT hr = D3DCompile(source.data(), source.size(), sourceName.c_str(), nullptr,
                                      D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                      "main", profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                      blob, &errors);
        if (FAILED(hr)) {
            std::string detail = std::string(label) + " compilation failed";
            if (errors && errors->GetBufferPointer()) {
                detail += ": ";
                detail.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            }
            Logger::Instance().LogError(detail);
            if (errors) errors->Release();
            return false;
        }
        if (errors) errors->Release();
        return true;
    };

    const std::string vsName = std::filesystem::path(vsPath).string();
    const std::string psName = psPath ? std::filesystem::path(psPath).string() : std::string();
    if (!compile(vsSource, vsName, "vs_5_0", &vsBlob_, "Vertex shader") ||
        (psPath && !compile(psSource, psName, "ps_5_0", &psBlob_, "Pixel shader"))) {
        Release();
        return false;
    }

    HRESULT hr = device->CreateVertexShader(vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Vertex shader object creation failed");
        Release();
        return false;
    }
    hr = psBlob_ ? device->CreatePixelShader(psBlob_->GetBufferPointer(), psBlob_->GetBufferSize(), nullptr, &ps_) : S_OK;
    if (FAILED(hr)) {
        LOG_HR(hr, "Pixel shader object creation failed");
        Release();
        return false;
    }

    // A vertex shader that only reads system values (a full-screen triangle uses SV_VertexID)
    // has no vertex data to describe, so it needs no input layout. Ask the compiled shader.
    bool needsLayout = true;
    ID3D11ShaderReflection* reflection = nullptr;
    if (SUCCEEDED(D3DReflect(vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize(),
                                IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) {
        D3D11_SHADER_DESC shaderDesc{};
        reflection->GetDesc(&shaderDesc);
        needsLayout = false;
        for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC parameter{};
            reflection->GetInputParameterDesc(i, &parameter);
            if (parameter.SystemValueType == D3D_NAME_UNDEFINED) needsLayout = true;   // a real vertex attribute
        }
        reflection->Release();
    }
    if (!needsLayout) return true;

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    // The layout may offer MORE than a vertex shader reads: vs.hlsl uses the tangent, the shadow and
    // water shaders simply ignore it. Offering less than a shader reads would fail right here.
    hr = device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                   vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize(), &layout_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Input layout creation failed");
        Release();
        return false;
    }
    return true;
}

void Shader::Apply(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !vs_) return;
    ctx->IASetInputLayout(layout_);   // nullptr for layout-free shaders: that is correct and intended
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);   // ps_ is nullptr for depth-only programs: that UNBINDS the old one
}

void Shader::Release()
{
    if (layout_) { layout_->Release(); layout_ = nullptr; }
    if (vs_) { vs_->Release(); vs_ = nullptr; }
    if (ps_) { ps_->Release(); ps_ = nullptr; }
    if (vsBlob_) { vsBlob_->Release(); vsBlob_ = nullptr; }
    if (psBlob_) { psBlob_->Release(); psBlob_ = nullptr; }
}


std::shared_ptr<Shader> ShaderCache::Get(ID3D11Device* device, const wchar_t* vsPath, const wchar_t* psPath)
{
    static std::map<std::wstring, std::weak_ptr<Shader>> cache;
    if (!vsPath) return nullptr;

    const std::wstring key = std::wstring(vsPath) + L"|" + (psPath ? psPath : L"");
    const auto found = cache.find(key);
    if (found != cache.end()) {
        if (std::shared_ptr<Shader> existing = found->second.lock()) return existing;
    }

    auto shader = std::make_shared<Shader>();
    if (!shader->Load(vsPath, psPath, device)) return nullptr;
    cache[key] = shader;
    return shader;
}
