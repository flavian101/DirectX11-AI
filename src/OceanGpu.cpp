#include "OceanGpu.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// A structured buffer is an array of equal-sized records the shader indexes like an array (StructuredBuffer<T>). It suits this data better than a
// texture: no filtering, no 2-D addressing, and the CPU array goes in as it is. IMMUTABLE: made once, never edited.
bool MakeStructuredBuffer(ID3D11Device* device, const void* data, UINT stride, UINT count, ID3D11Buffer** buffer, ID3D11ShaderResourceView** view)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = stride * count;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = data;
    HRESULT hr = device->CreateBuffer(&desc, &init, buffer);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean structured buffer creation failed"); *buffer = nullptr; return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_UNKNOWN;   // a structured buffer has no DXGI format: the stride says what a record is
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    viewDesc.Buffer.FirstElement = 0;
    viewDesc.Buffer.NumElements = count;
    hr = device->CreateShaderResourceView(*buffer, &viewDesc, view);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean structured buffer view creation failed"); *view = nullptr; return false; }
    return true;
}

template <class T> void SafeRelease(T*& pointer) { if (pointer) { pointer->Release(); pointer = nullptr; } }

}   // namespace

void OceanGpu::Band::Release()
{
    SafeRelease(uavA); SafeRelease(uavB);
    SafeRelease(srvA); SafeRelease(srvB);
    SafeRelease(textureA); SafeRelease(textureB);
    SafeRelease(modesView); SafeRelease(omegaView);
    SafeRelease(modes); SafeRelease(omega); SafeRelease(constants);
}

OceanGpu::~OceanGpu() { Release(); }

void OceanGpu::Release()
{
    for (Band& band : bands_) band.Release();
    bands_.clear();
}

