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

// The largest difference between the CPU's numbers and the GPU's, next to the biggest CPU number, so the error reads as a fraction of the wave size.
struct Fit {
    double biggest = 0.0, worst = 0.0;
    void Add(double reference, double gpu)
    {
        biggest = std::max(biggest, std::fabs(reference));
        // NaN fails every comparison, so test it explicitly: a NaN from the GPU must count as the worst error, not vanish from max().
        worst = std::max(worst, std::isfinite(gpu) ? std::fabs(gpu - reference) : 1e30);
    }
    double Relative() const { return biggest > 0.0 ? worst / biggest : 1e30; }
};

}   // namespace

void OceanGpu::Band::Release()
{
    SafeRelease(uavDisplacement); SafeRelease(uavSlope);
    SafeRelease(srvDisplacement); SafeRelease(srvSlope);
    SafeRelease(textureDisplacement); SafeRelease(textureSlope);
    SafeRelease(uavA); SafeRelease(uavB); SafeRelease(uavWorkA); SafeRelease(uavWorkB);
    SafeRelease(srvA); SafeRelease(srvB); SafeRelease(srvWorkA); SafeRelease(srvWorkB);
    SafeRelease(textureA); SafeRelease(textureB); SafeRelease(textureWorkA); SafeRelease(textureWorkB);
    SafeRelease(modesView); SafeRelease(omegaView);
    SafeRelease(modes); SafeRelease(omega); SafeRelease(constants);
}

OceanGpu::~OceanGpu() { Release(); }

void OceanGpu::Release()
{
    for (Band& band : bands_) band.Release();
    bands_.clear();
    SafeRelease(fftConstants_);
    SafeRelease(assembleConstants_);
}

