#include "Shader.h"
#include "Logger.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

Shader::~Shader() { Release(); }

bool Shader::Load(const wchar_t* vsPath, const wchar_t* psPath, ID3D11Device* device)
{
    Release();
    if (!device || !vsPath || !psPath) {
        LOG_ERROR("Shader::Load received a null device or path");
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
    if (!readFile(vsPath, vsSource) || !readFile(psPath, psSource)) return false;

    auto compile = [](const std::vector<char>& source, const char* profile,
                      ID3DBlob** blob, const char* label) {
        ID3DBlob* errors = nullptr;
        const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
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

    if (!compile(vsSource, "vs_5_0", &vsBlob_, "Vertex shader") ||
        !compile(psSource, "ps_5_0", &psBlob_, "Pixel shader")) {
        Release();
        return false;
    }

    HRESULT hr = device->CreateVertexShader(vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Vertex shader object creation failed");
        Release();
        return false;
    }
    hr = device->CreatePixelShader(psBlob_->GetBufferPointer(), psBlob_->GetBufferSize(), nullptr, &ps_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Pixel shader object creation failed");
        Release();
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
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
    if (!ctx || !vs_ || !ps_ || !layout_) return;
    ctx->IASetInputLayout(layout_);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
}

void Shader::Release()
{
    if (layout_) { layout_->Release(); layout_ = nullptr; }
    if (vs_) { vs_->Release(); vs_ = nullptr; }
    if (ps_) { ps_->Release(); ps_ = nullptr; }
    if (vsBlob_) { vsBlob_->Release(); vsBlob_ = nullptr; }
    if (psBlob_) { psBlob_->Release(); psBlob_ = nullptr; }
}