bool OceanGpu::CreateBand(const OceanGpuPack::PackedBand& packed, Band& band)
{
    const UINT count = static_cast<UINT>(packed.n) * static_cast<UINT>(packed.n);
    band.n = packed.n;
    band.deltaK = packed.DeltaK();
    if (!MakeStructuredBuffer(device_, packed.modes.data(), sizeof(OceanGpuPack::Float4), count, &band.modes, &band.modesView)) return false;
    if (!MakeStructuredBuffer(device_, packed.omega.data(), sizeof(float), count, &band.omega, &band.omegaView)) return false;

    // The constant buffer is rewritten every Evolve (UpdateSubresource), so it is DEFAULT, not IMMUTABLE.
    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = sizeof(OceanGpuPack::EvolveConstants);
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = device_->CreateBuffer(&constantsDesc, nullptr, &band.constants);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean constant buffer creation failed"); band.constants = nullptr; return false; }

    // The two output textures: written by the evolve pass through a UAV, read by the FFT through an SRV. Never both in one pass.
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = static_cast<UINT>(packed.n);
    textureDesc.Height = static_cast<UINT>(packed.n);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // two complex numbers per texel; typed UAV STORES to this format are guaranteed
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D** textures[2] = { &band.textureA, &band.textureB };
    ID3D11ShaderResourceView** srvs[2] = { &band.srvA, &band.srvB };
    ID3D11UnorderedAccessView** uavs[2] = { &band.uavA, &band.uavB };
    for (int i = 0; i < 2; ++i) {
        hr = device_->CreateTexture2D(&textureDesc, nullptr, textures[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum texture creation failed"); *textures[i] = nullptr; return false; }
        hr = device_->CreateShaderResourceView(*textures[i], nullptr, srvs[i]);   // nullptr = a view of the whole texture in its own format
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum SRV creation failed"); *srvs[i] = nullptr; return false; }
        hr = device_->CreateUnorderedAccessView(*textures[i], nullptr, uavs[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum UAV creation failed"); *uavs[i] = nullptr; return false; }
    }
    return true;
}

bool OceanGpu::Create(ID3D11Device* device, const SeaSpectrum::Settings& settings)
{
    Release();
    device_ = device;
    if (!device_) { LOG_ERROR("OceanGpu::Create received a null device"); return false; }
    if (!evolveShader_.Load(L"shaders/ocean_evolve_cs.hlsl", device_)) return false;

    const std::vector<SeaSpectrum::Band> bandList = SeaSpectrum::DefaultBands();
    bands_.resize(bandList.size());
    for (size_t i = 0; i < bandList.size(); ++i) {
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bandList[i], static_cast<int>(i));
        if (!CreateBand(OceanGpuPack::Pack(field), bands_[i])) {
            Release();   // a failed Create leaves nothing half-built
            return false;
        }
    }
    return true;
}

void OceanGpu::Evolve(ID3D11DeviceContext* ctx, float time)
{
    if (!ctx || !IsValid()) return;
    evolveShader_.Bind(ctx);
    for (Band& band : bands_) {
        OceanGpuPack::EvolveConstants constants;
        constants.n = static_cast<std::uint32_t>(band.n);
        constants.deltaK = band.deltaK;
        constants.time = time;
        ctx->UpdateSubresource(band.constants, 0, nullptr, &constants, 0, 0);

        ID3D11ShaderResourceView* inputs[2] = { band.modesView, band.omegaView };
        ID3D11UnorderedAccessView* outputs[2] = { band.uavA, band.uavB };
        ctx->CSSetConstantBuffers(0, 1, &band.constants);
        ctx->CSSetShaderResources(0, 2, inputs);
        ctx->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);   // nullptr: no append/consume counters to reset

        // One thread per mode, 8 x 8 threads a group (numthreads in the shader), rounded UP so every mode is covered.
        const UINT groups = ComputeShader::GroupCount(static_cast<UINT>(band.n), 8);
        evolveShader_.Dispatch(ctx, groups, groups, 1);
        ComputeShader::UnbindCompute(ctx, 2, 2);   // the FFT will READ these textures: they must not still be bound as outputs
    }
}

bool OceanGpu::ReadTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, std::vector<OceanGpuPack::Float4>& out)
{
    // The GPU's own textures cannot be mapped by the CPU. A STAGING copy can: copy there, then Map it.
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean staging texture creation failed"); return false; }
    ctx->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);   // waits until the GPU has finished the copy
    if (FAILED(hr)) { LOG_HR(hr, "Ocean staging texture map failed"); staging->Release(); return false; }
    out.assign(static_cast<size_t>(n) * n, OceanGpuPack::Float4{});
    for (int y = 0; y < n; ++y) {   // rows are RowPitch bytes apart, which can be more than n texels: copy row by row
        std::memcpy(&out[static_cast<size_t>(y) * n], static_cast<const char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(n) * sizeof(OceanGpuPack::Float4));
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    return true;
}

bool OceanGpu::ReadSpectra(ID3D11DeviceContext* ctx, int band, OceanGpuPack::PackedSpectra& out)
{
    if (!ctx || !IsValid() || band < 0 || band >= BandCount()) return false;
    const Band& b = bands_[static_cast<size_t>(band)];
    out.n = b.n;
    return ReadTexture(ctx, b.textureA, b.n, out.a) && ReadTexture(ctx, b.textureB, b.n, out.b);
}

bool OceanGpu::SelfCheck(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    // A strong wind so the numbers are big and a wrong sign cannot hide in rounding; a time where every mode has turned by a different phase.
    SeaSpectrum::Settings settings;
    settings.windSpeed = 12.0;
    settings.seed = 1;
    const float time = 3.7f;

    OceanGpu gpu;
    if (!gpu.Create(device, settings)) {
        LOG_WARNING("Ocean GPU check skipped: the evolve pass could not be created (see the errors above)");
        return false;
    }
    gpu.Evolve(ctx, time);

    const std::vector<SeaSpectrum::Band> bandList = SeaSpectrum::DefaultBands();
    bool allGood = true;
    for (int i = 0; i < gpu.BandCount(); ++i) {
        OceanGpuPack::PackedSpectra onGpu;
        if (!gpu.ReadSpectra(ctx, i, onGpu)) { LOG_WARNING("Ocean GPU check: could not read a band back"); return false; }
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bandList[static_cast<size_t>(i)], i);
        const OceanGpuPack::PackedSpectra onCpu = OceanGpuPack::EvolvePacked(OceanGpuPack::Pack(field), time);

        double biggest = 0.0, worst = 0.0;
        for (size_t t = 0; t < onCpu.a.size(); ++t) {
            const float cpu[8] = { onCpu.a[t].x, onCpu.a[t].y, onCpu.a[t].z, onCpu.a[t].w, onCpu.b[t].x, onCpu.b[t].y, onCpu.b[t].z, onCpu.b[t].w };
            const float gp[8] = { onGpu.a[t].x, onGpu.a[t].y, onGpu.a[t].z, onGpu.a[t].w, onGpu.b[t].x, onGpu.b[t].y, onGpu.b[t].z, onGpu.b[t].w };
            for (int c = 0; c < 8; ++c) {
                biggest = std::max(biggest, static_cast<double>(std::fabs(cpu[c])));
                // NaN fails every comparison, so test it explicitly: a NaN from the GPU must count as the worst error, not vanish from max().
                const double diff = std::isfinite(gp[c]) ? std::fabs(static_cast<double>(gp[c]) - cpu[c]) : 1e30;
                worst = std::max(worst, diff);
            }
        }
        // The GPU's sin and cos are not bit-identical to the CPU's, so allow a small fraction of the biggest value.
        const bool good = biggest > 0.0 && worst <= 1e-3 * biggest;
        allGood = allGood && good;
        char line[200];
        std::snprintf(line, sizeof(line), "Ocean GPU evolve check, band %d (%d x %d): biggest value %.4g, largest difference from the CPU %.3g (%.2g of it): %s",
                      i, gpu.Resolution(i), gpu.Resolution(i), biggest, worst, biggest > 0.0 ? worst / biggest : 0.0, good ? "ok" : "MISMATCH");
        if (good) LOG_INFO(std::string(line)); else LOG_WARNING(std::string(line));
    }
    return allGood;
}