bool OceanGpu::CreateBand(const OceanGpuPack::PackedBand& packed, Band& band)
{
    const UINT count = static_cast<UINT>(packed.n) * static_cast<UINT>(packed.n);
    band.n = packed.n;
    band.deltaK = packed.DeltaK();
    band.patchSize = static_cast<float>(packed.patchSize);
    if (!MakeStructuredBuffer(device_, packed.modes.data(), sizeof(OceanGpuPack::Float4), count, &band.modes, &band.modesView)) return false;
    if (!MakeStructuredBuffer(device_, packed.omega.data(), sizeof(float), count, &band.omega, &band.omegaView)) return false;

    // The constant buffer is rewritten every Evolve (UpdateSubresource), so it is DEFAULT, not IMMUTABLE.
    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = sizeof(OceanGpuPack::EvolveConstants);
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = device_->CreateBuffer(&constantsDesc, nullptr, &band.constants);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean constant buffer creation failed"); band.constants = nullptr; return false; }

    // The textures: A and B are written by the evolve pass through a UAV and read by the FFT through an SRV; the work pair holds the image between the
    // FFT's two passes. A texture is never read and written in the same pass, so the FFT goes A -> work -> A, and B the same way.
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = static_cast<UINT>(packed.n);
    textureDesc.Height = static_cast<UINT>(packed.n);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // two complex numbers per texel; typed UAV STORES to this format are guaranteed
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D** textures[4] = { &band.textureA, &band.textureB, &band.textureWorkA, &band.textureWorkB };
    ID3D11ShaderResourceView** srvs[4] = { &band.srvA, &band.srvB, &band.srvWorkA, &band.srvWorkB };
    ID3D11UnorderedAccessView** uavs[4] = { &band.uavA, &band.uavB, &band.uavWorkA, &band.uavWorkB };
    for (int i = 0; i < 4; ++i) {
        hr = device_->CreateTexture2D(&textureDesc, nullptr, textures[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum texture creation failed"); *textures[i] = nullptr; return false; }
        hr = device_->CreateShaderResourceView(*textures[i], nullptr, srvs[i]);   // nullptr = a view of the whole texture in its own format
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum SRV creation failed"); *srvs[i] = nullptr; return false; }
        hr = device_->CreateUnorderedAccessView(*textures[i], nullptr, uavs[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean spectrum UAV creation failed"); *uavs[i] = nullptr; return false; }
    }

    // The two maps the water reads. RGBA16F so they can be filtered and given a mip chain on every card (see OceanGpuAssemble.h). MipLevels = 0 asks
    // for the FULL chain (n, n/2, ... 1). GenerateMips needs the texture to be a render target as well as a shader resource, and says so with the misc
    // flag. The compute pass writes mip 0 through the UAV (a UAV with no description views mip 0), then GenerateMips fills the rest.
    D3D11_TEXTURE2D_DESC mapDesc = textureDesc;
    mapDesc.MipLevels = 0;
    mapDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mapDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    mapDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D** mapTextures[2] = { &band.textureDisplacement, &band.textureSlope };
    ID3D11ShaderResourceView** mapSrvs[2] = { &band.srvDisplacement, &band.srvSlope };
    ID3D11UnorderedAccessView** mapUavs[2] = { &band.uavDisplacement, &band.uavSlope };
    for (int i = 0; i < 2; ++i) {
        hr = device_->CreateTexture2D(&mapDesc, nullptr, mapTextures[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean map texture creation failed"); *mapTextures[i] = nullptr; return false; }
        hr = device_->CreateShaderResourceView(*mapTextures[i], nullptr, mapSrvs[i]);   // nullptr: all mips
        if (FAILED(hr)) { LOG_HR(hr, "Ocean map SRV creation failed"); *mapSrvs[i] = nullptr; return false; }
        hr = device_->CreateUnorderedAccessView(*mapTextures[i], nullptr, mapUavs[i]);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean map UAV creation failed"); *mapUavs[i] = nullptr; return false; }
    }
    return true;
}

bool OceanGpu::Create(ID3D11Device* device, const SeaSpectrum::Settings& settings)
{
    // A new sea (the wind changed, a new seed) replaces the BANDS only. The shaders and the constant buffers do not depend on the sea, and compiling
    // three shaders on every wind slider tick would stall the editor, so Create keeps what it already has.
    for (Band& band : bands_) band.Release();
    bands_.clear();
    device_ = device;
    if (!device_) { LOG_ERROR("OceanGpu::Create received a null device"); return false; }
    if (!evolveShader_.IsValid() && !evolveShader_.Load(L"shaders/ocean_evolve_cs.hlsl", device_)) return false;
    if (!fftShader_.IsValid() && !fftShader_.Load(L"shaders/ocean_fft_cs.hlsl", device_)) return false;
    if (!assembleShader_.IsValid() && !assembleShader_.Load(L"shaders/ocean_assemble_cs.hlsl", device_)) return false;

    D3D11_BUFFER_DESC fftConstantsDesc{};
    fftConstantsDesc.ByteWidth = sizeof(OceanGpuFft::FftConstants);
    fftConstantsDesc.Usage = D3D11_USAGE_DEFAULT;
    fftConstantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (!fftConstants_) {
        const HRESULT hr = device_->CreateBuffer(&fftConstantsDesc, nullptr, &fftConstants_);
        if (FAILED(hr)) { LOG_HR(hr, "Ocean FFT constant buffer creation failed"); fftConstants_ = nullptr; return false; }
    }
    D3D11_BUFFER_DESC assembleConstantsDesc{};
    assembleConstantsDesc.ByteWidth = sizeof(OceanGpuAssemble::AssembleConstants);
    assembleConstantsDesc.Usage = D3D11_USAGE_DEFAULT;
    assembleConstantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (!assembleConstants_) {
        const HRESULT assembleHr = device_->CreateBuffer(&assembleConstantsDesc, nullptr, &assembleConstants_);
        if (FAILED(assembleHr)) { LOG_HR(assembleHr, "Ocean assemble constant buffer creation failed"); assembleConstants_ = nullptr; return false; }
    }

    const std::vector<SeaSpectrum::Band> bandList = SeaSpectrum::DefaultBands();
    bands_.resize(bandList.size());
    for (size_t i = 0; i < bandList.size(); ++i) {
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bandList[i], static_cast<int>(i));
        // The FFT shader's groupshared array holds OceanGpuFft::kMaxN elements and the butterflies need a power of two.
        if (!OceanGpuFft::IsPowerOfTwo(field.N()) || field.N() > OceanGpuFft::kMaxN) {
            LOG_ERROR("Ocean band size is not a power of two the FFT shader supports");
            Release();
            return false;
        }
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

void OceanGpu::RunFftPass(ID3D11DeviceContext* ctx, const Band& band, ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* target, bool vertical)
{
    OceanGpuFft::FftConstants constants;
    constants.n = static_cast<std::uint32_t>(band.n);
    constants.log2n = static_cast<std::uint32_t>(OceanGpuFft::Log2(band.n));
    constants.vertical = vertical ? 1u : 0u;
    ctx->UpdateSubresource(fftConstants_, 0, nullptr, &constants, 0, 0);
    ctx->CSSetShaderResources(0, 1, &source);
    ctx->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
    // One thread group per line (a row, or a column): n groups, however long the lines are. The group's 128 threads share the line's work.
    fftShader_.Dispatch(ctx, static_cast<UINT>(band.n), 1, 1);
    ComputeShader::UnbindCompute(ctx, 1, 1);   // the next pass reads what this one wrote: nothing may still be bound as an output
}

void OceanGpu::Transform(ID3D11DeviceContext* ctx)
{
    if (!ctx || !IsValid()) return;
    fftShader_.Bind(ctx);
    ctx->CSSetConstantBuffers(0, 1, &fftConstants_);   // b0 was the evolve pass's constants a moment ago
    for (Band& band : bands_) {
        // Rows first (A -> work A, B -> work B), then columns (work A -> A, work B -> B): the 2-D inverse FFT, the sea in A and B.
        RunFftPass(ctx, band, band.srvA, band.uavWorkA, false);
        RunFftPass(ctx, band, band.srvB, band.uavWorkB, false);
        RunFftPass(ctx, band, band.srvWorkA, band.uavA, true);
        RunFftPass(ctx, band, band.srvWorkB, band.uavB, true);
    }
}

void OceanGpu::Assemble(ID3D11DeviceContext* ctx, float choppiness)
{
    if (!ctx || !IsValid()) return;
    assembleShader_.Bind(ctx);
    ctx->CSSetConstantBuffers(0, 1, &assembleConstants_);
    for (Band& band : bands_) {
        OceanGpuAssemble::AssembleConstants constants;
        constants.n = static_cast<std::uint32_t>(band.n);
        constants.choppiness = choppiness;
        ctx->UpdateSubresource(assembleConstants_, 0, nullptr, &constants, 0, 0);

        ID3D11ShaderResourceView* inputs[2] = { band.srvA, band.srvB };
        ID3D11UnorderedAccessView* outputs[2] = { band.uavDisplacement, band.uavSlope };
        ctx->CSSetShaderResources(0, 2, inputs);
        ctx->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
        const UINT groups = ComputeShader::GroupCount(static_cast<UINT>(band.n), 8);
        assembleShader_.Dispatch(ctx, groups, groups, 1);
        ComputeShader::UnbindCompute(ctx, 2, 2);   // GenerateMips below needs the maps to be free of their UAVs

        // The rest of each map's mip chain, from mip 0: every level is the average of 2 x 2 texels of the one above, so slopes and the Jacobian
        // are averaged over the area a far-away pixel covers.
        ctx->GenerateMips(band.srvDisplacement);
        ctx->GenerateMips(band.srvSlope);
    }
}

bool OceanGpu::ReadTextureBytes(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, size_t bytesPerTexel, std::vector<unsigned char>& out)
{
    // The GPU's own textures cannot be mapped by the CPU. A STAGING copy can: copy there, then Map it. (CopyResource wants the same size and mip count,
    // so the description is the texture's own with the usage, bind flags and misc flags changed.)
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) { LOG_HR(hr, "Ocean staging texture creation failed"); return false; }
    ctx->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);   // subresource 0 is mip 0; waits until the GPU has finished the copy
    if (FAILED(hr)) { LOG_HR(hr, "Ocean staging texture map failed"); staging->Release(); return false; }
    const size_t rowBytes = static_cast<size_t>(n) * bytesPerTexel;
    out.assign(rowBytes * static_cast<size_t>(n), 0);
    for (int y = 0; y < n; ++y) {   // rows are RowPitch bytes apart, which can be more than n texels: copy row by row
        std::memcpy(&out[static_cast<size_t>(y) * rowBytes], static_cast<const char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    return true;
}

bool OceanGpu::ReadTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, std::vector<OceanGpuPack::Float4>& out)
{
    std::vector<unsigned char> bytes;
    if (!ReadTextureBytes(ctx, texture, n, sizeof(OceanGpuPack::Float4), bytes)) return false;
    out.assign(static_cast<size_t>(n) * n, OceanGpuPack::Float4{});
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

bool OceanGpu::ReadSpectra(ID3D11DeviceContext* ctx, int band, OceanGpuPack::PackedSpectra& out)
{
    if (!ctx || !IsValid() || band < 0 || band >= BandCount()) return false;
    const Band& b = bands_[static_cast<size_t>(band)];
    out.n = b.n;
    return ReadTexture(ctx, b.textureA, b.n, out.a) && ReadTexture(ctx, b.textureB, b.n, out.b);
}

bool OceanGpu::ReadMaps(ID3D11DeviceContext* ctx, int band, std::vector<OceanGpuPack::Float4>& displacement, std::vector<OceanGpuPack::Float4>& slope)
{
    if (!ctx || !IsValid() || band < 0 || band >= BandCount()) return false;
    const Band& b = bands_[static_cast<size_t>(band)];
    ID3D11Texture2D* textures[2] = { b.textureDisplacement, b.textureSlope };
    std::vector<OceanGpuPack::Float4>* outputs[2] = { &displacement, &slope };
    for (int m = 0; m < 2; ++m) {
        std::vector<unsigned char> bytes;
        if (!ReadTextureBytes(ctx, textures[m], b.n, 4 * sizeof(std::uint16_t), bytes)) return false;   // RGBA16F: 4 halves a texel
        const size_t count = static_cast<size_t>(b.n) * b.n;
        outputs[m]->assign(count, OceanGpuPack::Float4{});
        for (size_t i = 0; i < count; ++i) {
            std::uint16_t half[4];
            std::memcpy(half, &bytes[i * sizeof(half)], sizeof(half));
            (*outputs[m])[i] = OceanGpuPack::Float4{ OceanGpuAssemble::HalfToFloat(half[0]), OceanGpuAssemble::HalfToFloat(half[1]),
                                                     OceanGpuAssemble::HalfToFloat(half[2]), OceanGpuAssemble::HalfToFloat(half[3]) };
        }
    }
    return true;
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

    // Second half: the finished sea. The FFT runs on what the evolve pass left in A and B, and the result is compared with OceanFFT::Evolve, the
    // double-precision reference the whole GPU sea has to agree with (height, displacement, slopes, and the Jacobian built from the derivatives).
    const double choppiness = 0.8;
    gpu.Transform(ctx);
    for (int i = 0; i < gpu.BandCount(); ++i) {
        OceanGpuPack::PackedSpectra sea;
        if (!gpu.ReadSpectra(ctx, i, sea)) { LOG_WARNING("Ocean GPU check: could not read a band back"); return false; }
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bandList[static_cast<size_t>(i)], i);
        const OceanFFT::Surface reference = OceanFFT::Evolve(field, static_cast<double>(time), choppiness);

        Fit height, displacement, slope;
        double worstJacobian = 0.0;
        for (size_t t = 0; t < sea.a.size(); ++t) {
            // A = (height, displacement x, displacement z, slope x); B = (slope z, dXdx, dZdz, dXdz).
            height.Add(reference.height[t], sea.a[t].x);
            displacement.Add(reference.displaceX[t], sea.a[t].y);
            displacement.Add(reference.displaceZ[t], sea.a[t].z);
            slope.Add(reference.slopeX[t], sea.a[t].w);
            slope.Add(reference.slopeZ[t], sea.b[t].x);
            const double jxx = 1.0 + choppiness * sea.b[t].y, jzz = 1.0 + choppiness * sea.b[t].z, jxz = choppiness * sea.b[t].w;
            const double jacobian = jxx * jzz - jxz * jxz;
            worstJacobian = std::max(worstJacobian, std::isfinite(jacobian) ? std::fabs(jacobian - reference.jacobian[t]) : 1e30);
        }
        const bool good = height.Relative() <= 1e-3 && displacement.Relative() <= 1e-3 && slope.Relative() <= 1e-3 && worstJacobian <= 2e-3;
        allGood = allGood && good;
        char line[220];
        std::snprintf(line, sizeof(line),
                      "Ocean GPU sea check, band %d: largest difference from the CPU as a fraction of the biggest value: height %.2g, displacement %.2g, slope %.2g; Jacobian %.2g: %s",
                      i, height.Relative(), displacement.Relative(), slope.Relative(), worstJacobian, good ? "ok" : "MISMATCH");
        if (good) LOG_INFO(std::string(line)); else LOG_WARNING(std::string(line));
    }

    // Third half: the two maps the water will read. Assemble runs on the finished sea (A and B), and the maps are compared with the same reference.
    // They are half floats, which keep about 1 part in 2000 of a value, so the allowed difference is a little larger than for the 32-bit stages.
    gpu.Assemble(ctx, static_cast<float>(choppiness));
    for (int i = 0; i < gpu.BandCount(); ++i) {
        std::vector<OceanGpuPack::Float4> displacementMap, slopeMap;
        if (!gpu.ReadMaps(ctx, i, displacementMap, slopeMap)) { LOG_WARNING("Ocean GPU check: could not read the maps back"); return false; }
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bandList[static_cast<size_t>(i)], i);
        const OceanFFT::Surface reference = OceanFFT::Evolve(field, static_cast<double>(time), choppiness);

        Fit height, displacement, slope;
        double worstJacobian = 0.0;
        for (size_t t = 0; t < displacementMap.size(); ++t) {
            displacement.Add(choppiness * reference.displaceX[t], displacementMap[t].x);
            displacement.Add(choppiness * reference.displaceZ[t], displacementMap[t].z);
            height.Add(reference.height[t], displacementMap[t].y);
            slope.Add(reference.slopeX[t], slopeMap[t].x);
            slope.Add(reference.slopeZ[t], slopeMap[t].y);
            worstJacobian = std::max(worstJacobian, std::isfinite(slopeMap[t].z) ? std::fabs(slopeMap[t].z - reference.jacobian[t]) : 1e30);
        }
        D3D11_TEXTURE2D_DESC mapDesc{};
        gpu.bands_[static_cast<size_t>(i)].textureDisplacement->GetDesc(&mapDesc);
        const bool good = height.Relative() <= 2e-3 && displacement.Relative() <= 2e-3 && slope.Relative() <= 2e-3 && worstJacobian <= 4e-3 && mapDesc.MipLevels > 1;
        allGood = allGood && good;
        char line[260];
        std::snprintf(line, sizeof(line),
                      "Ocean GPU map check, band %d: largest difference from the CPU as a fraction of the biggest value: height %.2g, displacement %.2g, slope %.2g; Jacobian %.2g; %u mip levels: %s",
                      i, height.Relative(), displacement.Relative(), slope.Relative(), worstJacobian, static_cast<unsigned>(mapDesc.MipLevels), good ? "ok" : "MISMATCH");
        if (good) LOG_INFO(std::string(line)); else LOG_WARNING(std::string(line));
    }
    return allGood;
}
