#include "ComputeShader.h"
#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

ComputeShader::~ComputeShader() { Release(); }

void ComputeShader::Release()
{
    if (shader_) { shader_->Release(); shader_ = nullptr; }
}

bool ComputeShader::Load(const wchar_t* path, ID3D11Device* device)
{
    Release();
    if (!device || !path) {
        LOG_ERROR("ComputeShader::Load received a null device or path");
        return false;
    }

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        Logger::Instance().LogErrorWithContext("Could not open compute shader file", "ComputeShader.cpp", 0,
                                               "ComputeShader::Load", std::filesystem::path(path).string());
        return false;
    }
    const std::vector<char> source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (source.empty()) {
        LOG_ERROR("Compute shader file is empty");
        return false;
    }

    // The same two choices Shader.cpp makes: the file name goes in the error messages, and D3D_COMPILE_STANDARD_FILE_INCLUDE lets
    // `#include` find files next to this one.
    const std::string name = std::filesystem::path(path).string();
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT compiled = D3DCompile(source.data(), source.size(), name.c_str(), nullptr,
                                        D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                        "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                        &blob, &errors);
    if (FAILED(compiled)) {
        std::string detail = "Compute shader compilation failed";
        if (errors && errors->GetBufferPointer()) {
            detail += ": ";
            detail.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        Logger::Instance().LogError(detail);
        if (errors) errors->Release();
        return false;
    }
    if (errors) errors->Release();   // warnings only

    const HRESULT created = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader_);
    blob->Release();   // the device keeps its own copy; unlike Shader, nothing needs the byte code later (no input layout)
    if (FAILED(created)) {
        LOG_HR(created, "Compute shader object creation failed");
        shader_ = nullptr;
        return false;
    }
    return true;
}

void ComputeShader::Bind(ID3D11DeviceContext* ctx) const
{
    if (ctx && shader_) ctx->CSSetShader(shader_, nullptr, 0);
}

void ComputeShader::Dispatch(ID3D11DeviceContext* ctx, UINT groupsX, UINT groupsY, UINT groupsZ) const
{
    if (!ctx || !shader_) return;
    ctx->Dispatch(groupsX, groupsY, groupsZ);
}

void ComputeShader::UnbindCompute(ID3D11DeviceContext* ctx, UINT srvCount, UINT uavCount)
{
    if (!ctx) return;
    // D3D11 allows at most 128 SRV slots and 8 UAV slots on the compute stage; ask for no more than that.
    ID3D11ShaderResourceView* noSrv[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* noUav[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
    if (srvCount > D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) srvCount = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    if (uavCount > D3D11_PS_CS_UAV_REGISTER_COUNT) uavCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
    if (srvCount) ctx->CSSetShaderResources(0, srvCount, noSrv);
    if (uavCount) ctx->CSSetUnorderedAccessViews(0, uavCount, noUav, nullptr);
}
